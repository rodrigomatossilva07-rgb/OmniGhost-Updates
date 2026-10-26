#pragma once
#include <vector>
#include <chrono>
#include "math/math.h"
#include "gameplay/snapshot_exchange.h"
#include "health_state.h"

namespace FiveM {
    namespace ESP {

        extern std::vector<uintptr_t> rawPedPointers;
        extern std::vector<Vec3> positions;
        extern std::vector<uintptr_t> validPeds;
        extern std::vector<Vec2> screenPositions;



        struct ValidPedsSnapshot {
            std::vector<uintptr_t> validPeds;
            std::vector<Vec3> positions;
            uint64_t generation = 0;
            std::chrono::steady_clock::time_point timestamp{};
        };


        void UpdateValidPedsSnapshot(const std::vector<uintptr_t>& peds, const std::vector<Vec3>& pos, uint64_t gen);
        [[nodiscard]] ValidPedsSnapshot GetValidPedsSnapshot() noexcept;
        [[nodiscard]] int LastRawPedCount() noexcept;
        [[nodiscard]] int LastFilteredPedCount() noexcept;


        [[nodiscard]] const std::vector<uintptr_t>& GetValidPeds() noexcept;
        [[nodiscard]] const std::vector<Vec3>& GetPositions() noexcept;


        extern int frameCount;
        extern std::chrono::steady_clock::time_point lastFrameTime;


        extern const int MAX_PEDS;


        void InitializeContainers();
        void RunESP();
        void StopAcquisition();
        bool RemoteProcessRecentlySeen() noexcept;


        void collectFrameData(uintptr_t localPlayer, const Vec3& localPos);
        void renderESP();


        void refreshCache();





        bool FrameCacheValid();
        const Matrix& GetFrameViewMatrix();
        const Vec3& GetFrameLocalPos();


        bool try_get_prepared_origin(uintptr_t ped, Vec3& out);
        bool try_get_prepared_velocity(uintptr_t ped, Vec3& out);
        bool try_get_prepared_health(uintptr_t ped, float& out);
        bool try_get_prepared_bone_position(uintptr_t ped, int bone, Vec3& out);


        std::atomic<uint64_t>& GetSessionGeneration();


        struct CameraSnapshotHandle {
            const Matrix* viewMatrix = nullptr;
            uint64_t sampleMs = 0;
            uint64_t sessionGeneration = 0;
            const std::array<Matrix, 12>* historyMatrices = nullptr;
            const std::array<uint64_t, 12>* historyTimesMs = nullptr;
            unsigned historyCount = 0;
            bool valid() const { return viewMatrix != nullptr; }
        };
        [[nodiscard]] CameraSnapshotHandle AcquireCameraSnapshot();


        struct LivenessSample {
            uintptr_t ped = 0;
            float health = -1.f;
            bool knocked = false;
            uint64_t sample_ms = 0;
            uint64_t generation = 0;
        };
        struct LivenessSnapshotHandle {
            const LivenessSample* players = nullptr;
            uint32_t count = 0;
            uint64_t timestamp_ms = 0;
            uint64_t generation = 0;
            bool valid() const { return players != nullptr && count > 0; }
        };
        [[nodiscard]] LivenessSnapshotHandle AcquireLivenessSnapshot();


    }
}
