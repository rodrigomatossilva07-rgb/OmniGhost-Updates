#include "esp_manager.h"
#include "config/app_settings.h"
#include "src/project/third_party/ImGui/imgui.h"
#include "offsets.h"
#include "../esp/esp.h"
#include "../esp/vehicle_esp.h"
#include "../radar/fivem_web_radar.h"
#include "../collision/fivem_collision_world.h"
#include "config/config_manager.h"
#include "../aimbot/aimbot.h"
#include "../friends/friends.h"
#include "visibility.h"
#include "../playerInfo/PedData.h"
#include "src/project/third_party/DMALibrary/Memory/Memory.h"
#include "globals.h"
#include <iostream>
#include <cstdio>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include "gameplay/snapshot_exchange.h"
#include "gameplay/frame_pipeline.h"
#include "gameplay/dma_telemetry_log.h"
#include "gameplay/camera_priority.h"

namespace FiveM {
    namespace ESP {

        const int MAX_PEDS = 110;






        constexpr uint32_t kPedConfigFlagsKnockedWordBytes = 2 * sizeof(uint32_t);
        constexpr uint32_t kPedConfigFlagsKnockedBit = 1u << 7;


        std::vector<uintptr_t> rawPedPointers;
        std::vector<Vec3> positions;
        std::vector<uintptr_t> validPeds;
        std::vector<Vec2> screenPositions;

        static std::mutex s_validPedsSnapshotMutex;
        static ValidPedsSnapshot s_validPedsSnapshot;

        void UpdateValidPedsSnapshot(const std::vector<uintptr_t>& peds, const std::vector<Vec3>& pos, uint64_t gen) {
            std::scoped_lock lock(s_validPedsSnapshotMutex);
            s_validPedsSnapshot.validPeds = peds;
            s_validPedsSnapshot.positions = pos;
            s_validPedsSnapshot.generation = gen;
            s_validPedsSnapshot.timestamp = std::chrono::steady_clock::now();
        }

        ValidPedsSnapshot GetValidPedsSnapshot() noexcept {
            std::scoped_lock lock(s_validPedsSnapshotMutex);
            return s_validPedsSnapshot;
        }






        struct AcquisitionSnapshot {
            std::array<uintptr_t, MAX_PEDS> validPeds{};
            std::array<Vec3, MAX_PEDS> positions{};
            std::array<Vec3, MAX_PEDS> velocities{};
            std::array<float, MAX_PEDS> healths{};
            std::array<bool, MAX_PEDS> knocked{};
            int count = 0;
            Matrix viewMatrix{};
            Vec3 localPos{};
            uintptr_t localPlayer = 0;
            bool frameCacheValid = false;
            uint64_t generation = 0;
            uint64_t sessionGeneration = 0;
            std::chrono::steady_clock::time_point timestamp{};
            std::chrono::steady_clock::time_point worldSampleTime{};
            std::chrono::steady_clock::time_point cameraSampleTime{};
            float acquireMs = 0.f;
        };

        struct CameraSnapshot {
            Matrix viewMatrix{};
            Vec3 position{};
            std::chrono::steady_clock::time_point sampleTime{};
            uint64_t sampleMs = 0;
            uint64_t sessionGeneration = 0;
            std::array<Matrix, 12> historyMatrices{};
            std::array<std::chrono::steady_clock::time_point, 12> historyTimes{};
            unsigned historyCount = 0;
        };

        static OmniGhost::Gameplay::SnapshotExchange<AcquisitionSnapshot, 4> s_snapshots;
        static OmniGhost::Gameplay::SnapshotExchange<CameraSnapshot, 3> s_cameraSnapshots;
        static std::atomic<uint64_t> s_cameraDeadlineMs{0};
        static std::atomic_bool s_cameraPending{false};
        static std::atomic<int> s_pressureLevel{0};
        struct AcquisitionOptions {
            esp::Config espConfig{};
            aimbot::Config aimConfig{};
            vehicle_esp::Config vehicleConfig{};
        };
        static OmniGhost::Gameplay::SnapshotExchange<AcquisitionOptions> s_optionSnapshots;
        static std::atomic<uint64_t> s_snapshotDrops{0};
        static VMMDLL_SCATTER_HANDLE s_acqScatter = nullptr;





        static std::vector<uintptr_t> s_acquireRawPeds;
        static std::vector<uintptr_t> s_acquireValidPeds;
        static std::vector<Vec3> s_acquirePositions;
        static std::vector<Vec3> s_acquireVelocities;
        static std::vector<float> s_acquireHealths;
        static std::vector<uint32_t> s_acquireKnockFlags;
        static Matrix s_acquireViewMatrix{};
        static Vec3 s_acquireLocalPos{};
        static std::chrono::steady_clock::time_point s_acquireWorldSampleTime{};
        static std::atomic_bool s_acquisitionRunning{false};
        static std::atomic_bool s_acquisitionStop{false};
        static std::atomic_bool s_needPeds{false};
        static std::atomic_bool s_selfEsp{false};
        static std::atomic_bool s_npcEsp{false};
        static std::atomic_bool s_performanceMode{false};
        static std::atomic<float> s_maxDistance{500.f};
        static std::atomic<int> s_lastRawPedCount{0};
        static std::atomic<int> s_lastFilteredPedCount{0};

        int LastRawPedCount() noexcept { return s_lastRawPedCount.load(std::memory_order_relaxed); }
        int LastFilteredPedCount() noexcept { return s_lastFilteredPedCount.load(std::memory_order_relaxed); }
        static std::atomic<float> s_displayWidth{1920.f};
        static std::atomic<float> s_displayHeight{1080.f};
        static std::thread s_acquisitionThread;
        static std::thread s_cameraThread;
        static std::thread s_livenessThread;
        static std::thread s_optionalThread;
        static std::atomic_bool s_livenessStop{false};
        static std::atomic_bool s_cameraStop{false};
        static std::atomic_bool s_optionalStop{false};
        static std::atomic_bool s_optionalBusy{false};
        static std::atomic_bool s_optionalPending{false};
        static std::mutex s_optionalMu;
        static std::condition_variable s_optionalCv;
        struct OptionalJob {
            std::vector<uintptr_t> peds;
            std::vector<Vec3> positions;
            std::vector<float> healths;
            Vec3 localPos{};
            uint16_t boneMask = 0;
            esp::Config espConfig{};
            aimbot::Config aimConfig{};
            vehicle_esp::Config vehicleConfig{};
            bool friendsListRequested = false;
            int pressure = 0;
            uint64_t sessionGeneration = 0;
        };
        static OptionalJob s_optionalJob;
        static std::atomic<ULONGLONG> s_livenessLastSeenMs{0};
        static uint64_t s_acquisitionGeneration = 0;
        std::atomic<uint64_t> s_sessionGeneration{1};
        static OmniGhost::Gameplay::PipelineTelemetry s_pipelineMetrics;

        struct PresentationState {
            Vec3 position{};
            Vec3 velocity{};
            uint64_t sourceGeneration = 0;
            uint64_t entry_generation = 0;
            bool initialized = false;
        };




        static std::unordered_map<uintptr_t, PresentationState> s_presentation;
        static std::shared_mutex s_presentation_mutex;
        static constexpr size_t kMaxPresentationEntries = 256;


        int frameCount = 0;
        std::chrono::steady_clock::time_point lastFrameTime;


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



        static Matrix s_viewMatrix{};
        static Vec3 s_localPos{};
        static bool s_frameCacheValid = false;

        bool FrameCacheValid() { return s_frameCacheValid; }
        const Matrix& GetFrameViewMatrix() { return s_viewMatrix; }
        const Vec3& GetFrameLocalPos() { return s_localPos; }
        std::atomic<uint64_t>& GetSessionGeneration() { return s_sessionGeneration; }

        CameraSnapshotHandle AcquireCameraSnapshot() {


            thread_local CameraSnapshot cameraCopy{};
            thread_local std::array<uint64_t, 12> historyTimesMs{};
            auto camera = s_cameraSnapshots.Acquire();
            if (!camera || camera->sessionGeneration != s_sessionGeneration.load(std::memory_order_acquire) ||
                camera->sampleTime.time_since_epoch().count() == 0)
                return {};
            cameraCopy = *camera;
            const auto steadyNow = std::chrono::steady_clock::now();
            const uint64_t tickNow = GetTickCount64();
            const auto toTickMs = [&](std::chrono::steady_clock::time_point when) {
                const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(steadyNow - when).count();
                return age >= 0 && static_cast<uint64_t>(age) <= tickNow
                    ? tickNow - static_cast<uint64_t>(age) : uint64_t{0};
            };
            cameraCopy.sampleMs = toTickMs(cameraCopy.sampleTime);
            for (unsigned i = 0; i < cameraCopy.historyCount && i < historyTimesMs.size(); ++i)
                historyTimesMs[i] = toTickMs(cameraCopy.historyTimes[i]);
            return { &cameraCopy.viewMatrix, &cameraCopy.position, cameraCopy.sampleMs, cameraCopy.sessionGeneration,
                     &cameraCopy.historyMatrices, &historyTimesMs, cameraCopy.historyCount };
        }

        LivenessSnapshotHandle AcquireLivenessSnapshot() {
            thread_local std::array<LivenessSample, MAX_PEDS> samples{};
            auto snapshot = s_snapshots.Acquire();
            if (!snapshot || snapshot->sessionGeneration != s_sessionGeneration.load(std::memory_order_acquire))
                return {};
            const uint64_t nowMs = GetTickCount64();
            const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - snapshot->worldSampleTime).count();
            if (snapshot->worldSampleTime.time_since_epoch().count() == 0 || age < 0 || age > 250)
                return {};
            const uint64_t sampleMs = nowMs >= static_cast<uint64_t>(age)
                ? nowMs - static_cast<uint64_t>(age) : 0;
            uint32_t count = 0;
            for (int i = 0; i < snapshot->count && i < MAX_PEDS; ++i) {
                if (!snapshot->validPeds[i] ||
                    ClassifyPedHealth(snapshot->healths[i]) == PedHealthState::Unknown)
                    continue;
                samples[count++] = { snapshot->validPeds[i], snapshot->healths[i],
                                     snapshot->knocked[i], sampleMs, snapshot->sessionGeneration };
            }
            return { samples.data(), count, sampleMs, snapshot->sessionGeneration };
        }

        static void PublishAcquisitionSnapshot(uintptr_t localPlayer, bool cacheValid,
                                               float acquireMs) {
            auto slot = s_snapshots.TryBeginWrite();
            if (!slot) {
                s_snapshotDrops.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            const int n = (std::min)(MAX_PEDS, static_cast<int>(s_acquireValidPeds.size()));
            slot.value->count = n;
            for (int i = 0; i < n; ++i) {
                slot.value->validPeds[i] = s_acquireValidPeds[static_cast<size_t>(i)];
                slot.value->positions[i] = (i < static_cast<int>(s_acquirePositions.size()))
                    ? s_acquirePositions[static_cast<size_t>(i)] : Vec3{};
                slot.value->velocities[i] = (i < static_cast<int>(s_acquireVelocities.size()))
                    ? s_acquireVelocities[static_cast<size_t>(i)] : Vec3{};
                slot.value->healths[i] = (i < static_cast<int>(s_acquireHealths.size()))
                    ? s_acquireHealths[static_cast<size_t>(i)] : -1.f;
                slot.value->knocked[i] = (i < static_cast<int>(s_acquireKnockFlags.size()))
                    ? ((s_acquireKnockFlags[static_cast<size_t>(i)] &
                        kPedConfigFlagsKnockedBit) != 0) : false;
            }
            slot.value->viewMatrix = s_acquireViewMatrix;
            slot.value->localPos = s_acquireLocalPos;
            slot.value->localPlayer = localPlayer;
            slot.value->frameCacheValid = cacheValid;
            slot.value->generation = ++s_acquisitionGeneration;
            slot.value->sessionGeneration = s_sessionGeneration;
            slot.value->timestamp = std::chrono::steady_clock::now();
            slot.value->worldSampleTime = s_acquireWorldSampleTime;
            {
                auto cam = s_cameraSnapshots.Acquire();
                if (cam && cam->sampleTime.time_since_epoch().count() != 0) {
                    slot.value->viewMatrix = cam->viewMatrix;
                    slot.value->cameraSampleTime = cam->sampleTime;
                }
            }
            slot.value->acquireMs = acquireMs;











            UpdateValidPedsSnapshot(s_acquireValidPeds, s_acquirePositions,
                                    s_sessionGeneration);

            s_snapshots.Publish(slot.index);
        }

        static bool GiveCameraPriorityBeforeOptionalDma() {
            struct CameraState : OmniGhost::Gameplay::CameraPriority::ICameraState {
                bool IsPending() const noexcept override { return s_cameraPending.load(std::memory_order_acquire); }
                uint64_t GetDeadlineMs() const noexcept override { return s_cameraDeadlineMs.load(std::memory_order_acquire); }
            };
            static CameraState state;
            return OmniGhost::Gameplay::CameraPriority::GiveCameraPriority(state);
        }

        static void CameraLoop() {


            constexpr auto kCameraInterval = std::chrono::microseconds(4167);
            using Clock = std::chrono::steady_clock;
            auto nextCameraTime = Clock::now();
            VMMDLL_SCATTER_HANDLE camScatter = nullptr;
            std::array<Matrix, 12> cameraHistory{};
            std::array<std::chrono::steady_clock::time_point, 12> cameraTimes{};
            unsigned cameraHistoryCount = 0;
            unsigned cameraHistoryNext = 0;
            uintptr_t historyViewport = 0;
            uint64_t lastKnownSessionGen = 0;
            while (!s_cameraStop.load(std::memory_order_acquire)) {
                const auto now = Clock::now();







                {
                    const uint64_t tickNow = GetTickCount64();
                    const int64_t deltaToDeadline = std::chrono::duration_cast<std::chrono::milliseconds>(
                        nextCameraTime - now).count();
                    s_cameraDeadlineMs.store(deltaToDeadline >= 0
                        ? tickNow + static_cast<uint64_t>(deltaToDeadline)
                        : (tickNow > static_cast<uint64_t>(-deltaToDeadline)
                            ? tickNow - static_cast<uint64_t>(-deltaToDeadline) : uint64_t{0}),
                        std::memory_order_release);
                }
                const uint64_t currentSessionGen = s_sessionGeneration.load(std::memory_order_acquire);


                if (currentSessionGen != lastKnownSessionGen) {
                    cameraHistoryCount = cameraHistoryNext = 0;
                    historyViewport = 0;
                    lastKnownSessionGen = currentSessionGen;
                    if (camScatter) {
                        mem.CloseScatterHandle(camScatter);
                        camScatter = nullptr;
                    }
                }

                if (historyViewport != offset::viewport) {
                    cameraHistoryCount = cameraHistoryNext = 0;
                    historyViewport = offset::viewport;
                }
                if (!offset::viewport || !mem.vHandle) {


                    const auto idleTarget = now + std::chrono::milliseconds(50);
                    const auto residual = idleTarget > now ? idleTarget - now : std::chrono::milliseconds(1);
                    std::this_thread::sleep_for(residual > std::chrono::milliseconds(25) ? std::chrono::milliseconds(25) : residual);
                    continue;
                }
                if (now < nextCameraTime) {
                    const auto residual = nextCameraTime - now;


                    std::this_thread::sleep_for(residual > std::chrono::milliseconds(8) ? std::chrono::milliseconds(8) : residual);
                    continue;
                }
                s_cameraPending.store(true, std::memory_order_release);
                Matrix matrix{};
                Vec3 cameraPosition{};
                bool ok = false;
                const auto cameraBegin = now;
                if (!camScatter)
                    camScatter = mem.CreateScatterHandle();
                if (camScatter) {
                    mem.AddScatterReadRequest(camScatter, offset::viewport + offset::viewportMatrix,
                                              &matrix, sizeof(Matrix));
                    if (offset::camera && offset::cam_position)
                        mem.AddScatterReadRequest(camScatter, offset::camera + offset::cam_position,
                                                  &cameraPosition, sizeof(Vec3));
                    mem.SetDmaPriority(Memory::DmaPri::Camera);
                    mem.SetDmaCallTag("FiveM.Camera");
                    ok = mem.ExecuteReadScatter(camScatter);
                } else {
                    ok = mem.Read(offset::viewport + offset::viewportMatrix, &matrix, sizeof(Matrix));
                }
                const float finalEnergy = std::fabs(matrix._11) +
                    std::fabs(matrix._22) + std::fabs(matrix._33);
                ok = ok && std::isfinite(finalEnergy) && finalEnergy > 0.001f;
                const auto cameraEnd = Clock::now();
                OmniGhost::Gameplay::DmaTelemetry::ObserveCameraRead(
                    OmniGhost::Gameplay::DmaTelemetry::FiveM(),
                    std::chrono::duration<float, std::milli>(cameraEnd - cameraBegin).count());
                if (ok) {
                    const auto sampleTime = cameraBegin + (cameraEnd - cameraBegin) / 2;
                    cameraHistory[cameraHistoryNext] = matrix;
                    cameraTimes[cameraHistoryNext] = sampleTime;
                    cameraHistoryNext = (cameraHistoryNext + 1u) % static_cast<unsigned>(cameraHistory.size());
                    cameraHistoryCount = (std::min)(cameraHistoryCount + 1u,
                        static_cast<unsigned>(cameraHistory.size()));
                    auto slot = s_cameraSnapshots.TryBeginWrite();
                    if (slot) {
                        slot.value->viewMatrix = matrix;
                        slot.value->position = (!cameraPosition.IsZero() &&
                            std::isfinite(cameraPosition.x) && std::isfinite(cameraPosition.y) &&
                            std::isfinite(cameraPosition.z)) ? cameraPosition : Vec3{};
                        slot.value->sampleTime = sampleTime;





                        {
                            const uint64_t tickNow = GetTickCount64();
                            const int64_t sampleAge = std::chrono::duration_cast<std::chrono::milliseconds>(
                                Clock::now() - sampleTime).count();
                            slot.value->sampleMs = sampleAge >= 0 && static_cast<uint64_t>(sampleAge) <= tickNow
                                ? tickNow - static_cast<uint64_t>(sampleAge) : uint64_t{0};
                        }
                        slot.value->sessionGeneration = s_sessionGeneration.load(std::memory_order_acquire);
                        slot.value->historyMatrices = cameraHistory;
                        slot.value->historyTimes = cameraTimes;
                        slot.value->historyCount = cameraHistoryCount;
                        s_cameraSnapshots.Publish(slot.index);
                    }
                }
                s_cameraPending.store(false, std::memory_order_release);

                nextCameraTime += kCameraInterval;
                if (nextCameraTime <= Clock::now())
                    nextCameraTime = Clock::now() + std::chrono::milliseconds(1);
            }
            if (camScatter) {
                mem.CloseScatterHandle(camScatter);
                camScatter = nullptr;
            }
        }

        static void EnsureAcqScatter() {
            if (!s_acqScatter && mem.vHandle)
                s_acqScatter = mem.CreateScatterHandle();
        }

        static void OptionalWorkerLoop() {
            while (!s_optionalStop.load(std::memory_order_acquire)) {
                OptionalJob job{};
                uint64_t jobGen = 0;
                {
                    std::unique_lock lock(s_optionalMu);

                    s_optionalCv.wait(lock, [] {
                        return s_optionalPending.load(std::memory_order_acquire) ||
                            s_optionalStop.load(std::memory_order_acquire);
                    });
                    if (s_optionalStop.load(std::memory_order_acquire))
                        break;
                    if (!s_optionalPending.load(std::memory_order_acquire))
                        continue;
                    job = std::move(s_optionalJob);
                    jobGen = job.sessionGeneration;
                    s_optionalPending.store(false, std::memory_order_release);
                }


                const uint64_t currentGen = s_sessionGeneration.load(std::memory_order_acquire);
                if (jobGen != currentGen)
                    continue;

                s_optionalBusy.store(true, std::memory_order_release);












                try {
                    do {
                    mem.SetDmaLane("fivem-optional");
                    mem.SetDmaPriority(Memory::DmaPri::Bones);
                    const int pressure = job.pressure;
                    const auto t0 = std::chrono::steady_clock::now();
                    constexpr float kBudgetMs = 6.f;
                    auto elapsed = [&] {
                        return OmniGhost::Gameplay::TimeMs(t0);
                    };

                    auto checkGen = [&]() -> bool {
                        return s_sessionGeneration.load(std::memory_order_acquire) == jobGen;
                    };

                    if (!job.peds.empty() && pressure < 2 && checkGen()) {
                        const bool needVis = job.espConfig.visibility_colors ||
                            job.espConfig.visible_check || job.aimConfig.visible_check;
                        std::vector<bool> prodVis;
                        if (needVis) {
                            mem.SetDmaPriority(Memory::DmaPri::OtherMetadata);
                            if (!GiveCameraPriorityBeforeOptionalDma())
                                break;


                            Matrix viewMatrix{};
                            Vec3 cameraPosition = job.localPos;
                            cameraPosition.z += 1.45f;
                            if (auto cam = s_cameraSnapshots.Acquire(); cam &&
                                cam->sessionGeneration == s_sessionGeneration.load(std::memory_order_acquire)) {
                                viewMatrix = cam->viewMatrix;
                                if (!cam->position.IsZero()) cameraPosition = cam->position;
                            }

                            std::vector<FiveM::Visibility::RaycastTargets> raycastTargets(job.peds.size());
                            for (size_t i = 0; i < job.peds.size(); ++i) {
                                const Vec3 origin = i < job.positions.size() ? job.positions[i] : Vec3{};
                                auto& target = raycastTargets[i];
                                target.head = Vec3(origin.x, origin.y, origin.z + 1.62f);
                                target.neck = Vec3(origin.x, origin.y, origin.z + 1.35f);
                                target.torso = Vec3(origin.x, origin.y, origin.z + 0.92f);
                                Vec3 prepared{};
                                if (esp::try_get_prepared_bone_position(job.peds[i], 0, prepared)) target.head = prepared;
                                if (esp::try_get_prepared_bone_position(job.peds[i], 7, prepared)) target.neck = prepared;
                                if (esp::try_get_prepared_bone_position(job.peds[i], 8, prepared)) target.torso = prepared;
                            }


                            const float maxVisDist = job.espConfig.max_esp_distance > 0.f
                                ? job.espConfig.max_esp_distance : 200.f;
                            FiveM::Visibility::BatchCheckVisibility(
                                job.peds, job.positions, job.localPos,
                                cameraPosition, raycastTargets,
                                &viewMatrix, maxVisDist, prodVis);
                        }
                        if (job.friendsListRequested || job.espConfig.enabled || job.espConfig.web_radar_enabled ||
                            job.aimConfig.aimbot_enabled || job.aimConfig.trigger_enabled ||
                            (job.espConfig.radar_enabled &&
                             (job.espConfig.team_check || friends::HasFriends()))) {
                            mem.SetDmaPriority(Memory::DmaPri::WeaponNameTeam);
                            if (!GiveCameraPriorityBeforeOptionalDma())
                                break;




                            esp::prepare_esp_frame(job.peds, job.positions, job.healths,
                                job.espConfig, job.aimConfig, nullptr);
                        }
                    }
                    if (job.boneMask && !job.peds.empty() && pressure < 2 && elapsed() < kBudgetMs && checkGen()) {
                        mem.SetDmaPriority(Memory::DmaPri::Bones);
                        if (!GiveCameraPriorityBeforeOptionalDma())
                            break;
                        esp::prepare_skeleton_frame(job.peds, job.positions, job.boneMask);
                    }
                    if (elapsed() < kBudgetMs && pressure < 1 && checkGen()) {
                        mem.SetDmaPriority(Memory::DmaPri::OtherMetadata);
                        if (!GiveCameraPriorityBeforeOptionalDma())
                            break;
                        esp::PrepareWaypointAndBlips(job.espConfig, job.localPos);
                    }
                    if (elapsed() < kBudgetMs && pressure < 2 && checkGen()) {
                        mem.SetDmaPriority(Memory::DmaPri::OtherMetadata);
                        if (!GiveCameraPriorityBeforeOptionalDma())
                            break;
                        Matrix vehicleCamera{};
                        if (auto cam = s_cameraSnapshots.Acquire(); cam &&
                            cam->sessionGeneration == s_sessionGeneration.load(std::memory_order_acquire))
                            vehicleCamera = cam->viewMatrix;
                        vehicle_esp::Collect(job.vehicleConfig, job.peds,
                            job.localPos, vehicleCamera,
                            job.espConfig.web_radar_enabled || job.espConfig.radar_enabled);
                    }
                    } while (false);
                } catch (const std::exception& ex) {
                    std::cerr << "[FiveM] Optional ESP job failed: " << ex.what() << '\n';
                } catch (...) {
                    std::cerr << "[FiveM] Optional ESP job failed: unknown exception\n";
                }
                mem.SetDmaLane("fivem");
                mem.SetDmaPriority(Memory::DmaPri::Position);
                s_optionalBusy.store(false, std::memory_order_release);
            }
        }

        static void QueueOptionalJob(OptionalJob&& job) {
            if (s_optionalStop.load(std::memory_order_acquire))
                return;
            if (s_optionalBusy.load(std::memory_order_acquire) ||
                s_optionalPending.load(std::memory_order_acquire))
                return;
            {
                std::lock_guard lock(s_optionalMu);


                const uint64_t currentGen = s_sessionGeneration.load(std::memory_order_acquire);
                if (job.sessionGeneration != currentGen) {
                    return;
                }
                s_optionalJob = std::move(job);
                s_optionalPending.store(true, std::memory_order_release);
            }
            s_optionalCv.notify_one();
        }

        static void AcquisitionLoop() {



            uintptr_t localPlayer = 0;
            bool previousNpcEsp = false;
            uint64_t scanId = 0;
            uint64_t lastGoodLocalMs = 0;
            uint64_t lastGoodViewMs = 0;
            uint64_t lastGoodLocalPosMs = 0;
            OmniGhost::Gameplay::FixedRateScheduler scheduler;
            while (!s_acquisitionStop.load(std::memory_order_acquire)) {
                mem.SetDmaLane("fivem");
                mem.SetDmaScanId(++scanId);
                const auto acquireBegin = std::chrono::steady_clock::now();

                const bool needPeds = s_needPeds.load(std::memory_order_acquire);
                const auto options = s_optionSnapshots.Acquire();
                esp::Config espOptions = options->espConfig;
                espOptions.skeleton_lod = true;
                espOptions.circle_type = 0;
                espOptions.player_name = false;
                espOptions.player_id = false;
                espOptions.hit_marker = false;
                espOptions.look_direction = false;
                espOptions.offscreen_arrows = false;
                const bool friendsListRequested = friends::PlayerListRequestedRecently();
                if (friendsListRequested) espOptions.player_name = true;
                const auto& aimOptions = options->aimConfig;
                auto vehicleOptions = options->vehicleConfig;
                vehicleOptions.show_speed = false;
                bool cacheValid = false;
                uint16_t pendingBoneMask = 0;
                const bool npcEspEnabled = s_npcEsp.load(std::memory_order_acquire);
                if (previousNpcEsp && !npcEspEnabled) {
                }
                previousNpcEsp = npcEspEnabled;

                if (needPeds && offset::world && offset::replay && offset::viewport) {
                    EnsureAcqScatter();
                    uintptr_t freshLp = 0;




                    Matrix freshView{};
                    uint64_t freshViewSampleMs = 0;
                    if (auto cam = s_cameraSnapshots.Acquire())
                        if (cam->sampleMs && cam->sessionGeneration == s_sessionGeneration.load(std::memory_order_acquire)) {
                            freshView = cam->viewMatrix;
                            freshViewSampleMs = cam->sampleMs;
                        }
                    if (s_acqScatter) {
                        mem.AddScatterReadRequest(s_acqScatter, offset::world + offset::world_localPlayer, &freshLp, sizeof(uintptr_t));
                        mem.SetDmaCallTag("FiveM.LocalAndCamera");
                        mem.ExecuteReadScatter(s_acqScatter);
                    }
                    const uint64_t sampleNowMs = GetTickCount64();
                    const uintptr_t previousLocalPlayer = localPlayer;
                    if (freshLp > 0x10000u && freshLp < 0x7FFFFFFFFFFFULL) {
                        localPlayer = freshLp;
                        lastGoodLocalMs = sampleNowMs;
                    } else if (sampleNowMs - lastGoodLocalMs > 250u) {
                        localPlayer = 0;
                    }
                    if (localPlayer != previousLocalPlayer) {
                        s_acquireLocalPos = {};
                        lastGoodLocalPosMs = 0;
                    }
                    const float matrixEnergy = std::fabs(freshView._11) +
                        std::fabs(freshView._22) + std::fabs(freshView._33);
                    if (std::isfinite(matrixEnergy) && matrixEnergy > 0.001f) {
                        s_acquireViewMatrix = freshView;
                        lastGoodViewMs = freshViewSampleMs;
                    }

                    if (localPlayer) {
                        if (s_acqScatter) {
                            Vec3 freshLocalPos{};
                            mem.AddScatterReadRequest(s_acqScatter, localPlayer + offset::playerPosition,
                                                      &freshLocalPos, sizeof(Vec3));
                            mem.SetDmaCallTag("FiveM.LocalPosition");
                            mem.ExecuteReadScatter(s_acqScatter);
                            if (!freshLocalPos.IsZero() &&
                                std::isfinite(freshLocalPos.x) &&
                                std::isfinite(freshLocalPos.y) &&
                                std::isfinite(freshLocalPos.z)) {
                                s_acquireLocalPos = freshLocalPos;
                                lastGoodLocalPosMs = GetTickCount64();
                            }
                        }
                        const uint64_t cacheNowMs = GetTickCount64();
                        cacheValid = lastGoodViewMs != 0 && lastGoodLocalPosMs != 0 &&
                            cacheNowMs >= lastGoodViewMs && cacheNowMs - lastGoodViewMs <= 100u &&
                            cacheNowMs >= lastGoodLocalPosMs && cacheNowMs - lastGoodLocalPosMs <= 100u;
                        const uint64_t scatterEndMs = GetTickCount64();
                        (void)scatterEndMs;
                        s_acquireWorldSampleTime = std::chrono::steady_clock::now();
                        if (!cacheValid) {
                            s_acquireValidPeds.clear();
                            s_acquirePositions.clear();
                    s_acquireVelocities.clear();
                    s_acquireHealths.clear();
                    s_acquireKnockFlags.clear();
                        } else {
                        collectFrameData(localPlayer, s_acquireLocalPos);


                        if (!s_acquireValidPeds.empty()) {

                            g_pedCacheManager.setCurrentGeneration(s_sessionGeneration);
                            g_pedCacheManager.fastCache(s_acquireValidPeds, s_acquirePositions);
                            g_pedCacheManager.update();
                            uint16_t boneMask = 0;
                            if (espOptions.skeleton || espOptions.head_circle || espOptions.chinese_hat
                                || espOptions.angel_wings || espOptions.devil_horns || espOptions.floating_crown
                                || espOptions.head_halo || espOptions.look_direction || aimOptions.aimbot_enabled) {




                                boneMask = esp::ComputeCombinedBoneMask(
                                    s_acquireValidPeds, s_acquirePositions, s_acquireLocalPos,
                                    espOptions.lod_full_dist, espOptions.lod_high_dist,
                                    espOptions.lod_med_dist);
                            }
                            if (espOptions.box_2d || espOptions.corner_box || espOptions.snaplines ||
                                espOptions.health_bar || espOptions.armor_bar)
                                boneMask |= 0x0007u;
                            if (espOptions.visible_check || espOptions.visibility_colors || aimOptions.visible_check)
                                boneMask |= uint16_t((1u << 0) | (1u << 7) | (1u << 8));
                            pendingBoneMask = boneMask;
                        }
                        }
                    } else {
                        s_acquireValidPeds.clear();
                        s_acquirePositions.clear();
                        s_acquireLocalPos = {};
                        s_acquireWorldSampleTime = {};
                    }



                } else {
                    s_acquireValidPeds.clear();
                    s_acquirePositions.clear();
                            s_acquireVelocities.clear();
                            s_acquireHealths.clear();
                            s_acquireKnockFlags.clear();
                    s_acquireLocalPos = {};
                    s_acquireWorldSampleTime = {};
                }

                const float coreAcquireMs = OmniGhost::Gameplay::TimeMs(acquireBegin);
                OmniGhost::Gameplay::PipelineTelemetry::Smooth(
                    s_pipelineMetrics.acquire_ms, coreAcquireMs);
                s_pipelineMetrics.entities.store(
                    static_cast<int>(s_acquireValidPeds.size()), std::memory_order_relaxed);
                PublishAcquisitionSnapshot(localPlayer, cacheValid, coreAcquireMs);

                {


                    const uint64_t currentGen = s_sessionGeneration.load(std::memory_order_acquire);
                    const int pressure = s_pressureLevel.load(std::memory_order_relaxed);
                    OptionalJob job{};
                    job.peds = s_acquireValidPeds;
                    job.positions = s_acquirePositions;
                    job.healths = s_acquireHealths;
                    job.localPos = s_acquireLocalPos;
                    job.boneMask = pendingBoneMask;
                    job.espConfig = espOptions;
                    job.aimConfig = aimOptions;
                    job.vehicleConfig = vehicleOptions;
                    job.friendsListRequested = friendsListRequested;
                    job.pressure = pressure;
                    job.sessionGeneration = currentGen;
                    QueueOptionalJob(std::move(job));
                }




                {
                    const float fullMs = OmniGhost::Gameplay::TimeMs(acquireBegin);
                    const uint64_t generation = s_sessionGeneration;
                    static std::array<float, 16> recentActiveScans{};
                    static size_t recentScanCount = 0;
                    static size_t recentScanCursor = 0;
                    static uint64_t pressureSessionGeneration = 0;
                    static int healthyPressureScans = 0;
                    if (generation != pressureSessionGeneration) {
                        pressureSessionGeneration = generation;
                        recentScanCount = recentScanCursor = 0;
                        healthyPressureScans = 0;
                    }
                    const bool activeSample = cacheValid && !s_acquireValidPeds.empty();
                    if (activeSample && std::isfinite(fullMs)) {
                        recentActiveScans[recentScanCursor] = fullMs;
                        recentScanCursor = (recentScanCursor + 1) % recentActiveScans.size();
                        recentScanCount = (std::min)(recentScanCount + 1, recentActiveScans.size());
                    }
                    auto sortedScans = recentActiveScans;
                    std::sort(sortedScans.begin(), sortedScans.begin() + recentScanCount);
                    const float recentP95 = recentScanCount
                        ? sortedScans[(recentScanCount - 1) * 95 / 100] : 0.f;
                    OmniGhost::Gameplay::DmaTelemetry::FiveM().acquire.pressure_window_p95_ms.store(
                        recentP95, std::memory_order_relaxed);

                    int level = s_pressureLevel.load(std::memory_order_relaxed);
                    int demanded = 0;

                    if (fullMs > 80.f || recentP95 > 70.f) demanded = 3;
                    else if (fullMs > 55.f || recentP95 > 50.f) demanded = 2;
                    else if (fullMs > 35.f || recentP95 > 32.f) demanded = 1;

                    const float exitWork = level >= 3 ? 30.f : level == 2 ? 26.f : 20.f;
                    const float exitP95 = level >= 3 ? 38.f : level == 2 ? 30.f : 24.f;
                    constexpr int kHealthyScansToRecover = 12;
                    if (demanded > level) {
                        level = demanded;
                        healthyPressureScans = 0;
                    } else if (fullMs < exitWork && recentP95 < exitP95) {
                        if (++healthyPressureScans >= kHealthyScansToRecover) {
                            level = (std::max)(0, level - 1);
                            healthyPressureScans = 0;
                        }
                    } else if (demanded < level) {
                        if (healthyPressureScans > 0)
                            --healthyPressureScans;
                    } else {
                        healthyPressureScans = 0;
                    }
                    s_pressureLevel.store(level, std::memory_order_relaxed);
                }
                {
                    const float fullAcquireMs = OmniGhost::Gameplay::TimeMs(acquireBegin);
                    auto& tel = OmniGhost::Gameplay::DmaTelemetry::FiveM();
                    OmniGhost::Gameplay::DmaTelemetry::ObserveAcquire(
                        tel, fullAcquireMs, cacheValid && !s_acquireValidPeds.empty(), scanId);
                    tel.counts.entities.store(static_cast<int>(s_acquireValidPeds.size()), std::memory_order_relaxed);
                    tel.counts.snapshot_drops.store(s_snapshotDrops.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    tel.process.dma_open.store(mem.vHandle != nullptr, std::memory_order_relaxed);
                }




                const int pressure = s_pressureLevel.load(std::memory_order_relaxed);
                int delayMs = s_performanceMode.load(std::memory_order_relaxed) ? 12 : 7;
                if (pressure >= 3) delayMs = (std::max)(delayMs, 22);
                else if (pressure == 2) delayMs = (std::max)(delayMs, 16);
                else if (pressure == 1) delayMs = (std::max)(delayMs, 12);
                const auto waitBegin = std::chrono::steady_clock::now();
                scheduler.Wait(std::chrono::milliseconds(delayMs));
                OmniGhost::Gameplay::DmaTelemetry::ObserveSchedulerWait(
                    OmniGhost::Gameplay::DmaTelemetry::FiveM(),
                    OmniGhost::Gameplay::TimeMs(waitBegin));
            }
        }

        static void ProcessLivenessLoop() {
            constexpr const char* aliases[] = {
                "GTAProcess.exe", "FiveM_GTAProcess.exe",
                "FiveM_b3258_GTAProcess.exe", "FiveM_b3407_GTAProcess.exe",
                "FiveM_b3570_GTAProcess.exe"
            };
            while (!s_livenessStop.load(std::memory_order_acquire)) {
                const std::string executable = OmniGhost::GameContext::Instance().GetValidExecutable();
                bool found = !executable.empty() && mem.GetPidFromName(executable) != 0;
                for (const char* alias : aliases) {
                    if (found || s_livenessStop.load(std::memory_order_acquire)) break;
                    found = mem.GetPidFromName(alias) != 0;
                }
                if (found)
                    s_livenessLastSeenMs.store(GetTickCount64(), std::memory_order_release);
                for (int i = 0; i < 20 && !s_livenessStop.load(std::memory_order_acquire); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }

        bool RemoteProcessRecentlySeen() noexcept {
            const ULONGLONG seen = s_livenessLastSeenMs.load(std::memory_order_acquire);
            const ULONGLONG now = GetTickCount64();
            return seen != 0 && now >= seen && now - seen <= 3000;
        }

        static void EnsureAcquisitionStarted() {
            bool expected = false;
            if (!s_acquisitionRunning.compare_exchange_strong(
                    expected, true, std::memory_order_acq_rel))
                return;


            s_snapshots.ResetProducerThread();
            s_cameraSnapshots.ResetProducerThread();



            FiveM::Collision::World::Instance().Initialize("data/fivem/collision");



            s_acquisitionStop.store(false, std::memory_order_release);
            s_cameraStop.store(false, std::memory_order_release);
            s_livenessStop.store(false, std::memory_order_release);
            s_livenessLastSeenMs.store(0, std::memory_order_release);
            s_livenessThread = std::thread(ProcessLivenessLoop);
            s_optionalStop.store(false, std::memory_order_release);
            s_optionalBusy.store(false, std::memory_order_release);
            s_optionalPending.store(false, std::memory_order_release);
            s_optionalThread = std::thread(OptionalWorkerLoop);
            s_cameraThread = std::thread(CameraLoop);
            s_acquisitionThread = std::thread(AcquisitionLoop);
        }

        void StopAcquisition() {
            FiveM::WebRadar::Stop();
            aimbot::ResetTrackingState();
            s_acquisitionStop.store(true, std::memory_order_release);
            s_cameraStop.store(true, std::memory_order_release);
            s_livenessStop.store(true, std::memory_order_release);
            s_optionalStop.store(true, std::memory_order_release);
            s_optionalCv.notify_all();
            if (s_optionalThread.joinable())
                s_optionalThread.join();
            if (s_livenessThread.joinable())
                s_livenessThread.join();
            if (s_cameraThread.joinable())
                s_cameraThread.join();
            if (s_acquisitionThread.joinable())
                s_acquisitionThread.join();
            s_optionalBusy.store(false, std::memory_order_release);
            s_optionalPending.store(false, std::memory_order_release);
            s_cameraPending.store(false, std::memory_order_release);
            s_cameraDeadlineMs.store(0, std::memory_order_release);
            s_livenessLastSeenMs.store(0, std::memory_order_release);
            if (s_acqScatter) {
                mem.CloseScatterHandle(s_acqScatter);
                s_acqScatter = nullptr;
            }

            FiveM::Visibility::Shutdown();
            FiveM::Collision::World::Instance().Shutdown();
            esp::ShutdownWaypointAndBlips();
            esp::ShutdownPreparedFrames();
            vehicle_esp::Shutdown();
            g_pedCacheManager.clearCache();
            s_acquisitionRunning.store(false, std::memory_order_release);
            s_acquireRawPeds.clear();
            s_acquireValidPeds.clear();
            s_acquirePositions.clear();
            s_acquireViewMatrix = {};
            s_acquireLocalPos = {};
            s_acquireWorldSampleTime = {};
            ++s_sessionGeneration;

            {
                std::unique_lock lock(s_presentation_mutex);
                s_presentation.clear();
            }
            s_snapshots.ResetProducerThread();
            PublishAcquisitionSnapshot(0, false, 0.f);
        }

        void RunESP() {
            InitializeContainers();
            frameCount++;
            auto currentTime = std::chrono::steady_clock::now();
            const auto renderBegin = currentTime;

            const bool needPeds = esp::config.enabled || esp::config.web_radar_enabled ||
                aimbot::config.aimbot_enabled || aimbot::config.trigger_enabled ||
                esp::config.radar_enabled || friends::PlayerListRequestedRecently();

            const ImVec2 display = ImGui::GetIO().DisplaySize;



            s_needPeds.store(needPeds, std::memory_order_release);


            s_selfEsp.store(esp::config.self_esp || esp::config.web_radar_enabled,
                std::memory_order_release);
            s_npcEsp.store(esp::config.npc_esp, std::memory_order_release);
            s_performanceMode.store(app_settings::config.performance_mode, std::memory_order_release);
            const float acquisitionDistance = esp::config.web_radar_enabled
                ? (std::max)(esp::config.max_esp_distance, esp::config.web_radar_distance)
                : esp::config.max_esp_distance;
            s_maxDistance.store(acquisitionDistance, std::memory_order_release);
            FiveM::WebRadar::Configure({
                esp::config.web_radar_distance,
                static_cast<std::uint32_t>(std::clamp(esp::config.web_radar_max_players, 8, MAX_PEDS)),
                static_cast<std::uint32_t>(std::clamp(esp::config.web_radar_max_vehicles, 8, 64)),
                esp::config.web_radar_update_hz,
                esp::config.web_radar_mode
            });
            s_displayWidth.store(display.x, std::memory_order_release);
            s_displayHeight.store(display.y, std::memory_order_release);
            if (auto slot = s_optionSnapshots.TryBeginWrite()) {
                slot.value->espConfig = esp::config;
                slot.value->aimConfig = aimbot::config;
                slot.value->vehicleConfig = vehicle_esp::config;
                if (esp::config.web_radar_enabled) {
                    slot.value->vehicleConfig.max_distance = (std::max)(
                        slot.value->vehicleConfig.max_distance,
                        esp::config.web_radar_distance);
                }
                if (esp::config.radar_enabled) {
                    slot.value->vehicleConfig.max_distance = (std::max)(
                        slot.value->vehicleConfig.max_distance,
                        esp::config.radar_range);
                }
                s_optionSnapshots.Publish(slot.index);
            }
            EnsureAcquisitionStarted();

            static uint64_t consumedGeneration = 0;
            static AcquisitionSnapshot previousSnapshot;
            static AcquisitionSnapshot currentSnapshot;
            static uint64_t presentationSession = 0;
            const uint64_t activeSession = s_sessionGeneration.load(std::memory_order_acquire);
            if (presentationSession != activeSession) {
                previousSnapshot = {};
                currentSnapshot = {};
                consumedGeneration = 0;
                presentationSession = activeSession;
            }
            auto snapshot = s_snapshots.Acquire();
            if (snapshot && snapshot->sessionGeneration == activeSession &&
                snapshot->generation != consumedGeneration) {
                previousSnapshot = std::move(currentSnapshot);
                currentSnapshot = *snapshot;
                s_viewMatrix = snapshot->viewMatrix;
                s_localPos = snapshot->localPos;
                s_frameCacheValid = snapshot->frameCacheValid;
                offset::localplayer = snapshot->localPlayer;
                consumedGeneration = snapshot->generation;
            }


            std::chrono::steady_clock::time_point selectedCameraTime{};
            if (auto cam = s_cameraSnapshots.Acquire()) {
                if (cam->sessionGeneration == currentSnapshot.sessionGeneration &&
                    cam->sampleTime.time_since_epoch().count() != 0) {
                    s_viewMatrix = cam->viewMatrix;
                    selectedCameraTime = cam->sampleTime;
                    const auto target = currentSnapshot.worldSampleTime + std::chrono::milliseconds(30);
                    if (currentSnapshot.worldSampleTime.time_since_epoch().count() != 0 &&
                        cam->sampleTime > target && cam->historyCount) {
                        auto bestDelta = std::chrono::steady_clock::duration::max();
                        for (unsigned i = 0; i < cam->historyCount; ++i) {
                            if (currentTime < cam->historyTimes[i] ||
                                currentTime - cam->historyTimes[i] > std::chrono::milliseconds(80))
                                continue;
                            const auto delta = cam->historyTimes[i] > target
                                ? cam->historyTimes[i] - target : target - cam->historyTimes[i];
                            if (delta < bestDelta) {
                                bestDelta = delta;
                                s_viewMatrix = cam->historyMatrices[i];
                                selectedCameraTime = cam->historyTimes[i];
                            }
                        }
                    }
                }
            }



            const bool worldFresh = currentSnapshot.worldSampleTime.time_since_epoch().count() != 0 &&
                currentTime >= currentSnapshot.worldSampleTime &&
                currentTime - currentSnapshot.worldSampleTime <= std::chrono::milliseconds(120);
            s_frameCacheValid = currentSnapshot.frameCacheValid && worldFresh;
            const int renderCount = worldFresh ? currentSnapshot.count : 0;
            validPeds.resize(static_cast<size_t>((std::max)(0, renderCount)));
            positions.resize(validPeds.size());
            static std::unordered_map<uintptr_t, int> prevIndex;
            if (previousSnapshot.generation != currentSnapshot.generation) {
                prevIndex.clear();
                for (int pi = 0; pi < previousSnapshot.count; ++pi)
                    prevIndex[previousSnapshot.validPeds[pi]] = pi;
            }
            const float presentDt = std::clamp(ImGui::GetIO().DeltaTime, .001f, .033f);


            const float rawAge = currentSnapshot.worldSampleTime.time_since_epoch().count()
                ? std::chrono::duration<float>(currentTime - currentSnapshot.worldSampleTime).count()
                : 0.f;
            const float snapshotAge = rawAge > 0.08f ? 0.f : std::clamp(rawAge, 0.f, 0.035f);
            for (int i = 0; i < renderCount; ++i) {
                validPeds[static_cast<size_t>(i)] = currentSnapshot.validPeds[i];
                const Vec3& raw = currentSnapshot.positions[i];
                const uintptr_t ped = validPeds[static_cast<size_t>(i)];

                {
                    std::unique_lock lock(s_presentation_mutex);
                    auto& presentation = s_presentation[ped];


                    if (presentation.entry_generation != s_sessionGeneration) {
                        presentation.position = raw;
                        presentation.velocity = Vec3{};
                        presentation.sourceGeneration = 0;
                        presentation.initialized = false;
                        presentation.entry_generation = s_sessionGeneration;
                    }
                    if (!presentation.initialized) {
                        presentation.position = raw;
                        presentation.initialized = true;
                    }
                    if (presentation.sourceGeneration != currentSnapshot.generation) {

                        const Vec3& dmaVel = currentSnapshot.velocities[static_cast<size_t>(i)];
                        const float dmaSpeedSq = dmaVel.x * dmaVel.x + dmaVel.y * dmaVel.y +
                            dmaVel.z * dmaVel.z;
                        if (dmaSpeedSq > 0.01f && dmaSpeedSq < 40000.f &&
                            std::isfinite(dmaVel.x) && std::isfinite(dmaVel.y) && std::isfinite(dmaVel.z)) {
                            presentation.velocity = dmaVel;
                        } else if (previousSnapshot.generation &&
                            currentSnapshot.worldSampleTime > previousSnapshot.worldSampleTime) {

                            const auto it = prevIndex.find(validPeds[static_cast<size_t>(i)]);
                            if (it != prevIndex.end() && it->second >= 0 && it->second < previousSnapshot.count) {
                                const float interval = std::chrono::duration<float>(
                                    currentSnapshot.worldSampleTime - previousSnapshot.worldSampleTime).count();
                                if (interval > .001f)
                                    presentation.velocity =
                                        (raw - previousSnapshot.positions[it->second]) * (1.f / interval);
                            }
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
            }



            {
                std::unique_lock lock(s_presentation_mutex);


                static std::unordered_set<uintptr_t> currentValidPeds;
                currentValidPeds.clear();
                currentValidPeds.reserve(validPeds.size() * 2);
                for (const uintptr_t ped : validPeds) {
                    currentValidPeds.insert(ped);
                }


                for (auto it = s_presentation.begin(); it != s_presentation.end();) {
                    if (!currentValidPeds.contains(it->first) ||
                        it->second.entry_generation != s_sessionGeneration) {
                        it = s_presentation.erase(it);
                    } else {
                        ++it;
                    }
                }


                if (s_presentation.size() > kMaxPresentationEntries) {

                    std::vector<std::pair<uintptr_t, uint64_t>> entries;
                    entries.reserve(s_presentation.size());
                    for (const auto& kv : s_presentation) {
                        entries.emplace_back(kv.first, kv.second.sourceGeneration);
                    }
                    std::sort(entries.begin(), entries.end(),
                        [](const auto& a, const auto& b) { return a.second < b.second; });

                    size_t toRemove = s_presentation.size() - kMaxPresentationEntries;
                    for (size_t i = 0; i < toRemove; ++i) {
                        s_presentation.erase(entries[i].first);
                    }
                }
            }



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
                if (bits != s_cfgBits) {
                    s_cfgBits = bits;
                    char blob[256];
                    std::snprintf(blob, sizeof(blob),
                        "skeleton=%d\nbox=%d\nhealth=%d\narmor=%d\nweapon=%d\nvisibility=%d\n"
                        "trails=%d\nwings=%d\nhalo=%d\ncrown=%d\nvehicles=%d\naim=%d",
                        esp::config.skeleton, esp::config.box_2d, esp::config.health_bar, esp::config.armor_bar,
                        esp::config.weapon_name, (esp::config.visibility_colors || esp::config.visible_check) ? 1 : 0,
                        esp::config.trails, esp::config.angel_wings, esp::config.head_halo, esp::config.floating_crown,
                        (esp::config.triangle_radar || esp::config.radar_enabled) ? 1 : 0,
                        aimbot::config.aimbot_enabled ? 1 : 0);
                    OmniGhost::Gameplay::DmaTelemetry::LogConfigChanged("FIVEM", blob);
                }
            }

            renderESP();


            if (offset::localplayer && esp::config.radar_enabled) {
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

            static double lastWp = 0.0;
            const double nowSec = ImGui::GetTime();
            if (offset::localplayer && esp::config.radar_enabled &&
                (esp::config.waypoint_line || esp::config.blip_esp)) {
                if (nowSec - lastWp >= 0.20) {
                    lastWp = nowSec;
                    if (s_frameCacheValid)
                        esp::DrawWaypointAndBlips(s_viewMatrix, offset::localplayer);
                }
            }

            vehicle_esp::Run();
            aimbot::Run();
            esp::DrawActiveFeaturesPanel();

            const float frameMs = std::chrono::duration<float, std::milli>(
                currentTime - lastFrameTime).count();
            lastFrameTime = currentTime;
            const float renderMs = OmniGhost::Gameplay::TimeMs(renderBegin);
            OmniGhost::Gameplay::PipelineTelemetry::Smooth(s_pipelineMetrics.render_ms, renderMs);
            {
                auto& tel = OmniGhost::Gameplay::DmaTelemetry::FiveM();
                OmniGhost::Gameplay::DmaTelemetry::ObserveRender(tel, renderMs);
                const float snapshotAgeMs = currentSnapshot.timestamp.time_since_epoch().count()
                    ? std::chrono::duration<float, std::milli>(currentTime - currentSnapshot.timestamp).count()
                    : 0.f;
                const float worldAgeMs = currentSnapshot.worldSampleTime.time_since_epoch().count()
                    ? std::chrono::duration<float, std::milli>(currentTime - currentSnapshot.worldSampleTime).count()
                    : 0.f;
                const float publishDelayMs = currentSnapshot.worldSampleTime.time_since_epoch().count() &&
                    currentSnapshot.timestamp >= currentSnapshot.worldSampleTime
                    ? std::chrono::duration<float, std::milli>(
                        currentSnapshot.timestamp - currentSnapshot.worldSampleTime).count()
                    : 0.f;
                tel.snapshot.snapshot_age_ms.store((std::max)(0.f, snapshotAgeMs), std::memory_order_relaxed);
                if (auto cam = s_cameraSnapshots.Acquire()) {
                    if (cam->sampleMs) {
                        const float camAge = static_cast<float>(GetTickCount64() - cam->sampleMs);
                        OmniGhost::Gameplay::DmaTelemetry::ObserveCameraAge(tel, camAge);
                        if (selectedCameraTime.time_since_epoch().count() &&
                            currentSnapshot.worldSampleTime.time_since_epoch().count()) {
                            const auto delta = selectedCameraTime > currentSnapshot.worldSampleTime
                                ? selectedCameraTime - currentSnapshot.worldSampleTime
                                : currentSnapshot.worldSampleTime - selectedCameraTime;
                            tel.camera_pos_delta.camera_position_delta_ms.store(
                                std::chrono::duration<float, std::milli>(delta).count(),
                                std::memory_order_relaxed);
                        }
                    }
                }
                {
                    const float posAge = (std::max)(0.f, worldAgeMs);
                    const float bonesAge = esp::config.skeleton ? posAge : 0.f;
                    OmniGhost::Gameplay::DmaTelemetry::ObservePresentationTiming(
                        tel, posAge, bonesAge, 0.f, 0.f);
                }
                tel.snapshot.world_age_ms.store((std::max)(0.f, worldAgeMs), std::memory_order_relaxed);
                tel.snapshot.publish_delay_ms.store(publishDelayMs, std::memory_order_relaxed);
                tel.counts.entities.store(static_cast<int>(validPeds.size()), std::memory_order_relaxed);
                tel.counts.snapshot_drops.store(s_snapshotDrops.load(std::memory_order_relaxed), std::memory_order_relaxed);
            }

            s_pipelineMetrics.RecordFrame(frameMs);
        }


        void collectFrameData(uintptr_t localPlayer, const Vec3& localPos) {


            auto& writeRawPeds = s_acquireRawPeds;
            auto& writePeds = s_acquireValidPeds;
            auto& writePositions = s_acquirePositions;
            static std::vector<uintptr_t> previousPeds;
            static std::vector<Vec3> previousPositions;
            static uint64_t previousSession = 0;
            const uint64_t currentSession = s_sessionGeneration.load(std::memory_order_acquire);
            if (previousSession != currentSession) {
                previousPeds.clear();
                previousPositions.clear();
                previousSession = currentSession;
            }




            if (!offset::world || !offset::replay || !offset::viewport || !localPlayer) {
                writePeds.clear();
                writePositions.clear();
                s_acquireVelocities.clear();
                s_acquireHealths.clear();
                s_acquireKnockFlags.clear();
                return;
            }
            writePeds.clear();
            writePositions.clear();
            s_acquireVelocities.clear();
            s_acquireHealths.clear();
            s_acquireKnockFlags.clear();


            EnsureAcqScatter();
            auto handle = s_acqScatter ? s_acqScatter : mem.CreateScatterHandle();
            const bool ownedHandle = (handle != s_acqScatter);
            if (!handle) {
                writeRawPeds.clear();
                s_acquireWorldSampleTime = {};
                return;
            }

            static uintptr_t ped_replay_interface = 0;
            static uintptr_t pedListBase = 0;
            static ULONGLONG nextChainRefresh = 0;
            static ULONGLONG lastGoodChainMs = 0;
            static uint64_t collectorSession = 0;
            if (collectorSession != s_sessionGeneration) {
                collectorSession = s_sessionGeneration;
                ped_replay_interface = 0;
                pedListBase = 0;
                nextChainRefresh = 0;
                lastGoodChainMs = 0;
            }
            const ULONGLONG now = GetTickCount64();




            if (now >= nextChainRefresh) {
                uintptr_t freshInterface = 0;
                uintptr_t freshList = 0;
                mem.AddScatterReadRequest(handle, offset::replay + offset::replayPedInterface,
                    &freshInterface, sizeof(uintptr_t));
                mem.SetDmaCallTag("FiveM.ReplayInterface");
                mem.ExecuteReadScatter(handle);
                if (freshInterface > 0x10000u) {
                    mem.AddScatterReadRequest(handle, freshInterface + offset::pedInterfaceList,
                        &freshList, sizeof(uintptr_t));
                    mem.SetDmaCallTag("FiveM.PedListPointer");
                    mem.ExecuteReadScatter(handle);
                }
                if (freshInterface > 0x10000u && freshList > 0x10000u) {
                    ped_replay_interface = freshInterface;
                    pedListBase = freshList;
                    lastGoodChainMs = now;
                    nextChainRefresh = now + 250;
                } else {
                    nextChainRefresh = now + 25;
                    if (now - lastGoodChainMs > 500u) {
                        ped_replay_interface = 0;
                        pedListBase = 0;
                    }
                }
            }

            if (ped_replay_interface) {
                if (pedListBase) {

                    const int listCap = MAX_PEDS;
                    if ((int)writeRawPeds.size() != listCap)
                        writeRawPeds.resize(listCap);


                    std::fill(writeRawPeds.begin(), writeRawPeds.end(), uintptr_t{0});

                    mem.AddScatterReadRequest(handle, pedListBase,
                        writeRawPeds.data(), sizeof(uintptr_t) * listCap);
                    mem.SetDmaCallTag("FiveM.PedList");
                    mem.ExecuteReadScatter(handle);
                    s_lastRawPedCount.store(static_cast<int>(std::count_if(
                        writeRawPeds.begin(), writeRawPeds.end(),
                        [](uintptr_t value) { return value > 0x10000u; })),
                        std::memory_order_relaxed);


                    static std::vector<uintptr_t> playerInfoPtrs;
                    static std::vector<uintptr_t> playerInfoOwners;
                    static std::vector<ULONGLONG> playerInfoVerifiedMs;
                    static std::vector<ULONGLONG> playerInfoRetryMs;
                    static std::vector<uint8_t> playerInfoFailures;
                    static ULONGLONG nextInfoRefresh = 0;
                    static uint64_t infoSession = 0;
                    if (infoSession != s_sessionGeneration) {
                        infoSession = s_sessionGeneration;
                        playerInfoPtrs.clear();
                        playerInfoOwners.clear();
                        playerInfoVerifiedMs.clear();
                        playerInfoRetryMs.clear();
                        playerInfoFailures.clear();
                        nextInfoRefresh = 0;
                    }
                    if ((int)playerInfoPtrs.size() != listCap) {
                        playerInfoPtrs.assign(listCap, 0);
                        playerInfoOwners.assign(listCap, 0);
                        playerInfoVerifiedMs.assign(listCap, 0);
                        playerInfoRetryMs.assign(listCap, 0);
                        playerInfoFailures.assign(listCap, 0);
                    }
                    const bool refreshAllInfo = now >= nextInfoRefresh;
                    bool queuedInfoReads = false;
                    static std::vector<uintptr_t> freshInfoPtrs;
                    static std::vector<uint8_t> infoRequested;
                    freshInfoPtrs.assign(listCap, 0);
                    infoRequested.assign(listCap, 0);
                    for (int i = 0; i < listCap; i++) {
                        const uintptr_t ped = writeRawPeds[i];
                        if (!ped || ped == localPlayer) {
                            playerInfoPtrs[i] = 0;
                            playerInfoOwners[i] = ped;
                            playerInfoVerifiedMs[i] = 0;
                            playerInfoRetryMs[i] = 0;
                            playerInfoFailures[i] = 0;
                        } else if (refreshAllInfo || playerInfoOwners[i] != ped ||
                                   (playerInfoPtrs[i] == 0 && playerInfoFailures[i] == 1 &&
                                    now >= playerInfoRetryMs[i])) {
                            if (playerInfoOwners[i] != ped) {
                                playerInfoPtrs[i] = 0;
                                playerInfoVerifiedMs[i] = 0;
                                playerInfoFailures[i] = 0;
                            }
                            playerInfoOwners[i] = ped;
                            mem.AddScatterReadRequest(handle, ped + offset::playerInfo,
                                &freshInfoPtrs[i], sizeof(uintptr_t));
                            infoRequested[i] = 1;
                            queuedInfoReads = true;
                        }
                    }
                    if (queuedInfoReads) {
                        mem.SetDmaCallTag("FiveM.PlayerInfoPointers");
                        mem.ExecuteReadScatter(handle);
                        for (int i = 0; i < listCap; ++i) {
                            if (!infoRequested[i]) continue;
                            const uintptr_t info = freshInfoPtrs[i];
                            if (info > 0x10000u && info < 0x7FFFFFFFFFFFULL) {
                                playerInfoPtrs[i] = info;
                                playerInfoVerifiedMs[i] = now;
                                playerInfoFailures[i] = 0;
                            } else if (playerInfoVerifiedMs[i] == 0 ||
                                       now - playerInfoVerifiedMs[i] > 100u) {
                                playerInfoPtrs[i] = 0;
                                if (playerInfoFailures[i] < 2) ++playerInfoFailures[i];
                                playerInfoRetryMs[i] = now + 75u;
                            }
                        }
                    }
                    if (refreshAllInfo)
                        nextInfoRefresh = now + 250;

                    for (int i = 0; i < (int)writeRawPeds.size(); i++) {
                        uintptr_t ped = writeRawPeds[i];
                        if (!ped || ped < 0x10000) continue;

                        const bool isLocal = (ped == localPlayer);
                        if (isLocal && !s_selfEsp.load(std::memory_order_relaxed))
                            continue;

                        if (playerInfoPtrs[i]) {


                            writePeds.push_back(ped);
                        } else if (s_npcEsp.load(std::memory_order_relaxed)) {
                            writePeds.push_back(ped);
                        } else if (isLocal && s_selfEsp.load(std::memory_order_relaxed)) {
                            writePeds.push_back(ped);
                        }


                    }




                    writePositions.clear();
                    s_acquireVelocities.clear();
                    s_acquireHealths.clear();
                    s_acquireKnockFlags.clear();
                    if (!writePeds.empty()) {
                        writePositions.resize(writePeds.size());
                        s_acquireVelocities.assign(writePeds.size(), Vec3{});
                        s_acquireHealths.assign(writePeds.size(), -1.f);
                        s_acquireKnockFlags.assign(writePeds.size(), 0u);
                        for (size_t i = 0; i < writePeds.size(); i++) {
                            mem.AddScatterReadRequest(handle, writePeds[i] + offset::playerPosition,
                                &writePositions[i], sizeof(Vec3));
                            mem.AddScatterReadRequest(handle, writePeds[i] + offset::pedVelocity,
                                &s_acquireVelocities[i], sizeof(Vec3));
                            mem.AddScatterReadRequest(handle, writePeds[i] + offset::playerHealth,
                                &s_acquireHealths[i], sizeof(float));
                            if (offset::pedConfigFlags) {
                                mem.AddScatterReadRequest(handle,
                                    writePeds[i] + offset::pedConfigFlags + kPedConfigFlagsKnockedWordBytes,
                                    &s_acquireKnockFlags[i], sizeof(uint32_t));
                            }
                        }
                        const auto positionReadBegin = std::chrono::steady_clock::now();
                        mem.SetDmaCallTag("FiveM.CoreWorld");
                        const bool coreReadOk = mem.ExecuteReadScatter(handle);
                        const auto positionReadEnd = std::chrono::steady_clock::now();
                        s_acquireWorldSampleTime = positionReadBegin +
                            (positionReadEnd - positionReadBegin) / 2;





                        {
                            static uint64_t nextAltProbeMs = 0;
                            int nearZero = 0;
                            int movedWithoutVelocity = 0;
                            for (size_t i = 0; i < s_acquireVelocities.size(); ++i) {
                                const auto& v = s_acquireVelocities[i];
                                const float sp = v.x * v.x + v.y * v.y + v.z * v.z;
                                if (!(sp > 0.01f && sp < 40000.f)) {
                                    ++nearZero;
                                    for (size_t old = 0; old < previousPeds.size() &&
                                        old < previousPositions.size(); ++old) {
                                        if (previousPeds[old] != writePeds[i]) continue;
                                        const Vec3 delta = writePositions[i] - previousPositions[old];
                                        const float movedSq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
                                        if (movedSq > 0.25f && movedSq < 2500.f) ++movedWithoutVelocity;
                                        break;
                                    }
                                }
                            }
                            const uint64_t probeNowMs = GetTickCount64();
                            if (nearZero > static_cast<int>(s_acquireVelocities.size() / 2) &&
                                movedWithoutVelocity > 0 && probeNowMs >= nextAltProbeMs && handle) {
                                nextAltProbeMs = probeNowMs + 1000u;
                                std::vector<Vec3> alt(s_acquireVelocities.size());
                                for (size_t i = 0; i < writePeds.size(); ++i)
                                    mem.AddScatterReadRequest(handle,
                                        writePeds[i] + offset::pedVelocityAlt, &alt[i], sizeof(Vec3));
                                mem.SetDmaCallTag("FiveM.VelocityAlt");
                                mem.ExecuteReadScatter(handle);
                                int altOk = 0;
                                for (size_t i = 0; i < alt.size(); ++i) {
                                    const float sp = alt[i].x * alt[i].x + alt[i].y * alt[i].y +
                                        alt[i].z * alt[i].z;
                                    if (sp > 0.01f && sp < 40000.f) {
                                        s_acquireVelocities[i] = alt[i];
                                        ++altOk;
                                    }
                                }

                                auto& tel = OmniGhost::Gameplay::DmaTelemetry::FiveM();
                                tel.counts.reads_s.fetch_add(1, std::memory_order_relaxed);
                                (void)altOk;
                            }

                        }


                        static std::vector<uintptr_t> alivePeds;
                        static std::vector<Vec3> alivePos;
                        static std::vector<Vec3> aliveVel;
                        static std::vector<float> aliveHp;
                        static std::vector<uint32_t> aliveKnock;
                        alivePeds.clear();
                        alivePos.clear();
                        aliveVel.clear();
                        aliveHp.clear();
                        aliveKnock.clear();
                        if (alivePeds.capacity() < MAX_PEDS) alivePeds.reserve(MAX_PEDS);
                        if (alivePos.capacity() < MAX_PEDS) alivePos.reserve(MAX_PEDS);
                        if (aliveVel.capacity() < MAX_PEDS) aliveVel.reserve(MAX_PEDS);
                        if (aliveHp.capacity() < MAX_PEDS) aliveHp.reserve(MAX_PEDS);
                        if (aliveKnock.capacity() < MAX_PEDS) aliveKnock.reserve(MAX_PEDS);
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
                            {
                                Vec3 vel = i < s_acquireVelocities.size()
                                    ? s_acquireVelocities[i] : Vec3{};
                                if (!std::isfinite(vel.x) || !std::isfinite(vel.y) ||
                                    !std::isfinite(vel.z) ||
                                    std::fabs(vel.x) > 200.f || std::fabs(vel.y) > 200.f ||
                                    std::fabs(vel.z) > 200.f)
                                    vel = {};
                                aliveVel.push_back(vel);
                            }
                            {
                                float hp = coreReadOk && i < s_acquireHealths.size()
                                    ? s_acquireHealths[i] : -1.f;
                                if (ClassifyPedHealth(hp) == PedHealthState::Unknown)
                                    hp = -1.f;
                                aliveHp.push_back(hp);
                            }
                            aliveKnock.push_back(i < s_acquireKnockFlags.size()
                                ? s_acquireKnockFlags[i] : 0u);
                        }
                        writePeds.swap(alivePeds);
                        writePositions.swap(alivePos);
                        s_acquireVelocities.swap(aliveVel);
                        s_acquireHealths.swap(aliveHp);
                        s_acquireKnockFlags.swap(aliveKnock);
                    }





                    size_t kMax = esp::config.web_radar_enabled
                        ? static_cast<size_t>(std::clamp(esp::config.web_radar_max_players, 8, MAX_PEDS))
                        : 48u;
                    if (s_performanceMode.load(std::memory_order_relaxed) ||
                        (esp::config.web_radar_enabled && esp::config.web_radar_mode == 2))
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
                        static std::vector<Vec3> rankedVel;
                        static std::vector<float> rankedHp;
                        static std::vector<uint32_t> rankedKnock;
                        rankedVel.clear();
                        rankedHp.clear();
                        rankedKnock.clear();
                        rankedPeds.reserve(take);
                        rankedPos.reserve(take);
                        rankedVel.reserve(take);
                        rankedHp.reserve(take);
                        rankedKnock.reserve(take);
                        for (size_t n = 0; n < take; ++n) {
                            const size_t idx = order[n].idx;
                            rankedPeds.push_back(writePeds[idx]);
                            rankedPos.push_back(writePositions[idx]);
                            rankedVel.push_back(idx < s_acquireVelocities.size()
                                ? s_acquireVelocities[idx] : Vec3{});
                            rankedHp.push_back(idx < s_acquireHealths.size()
                                ? s_acquireHealths[idx] : -1.f);
                            rankedKnock.push_back(idx < s_acquireKnockFlags.size()
                                ? s_acquireKnockFlags[idx] : 0u);
                        }
                        writePeds.swap(rankedPeds);
                        writePositions.swap(rankedPos);
                        s_acquireVelocities.swap(rankedVel);
                        s_acquireHealths.swap(rankedHp);
                        s_acquireKnockFlags.swap(rankedKnock);
                    } else if (writePeds.size() > kMax) {
                        writePeds.resize(kMax);
                        if (writePositions.size() > kMax) writePositions.resize(kMax);
                        if (s_acquireVelocities.size() > kMax) s_acquireVelocities.resize(kMax);
                        if (s_acquireHealths.size() > kMax) s_acquireHealths.resize(kMax);
                        if (s_acquireKnockFlags.size() > kMax) s_acquireKnockFlags.resize(kMax);
                    }
                    s_lastFilteredPedCount.store((std::max)(0,
                        s_lastRawPedCount.load(std::memory_order_relaxed) -
                        static_cast<int>(writePeds.size())), std::memory_order_relaxed);
                }
            }



            previousPeds.assign(writePeds.begin(), writePeds.end());
            previousPositions.assign(writePositions.begin(), writePositions.end());

            if (ownedHandle && handle) mem.CloseScatterHandle(handle);
        }


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



            uint16_t boneMask = 0;
            if (esp::config.enabled && esp::config.skeleton) {
                boneMask = 0x01FFu;
            } else {
                if (esp::config.enabled && (esp::config.head_circle ||
                    esp::config.head_halo || esp::config.look_direction || esp::config.chinese_hat ||
                    esp::config.angel_wings || esp::config.devil_horns || esp::config.floating_crown))
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

            (void)boneMask;

            if (!esp::config.enabled)
                return;





            for (const uintptr_t ped : validPeds) {
                if (ped)
                    esp::render_esp_for_ped(ped, view_matrix, offset::localplayer);
            }
            (void)maxDistSq;
        }





        void refreshCache() {
            g_pedCacheManager.manualCache();
        }


        bool try_get_prepared_origin(uintptr_t ped, Vec3& out) {
            PedData data;
            if (!g_pedCacheManager.getPedData(ped, data) || !data.isValid)
                return false;
            out = data.position_origin;
            return true;
        }

        bool try_get_prepared_velocity(uintptr_t ped, Vec3& out) {
            PedData data;
            if (g_pedCacheManager.getPedData(ped, data) && data.isValid) {
                const float sp = data.velocity.x * data.velocity.x +
                    data.velocity.y * data.velocity.y + data.velocity.z * data.velocity.z;
                if (sp > 0.01f && sp < 40000.f) {
                    out = data.velocity;
                    return true;
                }
            }

            std::shared_lock lock(s_presentation_mutex);
            const auto it = s_presentation.find(ped);
            if (it == s_presentation.end() || !it->second.initialized ||
                it->second.entry_generation != s_sessionGeneration)
                return false;
            out = it->second.velocity;
            return true;
        }

        bool try_get_prepared_health(uintptr_t ped, float& out) {
            PedData data;
            if (!g_pedCacheManager.getPedData(ped, data) || !data.isValid ||
                ClassifyPedHealth(data.health) == PedHealthState::Unknown)
                return false;
            out = data.health;
            return true;
        }

        bool try_get_prepared_bone_position(uintptr_t ped, int bone, Vec3& out) {
            return ::esp::try_get_prepared_bone_position(ped, bone, out);
        }
    }
}
