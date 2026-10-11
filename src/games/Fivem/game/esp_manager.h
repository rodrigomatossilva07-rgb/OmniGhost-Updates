#pragma once
#include <vector>
#include <chrono>
#include "math/math.h"
#include "gameplay/snapshot_exchange.h"
#include "health_state.h"

namespace FiveM {
    namespace ESP {
        // Pre-allocated containers for performance
        extern std::vector<uintptr_t> rawPedPointers;
        extern std::vector<Vec3> positions;
        extern std::vector<uintptr_t> validPeds;
        extern std::vector<Vec2> screenPositions;

        // Thread-safe access to validPeds and related containers
        // Uses the snapshot exchange for thread-safe access to acquisition data
        struct ValidPedsSnapshot {
            std::vector<uintptr_t> validPeds;
            std::vector<Vec3> positions;
            uint64_t generation = 0;
            std::chrono::steady_clock::time_point timestamp{};
        };

        // Thread-safe accessors (acquisition thread writes, render thread reads)
        void UpdateValidPedsSnapshot(const std::vector<uintptr_t>& peds, const std::vector<Vec3>& pos, uint64_t gen);
        [[nodiscard]] ValidPedsSnapshot GetValidPedsSnapshot() noexcept;
        
        // Legacy accessors (deprecated - use snapshot instead)
        [[nodiscard]] const std::vector<uintptr_t>& GetValidPeds() noexcept;
        [[nodiscard]] const std::vector<Vec3>& GetPositions() noexcept;

        // Performance tracking
        extern int frameCount;
        extern std::chrono::steady_clock::time_point lastFrameTime;

        // Constants
        extern const int MAX_PEDS;

        // Core ESP functions
        void InitializeContainers();
        void RunESP(); // Presentation loop; acquisition is asynchronous
        void StopAcquisition();
        bool RemoteProcessRecentlySeen() noexcept;

        // Data collection and rendering functions
        void collectFrameData(uintptr_t localPlayer, const Vec3& localPos); // acquisition thread only
        void renderESP();        // Rendering

        // Cache management
        void refreshCache();     // Manual cache refresh

        // Performance monitoring
        void printPerformanceStats();

        // Shared 1x/frame DMA cache (ESP + aim + radar)
        bool FrameCacheValid();
        const Matrix& GetFrameViewMatrix();
        const Vec3& GetFrameLocalPos();
        
        // Prepared data access for aimbot integration
        bool try_get_prepared_origin(uintptr_t ped, Vec3& out);
        bool try_get_prepared_velocity(uintptr_t ped, Vec3& out);
        bool try_get_prepared_health(uintptr_t ped, float& out);
        bool try_get_prepared_bone_position(uintptr_t ped, int bone, Vec3& out);
        
        // Session generation for cache invalidation
        std::atomic<uint64_t>& GetSessionGeneration();

        // Camera snapshot access for temporal coherence (opaque handle)
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
        
        // Liveness (health-only) snapshot for fast death detection (opaque handle)
        struct LivenessSample {
            uintptr_t ped = 0;
            float health = -1.f;
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
        
        // Session generation for cache invalidation
        std::atomic<uint64_t>& GetSessionGeneration();
    }
}
