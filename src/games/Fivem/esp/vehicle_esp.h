#pragma once
#include "../../ImGui/imgui.h"
#include "math/math.h"
#include "ui/ui_models.h"
#include <vector>
#include <cstdint>

namespace vehicle_esp {

    struct VehicleData {
        uintptr_t address = 0;
        Vec3 position{};
        float distance = 0.f;
        bool locked = false;
        bool lock_state_known = false;
        bool occupied = false;
        int8_t gear = 0;
        float engine_health = 0.f;
        bool valid = false;
        uint64_t sample_ms = 0; // acquisition midpoint for camera coherence
    };

    extern Config config;
    extern std::vector<VehicleData> vehicles;

    void Collect(const Config& frameConfig, const std::vector<uintptr_t>& peds,
                 uintptr_t localPlayer, bool force = false);
    void Render();
    void Run();
    void Shutdown();

} // namespace vehicle_esp
