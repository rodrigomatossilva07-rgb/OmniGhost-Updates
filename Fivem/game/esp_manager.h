#pragma once
#include <vector>
#include <chrono>
#include "math/math.h"

namespace FiveM {
    namespace ESP {
        // Pre-allocated containers for performance
        extern std::vector<uintptr_t> rawPedPointers;
        extern std::vector<Vec3> positions;
        extern std::vector<uintptr_t> validPeds;
        extern std::vector<Vec2> screenPositions;

        // Performance tracking
        extern int frameCount;
        extern std::chrono::steady_clock::time_point lastFrameTime;

        // Constants
        extern const int MAX_PEDS;

        // Core ESP functions
        void InitializeContainers();
        void RunESP(); // Main ESP loop (single-threaded)

        // Data collection and rendering functions
        void collectFrameData(); // Data collection
        void renderESP();        // Rendering

        // Cache management
        void refreshCache();     // Manual cache refresh

        // Performance monitoring
        void printPerformanceStats();

        // Shared 1×/frame DMA cache (ESP + aim + radar)
        bool FrameCacheValid();
        const Matrix& GetFrameViewMatrix();
        const Vec3& GetFrameLocalPos();
        
        // Prepared data access for aimbot integration
        bool try_get_prepared_origin(uintptr_t ped, Vec3& out);
        bool try_get_prepared_velocity(uintptr_t ped, Vec3& out);
        bool try_get_prepared_health(uintptr_t ped, float& out);
        bool try_get_prepared_bone_position(uintptr_t ped, int bone, Vec3& out);
    }
}
