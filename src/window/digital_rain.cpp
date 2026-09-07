#include "digital_rain.h"
#include "fonts.h"
#include "../platform/performance_manager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <iomanip>

namespace DigitalRain {

QualitySettings QualitySettings::FromPreset(QualityPreset preset) noexcept {
    QualitySettings s;
    
    switch (preset) {
        case QualityPreset::Disabled:
            s.maxColumns = 0;
            s.enableConstellations = false;
            s.enableTrails = false;
            s.spacing = 0.0f;
            break;
            
        case QualityPreset::Potato:
            s.maxColumns = 16;
            s.minTrail = 4;
            s.maxTrail = 8;
            s.maxStars = 0;
            s.maxClusters = 0;
            s.spacing = 32.0f;
            s.enableConstellations = false;
            s.enableTrails = false;
            s.fadeSeconds = 0.15f;
            s.baseAlphaMin = 20;
            s.baseAlphaMax = 40;
            s.fontSizeMin = 8.0f;
            s.fontSizeMax = 10.0f;
            s.speedMin = 20.0f;
            s.speedMax = 50.0f;
            s.trailStepFactor = 1.0f;
            s.alphaScaleHead = 1.5f;
            s.alphaScaleTail = 0.2f;
            s.alphaDecay = 0.05f;
            s.linkAlphaMax = 0;
            s.nodeAlphaMax = 0;
            s.linkDistanceFactor = 0;
            break;
            
        case QualityPreset::Low:
            s.maxColumns = 32;
            s.minTrail = 6;
            s.maxTrail = 14;
            s.maxStars = 0;
            s.maxClusters = 0;
            s.spacing = 20.0f;
            s.enableConstellations = false;
            s.enableTrails = true;
            s.fadeSeconds = 0.20f;
            s.baseAlphaMin = 30;
            s.baseAlphaMax = 60;
            s.fontSizeMin = 9.0f;
            s.fontSizeMax = 12.0f;
            s.speedMin = 22.0f;
            s.speedMax = 70.0f;
            s.trailStepFactor = 1.2f;
            s.alphaScaleHead = 1.8f;
            s.alphaScaleTail = 0.25f;
            s.alphaDecay = 0.04f;
            s.linkAlphaMax = 0;
            s.nodeAlphaMax = 0;
            s.linkDistanceFactor = 0;
            break;
            
        case QualityPreset::Medium:
            s.maxColumns = 64;
            s.minTrail = 8;
            s.maxTrail = 20;
            s.maxStars = 24;
            s.maxClusters = 4;
            s.spacing = 16.0f;
            s.enableConstellations = true;
            s.enableTrails = true;
            s.fadeSeconds = 0.22f;
            s.baseAlphaMin = 40;
            s.baseAlphaMax = 80;
            s.fontSizeMin = 10.0f;
            s.fontSizeMax = 14.0f;
            s.speedMin = 25.0f;
            s.speedMax = 85.0f;
            s.trailStepFactor = 1.3f;
            s.alphaScaleHead = 2.0f;
            s.alphaScaleTail = 0.3f;
            s.alphaDecay = 0.03f;
            s.linkAlphaMax = 18;
            s.nodeAlphaMax = 30;
            s.linkDistanceFactor = 5;
            break;
            
        case QualityPreset::High:
            s.maxColumns = 96;
            s.minTrail = 10;
            s.maxTrail = 28;
            s.maxStars = 48;
            s.maxClusters = 7;
            s.spacing = 12.0f;
            s.enableConstellations = true;
            s.enableTrails = true;
            s.fadeSeconds = 0.22f;
            s.baseAlphaMin = 48;
            s.baseAlphaMax = 90;
            s.fontSizeMin = 10.0f;
            s.fontSizeMax = 15.0f;
            s.speedMin = 28.0f;
            s.speedMax = 98.0f;
            s.trailStepFactor = 1.4f;
            s.alphaScaleHead = 2.05f;
            s.alphaScaleTail = 0.35f;
            s.alphaDecay = 0.028f;
            s.linkAlphaMax = 28;
            s.nodeAlphaMax = 42;
            s.linkDistanceFactor = 7;
            break;
            
        case QualityPreset::Ultra:
            s.maxColumns = 96;
            s.minTrail = 12;
            s.maxTrail = 32;
            s.maxStars = 64;
            s.maxClusters = 8;
            s.spacing = 10.0f;
            s.enableConstellations = true;
            s.enableTrails = true;
            s.enableGlow = true;
            s.fadeSeconds = 0.25f;
            s.baseAlphaMin = 55;
            s.baseAlphaMax = 100;
            s.fontSizeMin = 11.0f;
            s.fontSizeMax = 16.0f;
            s.speedMin = 30.0f;
            s.speedMax = 110.0f;
            s.trailStepFactor = 1.5f;
            s.alphaScaleHead = 2.2f;
            s.alphaScaleTail = 0.4f;
            s.alphaDecay = 0.025f;
            s.linkAlphaMax = 35;
            s.nodeAlphaMax = 50;
            s.linkDistanceFactor = 8;
            break;
    }
    return s;
}

// ============================================================
// Quality Presets Implementation
// ============================================================

QualitySettings GetQualitySettings(QualityPreset preset) noexcept {
    return QualitySettings::FromPreset(preset);
}

// ============================================================
// Global State (namespace scope, not anonymous)
// ============================================================

constexpr int kMaxColumns = 96;
constexpr int kMaxTrail = 28;
constexpr int kMinTrail = 10;
constexpr int kMaxStars = 48;
constexpr char kCharset[] = "0123456789ABCDEF";
constexpr std::uint32_t kInitialSeed = 0xA5A5F00Du;

QualitySettings g_qualitySettings = QualitySettings::FromPreset(QualityPreset::High);
QualityPreset g_activePreset = QualityPreset::High;
QualityPreset g_overridePreset = QualityPreset::High;
bool g_overrideActive = false;
bool g_igpuFallbackEnabled = true;
bool g_qualityOverrideActive = false;

struct Column {
    float x = 0.0f;
    float y = 0.0f;
    float speed = 0.0f;
    int length = 10;
    float font_size = 11.0f;
    std::uint8_t base_alpha = 55;
    char glyphs[28]{};
};

struct Star {
    float x = 0.f, y = 0.f;
    float vx = 0.f, vy = 0.f;
    float phase = 0.f;
    float radius = 1.2f;
    int cluster = 0;
};

constexpr int kMaxClusters = 7;
struct Cluster {
    float cx = 0.f, cy = 0.f;
    float tx = 0.f, ty = 0.f;
    float scale = 1.f;
    int start = 0;
    int count = 0;
    int shape = 0;
};

Cluster g_clusters[7]{};
int g_cluster_count = 0;

Column g_cols[96]{};
Star g_stars[48]{};
int g_count = 0;
int g_star_count = 0;
bool g_ready = false;
float g_last_width = 0.0f;
float g_last_height = 0.0f;
float g_fade = 0.0f;
float g_last_density = -1.0f;
bool g_last_performance = false;
std::uint32_t g_rng = 0xA5A5F00Du;

float g_dynamicDensity = 1.0f;
float g_dynamicMotionScale = 1.0f;
float g_dynamicConstellationDensity = 1.0f;

GPUInfo g_gpuInfo{};
bool g_gpuDetected = false;
bool g_autoQualityEnabled = true;

// ============================================================
// Helpers
// ============================================================

std::uint32_t NextRand() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    if (g_rng == 0) g_rng = 0xA5A5F00Du;
    return g_rng;
}

float Rand01() {
    return static_cast<float>(NextRand() & 0xFFFFu) / 65535.0f;
}

bool IsValidSize(float width, float height) {
    return std::isfinite(width) && std::isfinite(height) && width >= 32.0f && height >= 32.0f;
}

int CalculateTargetColumnCount(float width, bool performance_mode) {
    if (!std::isfinite(width) || width <= 0.0f) return 0;
    const float density_scale = g_dynamicDensity;
    if (density_scale <= 0.001f) return 0;
    
    const float spacing = performance_mode ? g_qualitySettings.spacing * 1.8f : g_qualitySettings.spacing;
    const int full = static_cast<int>(std::ceil(width / spacing)) + 2;
    const int maximum = std::min(g_qualitySettings.maxColumns, kMaxColumns);
    const int scaled = static_cast<int>(std::lround(full * density_scale));
    return std::clamp(scaled, 1, maximum);
}

void GenerateGlyphs(Column& column) {
    for (int i = 0; i < kMaxTrail; ++i) {
        if ((NextRand() % 10u) == 0u)
            column.glyphs[i] = kCharset[10 + (NextRand() % 6u)];
        else
            column.glyphs[i] = kCharset[NextRand() % 10u];
    }
}

void SeedColumn(Column& column, float width, float height, bool random_y) {
    column = Column{};
    if (!IsValidSize(width, height)) return;

    const float usable = std::max(0.0f, width - 16.0f);
    column.x = 8.0f + Rand01() * usable;
    column.y = random_y ? Rand01() * height : -Rand01() * height * 0.45f;
    column.speed = g_qualitySettings.speedMin + Rand01() * (g_qualitySettings.speedMax - g_qualitySettings.speedMin);
    column.length = std::clamp(
        g_qualitySettings.minTrail + static_cast<int>(NextRand() % static_cast<std::uint32_t>(g_qualitySettings.maxTrail - g_qualitySettings.minTrail + 1)),
        g_qualitySettings.minTrail, g_qualitySettings.maxTrail);
    column.font_size = g_qualitySettings.fontSizeMin + Rand01() * (g_qualitySettings.fontSizeMax - g_qualitySettings.fontSizeMin);
    column.base_alpha = static_cast<std::uint8_t>(
        std::clamp(g_qualitySettings.baseAlphaMin + static_cast<int>(Rand01() * (g_qualitySettings.baseAlphaMax - g_qualitySettings.baseAlphaMin)), 1, 255));
    GenerateGlyphs(column);
}

void SeedStars(float width, float height, bool performance_mode) {
    if (!g_qualitySettings.enableConstellations) {
        g_cluster_count = 0;
        g_star_count = 0;
        return;
    }
    
    const float density_scale = g_dynamicConstellationDensity;
    const int fullClusters = performance_mode ? std::min(4, g_qualitySettings.maxClusters) : g_qualitySettings.maxClusters;
    g_cluster_count = std::clamp(static_cast<int>(std::lround(fullClusters * density_scale)), 0, fullClusters);
    g_star_count = 0;

    for (int c = 0; c < g_cluster_count; ++c) {
        Cluster& cl = g_clusters[c];
        cl.cx = width * (0.12f + Rand01() * 0.76f);
        cl.cy = height * (0.14f + Rand01() * 0.72f);
        for (int p = 0; p < c; ++p) {
            float dx = cl.cx - g_clusters[p].cx;
            float dy = cl.cy - g_clusters[p].cy;
            float min_d = (std::min)(width, height) * 0.18f;
            if (dx * dx + dy * dy < min_d * min_d) {
                cl.cx = width * (0.12f + Rand01() * 0.76f);
                cl.cy = height * (0.14f + Rand01() * 0.72f);
            }
        }
        cl.tx = cl.cx;
        cl.ty = cl.cy;
        cl.scale = 0.55f + Rand01() * 1.15f;
        cl.shape = static_cast<int>(NextRand() % 5u);
        cl.start = g_star_count;
        const int n = performance_mode
            ? (3 + static_cast<int>(NextRand() % 3u))
            : (4 + static_cast<int>(NextRand() % 6u));
        cl.count = n;
        if (g_star_count + n > 48)
            cl.count = 48 - g_star_count;
        if (cl.count <= 0) {
            g_cluster_count = c;
            break;
        }

        const float base_r = (std::min)(width, height) * 0.045f * cl.scale; (void)base_r;
        for (int i = 0; i < cl.count; ++i) {
            Star& s = g_stars[g_star_count++];
            s.cluster = c;
            s.phase = Rand01() * 6.28318f;
            s.radius = 0.7f + Rand01() * 1.3f * (0.7f + 0.3f * cl.scale);
            s.vx = (Rand01() - 0.5f) * 6.f;
            s.vy = (Rand01() - 0.5f) * 6.f;

            float ang = 0.f, rad = (std::min)(width, height) * 0.045f * cl.scale;
            switch (cl.shape) {
            case 1: // triangle
                ang = (i / (float)cl.count) * 6.28318f + 0.3f;
                rad *= (0.85f + 0.25f * Rand01());
                break;
            case 2: // diamond
                ang = (i % 4) * 1.5708f + (i >= 4 ? 0.4f : 0.f);
                rad *= (i < 4 ? 1.f : 0.45f);
                break;
            case 3: // arc
                ang = -0.9f + (i / (float)(std::max)(1, cl.count - 1)) * 1.8f;
                rad *= (0.7f + 0.5f * std::fabs(std::sin(ang * 1.2f)));
                s.x = cl.cx + ang * rad * 1.4f;
                s.y = cl.cy + std::sin(ang) * rad * 0.6f;
                continue;
            case 4: // scatter
                ang = Rand01() * 6.28318f;
                rad *= (0.3f + Rand01() * 1.1f);
                break;
            default: // ring
                ang = (i / (float)cl.count) * 6.28318f + Rand01() * 0.2f;
                rad *= (0.75f + Rand01() * 0.35f);
                break;
            }
            s.x = cl.cx + std::cos(ang) * rad;
            s.y = cl.cy + std::sin(ang) * rad;
        }
    }
    for (int i = g_star_count; i < 48; ++i)
        g_stars[i] = Star{};
}

void EnsureColumns(float width, float height, bool performance_mode) {
    if (!IsValidSize(width, height)) {
        g_count = 0;
        g_star_count = 0;
        g_cluster_count = 0;
        g_ready = false;
        return;
    }

    const float density_scale = g_dynamicDensity;
    const int target = CalculateTargetColumnCount(width, performance_mode);
    if (target <= 0) {
        g_count = 0;
        g_ready = false;
        return;
    }

    const bool size_changed =
        std::fabs(width - g_last_width) > 16.0f ||
        std::fabs(height - g_last_height) > 16.0f;

    const bool density_changed = std::fabs(density_scale - g_last_density) > 0.02f;
    const bool performance_changed = performance_mode != g_last_performance;
    if (g_ready && g_count == target && !size_changed && !density_changed && !performance_changed)
        return;

    g_count = std::clamp(target, 0, std::min(g_qualitySettings.maxColumns, kMaxColumns));
    for (int i = 0; i < g_count; ++i)
        SeedColumn(g_cols[i], width, height, true);
    for (int i = g_count; i < kMaxColumns; ++i)
        g_cols[i] = Column{};

    SeedStars(width, height, performance_mode);

    g_last_width = width;
    g_last_height = height;
    g_last_density = density_scale;
    g_last_performance = performance_mode;
    g_ready = g_count > 0;
}

// ============================================================
// GPU Detection
// ============================================================

GPUInfo DetectGPU() noexcept {
    GPUInfo info;
    
    // Try to get GPU info from WGL/GL
    // This is a simplified version - in production would use wglGetExtensionsStringARB, etc.
    
    // Check for integrated GPU indicators
    // Common integrated GPU vendors
    info.vendor = "Unknown";
    info.renderer = "Unknown";
    info.version = "Unknown";
    info.isIntegrated = false;
    info.isLowEnd = false;
    info.vramMB = 0;
    
    // Try to get from D3D11 if available
    // This is a placeholder - real implementation would use DXGI
    
    // Heuristic: if running on laptop battery or known integrated GPU names
    // For now, assume discrete GPU
    
    g_gpuInfo = info;
    g_gpuDetected = true;
    return info;
}

void AutoSelectQualityPreset() noexcept {
    if (!g_autoQualityEnabled) return;
    
    GPUInfo gpu = DetectGPU();
    
    if (!g_gpuDetected || gpu.isIntegrated || gpu.isLowEnd) {
        SetQualityPresetOverride(QualityPreset::Low);
    } else if (gpu.vramMB > 0 && gpu.vramMB < 2048) {
        SetQualityPresetOverride(QualityPreset::Medium);
    } else if (gpu.vramMB > 0 && gpu.vramMB < 4096) {
        SetQualityPresetOverride(QualityPreset::High);
    } else {
        SetQualityPresetOverride(QualityPreset::Ultra);
    }
}

void ApplyIGPUFallback() noexcept {
    SetQualityPresetOverride(QualityPreset::Low);
    g_igpuFallbackEnabled = true;
}

// ============================================================
// Quality Control
// ============================================================

void SetQualityPreset(QualityPreset preset) noexcept {
    if (g_qualityOverrideActive) return;
    g_activePreset = preset;
    g_qualitySettings = QualitySettings::FromPreset(preset);
}

QualityPreset GetQualityPreset() noexcept {
    return g_activePreset;
}

void SetQualityPresetOverride(QualityPreset preset) noexcept {
    g_activePreset = preset;
    g_qualitySettings = QualitySettings::FromPreset(preset);
    g_overridePreset = preset;
    g_overrideActive = true;
    g_qualityOverrideActive = true;
}

bool IsQualityOverrideActive() noexcept {
    return g_overrideActive;
}

void SetIGPUFallbackEnabled(bool enabled) noexcept {
    g_igpuFallbackEnabled = enabled;
}

bool IsIGPUFallbackEnabled() noexcept {
    return g_igpuFallbackEnabled;
}

void SetCustomQualitySettings(const struct QualitySettings& settings) noexcept {
    g_qualitySettings = settings;
}

QualitySettings GetCurrentQualitySettings() noexcept {
    return g_qualitySettings;
}

// ============================================================
// Main API Implementation
// ============================================================

void Initialize() {
    g_rng = kInitialSeed;
    (void)DetectGPU();
    if (g_autoQualityEnabled) {
        AutoSelectQualityPreset();
    }
    g_ready = false;
}

void Shutdown() {
    g_ready = false;
    g_count = 0;
    g_star_count = 0;
    g_cluster_count = 0;
}

void Draw(
    ImDrawList* dl,
    const ImVec2& window_pos,
    const ImVec2& window_size,
    bool enabled,
    bool performance_mode,
    float quiet_left_width,
    float opacity,
    float density_scale,
    float motion_scale,
    bool edge_only) {

    if (!enabled || !dl || opacity <= 0.0f || !IsValidSize(window_size.x, window_size.y)) {
        return;
    }

    g_dynamicDensity = density_scale;
    g_dynamicMotionScale = motion_scale;

    if (density_scale <= 0.001f) {
        return;
    }

    float width = window_size.x;
    float height = window_size.y;
    float left = window_pos.x + quiet_left_width;
    float right = window_pos.x + window_size.x;

    if (edge_only) {
        width = window_size.x * 0.5f;
        left = window_pos.x + window_size.x * 0.5f;
        right = window_pos.x + window_size.x;
    }

    EnsureColumns(width, height, performance_mode);
    if (!g_ready || g_count == 0) {
        return;
    }

    ImFont* font = CyberFonts::GetMonoFont();
    if (!font) font = ImGui::GetFont();

    const float dt = ImGui::GetIO().DeltaTime * motion_scale;
    if (dt <= 0.0f || dt > 0.1f) {
        return;
    }

    const float fade = g_qualitySettings.fadeSeconds > 0.0f ? std::exp(-dt / g_qualitySettings.fadeSeconds) : 0.9f;

    // Update columns
    for (int i = 0; i < g_count; ++i) {
        Column& col = g_cols[i];
        col.y += col.speed * dt;
        if (col.y > height + col.length * col.font_size) {
            SeedColumn(col, width, height, false);
            col.x = left + (col.x - window_pos.x);
        }
    }

    // Update stars
    if (g_qualitySettings.enableConstellations) {
        for (int i = 0; i < g_star_count; ++i) {
            Star& s = g_stars[i];
            s.x += s.vx * dt;
            s.y += s.vy * dt;
            s.phase += dt * 2.0f;
        }

        // Update clusters
        for (int c = 0; c < g_cluster_count; ++c) {
            Cluster& cl = g_clusters[c];
            float dx = cl.tx - cl.cx;
            float dy = cl.ty - cl.cy;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist > 2.0f) {
                cl.cx += dx * std::min(1.0f, dt * 3.0f);
                cl.cy += dy * std::min(1.0f, dt * 3.0f);
            } else {
                cl.tx = width * (0.12f + Rand01() * 0.76f);
                cl.ty = height * (0.14f + Rand01() * 0.72f);
            }
        }
    }

    // Draw columns
    for (int i = 0; i < g_count; ++i) {
        Column& col = g_cols[i];
        float x = col.x;
        float y = col.y;

        if (x < left || x > right) continue;

        for (int t = 0; t < col.length; ++t) {
            float ty = y - t * col.font_size * g_qualitySettings.trailStepFactor;
            if (ty < -col.font_size) break;

            char ch = col.glyphs[t];
            float alpha = col.base_alpha * std::pow(fade, static_cast<float>(t));
            if (t == 0) alpha = static_cast<float>(col.base_alpha) * g_qualitySettings.alphaScaleHead;
            else alpha *= g_qualitySettings.alphaScaleTail;

            alpha = std::clamp(alpha * opacity, 1.0f, 255.0f);
            if (alpha < 1.0f) continue;

            ImU32 color = IM_COL32(
                (int)(212 * alpha / 255.0f),
                (int)(175 * alpha / 255.0f),
                (int)(55 * alpha / 255.0f),
                static_cast<int>(alpha));

            ImVec2 pos(x, window_pos.y + ty);
            dl->AddText(font, col.font_size, pos, color, &ch, &ch + 1);
        }
    }

    // Draw constellations
    if (g_qualitySettings.enableConstellations && g_cluster_count > 0) {
        for (int c = 0; c < g_cluster_count; ++c) {
            Cluster& cl = g_clusters[c];
            int end = cl.start + cl.count;
            for (int i = cl.start; i < end; ++i) {
                Star& s = g_stars[i];
                if (s.x < left || s.x > right) continue;
                if (s.y < 0 || s.y > height) continue;

                ImU32 color = IM_COL32(212, 175, 55, g_qualitySettings.nodeAlphaMax);
                dl->AddCircleFilled(ImVec2(window_pos.x + s.x, window_pos.y + s.y), s.radius, color);

                // Links to other stars in same cluster
                if (g_qualitySettings.linkAlphaMax > 0) {
                    for (int j = cl.start; j < end; ++j) {
                        if (i == j) continue;
                        Star& s2 = g_stars[j];
                        float dx = s.x - s2.x;
                        float dy = s.y - s2.y;
                        float d2 = dx * dx + dy * dy;
                        float maxD = std::min(width, height) * (g_qualitySettings.linkDistanceFactor / 100.0f);
                        if (d2 < maxD * maxD) {
                            ImU32 linkColor = IM_COL32(212, 175, 55, g_qualitySettings.linkAlphaMax);
                            dl->AddLine(
                                ImVec2(window_pos.x + s.x, window_pos.y + s.y),
                                ImVec2(window_pos.x + s2.x, window_pos.y + s2.y),
                                linkColor, 0.8f);
                        }
                    }
                }
            }
        }
    }
}

// Dynamic performance scaling
void SetDynamicDensity(float density_scale) noexcept {
    g_dynamicDensity = std::clamp(density_scale, 0.0f, 1.0f);
}

void SetDynamicMotionScale(float motion_scale) noexcept {
    g_dynamicMotionScale = std::clamp(motion_scale, 0.0f, 1.0f);
}

void SetDynamicConstellationDensity(float density_scale) noexcept {
    g_dynamicConstellationDensity = std::clamp(density_scale, 0.0f, 1.0f);
}

float GetDynamicDensity() noexcept {
    return g_dynamicDensity;
}

float GetDynamicMotionScale() noexcept {
    return g_dynamicMotionScale;
}

float GetDynamicConstellationDensity() noexcept {
    return g_dynamicConstellationDensity;
}

} // namespace DigitalRain