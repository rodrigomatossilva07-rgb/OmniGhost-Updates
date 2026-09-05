#include "digital_rain.h"
#include "fonts.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace DigitalRain {
namespace {

    constexpr int kMaxColumns = 96;
    constexpr int kMaxTrail = 28;
    constexpr int kMinTrail = 10;
    constexpr int kMaxStars = 48;
    constexpr char kCharset[] = "0123456789ABCDEF";
    constexpr std::uint32_t kInitialSeed = 0xA5A5F00Du;
    constexpr float kFadeSeconds = 0.22f;

    // Dynamic scaling factors (set by PerformanceManager)
    float g_dynamicDensity = 1.0f;
    float g_dynamicMotionScale = 1.0f;
    float g_dynamicConstellationDensity = 1.0f;

    struct Column {
        float x = 0.0f;
        float y = 0.0f;
        float speed = 0.0f;
        int length = kMinTrail;
        float font_size = 11.0f;
        std::uint8_t base_alpha = 55;
        char glyphs[kMaxTrail]{};
    };

    struct Star {
        float x = 0.f, y = 0.f;
        float vx = 0.f, vy = 0.f;
        float phase = 0.f;
        float radius = 1.2f;
        int cluster = 0; // which constellation group
    };

    // Multiple independent constellation clusters across the screen
    constexpr int kMaxClusters = 7;
    struct Cluster {
        float cx = 0.f, cy = 0.f;
        float tx = 0.f, ty = 0.f;
        float scale = 1.f;   // size of this constellation
        int start = 0;       // index into g_stars
        int count = 0;
        int shape = 0;       // 0 ring, 1 triangle, 2 diamond, 3 arc, 4 scatter
    };
    Cluster g_clusters[kMaxClusters]{};
    int g_cluster_count = 0;

    Column g_cols[kMaxColumns]{};
    Star g_stars[kMaxStars]{};
    int g_count = 0;
    int g_star_count = 0;
    bool g_ready = false;
    float g_last_width = 0.0f;
    float g_last_height = 0.0f;
    float g_fade = 0.0f;
    float g_last_density = -1.0f;
    bool g_last_performance = false;
    std::uint32_t g_rng = kInitialSeed;

    std::uint32_t NextRand()
    {
        g_rng ^= g_rng << 13;
        g_rng ^= g_rng >> 17;
        g_rng ^= g_rng << 5;
        if (g_rng == 0)
            g_rng = kInitialSeed;
        return g_rng;
    }

    float Rand01()
    {
        return static_cast<float>(NextRand() & 0xFFFFu) / 65535.0f;
    }

    bool IsValidSize(float width, float height)
    {
        return std::isfinite(width) && std::isfinite(height)
            && width >= 32.0f && height >= 32.0f;
    }

    int CalculateTargetColumnCount(float width, bool performance_mode)
    {
        if (!std::isfinite(width) || width <= 0.0f)
            return 0;
        const float density_scale = g_dynamicDensity;
        if (density_scale <= 0.001f)
            return 0;
        const float spacing = performance_mode ? 22.0f : 12.0f;
        const int full = static_cast<int>(std::ceil(width / spacing)) + 2;
        const int maximum = performance_mode ? std::min(48, kMaxColumns) : kMaxColumns;
        const int scaled = static_cast<int>(std::lround(full * density_scale));
        return std::clamp(scaled, 1, maximum);
    }

    void GenerateGlyphs(Column& column)
    {
        for (int i = 0; i < kMaxTrail; ++i) {
            if ((NextRand() % 10u) == 0u)
                column.glyphs[i] = kCharset[10 + (NextRand() % 6u)];
            else
                column.glyphs[i] = kCharset[NextRand() % 10u];
        }
    }

    void SeedColumn(Column& column, float width, float height, bool random_y)
    {
        column = Column{};
        if (!IsValidSize(width, height))
            return;

        const float usable = std::max(0.0f, width - 16.0f);
        column.x = 8.0f + Rand01() * usable;
        column.y = random_y ? Rand01() * height : -Rand01() * height * 0.45f;
        column.speed = 28.0f + Rand01() * 70.0f;
        column.length = std::clamp(
            kMinTrail + static_cast<int>(NextRand() % static_cast<std::uint32_t>(kMaxTrail - kMinTrail + 1)),
            kMinTrail, kMaxTrail);
        column.font_size = 10.0f + Rand01() * 5.0f; // 10–15 px
        // Stronger ambient rain (visible like reference ~20–35%)
        column.base_alpha = static_cast<std::uint8_t>(
            std::clamp(48 + static_cast<int>(Rand01() * 42.0f), 1, 255));
        GenerateGlyphs(column);
    }

    void SeedStars(float width, float height, bool performance_mode)
    {
        const float density_scale = g_dynamicConstellationDensity;
        const int fullClusters = performance_mode ? 4 : kMaxClusters;
        g_cluster_count = std::clamp(static_cast<int>(std::lround(fullClusters * density_scale)), 0, fullClusters);
        g_star_count = 0;

        // Place several constellations of different shapes/sizes across the screen
        for (int c = 0; c < g_cluster_count; ++c) {
            Cluster& cl = g_clusters[c];
            // Avoid edges and leave space between groups
            cl.cx = width * (0.12f + Rand01() * 0.76f);
            cl.cy = height * (0.14f + Rand01() * 0.72f);
            // Slight separation: nudge if too close to previous
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
            cl.scale = 0.55f + Rand01() * 1.15f; // varied sizes
            cl.shape = static_cast<int>(NextRand() % 5u);
            cl.start = g_star_count;
            // 4–9 stars per constellation
            const int n = performance_mode
                ? (3 + static_cast<int>(NextRand() % 3u))
                : (4 + static_cast<int>(NextRand() % 6u));
            cl.count = n;
            if (g_star_count + n > kMaxStars)
                cl.count = kMaxStars - g_star_count;
            if (cl.count <= 0) {
                g_cluster_count = c;
                break;
            }

            const float base_r = (std::min)(width, height) * 0.045f * cl.scale;
            for (int i = 0; i < cl.count; ++i) {
                Star& s = g_stars[g_star_count++];
                s.cluster = c;
                s.phase = Rand01() * 6.28318f;
                s.radius = 0.7f + Rand01() * 1.3f * (0.7f + 0.3f * cl.scale);
                s.vx = (Rand01() - 0.5f) * 6.f;
                s.vy = (Rand01() - 0.5f) * 6.f;

                float ang = 0.f, rad = base_r;
                switch (cl.shape) {
                case 1: // triangle vertices + midpoints
                    ang = (i / (float)cl.count) * 6.28318f + 0.3f;
                    rad = base_r * (0.85f + 0.25f * Rand01());
                    break;
                case 2: // diamond / cross
                    ang = (i % 4) * 1.5708f + (i >= 4 ? 0.4f : 0.f);
                    rad = base_r * (i < 4 ? 1.f : 0.45f);
                    break;
                case 3: // arc / chain
                    ang = -0.9f + (i / (float)(std::max)(1, cl.count - 1)) * 1.8f;
                    rad = base_r * (0.7f + 0.5f * std::fabs(std::sin(ang * 1.2f)));
                    s.x = cl.cx + ang * base_r * 1.4f;
                    s.y = cl.cy + std::sin(ang) * base_r * 0.6f;
                    continue;
                case 4: // irregular scatter
                    ang = Rand01() * 6.28318f;
                    rad = base_r * (0.3f + Rand01() * 1.1f);
                    break;
                default: // ring
                    ang = (i / (float)cl.count) * 6.28318f + Rand01() * 0.2f;
                    rad = base_r * (0.75f + Rand01() * 0.35f);
                    break;
                }
                s.x = cl.cx + std::cos(ang) * rad;
                s.y = cl.cy + std::sin(ang) * rad;
            }
        }
        for (int i = g_star_count; i < kMaxStars; ++i)
            g_stars[i] = Star{};
    }

    void EnsureColumns(float width, float height, bool performance_mode)
    {
        if (!IsValidSize(width, height)) {
            g_count = 0;
            g_star_count = 0;
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

        g_count = std::clamp(target, 0, kMaxColumns);
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

    void DrawConstellation(ImDrawList* dl, const ImVec2& window_pos,
        float width, float height, float fade, float opacity, bool performance_mode,
        float motion_scale, float constellation_density)
    {
        if (!dl || g_star_count <= 0 || fade <= 0.01f || opacity <= 0.01f)
            return;

        // Apply dynamic constellation density
        const float density_scale = constellation_density * g_dynamicConstellationDensity;
        if (density_scale <= 0.001f)
            return;

        const float dt = ImGui::GetIO().DeltaTime * std::clamp(motion_scale, 0.0f, 1.0f);
        const float t = static_cast<float>(ImGui::GetTime());
        const float margin = 24.f;

        // Slow independent wander per cluster
        for (int c = 0; c < g_cluster_count; ++c) {
            Cluster& cl = g_clusters[c];
            // Pick new target occasionally
            if ((NextRand() % 400u) == 0u) {
                cl.tx = width * (0.10f + Rand01() * 0.80f);
                cl.ty = height * (0.12f + Rand01() * 0.76f);
            }
            cl.cx += (cl.tx - cl.cx) * std::min(1.f, dt * 0.08f);
            cl.cy += (cl.ty - cl.cy) * std::min(1.f, dt * 0.08f);
        }

        // Move stars: individual drift + soft pull toward own cluster center
        for (int i = 0; i < g_star_count; ++i) {
            Star& s = g_stars[i];
            const int c = std::clamp(s.cluster, 0, g_cluster_count - 1);
            Cluster& cl = g_clusters[c];
            const float max_r = (std::min)(width, height) * 0.055f * cl.scale;

            s.vx += (Rand01() - 0.5f) * 4.f * dt;
            s.vy += (Rand01() - 0.5f) * 4.f * dt;
            // soft cohesion to cluster
            s.vx += (cl.cx - s.x) * 0.35f * dt;
            s.vy += (cl.cy - s.y) * 0.35f * dt;
            // soft radius constraint
            float dx = s.x - cl.cx, dy = s.y - cl.cy;
            float d = std::sqrt(dx * dx + dy * dy) + 1e-3f;
            if (d > max_r) {
                s.vx -= (dx / d) * (d - max_r) * 1.2f * dt;
                s.vy -= (dy / d) * (d - max_r) * 1.2f * dt;
            }
            // damp
            s.vx *= (1.f - 1.2f * dt);
            s.vy *= (1.f - 1.2f * dt);
            s.x += s.vx * dt;
            s.y += s.vy * dt;
            s.x = std::clamp(s.x, margin, width - margin);
            s.y = std::clamp(s.y, margin, height - margin);
        }

        // Links only within the same cluster — subtle gold
        const float link_scale = performance_mode ? 0.85f : 1.f;
        for (int c = 0; c < g_cluster_count; ++c) {
            const Cluster& cl = g_clusters[c];
            const float link_dist = (std::min)(width, height) * 0.07f * cl.scale * link_scale;
            const float link_dist2 = link_dist * link_dist;
            for (int a = 0; a < cl.count; ++a) {
                const int i = cl.start + a;
                if (i >= g_star_count) break;
                for (int b = a + 1; b < cl.count; ++b) {
                    const int j = cl.start + b;
                    if (j >= g_star_count) break;
                    const float dx = g_stars[i].x - g_stars[j].x;
                    const float dy = g_stars[i].y - g_stars[j].y;
                    const float d2 = dx * dx + dy * dy;
                    if (d2 > link_dist2 || d2 < 1.f)
                        continue;
                    const float d = std::sqrt(d2);
                    const float strength = 1.f - (d / link_dist);
                    // Much weaker lines — ambient, not dominant
                    int alpha = static_cast<int>(14.f * strength * fade * opacity);
                    alpha = std::clamp(alpha, 0, 28);
                    if (alpha < 3)
                        continue;
                    dl->AddLine(
                        ImVec2(window_pos.x + g_stars[i].x, window_pos.y + g_stars[i].y),
                        ImVec2(window_pos.x + g_stars[j].x, window_pos.y + g_stars[j].y),
                        IM_COL32(200, 170, 70, alpha), 1.0f);
                }
            }
        }

        // Nodes — small, soft, low alpha
        for (int i = 0; i < g_star_count; ++i) {
            const float pulse = 0.82f + 0.18f * std::sin(t * 1.6f + g_stars[i].phase);
            int a = static_cast<int>(28.f * pulse * fade * opacity);
            a = std::clamp(a, 0, 42);
            if (a < 4)
                continue;
            dl->AddCircleFilled(
                ImVec2(window_pos.x + g_stars[i].x, window_pos.y + g_stars[i].y),
                g_stars[i].radius * 0.85f,
                IM_COL32(210, 185, 90, a), 8);
        }
    }

} // namespace


    void Initialize()
    {
        g_count = 0;
        g_star_count = 0;
        g_cluster_count = 0;
        g_ready = false;
        g_fade = 0.0f;
        g_last_width = g_last_height = 0.0f;
        g_last_density = -1.0f;
        g_last_performance = false;
        g_rng = kInitialSeed;
        for (Column& c : g_cols)
            c = Column{};
        for (Star& s : g_stars)
            s = Star{};
        for (Cluster& cl : g_clusters)
            cl = Cluster{};
    }

    void Shutdown()
    {
        Initialize();
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
        bool edge_only)
    {
        if (!dl)
            return;

        if (!std::isfinite(opacity))
            opacity = 1.0f;
        opacity = std::clamp(opacity, 0.0f, 1.0f);
        // Combine passed density with dynamic scaling
        density_scale = std::clamp(density_scale * g_dynamicDensity, 0.0f, 1.0f);
        motion_scale = std::clamp(motion_scale * g_dynamicMotionScale, 0.0f, 1.0f);
        quiet_left_width = std::max(0.0f, quiet_left_width);
        enabled = enabled && density_scale > 0.001f && opacity > 0.001f;

        float dt = ImGui::GetIO().DeltaTime;
        if (!std::isfinite(dt) || dt < 0.0f)
            dt = 0.0f;
        if (dt > 0.05f)
            dt = 0.05f;

        const float target = enabled ? 1.0f : 0.0f;
        if (motion_scale <= 0.001f) {
            g_fade = target;
        }
        else if (g_fade < target) {
            g_fade += dt / kFadeSeconds;
            if (g_fade > 1.0f) g_fade = 1.0f;
        }
        else if (g_fade > target) {
            g_fade -= dt / kFadeSeconds;
            if (g_fade < 0.0f) g_fade = 0.0f;
        }

        if (g_fade <= 0.001f)
            return;

        if (!IsValidSize(window_size.x, window_size.y))
            return;

        EnsureColumns(window_size.x, window_size.y, performance_mode);
        const int safe_count = std::clamp(g_count, 0, kMaxColumns);
        if (safe_count <= 0)
            return;

        dl->PushClipRect(
            window_pos,
            ImVec2(window_pos.x + window_size.x, window_pos.y + window_size.y),
            true);

        // Constellation network behind the rain
        if (!edge_only) {
            DrawConstellation(dl, window_pos, window_size.x, window_size.y,
                g_fade, opacity, performance_mode, motion_scale, g_dynamicConstellationDensity);
        }

        ImFont* font = CyberFonts::GetMonoFont();
        if (!font)
            font = ImGui::GetFont();

        for (int i = 0; i < safe_count; ++i) {
            Column& column = g_cols[i];
            column.y += column.speed * dt * motion_scale;

            if (edge_only) {
                const float normalized = window_size.x > 1.0f ? column.x / window_size.x : 0.5f;
                if (normalized > 0.20f && normalized < 0.80f)
                    continue;
            }

            float edge_mul = 1.0f;
            const float edge = 48.0f;
            if (column.x < edge)
                edge_mul *= column.x / edge;
            if (column.x > window_size.x - edge)
                edge_mul *= (window_size.x - column.x) / edge;
            if (quiet_left_width > 0.0f && column.x < quiet_left_width)
                edge_mul *= 0.55f;

            const float trail_step = column.font_size + 1.4f;
            const int length = std::clamp(column.length, 1, kMaxTrail);

            for (int g = 0; g < length; ++g) {
                const float gy = column.y - static_cast<float>(g) * trail_step;
                if (gy < -24.0f || gy > window_size.y + 24.0f)
                    continue;

                float alpha_scale = (g == 0) ? 2.05f : (1.0f - static_cast<float>(g) * 0.028f);
                if (alpha_scale < 0.35f)
                    alpha_scale = 0.35f;

                int alpha = static_cast<int>(
                    static_cast<float>(column.base_alpha) * alpha_scale * edge_mul * g_fade * opacity + 0.5f);
                alpha = std::clamp(alpha, 0, 255);
                if (alpha < 1)
                    continue;

                const int r = (g == 0) ? 240 : 212;
                const int grn = (g == 0) ? 205 : 175;
                const int b = (g == 0) ? 90 : 55;
                const ImU32 colour = IM_COL32(r, grn, b, alpha);

                const char text[2] = { column.glyphs[g], '\0' };
                const ImVec2 pos(window_pos.x + column.x, window_pos.y + gy);
                if (font)
                    dl->AddText(font, column.font_size, pos, colour, text);
                else
                    dl->AddText(pos, colour, text);
            }

            const float trail_top = column.y - static_cast<float>(length) * trail_step;
            if (trail_top > window_size.y + 30.0f) {
                SeedColumn(column, window_size.x, window_size.y, false);
                column.x += (Rand01() - 0.5f) * 14.0f;
                column.x = std::clamp(column.x, 4.0f, window_size.x - 8.0f);
            }

            if (motion_scale > 0.001f && !performance_mode && (NextRand() & 0x3F) == 0) {
                const int idx = static_cast<int>(NextRand() % static_cast<std::uint32_t>(length));
                if (idx >= 0 && idx < kMaxTrail)
                    column.glyphs[idx] = kCharset[NextRand() % 16u];
            }
        }

        dl->PopClipRect();
    }

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
