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
        bool occupied = false;
        bool valid = false;
    };

    extern Config config;
    extern std::vector<VehicleData> vehicles;

    void Collect(bool force = false);
    void Render();
    void Run();

} // namespace vehicle_esp
