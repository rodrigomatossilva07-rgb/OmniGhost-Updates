#include "esp_manager.h"
#include "../object_esp/object_esp.h"
#include "../object_esp/object_esp_renderer.h"
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
#include <cstdio>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "gameplay/esp_optimizer.h"
#include "gameplay/snapshot_exchange.h"
#include "gameplay/frame_pipeline.h"
#include "gameplay/dma_telemetry_log.h"

namespace FiveM {
    namespace ESP {
        // Global containers (kept for backward compatibility with render consumers)
        std::vector<uintptr_t> rawPedPointers;
        std::vector<Vec3> positions;
        std::vector<uintptr_t> validPeds;
        std::vector<Vec2> screenPositions;

        static OmniGhost::Gameplay::SnapshotExchange<AcquisitionSnapshot, 4> s_snapshots;
        static std::atomic<uint64_t> s_snapshotDrops{0};
        static VMMDLL_SCATTER_HANDLE s_acqScatter = nullptr;
        static std::vector<uintptr_t> s_acquireRawPeds;
        static std::vector<uintptr_t> s_acquireValidPeds;
        static std::vector<Vec3> s_acquirePositions;
        static std::vector<EntityFrame> s_acquireEntityFrames;
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
        static OmniGhost::Gameplay::PipelineTelemetry s_pipelineMetrics;

        // Vehicle acquisition lane
        static OmniGhost::Gameplay::SnapshotExchange<VehicleSnapshot, 4> s_vehicleSnapshots;
        static std::atomic<uint64_t> s_vehicleSnapshotDrops{0};
        static uint64_t s_vehicleGeneration = 0;
        static std::chrono::steady_clock::time_point s_lastVehicleDiscovery{};
        static std::chrono::steady_clock::time_point s_lastVehiclePose{};

        struct PresentationState {
            Vec3 position{};
            Vec3 velocity{};
            uint64_t sourceGeneration = 0;
            bool initialized = false;
        };
        static std::unordered_map<uintptr_t, PresentationState> s_presentation;

        // Performance tracking
        int frameCount = 0;
        std::chrono::steady_clock::time_point lastFrameTime;

        // Per-frame shared caches (avoid re-reading viewport/localpos everywhere)
        static Matrix s_viewMatrix{};
        static Vec3 s_localPos{};
        static bool s_frameCacheValid = false;

        bool FrameCacheValid() { return s_frameCacheValid; }
        const Matrix& GetFrameViewMatrix() { return s_viewMatrix; }
        const Vec3& GetFrameLocalPos() { return s_localPos; }

        const AcquisitionSnapshot* AcquireSnapshot() {
            auto lease = s_snapshots.Acquire();
            return lease ? lease.operator->() : nullptr;
        }

        const VehicleSnapshot* AcquireVehicleSnapshot() {
            auto lease = s_vehicleSnapshots.Acquire();
            return lease ? lease.operator->() : nullptr;
        }

        static void EnsureAcqScatter() {
            if (!s_acqScatter && mem.vHandle)
                s_acqScatter = mem.CreateScatterHandle();
        }

        static void PublishAcquisitionSnapshot(uintptr_t localPlayer, bool cacheValid, float acquireMs) {
            auto slot = s_snapshots.TryBeginWrite();
            if (!slot) {
                s_snapshotDrops.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            const int n = (std::min)(MAX_PEDS, static_cast<int>(s_acquireValidPeds.size()));
            slot.value->count = n;
            for (int i = 0; i < n; ++i) {
                EntityFrame& ef = slot.value->entities[static_cast<size_t>(i)];
                if (i < static_cast<int>(s_acquireEntityFrames.size())) {
                    ef = s_acquireEntityFrames[static_cast<size_t>(i)];
                } else {
                    ef.ped = s_acquireValidPeds[static_cast<size_t>(i)];
                    ef.position = (i < static_cast<int>(s_acquirePositions.size()))
                        ? s_acquirePositions[static_cast<size_t>(i)] : Vec3{};
                    ef.valid = true;
                }
            }
            slot.value->viewMatrix = s_acquireViewMatrix;
            slot.value->localPos = s_acquireLocalPos;
            slot.value->localPlayer = localPlayer;
            slot.value->frameCacheValid = cacheValid;
            slot.value->generation = ++s_acquisitionGeneration;
            slot.value->timestamp = std::chrono::steady_clock::now();
            slot.value->acquireMs = acquireMs;
            s_snapshots.Publish(slot.index);
        }

        // Vehicle data collection for acquisition lane
        static void CollectVehicleData(uintptr_t localPlayer, const Vec3& localPos, bool discovery) {
            if (!localPlayer || !offset::replay || !offset::viewport)
                return;

            const int MAX_VEHICLES = 64;
            constexpr uintptr_t VEHICLE_LIST_OFFSET = 0x180;
            constexpr uintptr_t VEHICLE_COUNT_OFFSET = 0x188;
            constexpr uintptr_t VEHICLE_ENTRY_STRIDE = 0x10;
            constexpr uintptr_t VEHICLE_POSITION_OFFSET = 0x90;
            constexpr uintptr_t VEHICLE_MATRIX_OFFSET = 0x60;

            auto IsValidPtr = [](uintptr_t p) {
                return p > 0x10000ULL && p < 0x7FFFFFFFFFFFULL;
            };

            auto vSlot = s_vehicleSnapshots.TryBeginWrite();
            if (!vSlot) {
                s_vehicleSnapshotDrops.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            auto handle = s_acqScatter ? s_acqScatter : mem.CreateScatterHandle();
            const bool ownedHandle = (handle != s_acqScatter);

            Matrix view_matrix{};
            uintptr_t vehicle_interface = 0;
            uintptr_t vehicle_interface_alt = 0;

            mem.AddScatterReadRequest(handle, offset::viewport + 0x24C, &view_matrix, sizeof(Matrix));
            mem.AddScatterReadRequest(handle, offset::replay + 0x10, &vehicle_interface, sizeof(uintptr_t));
            mem.AddScatterReadRequest(handle, offset::replay + 0xD10, &vehicle_interface_alt, sizeof(uintptr_t));
            mem.ExecuteReadScatter(handle);

            if (!IsValidPtr(vehicle_interface) && IsValidPtr(vehicle_interface_alt))
                vehicle_interface = vehicle_interface_alt;
            if (!IsValidPtr(vehicle_interface)) {
                if (ownedHandle && handle) mem.CloseScatterHandle(handle);
                vSlot.value->count = 0;
                vSlot.value->localPos = localPos;
                vSlot.value->generation = ++s_vehicleGeneration;
                vSlot.value->timestamp = std::chrono::steady_clock::now();
                s_vehicleSnapshots.Publish(vSlot.index);
                return;
            }

            uintptr_t vehicleListBase = 0, vehicleListBaseAlt = 0;
            int vehicleCount = 0, vehicleCountAlt = 0;
            mem.AddScatterReadRequest(handle, vehicle_interface + VEHICLE_LIST_OFFSET, &vehicleListBase, sizeof(uintptr_t));
            mem.AddScatterReadRequest(handle, vehicle_interface + VEHICLE_COUNT_OFFSET, &vehicleCount, sizeof(int));
            if (IsValidPtr(vehicle_interface_alt) && vehicle_interface_alt != vehicle_interface) {
                mem.AddScatterReadRequest(handle, vehicle_interface_alt + VEHICLE_LIST_OFFSET, &vehicleListBaseAlt, sizeof(uintptr_t));
                mem.AddScatterReadRequest(handle, vehicle_interface_alt + VEHICLE_COUNT_OFFSET, &vehicleCountAlt, sizeof(int));
            }
            mem.ExecuteReadScatter(handle);

            auto validList = [&](uintptr_t list, int count) {
                return IsValidPtr(list) && count > 0 && count <= 2048;
            };
            if (!validList(vehicleListBase, vehicleCount) && validList(vehicleListBaseAlt, vehicleCountAlt)) {
                vehicle_interface = vehicle_interface_alt;
                vehicleListBase = vehicleListBaseAlt;
                vehicleCount = vehicleCountAlt;
            }
            if (!IsValidPtr(vehicleListBase)) {
                if (ownedHandle && handle) mem.CloseScatterHandle(handle);
                vSlot.value->count = 0;
                vSlot.value->localPos = localPos;
                vSlot.value->generation = ++s_vehicleGeneration;
                vSlot.value->timestamp = std::chrono::steady_clock::now();
                s_vehicleSnapshots.Publish(vSlot.index);
                return;
            }

            const int listCount = (vehicleCount > 0 && vehicleCount <= 2048)
                ? (std::min)(vehicleCount, MAX_VEHICLES) : MAX_VEHICLES;
            std::vector<uintptr_t> rawPtrs(listCount, 0);
            for (int i = 0; i < listCount; ++i)
                mem.AddScatterReadRequest(handle, vehicleListBase + (uintptr_t)i * VEHICLE_ENTRY_STRIDE,
                                          &rawPtrs[i], sizeof(uintptr_t));
            mem.ExecuteReadScatter(handle);

            std::vector<uintptr_t> valid;
            valid.reserve(MAX_VEHICLES);
            for (int i = 0; i < listCount; ++i)
                if (IsValidPtr(rawPtrs[i]))
                    valid.push_back(rawPtrs[i]);

            if (valid.empty()) {
                if (ownedHandle && handle) mem.CloseScatterHandle(handle);
                vSlot.value->count = 0;
                vSlot.value->localPos = localPos;
                vSlot.value->generation = ++s_vehicleGeneration;
                vSlot.value->timestamp = std::chrono::steady_clock::now();
                s_vehicleSnapshots.Publish(vSlot.index);
                return;
            }

            // For discovery, we need positions; for pose update, we need matrices + gear/engine
            std::vector<Vec3> vehiclePositions(valid.size());
            std::vector<Matrix> matrices;
            std::vector<uint32_t> lockState;
            std::vector<uintptr_t> driverPrimary, driverFallback;

            bool needMatrix = discovery ? false : true; // Only need matrix for pose updates
            bool needLock = discovery;
            bool needOccupied = discovery;
            bool needGearEngine = !discovery;

            if (needMatrix) matrices.resize(valid.size());
            if (needLock) lockState.assign(valid.size(), UINT32_MAX);
            if (needOccupied) {
                driverPrimary.assign(valid.size(), 0);
                driverFallback.assign(valid.size(), 0);
            }

            for (size_t i = 0; i < valid.size(); ++i) {
                mem.AddScatterReadRequest(handle, valid[i] + VEHICLE_POSITION_OFFSET, &vehiclePositions[i], sizeof(Vec3));
                if (needMatrix)
                    mem.AddScatterReadRequest(handle, valid[i] + VEHICLE_MATRIX_OFFSET, &matrices[i], sizeof(Matrix));
                if (needLock)
                    mem.AddScatterReadRequest(handle, valid[i] + offset::vehicleLock, &lockState[i], sizeof(uint32_t));
                if (needOccupied) {
                    const uintptr_t buildDriver = offset::buildVersion >= 3751 ? 0xCA8 : offset::vehicleDriver;
                    mem.AddScatterReadRequest(handle, valid[i] + buildDriver, &driverPrimary[i], sizeof(uintptr_t));
                    if (buildDriver != offset::vehicleDriver)
                        mem.AddScatterReadRequest(handle, valid[i] + offset::vehicleDriver, &driverFallback[i], sizeof(uintptr_t));
                }
            }
            mem.ExecuteReadScatter(handle);

            // Ped->vehicle for occupied check
            std::vector<uintptr_t> pedVehicles;
            if (needOccupied && !s_acquireValidPeds.empty()) {
                pedVehicles.assign(s_acquireValidPeds.size(), 0);
                for (size_t i = 0; i < s_acquireValidPeds.size(); ++i)
                    mem.AddScatterReadRequest(handle, s_acquireValidPeds[i] + offset::pedVehicle, &pedVehicles[i], sizeof(uintptr_t));
                mem.ExecuteReadScatter(handle);
            }

            std::unordered_set<uintptr_t> occupiedVehicles;
            occupiedVehicles.reserve(pedVehicles.size() * 2 + valid.size());
            for (uintptr_t vehicle : pedVehicles)
                if (IsValidPtr(vehicle)) occupiedVehicles.insert(vehicle);
            for (size_t i = 0; i < valid.size(); ++i) {
                uintptr_t driver = IsValidPtr(driverPrimary[i]) ? driverPrimary[i] : driverFallback[i];
                if (IsValidPtr(driver)) occupiedVehicles.insert(valid[i]);
            }

            // Gear/Engine: slower batch (~80ms)
            std::vector<int8_t> gears;
            std::vector<float> engines;
            if (needGearEngine) {
                gears.resize(valid.size(), 0);
                engines.resize(valid.size(), 0.f);
                auto gearHandle = mem.CreateScatterHandle();
                for (size_t i = 0; i < valid.size(); ++i) {
                    mem.AddScatterReadRequest(gearHandle, valid[i] + offset::vehicleGear, &gears[i], sizeof(int8_t));
                    mem.AddScatterReadRequest(gearHandle, valid[i] + offset::vehicleEngineHp, &engines[i], sizeof(float));
                }
                mem.ExecuteReadScatter(gearHandle);
                mem.CloseScatterHandle(gearHandle);
            }

            // Populate vehicle snapshot
            int count = 0;
            for (size_t i = 0; i < valid.size() && count < MAX_VEHICLES; ++i) {
                if (vehiclePositions[i].IsZero()) continue;
                float dist = vehiclePositions[i].distance_to(localPos);
                if (dist > 500.f || dist < 0.1f) continue; // Default max distance

                bool occupied = occupiedVehicles.find(valid[i]) != occupiedVehicles.end();

                auto& vd = vSlot.value->vehicles[static_cast<size_t>(count)];
                vd.address = valid[i];
                vd.position = positions[i];
                vd.distance = dist;
                vd.locked = (lockState[i] <= 10u && lockState[i] >= 2);
                vd.lock_state_known = (lockState[i] <= 10u);
                vd.occupied = occupied;
                if (needMatrix && i < matrices.size()) vd.matrix = matrices[i];
                if (needGearEngine) {
                    vd.gear = gears[i];
                    vd.engine_hp = engines[i];
                }
                vd.valid = true;
                count++;
            }
            vSlot.value->count = count;
            vSlot.value->localPos = localPos;
            vSlot.value->generation = ++s_vehicleGeneration;
            vSlot.value->timestamp = std::chrono::steady_clock::now();
            s_vehicleSnapshots.Publish(vSlot.index);

            if (ownedHandle && handle) mem.CloseScatterHandle(handle);
        }

        static void AcquisitionLoop() {
            uintptr_t localPlayer = offset::localplayer;
            OmniGhost::Gameplay::FixedRateScheduler scheduler;
            while (!s_acquisitionStop.load(std::memory_order_acquire)) {
                const auto acquireBegin = std::chrono::steady_clock::now();
                const bool needPeds = s_needPeds.load(std::memory_order_relaxed);
                bool cacheValid = false;

                if (needPeds && offset::world && offset::replay && offset::viewport) {
                    EnsureAcqScatter();
                    uintptr_t freshLp = 0;
                    if (s_acqScatter) {
                        mem.AddScatterReadRequest(s_acqScatter, offset::world + 0x8, &freshLp, sizeof(uintptr_t));
                        mem.AddScatterReadRequest(s_acqScatter, offset::viewport + 0x24C,
                                                  &s_acquireViewMatrix, sizeof(Matrix));
                        mem.ExecuteReadScatter(s_acqScatter);
                    }
                    if (freshLp) localPlayer = freshLp;

                    if (localPlayer) {
                        if (s_acqScatter) {
                            mem.AddScatterReadRequest(s_acqScatter, localPlayer + offset::playerPosition,
                                                      &s_acquireLocalPos, sizeof(Vec3));
                            mem.ExecuteReadScatter(s_acqScatter);
                        }
                        cacheValid = true;
                        collectFrameData(localPlayer, s_acquireLocalPos);
                        // Producer-only DMA for ped identity / visibility / prepared ESP.
                        // Presentation must consume caches without further mem.Read.
                        if (!s_acquireValidPeds.empty()) {
                            g_pedCacheManager.fastCache(s_acquireValidPeds, s_acquirePositions);
                            g_pedCacheManager.update();
                            static std::vector<bool> prodVis;
                            const bool needVis = esp::config.visibility_colors || esp::config.visible_check
                                || aimbot::config.visible_check;
                            if (needVis)
                                FiveM::Visibility::BatchCheckVisibility(s_acquireValidPeds, prodVis);

                            // Prepare full entity frames (health, armor, weapon, vehicle, bones, visibility)
                            esp::prepare_entity_frames(s_acquireValidPeds, s_acquirePositions, s_acquireEntityFrames);

                            // Stamp visibility results into entity frames
                            if (needVis && prodVis.size() == s_acquireEntityFrames.size()) {
                                for (size_t i = 0; i < s_acquireEntityFrames.size(); ++i) {
                                    s_acquireEntityFrames[i].visible = prodVis[i];
                                }
                            }
                        }
                    } else {
                        s_acquireValidPeds.clear();
                        s_acquirePositions.clear();
                        s_acquireEntityFrames.clear();
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
                        s_acquireEntityFrames.clear();
                        s_acquireLocalPos = {};
                        s_emptyFrames = 0;
                    }

                    // Vehicle acquisition lane (separate rate: discovery ~100ms, pose ~33ms)
                    static bool s_vehicleDiscoveryDue = true;
                    static bool s_vehiclePoseDue = true;
                    auto nowAcq = std::chrono::steady_clock::now();
                    if (s_vehicleDiscoveryDue || nowAcq - s_lastVehicleDiscovery >= std::chrono::milliseconds(100)) {
                        s_vehicleDiscoveryDue = false;
                        s_lastVehicleDiscovery = nowAcq;
                        CollectVehicleData(localPlayer, s_acquireLocalPos, true);
                    }
                    if (s_vehiclePoseDue || nowAcq - s_lastVehiclePose >= std::chrono::milliseconds(33)) {
                        s_vehiclePoseDue = false;
                        s_lastVehiclePose = nowAcq;
                        CollectVehicleData(localPlayer, s_acquireLocalPos, false);
                    }

                    const float acquireMs = OmniGhost::Gameplay::TimeMs(acquireBegin);
                OmniGhost::Gameplay::PipelineTelemetry::Smooth(
                    s_pipelineMetrics.acquire_ms, acquireMs);
                s_pipelineMetrics.entities.store(
                    static_cast<int>(s_acquireValidPeds.size()), std::memory_order_relaxed);
                PublishAcquisitionSnapshot(localPlayer, cacheValid, acquireMs);
                {
                    auto& tel = OmniGhost::Gameplay::DmaTelemetry::FiveM();
                    OmniGhost::Gameplay::DmaTelemetry::ObserveAcquire(tel, acquireMs, !s_acquireValidPeds.empty());
                    tel.entities.store(static_cast<int>(s_acquireValidPeds.size()), std::memory_order_relaxed);
                    tel.snapshot_drops.store(s_snapshotDrops.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    tel.dma_open.store(mem.vHandle != nullptr, std::memory_order_relaxed);
                }

                int delayMs = 20;
                if (needPeds) {
                    const float fps = s_renderFps.load(std::memory_order_relaxed);
                    delayMs = (fps > 1.f && fps < 45.f) ? 16
                            : (fps > 1.f && fps < 90.f) ? 18
                            : 20;
                    if (s_performanceMode.load(std::memory_order_relaxed))
                        delayMs = (std::max)(delayMs, 24);
                }
                scheduler.Wait(std::chrono::milliseconds(delayMs));
            }
        }

        void EnsureAcquisitionStarted() {
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
            if (s_acqScatter) {
                mem.CloseScatterHandle(s_acqScatter);
                s_acqScatter = nullptr;
            }
            s_acquisitionRunning.store(false, std::memory_order_release);
            s_acquireRawPeds.clear();
            s_acquireValidPeds.clear();
            s_acquirePositions.clear();
            s_acquireEntityFrames.clear();
            s_lastGoodPeds.clear();
            s_lastGoodPos.clear();
            s_acquireViewMatrix = {};
            s_acquireLocalPos = {};
            s_emptyFrames = 0;
            PublishAcquisitionSnapshot(0, false, 0.f);
        }

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
                s_acquireEntityFrames.reserve(MAX_PEDS);
                initialized = true;
            }
        }

        void RunESP() {
            InitializeContainers();
            frameCount++;
            auto currentTime = std::chrono::steady_clock::now();
            const auto renderBegin = currentTime;

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
            static AcquisitionSnapshot previousSnapshot;
            static AcquisitionSnapshot currentSnapshot;
            auto snapshot = s_snapshots.Acquire();
            if (snapshot && snapshot->generation != consumedGeneration) {
                previousSnapshot = std::move(currentSnapshot);
                currentSnapshot = *snapshot;
                s_viewMatrix = snapshot->viewMatrix;
                s_localPos = snapshot->localPos;
                s_frameCacheValid = snapshot->frameCacheValid;
                offset::localplayer = snapshot->localPlayer;
                consumedGeneration = snapshot->generation;
            }

            // Fixed storage snapshot → vectors for existing render consumers
            validPeds.resize(static_cast<size_t>(std::max(0, currentSnapshot.count)));
            positions.resize(validPeds.size());
            static std::unordered_map<uintptr_t, int> prevIndex;
            if (previousSnapshot.generation != currentSnapshot.generation) {
                prevIndex.clear();
                for (int pi = 0; pi < previousSnapshot.count; ++pi)
                    prevIndex[previousSnapshot.entities[pi].ped] = pi;
            }
            const float presentDt = std::clamp(ImGui::GetIO().DeltaTime, .001f, .033f);
            const float snapshotAge = currentSnapshot.timestamp.time_since_epoch().count()
                ? std::clamp(std::chrono::duration<float>(currentTime - currentSnapshot.timestamp).count(), 0.f, .008f)
                : 0.f;
            for (int i = 0; i < currentSnapshot.count; ++i) {
                const EntityFrame& ef = currentSnapshot.entities[static_cast<size_t>(i)];
                validPeds[static_cast<size_t>(i)] = ef.ped;
                const Vec3& raw = ef.position;
                auto& presentation = s_presentation[ef.ped];
                if (!presentation.initialized) {
                    presentation.position = raw;
                    presentation.initialized = true;
                }
                if (presentation.sourceGeneration != currentSnapshot.generation &&
                    previousSnapshot.generation && currentSnapshot.timestamp > previousSnapshot.timestamp) {
                    const auto it = prevIndex.find(ef.ped);
                    if (it != prevIndex.end() && it->second >= 0 && it->second < previousSnapshot.count) {
                        const float interval = std::chrono::duration<float>(
                            currentSnapshot.timestamp - previousSnapshot.timestamp).count();
                        if (interval > .001f)
                            presentation.velocity =
                                (raw - previousSnapshot.entities[it->second].position) * (1.f / interval);
                    }
                    presentation.sourceGeneration = currentSnapshot.generation;
                }
                Vec3 target = raw + presentation.velocity * snapshotAge;
                const Vec3 delta = target - presentation.position;
                const float distanceSq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
                if (distanceSq > 9.f) {
                    presentation.position = raw;
                } else {
                    const float speedSq = presentation.velocity.x * presentation.velocity.x +
                        presentation.velocity.y * presentation.velocity.y + presentation.velocity.z * presentation.velocity.z;
                    const float tau = speedSq > 9.f ? .007f : .014f;
                    const float alpha = 1.f - std::exp(-presentDt / tau);
                    presentation.position = presentation.position + delta * alpha;
                }
                positions[static_cast<size_t>(i)] = presentation.position;
            }

            // Detect ESP/aim feature bit changes for telemetry correlation
            if (OmniGhost::Gameplay::DmaTelemetry::IsEnabled()) {
                static uint64_t s_cfgBits = 0;
                uint64_t bits = 0;
                auto bit = [&](bool v, int i) { if (v) bits |= (1ull << i); };
                bit(esp::config.enabled, 0); bit(esp::config.skeleton, 1);
                bit(esp::config.box_2d, 2); bit(esp::config.health_bar, 3);
                bit(esp::config.armor_bar, 4); bit(esp::config.weapon_name, 5);
                bit(esp::config.visibility_colors || esp::config.visible_check, 6);
                bit(esp::config.trails, 7); bit(esp::config.angel_wings, 8);
                bit(esp::config.head_halo, 9); bit(esp::config.floating_crown, 10);
                bit(esp::config.triangle_radar || esp::config.radar_enabled, 11); bit(aimbot::config.aimbot_enabled, 12);
                bit(object_esp::GetObjectESPManager().GetConfig().enabled, 13);
                if (bits != s_cfgBits) {
                    s_cfgBits = bits;
                    char blob[256];
                    std::snprintf(blob, sizeof(blob),
                        "skeleton=%d\nbox=%d\nhealth=%d\narmor=%d\nweapon=%d\nvisibility=%d\n"
                        "trails=%d\nwings=%d\nhalo=%d\ncrown=%d\nvehicles=%d\nobjects=%d\naim=%d",
                        esp::config.skeleton, esp::config.box_2d, esp::config.health_bar, esp::config.armor_bar,
                        esp::config.weapon_name, (esp::config.visibility_colors || esp::config.visible_check) ? 1 : 0,
                        esp::config.trails, esp::config.angel_wings, esp::config.head_halo, esp::config.floating_crown,
                        (esp::config.triangle_radar || esp::config.radar_enabled) ? 1 : 0,
                        object_esp::GetObjectESPManager().GetConfig().enabled ? 1 : 0,
                        aimbot::config.aimbot_enabled ? 1 : 0);
                    OmniGhost::Gameplay::DmaTelemetry::LogConfigChanged("FIVEM", blob);
                }
            }

            renderESP();

            // Object ESP (scan / track / draw) — independent from player ESP master toggle
            try {
                auto& objEsp = object_esp::GetObjectESPManager();
                if (!objEsp.IsInitialized())
                    objEsp.Initialize();
                objEsp.Update();
                if (objEsp.GetConfig().enabled && s_frameCacheValid) {
                    object_esp::GetObjectRenderer().Render(s_viewMatrix, offset::localplayer);
                }
            } catch (const std::exception& ex) {
                std::cerr << "[FiveM][ObjectESP] frame exception: " << ex.what() << std::endl;
            } catch (...) {
                std::cerr << "[FiveM][ObjectESP] frame exception (unknown)" << std::endl;
            }

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
                if (s_frameCacheValid)
                    esp::DrawPlayerRadar(s_viewMatrix, offset::localplayer);
            }
            // Waypoint/blip less critical — ~5 Hz is fine
            static double lastWp = 0.0;
            const double nowSec = ImGui::GetTime();
            if (offset::localplayer && (esp::config.waypoint_line || esp::config.blip_esp)) {
                if (nowSec - lastWp >= 0.20) {
                    lastWp = nowSec;
                    if (s_frameCacheValid)
                        esp::DrawWaypointAndBlips(s_viewMatrix, offset::localplayer);
                }
            }

            vehicle_esp::Run();
            aimbot::Run();

            const float frameMs = std::chrono::duration<float, std::milli>(
                currentTime - lastFrameTime).count();
            lastFrameTime = currentTime;
            const float renderMs = OmniGhost::Gameplay::TimeMs(renderBegin);
            OmniGhost::Gameplay::PipelineTelemetry::Smooth(s_pipelineMetrics.render_ms, renderMs);
            {
                auto& tel = OmniGhost::Gameplay::DmaTelemetry::FiveM();
                OmniGhost::Gameplay::DmaTelemetry::ObserveRender(tel, renderMs);
                tel.entities.store(static_cast<int>(validPeds.size()), std::memory_order_relaxed);
                tel.snapshot_drops.store(s_snapshotDrops.load(std::memory_order_relaxed), std::memory_order_relaxed);
            }

            s_pipelineMetrics.RecordFrame(frameMs);
        }

        // Producer-side data collection.
        void collectFrameData(uintptr_t localPlayer, const Vec3& localPos) {
            auto& writeRawPeds = s_acquireRawPeds;
            auto& writePeds = s_acquireValidPeds;
            auto& writePositions = s_acquirePositions;
            if (!offset::world || !offset::replay || !offset::viewport || !localPlayer) {
                writePeds.clear();
                writePositions.clear();
                return;
            }
            writePeds.clear();
            writePositions.clear();

            EnsureAcqScatter();
            auto handle = s_acqScatter ? s_acqScatter : mem.CreateScatterHandle();
            const bool ownedHandle = (handle != s_acqScatter);

            static uintptr_t ped_replay_interface = 0;
            static uintptr_t pedListBase = 0;
            static ULONGLONG nextChainRefresh = 0;
            const ULONGLONG now = GetTickCount64();

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
                    const int listCap = MAX_PEDS;
                    if ((int)writeRawPeds.size() != listCap)
                        writeRawPeds.resize(listCap);

                    mem.AddScatterReadRequest(handle, pedListBase,
                        writeRawPeds.data(), sizeof(uintptr_t) * listCap);
                    mem.ExecuteReadScatter(handle);

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
                            writePeds.push_back(ped);
                            continue;
                        }
                        if (playerInfoPtrs[i]) {
                            writePeds.push_back(ped);
                        } else if (s_npcEsp.load(std::memory_order_relaxed)) {
                            writePeds.push_back(ped);
                        } else if (isLocal && s_selfEsp.load(std::memory_order_relaxed)) {
                            writePeds.push_back(ped);
                        }
                    }

                    writePositions.clear();
                    if (!writePeds.empty()) {
                        writePositions.resize(writePeds.size());
                        for (size_t i = 0; i < writePeds.size(); i++) {
                            mem.AddScatterReadRequest(handle, writePeds[i] + offset::playerPosition,
                                &writePositions[i], sizeof(Vec3));
                        }
                        mem.ExecuteReadScatter(handle);

                        std::vector<uintptr_t> alivePeds;
                        std::vector<Vec3> alivePos;
                        alivePeds.reserve(writePeds.size());
                        alivePos.reserve(writePeds.size());
                        for (size_t i = 0; i < writePeds.size(); i++) {
                            const Vec3& p = writePositions[i];
                            if (p.IsZero()) continue;
                            if (p.x < -10000.f || p.x > 10000.f || p.y < -10000.f || p.y > 10000.f)
                                continue;
                            if (p.z < -500.f || p.z > 3000.f)
                                continue;
                            const float maxDistance = s_maxDistance.load(std::memory_order_relaxed);
                            if (!localPos.IsZero()) {
                                if (maxDistance <= 0.f)
                                    continue;
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
                    const float fps = s_renderFps.load(std::memory_order_relaxed);
                    size_t kMax = 48;
                    if (fps > 1.f && fps < 45.f) kMax = 28;
                    else if (fps >= 45.f && fps < 70.f) kMax = 40;
                    if (s_performanceMode.load(std::memory_order_relaxed))
                        kMax = (std::min)(kMax, (size_t)30);

                    const Matrix vmCull = s_acquireViewMatrix;
                    const ImVec2 ds(s_displayWidth.load(std::memory_order_relaxed),
                                    s_displayHeight.load(std::memory_order_relaxed));
                    const float margin = 80.f;

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
                            const float d = writePositions[i].distance_sq(localPos);
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

            if (ownedHandle && handle) mem.CloseScatterHandle(handle);
        }

        // Rendering operations (now with batch skeleton support)
        void renderESP() {
            if (validPeds.empty() || positions.empty())
                return;
            if (!offset::viewport || !offset::localplayer)
                return;
            if (!s_frameCacheValid)
                return;
            Matrix view_matrix = s_viewMatrix;
            Vec3 localPos = s_localPos;

            const float maxDist = esp::config.max_esp_distance;
            const float maxDistSq = (maxDist > 0.f) ? (maxDist * maxDist) : 0.f;

            // Visibility stamped on acquisition thread — no DMA here.
            static std::vector<bool> frameVisibility;
            const bool needsEspVisibility = esp::config.enabled &&
                (esp::config.skeleton || esp::config.head_circle || esp::config.trails ||
                 esp::config.head_halo || esp::config.look_direction || esp::config.chinese_hat ||
                 esp::config.angel_wings || esp::config.devil_horns || esp::config.floating_crown || esp::has_extra_visuals()) &&
                (esp::config.visibility_colors || esp::config.visible_check);
            if (needsEspVisibility || aimbot::config.visible_check) {
                frameVisibility.resize(validPeds.size(), true);
                // Visibility already computed on acquisition thread and stamped in EntityFrame
            }

            if (!esp::config.enabled)
                return;

            for (const uintptr_t ped : validPeds) {
                if (ped)
                    esp::render_esp_for_ped(ped, view_matrix, offset::localplayer);
            }
            (void)maxDistSq;
        }

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

        void refreshCache() {
            g_pedCacheManager.manualCache();
        }

        // Prepared data access for aimbot integration (read from current snapshot)
        bool try_get_prepared_origin(uintptr_t ped, Vec3& out) {
            auto snap = s_snapshots.Acquire();
            if (!snap) return false;
            for (int i = 0; i < snap->count; ++i) {
                if (snap->entities[i].ped == ped && snap->entities[i].valid) {
                    out = snap->entities[i].position;
                    return true;
                }
            }
            return false;
        }

        bool try_get_prepared_velocity(uintptr_t ped, Vec3& out) {
            const auto it = s_presentation.find(ped);
            if (it == s_presentation.end() || !it->second.initialized)
                return false;
            out = it->second.velocity;
            return true;
        }

        bool try_get_prepared_health(uintptr_t ped, float& out) {
            auto snap = s_snapshots.Acquire();
            if (!snap) return false;
            for (int i = 0; i < snap->count; ++i) {
                if (snap->entities[i].ped == ped && snap->entities[i].valid) {
                    out = snap->entities[i].health;
                    return snap->entities[i].health > 0.f && snap->entities[i].health < 1000.f;
                }
            }
            return false;
        }

        bool try_get_prepared_armor(uintptr_t ped, float& out) {
            auto snap = s_snapshots.Acquire();
            if (!snap) return false;
            for (int i = 0; i < snap->count; ++i) {
                if (snap->entities[i].ped == ped && snap->entities[i].valid) {
                    out = snap->entities[i].armor;
                    return true;
                }
            }
            return false;
        }

        bool try_get_prepared_weapon(uintptr_t ped, uint32_t& out) {
            auto snap = s_snapshots.Acquire();
            if (!snap) return false;
            for (int i = 0; i < snap->count; ++i) {
                if (snap->entities[i].ped == ped && snap->entities[i].valid) {
                    out = snap->entities[i].weapon_hash;
                    return true;
                }
            }
            return false;
        }

        bool try_get_prepared_vehicle(uintptr_t ped, uintptr_t& out) {
            auto snap = s_snapshots.Acquire();
            if (!snap) return false;
            for (int i = 0; i < snap->count; ++i) {
                if (snap->entities[i].ped == ped && snap->entities[i].valid) {
                    out = snap->entities[i].vehicle;
                    return true;
                }
            }
            return false;
        }

        bool try_get_prepared_bone_position(uintptr_t ped, int bone, Vec3& out) {
            return ::esp::try_get_prepared_bone_position(ped, bone, out);
        }

        bool try_get_prepared_visibility(uintptr_t ped, bool& out) {
            auto snap = s_snapshots.Acquire();
            if (!snap) return false;
            for (int i = 0; i < snap->count; ++i) {
                if (snap->entities[i].ped == ped && snap->entities[i].valid) {
                    out = snap->entities[i].visible;
                    return true;
                }
            }
            return false;
        }

        bool try_get_prepared_network_id(uintptr_t ped, uint32_t& out) {
            auto snap = s_snapshots.Acquire();
            if (!snap) return false;
            for (int i = 0; i < snap->count; ++i) {
                if (snap->entities[i].ped == ped && snap->entities[i].valid) {
                    out = snap->entities[i].network_id;
                    return true;
                }
            }
            return false;
        }
    }
}