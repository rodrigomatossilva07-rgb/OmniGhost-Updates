#pragma once

#include "../ImGui/imgui.h"
#include <array>

namespace DigitalRain {

    // Soft gold matrix rain behind all UI. density_scale controls how many
    // columns/constellations are created (Subtle ≈ 0.28, Full = 1.0).
    // motion_scale is 0 when Reduce Motion is enabled. edge_only keeps the
    // centre calm, useful for authentication forms.
    void Initialize();
    void Shutdown();
    void Draw(
        ImDrawList* dl,
        const ImVec2& window_pos,
        const ImVec2& window_size,
        bool enabled,
        bool performance_mode,
        float quiet_left_width = 0.0f,
        float opacity = 1.0f,
        float density_scale = 1.0f,
        float motion_scale = 1.0f,
        bool edge_only = false);

    // Dynamic performance scaling - called from PerformanceManager
    void SetDynamicDensity(float density_scale) noexcept;
    void SetDynamicMotionScale(float motion_scale) noexcept;
    void SetDynamicConstellationDensity(float density_scale) noexcept;
    [[nodiscard]] float GetDynamicDensity() noexcept;
    [[nodiscard]] float GetDynamicMotionScale() noexcept;
    [[nodiscard]] float GetDynamicConstellationDensity() noexcept;

} // namespace DigitalRain
