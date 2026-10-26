#pragma once
#include "third_party/ImGui/imgui.h"
#include "math/math.h"
#include "ui/ui_models.h"
#include <vector>
#include <cstdint>
#include <string>

namespace vehicle_esp {

    struct VehicleData {
        uintptr_t address = 0;
        Vec3 position{};
        float distance = 0.f;
        bool locked = false;
        bool lock_state_known = false;
        bool occupied = false;
        int occupants = 0;
        bool local_vehicle = false;
        int8_t gear = 0;
        float engine_health = 0.f;
        float speed_kmh = 0.f;
        float heading_degrees = 0.f;
        std::string display_name{"Veículo"};
        bool valid = false;
        uint64_t sample_ms = 0;
    };

    extern Config config;
    extern std::vector<VehicleData> vehicles;
    std::vector<VehicleData> GetSnapshot();

    void Collect(const Config& frameConfig, const std::vector<uintptr_t>& peds,
                 const Vec3& localPos,
                 const Matrix& cameraMatrix, bool force = false);
    void Render();
    void Run();
    void Shutdown();

}
