#pragma once
#include <vector>
#include <chrono>
#include <array>
#include "math/math.h"

namespace FiveM {
    namespace ESP {

        // Constants
        const int MAX_PEDS = 110;

        // Global containers (kept for backward compatibility with render consumers)
        extern std::vector<uintptr_t> rawPedPointers;
        extern std::vector<Vec3> positions;
        extern std::vector<uintptr_t> validPeds;
        extern std::vector<Vec2> screenPositions;

        // Per-entity data frame — fully populated on acquisition thread,
        // immutable once published via SnapshotExchange.
        struct EntityFrame {
            uintptr_t ped = 0;
            Vec3 position{};
            Vec3 velocity{};           // for prediction/smoothing
            float health = 0.f;
            float max_health = 200.f;
            float armor = 0.f;
            bool visible = true;
            uintptr_t player_info = 0;
            uint32_t network_id = 0;
            uintptr_t weapon_manager = 0;
            uintptr_t weapon_info = 0;
            uint32_t weapon_hash = 0;
            uintptr_t vehicle = 0;
            Matrix bone_matrix{};
            std::array<Vector3, 9> bone_offsets{};
            uint16_t bone_mask = 0;
            bool valid = false;
        };

        // Snapshot exchanged between acquisition and render threads.
        // All data is fully populated on acquisition side; render thread
        // only reads — zero DMA on render path.
        struct AcquisitionSnapshot {
            std::array<EntityFrame, MAX_PEDS> entities{};
            int count = 0;
            Matrix viewMatrix{};
            Vec3 localPos{};
            uintptr_t localPlayer = 0;
            bool frameCacheValid = false;
            uint64_t generation = 0;
            std::chrono::steady_clock::time_point timestamp{};
            float acquireMs = 0.f;
        };

        // Vehicle snapshot for acquisition lane (namespace level)
        struct VehicleSnapshot {
            struct VehicleData {
                uintptr_t address = 0;
                Vec3 position{};
                Matrix matrix{};
                float distance = 0.f;
                bool locked = false;
                bool lock_state_known = false;
                bool occupied = false;
                int8_t gear = 0;
                float engine_hp = 0.f;
                bool valid = false;
            };
            std::array<VehicleData, 64> vehicles{};
            int count = 0;
            Vec3 localPos{};
            uint64_t generation = 0;
            std::chrono::steady_clock::time_point timestamp{};
            float acquireMs = 0.f;
        };

        // Core ESP functions
        void InitializeContainers();
        void RunESP(); // Presentation loop; acquisition is asynchronous
        void StopAcquisition();
        void EnsureAcquisitionStarted(); // Restart acquisition after reinit

        // Data collection (acquisition thread only)
        void collectFrameData(uintptr_t localPlayer, const Vec3& localPos);

        // Rendering (render thread only — zero DMA)
        void renderESP();

        // Cache management
        void refreshCache();

        // Performance monitoring
        void printPerformanceStats();

        // Shared 1×/frame DMA cache (ESP + aim + radar)
        bool FrameCacheValid();
        const Matrix& GetFrameViewMatrix();
        const Vec3& GetFrameLocalPos();

        // Prepared data access for aimbot integration (read from current snapshot)
        bool try_get_prepared_origin(uintptr_t ped, Vec3& out);
        bool try_get_prepared_velocity(uintptr_t ped, Vec3& out);
        bool try_get_prepared_health(uintptr_t ped, float& out);
        bool try_get_prepared_armor(uintptr_t ped, float& out);
        bool try_get_prepared_weapon(uintptr_t ped, uint32_t& out);
        bool try_get_prepared_vehicle(uintptr_t ped, uintptr_t& out);
        bool try_get_prepared_bone_position(uintptr_t ped, int bone, Vec3& out);
        bool try_get_prepared_visibility(uintptr_t ped, bool& out);
        bool try_get_prepared_network_id(uintptr_t ped, uint32_t& out);

        // Snapshot access for render
        const AcquisitionSnapshot* AcquireSnapshot();
        const VehicleSnapshot* AcquireVehicleSnapshot();
    }
}
