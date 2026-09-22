#pragma once
#include "../../ImGui/imgui.h"
#include "math/math.h"
#include "ui/ui_models.h"
#include <cstdint>
#include <chrono>

namespace aimbot {

    struct TargetInfo {
        uintptr_t ped = 0;
        Vec3 world_pos{};
        Vec2 screen_pos{};
        float distance = 0.f;
        float crosshair_dist = 99999.f;
        bool valid = false;
    };

    extern Config config;
    extern TargetInfo current_target;

    void Initialize();
    void Run();
    void DrawFOV();
    int HitboxToBoneIndex(Hitbox hb);
    bool FindBestTarget(TargetInfo& out, float fov_px, float max_dist);
    void HumanizedMove(float dx, float dy);
    void ResetTrackingState();
    void ApplyLegitProfile();
    void ApplyRageProfile();

} // namespace aimbot
