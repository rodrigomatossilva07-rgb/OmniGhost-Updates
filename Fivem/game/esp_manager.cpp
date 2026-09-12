#include "esp_manager.h"
#include "config/app_settings.h"
#include "../../ImGui/imgui.h"
#include "offsets.h"
#include "../esp/esp.h"
#include "../esp/vehicle_esp.h"
#include "config/config_manager.h"
#include "../aimbot/aimbot.h"
#include "../friends/friends.h"
#include "visibility.h"
#include "../playerInfo/PedData.h"
#include "../../DMALibrary/Memory/Memory.h"
#include <iostream>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include "gameplay/esp_optimizer.h"
#include "gameplay/snapshot_exchange.h"

namespace FiveM {
    namespace ESP {
        // Constants definition
        const int MAX_PEDS = 110;

        // Global containers
        std::vector<uintptr_t> rawPedPointers;
        std::vector<Vec3> positions;
        std::vector<uintptr_t> validPeds;
        std::vector<Vec2> screenPositions;

        struct AcquisitionSnapshot {
            std::vector<uintptr_t> validPeds;
            std::vector<Vec3> positions;
            Matrix viewMatrix{};
            Vec3 localPos{};
            uintptr_t localPlayer = 0;
            bool frameCacheValid = false;
            uint64_t generation = 0;
        };

        static OmniGhost::Gameplay::SnapshotExchange<AcquisitionSnapshot> s_snapshots;
        static std::vector<uintptr_t> s_acquireRawPeds;
        static std::vector<uintptr_t> s_acquireValidPeds;
        static std::vector<Vec3> s_acquirePositions;
        static std::vector<uintptr_t> s_lastGoodPeds;
        static std::vector<Vec3> s_lastGoodPos;
        static Matrix s_acquireViewMatrix{};
        static Vec3 s_acquireLocalPos{};
        static int s_emptyFrames = 0;
        static std::atomic_bool s_acquisitionRunning{false};
        static std::atomic_bool s_acquisitionStop{false};
        static std::atomic_bool s_needPeds{false};
        static std::atomic_bool s_selfEsp{false};
        static std::atomic_bool s_npcEsp{false};
        static std::atomic_bool s_performanceMode{false};
        static std::atomic<float> s_maxDistance{500.f};
        static std::atomic<float> s_renderFps{60.f};
        static std::atomic<float> s_displayWidth{1920.f};
        static std::atomic<float> s_displayHeight{1080.f};
        static std::thread s_acquisitionThread;
        static uint64_t s_acquisitionGeneration = 0;

        // Performance tracking
        int frameCount = 0;
        std::chrono::steady_clock::time_point lastFrameTime;

        // Initialize containers with reserved memory
        void InitializeContainers() {
            static bool initialized = false;
            if (!initialized) {
                rawPedPointers.reserve(MAX_PEDS);
                positions.reserve(MAX_PEDS);
                validPeds.reserve(MAX_PEDS);
                screenPositions.reserve(MAX_PEDS);
                s_acquireRawPeds.reserve(MAX_PEDS);
                s_acquireValidPeds.reserve(MAX_PEDS);
                s_acquirePositions.reserve(MAX_PEDS);
                initialized = true;
                lastFrameTime = std::chrono::steady_clock::now();
            }
        }

        // Single-threaded main loop - called every frame
        // Per-frame shared caches (avoid re-reading viewport/localpos everywhere)
        static Matrix s_viewMatrix{};
        static Vec3 s_localPos{};
        static bool s_frameCacheValid = false;

        bool FrameCacheValid() { return s_frameCacheValid; }
        const Matrix& GetFrameViewMatrix() { return s_viewMatrix; }
        const Vec3& GetFrameLocalPos() { return s_localPos; }

        static void PublishAcquisitionSnapshot(uintptr_t localPlayer, bool cacheValid) {
            auto slot = s_snapshots.TryBeginWrite();
            if (!slot) return;
            slot.value->validPeds.assign(s_acquireValidPeds.begin(), s_acquireValidPeds.end());
            slot.value->positions.assign(s_acquirePositions.begin(), s_acquirePositions.end());
            slot.value->viewMatrix = s_acquireViewMatrix;
            slot.value->localPos = s_acquireLocalPos;
            slot.value->localPlayer = localPlayer;
            slot.value->frameCacheValid = cacheValid;
            slot.value->generation = ++s_acquisitionGeneration;
            s_snapshots.Publish(slot.index);
        }

        static void AcquisitionLoop() {
            uintptr_t localPlayer = offset::localplayer;
            while (!s_acquisitionStop.load(std::memory_order_acquire)) {
                const bool needPeds = s_needPeds.load(std::memory_order_relaxed);
                bool cacheValid = false;

                if (needPeds && offset::world && offset::replay && offset::viewport) {
                    auto h = mem.CreateScatterHandle();
                    uintptr_t freshLp = 0;
                    mem.AddScatterReadRequest(h, offset::world + 0x8, &freshLp, sizeof(uintptr_t));
                    mem.AddScatterReadRequest(h, offset::viewport + 0x24C,
                                              &s_acquireViewMatrix, sizeof(Matrix));
                    mem.ExecuteReadScatter(h);
                    mem.CloseScatterHandle(h);
                    if (freshLp) localPlayer = freshLp;

                    if (localPlayer) {
                        h = mem.CreateScatterHandle();
                        mem.AddScatterReadRequest(h, localPlayer + offset::playerPosition,
                                                  &s_acquireLocalPos, sizeof(Vec3));
                        mem.ExecuteReadScatter(h);
                        mem.CloseScatterHandle(h);
                        cacheValid = true;
                        collectFrameData(localPlayer, s_acquireLocalPos);
                    } else {
                        s_acquireValidPeds.clear();
                        s_acquirePositions.clear();
                        s_acquireLocalPos = {};
                    }

                    if (s_acquireValidPeds.empty() && !s_lastGoodPeds.empty() && s_emptyFrames < 8) {
                        s_acquireValidPeds.assign(s_lastGoodPeds.begin(), s_lastGoodPeds.end());
                        s_acquirePositions.assign(s_lastGoodPos.begin(), s_lastGoodPos.end());
                        ++s_emptyFrames;
                    } else if (!s_acquireValidPeds.empty()) {
                        s_lastGoodPeds.assign(s_acquireValidPeds.begin(), s_acquireValidPeds.end());
                        s_lastGoodPos.assign(s_acquirePositions.begin(), s_acquirePositions.end());
                        s_emptyFrames = 0;
                    } else {
                        ++s_emptyFrames;
                    }
                } else {
                    s_acquireValidPeds.clear();
                    s_acquirePositions.clear();
                    s_acquireLocalPos = {};
                    s_emptyFrames = 0;
                }

                PublishAcquisitionSnapshot(localPlayer, cacheValid);

                // Hierarchical/adaptive acquisition frequency.  Position data
                // stays fast when presentation has headroom, while an already
                // overloaded renderer stops asking DMA for 250 updates/second.
                // The exchange always exposes only the newest generation, so
                // reducing producer pressure cannot build a stale backlog.
                int delayMs = 16;
                if (needPeds) {
                    const float fps = s_renderFps.load(std::memory_order_relaxed);
                    delayMs = (fps > 1.f && fps < 45.f) ? 8
                            : (fps > 1.f && fps < 80.f) ? 6
                            : 4;
                    if (s_performanceMode.load(std::memory_order_relaxed))
                        delayMs = (std::max)(delayMs, 7);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            }
        }

        static void EnsureAcquisitionStarted() {
            bool expected = false;
            if (!s_acquisitionRunning.compare_exchange_strong(
                    expected, true, std::memory_order_acq_rel))
                return;
            s_acquisitionStop.store(false, std::memory_order_release);
            s_acquisitionThread = std::thread(AcquisitionLoop);
        }

        void StopAcquisition() {
            s_acquisitionStop.store(true, std::memory_order_release);
            if (s_acquisitionThread.joinable())
                s_acquisitionThread.join();
            s_acquisitionRunning.store(false, std::memory_order_release);
            s_acquireRawPeds.clear();
            s_acquireValidPeds.clear();
            s_acquirePositions.clear();
            s_lastGoodPeds.clear();
            s_lastGoodPos.clear();
            s_acquireViewMatrix = {};
            s_acquireLocalPos = {};
            s_emptyFrames = 0;
            PublishAcquisitionSnapshot(0, false);
        }

        void RunESP() {
            InitializeContainers();
            frameCount++;
            auto currentTime = std::chrono::steady_clock::now();

            const bool needPeds = esp::config.enabled
                || aimbot::config.aimbot_enabled
                                || aimbot::config.trigger_enabled
                || esp::config.triangle_radar
                || esp::config.radar_enabled
                || esp::config.square_radar
                || esp::config.waypoint_line
                || esp::config.blip_esp;

            const ImVec2 display = ImGui::GetIO().DisplaySize;
            s_needPeds.store(needPeds, std::memory_order_relaxed);
            s_selfEsp.store(esp::config.self_esp, std::memory_order_relaxed);
            s_npcEsp.store(esp::config.npc_esp, std::memory_order_relaxed);
            s_performanceMode.store(app_settings::config.performance_mode, std::memory_order_relaxed);
            s_maxDistance.store(esp::config.max_esp_distance, std::memory_order_relaxed);
            s_renderFps.store(ImGui::GetIO().Framerate, std::memory_order_relaxed);
            s_displayWidth.store(display.x, std::memory_order_relaxed);
            s_displayHeight.store(display.y, std::memory_order_relaxed);
            EnsureAcquisitionStarted();

            static uint64_t consumedGeneration = 0;
            auto snapshot = s_snapshots.Acquire();
            if (snapshot && snapshot->generation != consumedGeneration) {
                validPeds.assign(snapshot->validPeds.begin(), snapshot->validPeds.end());
                positions.assign(snapshot->positions.begin(), snapshot->positions.end());
                s_viewMatrix = snapshot->viewMatrix;
                s_localPos = snapshot->localPos;
                s_frameCacheValid = snapshot->frameCacheValid;
                offset::localplayer = snapshot->localPlayer;
                consumedGeneration = snapshot->generation;
            }

            renderESP();

            // Radar every frame (10Hz caused triangle flicker)
            if (offset::localplayer && (esp::config.triangle_radar || esp::config.square_radar || esp::config.radar_enabled)) {
                static float s_lastFovPx = -1.f;
                float fovPx = 0.f;
                if (aimbot::config.aimbot_enabled) fovPx = (std::max)(fovPx, aimbot::config.fov_size);
                if (aimbot::config.trigger_enabled) fovPx = (std::max)(fovPx, aimbot::config.trigger_fov);
                if (fovPx > 1.f && fabsf(fovPx - s_lastFovPx) > 0.5f) {
                    esp::config.triangle_radar_radius = (std::max)(50.f, fovPx * 0.92f);
                    s_lastFovPx = fovPx;
                }
                Matrix vm = s_frameCacheValid ? s_viewMatrix : mem.Read<Matrix>(offset::viewport + 0x24C);
                esp::DrawPlayerRadar(vm, offset::localplayer);
            }
            // Waypoint/blip less critical — ~5 Hz is fine
            static double lastWp = 0.0;
            const double nowSec = ImGui::GetTime();
            if (offset::localplayer && (esp::config.waypoint_line || esp::config.blip_esp)) {
                if (nowSec - lastWp >= 0.20) {
                    lastWp = nowSec;
                    Matrix vm = s_frameCacheValid ? s_viewMatrix : mem.Read<Matrix>(offset::viewport + 0x24C);
                    esp::DrawWaypointAndBlips(vm, offset::localplayer);
                }
            }

            vehicle_esp::Run();
            aimbot::Run();
            config_manager::TickAutoSave();

            if (!validPeds.empty() && esp::get_use_cache() && !app_settings::config.performance_mode) {
                g_pedCacheManager.fastCache(validPeds, positions);
                g_pedCacheManager.update();
            }

            lastFrameTime = currentTime;
        }

        // Producer-side data collection.
        void collectFrameData(uintptr_t localPlayer, const Vec3& localPos) {
            // Producer-private containers. Presentation owns the public vectors
            // and therefore never observes a partially rebuilt entity list.
            auto& writeRawPeds = s_acquireRawPeds;
            auto& writePeds = s_acquireValidPeds;
            auto& writePositions = s_acquirePositions;
            // Never submit DMA reads with an incomplete pointer chain. FiveM can
            // transition through lobby/loading states where one of these pointers
            // is temporarily unavailable; treating that state as an empty frame
            // keeps the launcher/session alive instead of issuing reads from 0.
            if (!offset::world || !offset::replay || !offset::viewport || !localPlayer) {
                writePeds.clear();
                writePositions.clear();
                return;
            }
            writePeds.clear();
            writePositions.clear();

            // Single scatter handle for fast operations
            auto handle = mem.CreateScatterHandle();

            static uintptr_t ped_replay_interface = 0;
            static uintptr_t pedListBase = 0;
            static ULONGLONG nextChainRefresh = 0;
            const ULONGLONG now = GetTickCount64();

            // Replay interface/list addresses are slow metadata. Refresh them at
            // 4 Hz, or immediately after a chain failure, while positions stay
            // on the fast lane.
            if (now >= nextChainRefresh) {
                ped_replay_interface = 0;
                pedListBase = 0;
                mem.AddScatterReadRequest(handle, offset::replay + 0x18,
                    &ped_replay_interface, sizeof(uintptr_t));
                mem.ExecuteReadScatter(handle);
                if (ped_replay_interface) {
                    mem.AddScatterReadRequest(handle, ped_replay_interface + 0x100,
                        &pedListBase, sizeof(uintptr_t));
                    mem.ExecuteReadScatter(handle);
                }
                nextChainRefresh = now + 250;
            }

            if (ped_replay_interface) {
                if (pedListBase) {
                    // Full ped list capacity every frame (do not shrink — misses players)
                    const int listCap = MAX_PEDS;
                    if ((int)writeRawPeds.size() != listCap)
                        writeRawPeds.resize(listCap);

                    mem.AddScatterReadRequest(handle, pedListBase,
                        writeRawPeds.data(), sizeof(uintptr_t) * listCap);
                    mem.ExecuteReadScatter(handle);

                    // Then batch read playerInfo for all peds (static buffer)
                    static std::vector<uintptr_t> playerInfoPtrs;
                    static std::vector<uintptr_t> playerInfoOwners;
                    static ULONGLONG nextInfoRefresh = 0;
                    if ((int)playerInfoPtrs.size() != listCap) {
                        playerInfoPtrs.assign(listCap, 0);
                        playerInfoOwners.assign(listCap, 0);
                    }
                    const bool refreshAllInfo = now >= nextInfoRefresh;
                    bool queuedInfoReads = false;
                    for (int i = 0; i < listCap; i++) {
                        const uintptr_t ped = writeRawPeds[i];
                        if (!ped || ped == localPlayer) {
                            playerInfoPtrs[i] = 0;
                            playerInfoOwners[i] = ped;
                        } else if (refreshAllInfo || playerInfoOwners[i] != ped) {
                            playerInfoPtrs[i] = 0;
                            playerInfoOwners[i] = ped;
                            mem.AddScatterReadRequest(handle, writeRawPeds[i] + offset::playerInfo,
                                &playerInfoPtrs[i], sizeof(uintptr_t));
                            queuedInfoReads = true;
                        }
                    }
                    if (queuedInfoReads)
                        mem.ExecuteReadScatter(handle);
                    if (refreshAllInfo)
                        nextInfoRefresh = now + 500;

                    // Filter like the original working base:
                    //  - prefer peds with playerInfo (real players)
                    //  - if playerInfo chain is dead (all null), keep raw peds so ESP still works
                    int withInfo = 0;
                    for (int i = 0; i < (int)writeRawPeds.size(); i++) {
                        if (writeRawPeds[i] && playerInfoPtrs[i])
                            ++withInfo;
                    }
                    const bool playerInfoReliable = (withInfo > 0);

                    for (int i = 0; i < (int)writeRawPeds.size(); i++) {
                        uintptr_t ped = writeRawPeds[i];
                        if (!ped || ped < 0x10000) continue;

                        const bool isLocal = (ped == localPlayer);
                        if (isLocal && !s_selfEsp.load(std::memory_order_relaxed))
                            continue;

                        if (!playerInfoReliable) {
                            // No playerInfo on entire list (offset lag) — keep everyone so ESP/aim live,
                            // but still honor npc_esp when we *can* tell NPCs apart (we can't here).
                            writePeds.push_back(ped);
                            continue;
                        }
                        if (playerInfoPtrs[i]) {
                            // Real player (has CPlayerInfo)
                            writePeds.push_back(ped);
                        } else if (s_npcEsp.load(std::memory_order_relaxed)) {
                            writePeds.push_back(ped);
                        } else if (isLocal && s_selfEsp.load(std::memory_order_relaxed)) {
                            writePeds.push_back(ped);
                        }
                        // else: NPC with npc_esp OFF → skip (do NOT push)

                    }

                    // Read positions for ALL candidates first
                    writePositions.clear();
                    if (!writePeds.empty()) {
                        writePositions.resize(writePeds.size());
                        for (size_t i = 0; i < writePeds.size(); i++) {
                            mem.AddScatterReadRequest(handle, writePeds[i] + offset::playerPosition,
                                &writePositions[i], sizeof(Vec3));
                        }
                        mem.ExecuteReadScatter(handle);

                        // Drop invalid / stale entity slots (ghost peds)
                        std::vector<uintptr_t> alivePeds;
                        std::vector<Vec3> alivePos;
                        alivePeds.reserve(writePeds.size());
                        alivePos.reserve(writePeds.size());
                        for (size_t i = 0; i < writePeds.size(); i++) {
                            const Vec3& p = writePositions[i];
                            if (p.IsZero()) continue;
                            // GTA map sanity
                            if (p.x < -10000.f || p.x > 10000.f || p.y < -10000.f || p.y > 10000.f)
                                continue;
                            if (p.z < -500.f || p.z > 3000.f)
                                continue;
                            // Also respect global ESP max distance early (frees cap for near players)
                            const float maxDistance = s_maxDistance.load(std::memory_order_relaxed);
                            if (!localPos.IsZero() && maxDistance > 1.f) {
                                const float maxDistanceSq = maxDistance * maxDistance;
                                if (p.distance_sq(localPos) > maxDistanceSq)
                                    continue;
                            }
                            alivePeds.push_back(writePeds[i]);
                            alivePos.push_back(p);
                        }
                        writePeds.swap(alivePeds);
                        writePositions.swap(alivePos);
                    }
                    // Frustum-first + distance (game-style streaming for ESP):
                    // 1) Prefer peds currently on screen / just at the edge of FOV
                    // 2) Fill remaining slots with nearest off-screen (aim sticky / turn-in)
                    // When you turn the camera, next frame W2S promotes them → full ESP ASAP
                    // without paying bone/DMA cost for the whole server list.
                    const float fps = s_renderFps.load(std::memory_order_relaxed);
                    size_t kMax = 48;
                    if (fps > 1.f && fps < 45.f) kMax = 28;
                    else if (fps >= 45.f && fps < 70.f) kMax = 40;
                    if (s_performanceMode.load(std::memory_order_relaxed))
                        kMax = (std::min)(kMax, (size_t)30);

                    const Matrix vmCull = s_acquireViewMatrix;
                    const ImVec2 ds(s_displayWidth.load(std::memory_order_relaxed),
                                    s_displayHeight.load(std::memory_order_relaxed));
                    const float margin = 80.f; // soft edge: almost in view still counts as "streaming in"

                    if (!writePeds.empty() && !localPos.IsZero()) {
                        struct PedRank {
                            size_t idx;
                            float dist;
                            bool on_screen;
                            float crossSq;
                        };
                        static std::vector<PedRank> order;
                        static std::vector<uintptr_t> rankedPeds;
                        static std::vector<Vec3> rankedPos;
                        order.clear();
                        order.reserve(writePeds.size());
                        for (size_t i = 0; i < writePeds.size(); ++i) {
                            const float d = writePositions[i].distance_sq(localPos); // sort by dist²
                            Vec2 sp{};
                            bool on = writePositions[i].world_to_screen(vmCull, sp);
                            float crossSq = 1.0e30f;
                            if (on) {
                                const bool inFrame =
                                    sp.x >= -margin && sp.x <= ds.x + margin &&
                                    sp.y >= -margin && sp.y <= ds.y + margin;
                                on = inFrame;
                                if (on) {
                                    const float cx = sp.x - ds.x * 0.5f;
                                    const float cy = sp.y - ds.y * 0.5f;
                                    crossSq = cx * cx + cy * cy;
                                }
                            }
                            order.push_back({ i, d, on, crossSq });
                        }
                        auto rankLess = [](const PedRank& a, const PedRank& b) {
                                if (a.on_screen != b.on_screen) return a.on_screen > b.on_screen;
                                if (a.on_screen) {
                                    if (fabsf(a.crossSq - b.crossSq) > 1.f) return a.crossSq < b.crossSq;
                                    return a.dist < b.dist;
                                }
                                return a.dist < b.dist;
                            };
                        const size_t take = (std::min)(kMax, order.size());
                        if (take < order.size())
                            std::partial_sort(order.begin(), order.begin() + (std::ptrdiff_t)take, order.end(), rankLess);
                        else
                            std::sort(order.begin(), order.end(), rankLess);
                        rankedPeds.clear();
                        rankedPos.clear();
                        rankedPeds.reserve(take);
                        rankedPos.reserve(take);
                        for (size_t n = 0; n < take; ++n) {
                            rankedPeds.push_back(writePeds[order[n].idx]);
                            rankedPos.push_back(writePositions[order[n].idx]);
                        }
                        writePeds.swap(rankedPeds);
                        writePositions.swap(rankedPos);
                    } else if (writePeds.size() > kMax) {
                        writePeds.resize(kMax);
                        if (writePositions.size() > kMax) writePositions.resize(kMax);
                    }
                }
            }

            mem.CloseScatterHandle(handle);
        }

        // Rendering operations (now with batch skeleton support)
        void renderESP() {
            if (validPeds.empty() || positions.empty())
                return;
            if (!offset::viewport || !offset::localplayer)
                return;

            // Do NOT auto-enable any visual (head circle, etc.) — master ESP alone draws nothing.
            Matrix view_matrix = s_frameCacheValid ? s_viewMatrix
                : mem.Read<Matrix>(offset::viewport + 0x24C);
            Vec3 localPos = s_frameCacheValid ? s_localPos : Vec3{};
            if (localPos.IsZero() && offset::localplayer)
                localPos = mem.Read<Vec3>(offset::localplayer + offset::playerPosition);

            const float maxDist = esp::config.max_esp_distance;
            const float maxDistSq = (maxDist > 1.f) ? (maxDist * maxDist) : 0.f;

            // Prime visibility, visual data and only the bone anchors required
            // by the enabled features. Every consumer shares these batches.
            static std::vector<bool> frameVisibility;
            const bool hasEspDrawing = (esp::config.enabled &&
                (esp::config.skeleton || esp::config.head_circle || esp::config.trails ||
                 esp::config.head_halo || esp::config.look_direction || esp::config.chinese_hat || esp::has_extra_visuals())) ||
                esp::config.triangle_radar || esp::config.square_radar ||
                esp::config.radar_enabled;
            const bool needsEspVisibility = hasEspDrawing &&
                (esp::config.visibility_colors || esp::config.visible_check);
            if (needsEspVisibility || aimbot::config.visible_check)
                FiveM::Visibility::BatchCheckVisibility(validPeds, frameVisibility);

            uint16_t boneMask = 0;
            if (esp::config.enabled && esp::config.skeleton) {
                boneMask = 0x01FFu;
            } else {
                if (esp::config.enabled && (esp::config.head_circle ||
                    esp::config.head_halo || esp::config.look_direction || esp::config.chinese_hat))
                    boneMask |= uint16_t(1u << 0);
                if (esp::config.enabled && (esp::config.box_2d || esp::config.corner_box ||
                    esp::config.snaplines || esp::config.health_bar || esp::config.armor_bar)) {
                    boneMask |= uint16_t((1u << 0) | (1u << 1) | (1u << 2));
                }
                auto addAimBones = [&](aimbot::Hitbox hitbox) {
                    switch (hitbox) {
                    case aimbot::Hitbox::Head:   boneMask |= uint16_t(1u << 0); break;
                    case aimbot::Hitbox::Neck:   boneMask |= uint16_t(1u << 7); break;
                    case aimbot::Hitbox::Torso:  boneMask |= uint16_t((1u << 7) | (1u << 8)); break;
                    case aimbot::Hitbox::Pelvis: boneMask |= uint16_t(1u << 8); break;
                    case aimbot::Hitbox::Legs:   boneMask |= uint16_t((1u << 1) | (1u << 2) | (1u << 8)); break;
                    }
                };
                if (aimbot::config.aimbot_enabled)
                    addAimBones(aimbot::config.hitbox);
                if (aimbot::config.trigger_enabled)
                    addAimBones(aimbot::config.trigger_head_only
                        ? aimbot::Hitbox::Head : aimbot::config.hitbox);
            }
            if (boneMask)
                esp::prepare_skeleton_frame(validPeds, positions, boneMask);
            const bool needsFriendData = esp::config.enabled && friends::HasFriends();
            if ((esp::config.enabled && (esp::has_extra_visuals() || esp::config.npc_esp ||
                esp::config.team_check || esp::config.trails || esp::config.head_halo ||
                esp::config.look_direction || esp::config.chinese_hat)) || needsFriendData ||
                aimbot::config.aimbot_enabled || aimbot::config.trigger_enabled)
                esp::prepare_esp_frame(validPeds, positions);

            if (!esp::config.enabled)
                return;

            // The data preparation above deliberately happens once per frame.
            // It still needs to be consumed by the existing per-ped renderer;
            // without this dispatch the ESP can be enabled and have valid data,
            // yet never draw anything.
            for (const uintptr_t ped : validPeds) {
                if (ped)
                    esp::render_esp_for_ped(ped, view_matrix, offset::localplayer);
            }
            (void)maxDistSq;
        }

        // Performance monitoring
        void printPerformanceStats() {
            static auto lastPrint = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();

            if (now - lastPrint >= std::chrono::seconds(5)) {
                auto fps = frameCount / 5.0;
                auto cacheSize = g_pedCacheManager.getCacheSize();

                (void)fps;
                (void)cacheSize;
                frameCount = 0;
                lastPrint = now;
            }
        }

        // Manual cache refresh (called when needed)
        void refreshCache() {
            g_pedCacheManager.manualCache();
        }
        
        // Prepared data access for aimbot integration
        bool try_get_prepared_origin(uintptr_t ped, Vec3& out) {
            PedData data;
            if (!g_pedCacheManager.getPedData(ped, data) || !data.isValid)
                return false;
            out = data.position_origin;
            return true;
        }
        
        bool try_get_prepared_velocity(uintptr_t ped, Vec3& out) {
            (void)ped;
            (void)out;
            return false;
        }
        
        bool try_get_prepared_health(uintptr_t ped, float& out) {
            PedData data;
            if (!g_pedCacheManager.getPedData(ped, data) || !data.isValid)
                return false;
            out = data.health;
            return true;
        }
        
        bool try_get_prepared_bone_position(uintptr_t ped, int bone, Vec3& out) {
            (void)ped;
            (void)bone;
            (void)out;
            return false;
        }
    }
}
