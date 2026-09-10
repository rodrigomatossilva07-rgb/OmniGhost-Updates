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
#include <chrono>
#include "gameplay/esp_optimizer.h"

namespace FiveM {
    namespace ESP {
        // Constants definition
        const int MAX_PEDS = 110;

        // Global containers
        std::vector<uintptr_t> rawPedPointers;
        std::vector<Vec3> positions;
        std::vector<uintptr_t> validPeds;
        static std::vector<uintptr_t> s_lastGoodPeds;
        static std::vector<Vec3> s_lastGoodPos;
        static int s_emptyFrames = 0;
        std::vector<Vec2> screenPositions;

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

        void RunESP() {
            InitializeContainers();
            frameCount++;
            auto currentTime = std::chrono::steady_clock::now();
            s_frameCacheValid = false;

            const bool needPeds = esp::config.enabled
                || aimbot::config.aimbot_enabled
                                || aimbot::config.trigger_enabled
                || esp::config.triangle_radar
                || esp::config.radar_enabled
                || esp::config.square_radar
                || esp::config.waypoint_line
                || esp::config.blip_esp;

            if (needPeds) {
                // Frame cache: localplayer from World+0x8, then matrix + pos
                {
                    auto h = mem.CreateScatterHandle();
                    uintptr_t freshLp = 0;
                    if (offset::world)
                        mem.AddScatterReadRequest(h, offset::world + 0x8, &freshLp, sizeof(uintptr_t));
                    if (offset::viewport)
                        mem.AddScatterReadRequest(h, offset::viewport + 0x24C, &s_viewMatrix, sizeof(Matrix));
                    mem.ExecuteReadScatter(h);
                    mem.CloseScatterHandle(h);
                    if (freshLp)
                        offset::localplayer = freshLp;
                }
                if (offset::localplayer) {
                    auto h = mem.CreateScatterHandle();
                    mem.AddScatterReadRequest(h, offset::localplayer + offset::playerPosition, &s_localPos, sizeof(Vec3));
                    mem.ExecuteReadScatter(h);
                    mem.CloseScatterHandle(h);
                    s_frameCacheValid = (offset::viewport != 0);
                } else {
                    s_frameCacheValid = false;
                    s_localPos = {};
                }
                collectFrameData();
                if (validPeds.empty() && !s_lastGoodPeds.empty() && s_emptyFrames < 8) {
                    // Brief hold — avoids ESP blink (pointer copy of last good, no realloc)
                    validPeds.assign(s_lastGoodPeds.begin(), s_lastGoodPeds.end());
                    positions.assign(s_lastGoodPos.begin(), s_lastGoodPos.end());
                    ++s_emptyFrames;
                } else if (!validPeds.empty()) {
                    s_lastGoodPeds.assign(validPeds.begin(), validPeds.end());
                    s_lastGoodPos.assign(positions.begin(), positions.end());
                    s_emptyFrames = 0;
                } else {
                    ++s_emptyFrames;
                }
            } else {
                validPeds.clear();
                positions.clear();
                s_emptyFrames = 0;
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

        // Data collection (now synchronous)
        void collectFrameData() {
            // Never submit DMA reads with an incomplete pointer chain. FiveM can
            // transition through lobby/loading states where one of these pointers
            // is temporarily unavailable; treating that state as an empty frame
            // keeps the launcher/session alive instead of issuing reads from 0.
            if (!offset::world || !offset::replay || !offset::viewport || !offset::localplayer) {
                validPeds.clear();
                positions.clear();
                return;
            }

            // Single scatter handle for fast operations
            auto handle = mem.CreateScatterHandle();

            // Fast critical data reads
            Matrix view_matrix;
            Vec3 localPos;
            uintptr_t ped_replay_interface = 0;
            uintptr_t pedListBase = 0;

            // Batch critical reads
            mem.AddScatterReadRequest(handle, offset::viewport + 0x24C,
                &view_matrix, sizeof(Matrix));
            mem.AddScatterReadRequest(handle, offset::localplayer + offset::playerPosition,
                &localPos, sizeof(Vec3));
            mem.AddScatterReadRequest(handle, offset::replay + 0x18,
                &ped_replay_interface, sizeof(uintptr_t));
            uintptr_t freshLpEarly = 0;
            if (offset::world)
                mem.AddScatterReadRequest(handle, offset::world + 0x8, &freshLpEarly, sizeof(uintptr_t));

            mem.ExecuteReadScatter(handle);
            if (freshLpEarly)
                offset::localplayer = freshLpEarly;

            // Diagnostics every ~3s when ESP is on but ped chain fails
            static double lastDiag = 0.0;
            const double nowDiag = ImGui::GetTime();
            if (esp::config.enabled && (nowDiag - lastDiag) > 3.0) {
                lastDiag = nowDiag;
                if (!offset::replay || !offset::viewport || !offset::world) {
                    if (false) std::cout << "[ESP] offsets nulos world=0x" << std::hex << offset::world
                              << " replay=0x" << offset::replay
                              << " viewport=0x" << offset::viewport << std::dec << std::endl;
                } else if (!ped_replay_interface) {
                    if (false) std::cout << "[ESP] ped_replay_interface=0 (replay+0x18 falhou). replay=0x"
                              << std::hex << offset::replay << std::dec << std::endl;
                }
            }

            if (ped_replay_interface) {
                mem.AddScatterReadRequest(handle, ped_replay_interface + 0x100,
                    &pedListBase, sizeof(uintptr_t));
                mem.ExecuteReadScatter(handle);

                if (!pedListBase && esp::config.enabled && (nowDiag - lastDiag) <= 0.05) {
                }

                if (pedListBase) {
                    // Full ped list capacity every frame (do not shrink — misses players)
                    const int listCap = MAX_PEDS;
                    if ((int)rawPedPointers.size() != listCap)
                        rawPedPointers.resize(listCap);

                    mem.AddScatterReadRequest(handle, pedListBase,
                        rawPedPointers.data(), sizeof(uintptr_t) * listCap);
                    mem.ExecuteReadScatter(handle);

                    // Refresh localplayer every tick (prevents self-ESP when pointer goes stale)
                    // Continuous probe: lobby → sessão → lobby without menu restart
                    if (offset::world) {
                        uintptr_t freshLp = 0;
                        mem.AddScatterReadRequest(handle, offset::world + 0x8, &freshLp, sizeof(uintptr_t));
                        mem.ExecuteReadScatter(handle);
                        if (freshLp) offset::localplayer = freshLp;
                    }

                    // Then batch read playerInfo for all peds (static buffer)
                    static std::vector<uintptr_t> playerInfoPtrs;
                    if ((int)playerInfoPtrs.size() != listCap)
                        playerInfoPtrs.assign(listCap, 0);
                    else
                        std::fill(playerInfoPtrs.begin(), playerInfoPtrs.end(), 0);
                    for (int i = 0; i < listCap; i++) {
                        if (rawPedPointers[i] && rawPedPointers[i] != offset::localplayer) {
                            mem.AddScatterReadRequest(handle, rawPedPointers[i] + offset::playerInfo,
                                &playerInfoPtrs[i], sizeof(uintptr_t));
                        }
                    }
                    mem.ExecuteReadScatter(handle);

                    // Filter like the original working base:
                    //  - prefer peds with playerInfo (real players)
                    //  - if playerInfo chain is dead (all null), keep raw peds so ESP still works
                    validPeds.clear();
                    int withInfo = 0;
                    int rawCount = 0;
                    for (int i = 0; i < (int)rawPedPointers.size(); i++) {
                        if (rawPedPointers[i] && rawPedPointers[i] > 0x10000)
                            ++rawCount;
                        if (rawPedPointers[i] && playerInfoPtrs[i])
                            ++withInfo;
                    }
                    const bool playerInfoReliable = (withInfo > 0);

                    for (int i = 0; i < (int)rawPedPointers.size(); i++) {
                        uintptr_t ped = rawPedPointers[i];
                        if (!ped || ped < 0x10000) continue;

                        const bool isLocal = (ped == offset::localplayer);
                        if (isLocal && !esp::config.self_esp)
                            continue;

                        if (!playerInfoReliable) {
                            // No playerInfo on entire list (offset lag) — keep everyone so ESP/aim live,
                            // but still honor npc_esp when we *can* tell NPCs apart (we can't here).
                            validPeds.push_back(ped);
                            continue;
                        }
                        if (playerInfoPtrs[i]) {
                            // Real player (has CPlayerInfo)
                            validPeds.push_back(ped);
                        } else if (esp::config.npc_esp) {
                            validPeds.push_back(ped);
                        } else if (isLocal && esp::config.self_esp) {
                            validPeds.push_back(ped);
                        }
                        // else: NPC with npc_esp OFF → skip (do NOT push)

                    }

                    if (esp::config.enabled && (nowDiag - lastDiag) <= 0.05) {
                        if (false) std::cout << "[ESP] raw=" << rawCount
                                  << " withInfo=" << withInfo
                                  << " valid=" << validPeds.size()
                                  << " local=0x" << std::hex << offset::localplayer << std::dec
                                  << std::endl;
                    }

                    // Read positions for ALL candidates first
                    positions.clear();
                    if (!validPeds.empty()) {
                        positions.resize(validPeds.size());
                        for (size_t i = 0; i < validPeds.size(); i++) {
                            mem.AddScatterReadRequest(handle, validPeds[i] + offset::playerPosition,
                                &positions[i], sizeof(Vec3));
                        }
                        mem.ExecuteReadScatter(handle);

                        // Drop invalid / stale entity slots (ghost peds)
                        std::vector<uintptr_t> alivePeds;
                        std::vector<Vec3> alivePos;
                        alivePeds.reserve(validPeds.size());
                        alivePos.reserve(validPeds.size());
                        for (size_t i = 0; i < validPeds.size(); i++) {
                            const Vec3& p = positions[i];
                            if (p.IsZero()) continue;
                            // GTA map sanity
                            if (p.x < -10000.f || p.x > 10000.f || p.y < -10000.f || p.y > 10000.f)
                                continue;
                            if (p.z < -500.f || p.z > 3000.f)
                                continue;
                            // Also respect global ESP max distance early (frees cap for near players)
                            if (!localPos.IsZero() && esp::config.max_esp_distance > 1.f) {
                                if (p.distance_to(localPos) > esp::config.max_esp_distance)
                                    continue;
                            }
                            alivePeds.push_back(validPeds[i]);
                            alivePos.push_back(p);
                        }
                        validPeds.swap(alivePeds);
                        positions.swap(alivePos);
                    }
                    // Frustum-first + distance (game-style streaming for ESP):
                    // 1) Prefer peds currently on screen / just at the edge of FOV
                    // 2) Fill remaining slots with nearest off-screen (aim sticky / turn-in)
                    // When you turn the camera, next frame W2S promotes them → full ESP ASAP
                    // without paying bone/DMA cost for the whole server list.
                    float fps = ImGui::GetIO().Framerate;
                    size_t kMax = 48;
                    if (fps > 1.f && fps < 45.f) kMax = 28;
                    else if (fps >= 45.f && fps < 70.f) kMax = 40;
                    if (app_settings::config.performance_mode)
                        kMax = (std::min)(kMax, (size_t)30);

                    Matrix vmCull = s_frameCacheValid ? s_viewMatrix
                        : (offset::viewport ? mem.Read<Matrix>(offset::viewport + 0x24C) : Matrix{});
                    const ImVec2 ds = ImGui::GetIO().DisplaySize;
                    const float margin = 80.f; // soft edge: almost in view still counts as "streaming in"

                    if (!validPeds.empty() && !localPos.IsZero()) {
                        struct PedRank {
                            size_t idx;
                            float dist;
                            bool on_screen;
                            float cross;
                        };
                        static std::vector<PedRank> order;
                        static std::vector<uintptr_t> rankedPeds;
                        static std::vector<Vec3> rankedPos;
                        order.clear();
                        order.reserve(validPeds.size());
                        for (size_t i = 0; i < validPeds.size(); ++i) {
                            const float d = positions[i].distance_sq(localPos); // sort by dist²
                            Vec2 sp{};
                            bool on = positions[i].world_to_screen(vmCull, sp);
                            float cross = 1e9f;
                            if (on) {
                                const bool inFrame =
                                    sp.x >= -margin && sp.x <= ds.x + margin &&
                                    sp.y >= -margin && sp.y <= ds.y + margin;
                                on = inFrame;
                                if (on) {
                                    const float cx = sp.x - ds.x * 0.5f;
                                    const float cy = sp.y - ds.y * 0.5f;
                                    cross = sqrtf(cx * cx + cy * cy);
                                }
                            }
                            order.push_back({ i, d, on, cross });
                        }
                        auto rankLess = [](const PedRank& a, const PedRank& b) {
                                if (a.on_screen != b.on_screen) return a.on_screen > b.on_screen;
                                if (a.on_screen) {
                                    if (fabsf(a.cross - b.cross) > 1.f) return a.cross < b.cross;
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
                            rankedPeds.push_back(validPeds[order[n].idx]);
                            rankedPos.push_back(positions[order[n].idx]);
                        }
                        validPeds.swap(rankedPeds);
                        positions.swap(rankedPos);
                    } else if (validPeds.size() > kMax) {
                        validPeds.resize(kMax);
                        if (positions.size() > kMax) positions.resize(kMax);
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
                 esp::config.head_halo || esp::config.look_direction || esp::has_extra_visuals())) ||
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
                    esp::config.head_halo || esp::config.look_direction))
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
                esp::config.look_direction)) || needsFriendData ||
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
