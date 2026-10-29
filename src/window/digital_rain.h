#pragma once

#include "src/project/third_party/ImGui/imgui.h"
#include <array>
#include <string>

namespace DigitalRain {




enum class QualityPreset : uint8_t {
    Disabled = 0,
    Potato = 1,
    Low = 2,
    Medium = 3,
    High = 4,
    Ultra = 5
};

struct QualitySettings {
    int maxColumns = 96;
    int minTrail = 10;
    int maxTrail = 28;
    int maxStars = 48;
    int maxClusters = 7;
    float spacing = 12.0f;
    bool enableConstellations = true;
    bool enableTrails = true;
    bool enableGlow = false;
    float fadeSeconds = 0.22f;
    int baseAlphaMin = 48;
    int baseAlphaMax = 90;
    float fontSizeMin = 10.0f;
    float fontSizeMax = 15.0f;
    float speedMin = 28.0f;
    float speedMax = 98.0f;
    float trailStepFactor = 1.4f;
    float alphaScaleHead = 2.05f;
    float alphaScaleTail = 0.35f;
    float alphaDecay = 0.028f;
    int linkAlphaMax = 28;
    int nodeAlphaMax = 42;
    int linkDistanceFactor = 7;

    static QualitySettings FromPreset(QualityPreset preset) noexcept;
};

[[nodiscard]] QualitySettings GetQualitySettings(QualityPreset preset) noexcept;
void SetQualityPreset(QualityPreset preset) noexcept;
[[nodiscard]] QualityPreset GetQualityPreset() noexcept;




struct GPUInfo {
    std::string vendor;
    std::string renderer;
    std::string version;
    bool isIntegrated = false;
    bool isLowEnd = false;
    size_t vramMB = 0;
};

[[nodiscard]] GPUInfo DetectGPU() noexcept;
void AutoSelectQualityPreset() noexcept;









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
    bool edge_only = false,
    bool draw_constellations = true);



void DrawGoldMotif(ImDrawList* dl, const ImVec2& position, const ImVec2& size,
                   bool performance_mode, float motion_scale,
                   float opacity = 1.0f, float quiet_left_width = 0.0f);


void SetDynamicDensity(float density_scale) noexcept;
void SetDynamicMotionScale(float motion_scale) noexcept;
void SetDynamicConstellationDensity(float density_scale) noexcept;
[[nodiscard]] float GetDynamicDensity() noexcept;
[[nodiscard]] float GetDynamicMotionScale() noexcept;
[[nodiscard]] float GetDynamicConstellationDensity() noexcept;


void SetQualityPreset(QualityPreset preset) noexcept;
[[nodiscard]] QualityPreset GetQualityPreset() noexcept;
void SetQualityPresetOverride(QualityPreset preset) noexcept;
[[nodiscard]] bool IsQualityOverrideActive() noexcept;


void SetIGPUFallbackEnabled(bool enabled) noexcept;
[[nodiscard]] bool IsIGPUFallbackEnabled() noexcept;
void ApplyIGPUFallback() noexcept;


void SetCustomQualitySettings(const struct QualitySettings& settings) noexcept;
[[nodiscard]] struct QualitySettings GetCurrentQualitySettings() noexcept;

}
