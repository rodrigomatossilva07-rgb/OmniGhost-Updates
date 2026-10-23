#include "cs2_esp.h"

#include "../cs2_game.h"
#include "../config/cs2_config.h"
#include "../weapons/cs2_weapon_icons.h"
#include "gameplay/dma_telemetry_log.h"
#include "third_party/ImGui/imgui.h"
#include "../../src/config/app_settings.h"
#include "../../src/window/window.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>
#include <cstdio>
#include <charconv>
#include <cfloat>
#include <functional>
#include <unordered_map>
#include <utility>

namespace {


inline int FormatDistance(char* buffer, size_t size, float distance) {
    auto [ptr, ec] = std::to_chars(buffer, buffer + size - 1, distance, std::chars_format::fixed, 0);
    if (ec == std::errc{}) {

        if (ptr + 2 < buffer + size) {
            *ptr++ = ' ';
            *ptr++ = 'm';
            *ptr = '\0';
            return static_cast<int>(ptr - buffer);
        }
    }

    return std::snprintf(buffer, size, "%.0f m", distance);
}

inline int FormatAmmo(char* buffer, size_t size, int clip, int reserve) {
    auto [ptr, ec] = std::to_chars(buffer, buffer + size - 1, clip);
    if (ec == std::errc{} && ptr + 1 < buffer + size) {
        *ptr++ = '/';
        auto [ptr2, ec2] = std::to_chars(ptr, buffer + size - 1, reserve);
        if (ec2 == std::errc{}) {
            *ptr2 = '\0';
            return static_cast<int>(ptr2 - buffer);
        }
    }
    return std::snprintf(buffer, size, "%d/%d", clip, reserve);
}

inline int FormatInt(char* buffer, size_t size, int value) {
    auto [ptr, ec] = std::to_chars(buffer, buffer + size, value);
    if (ec == std::errc{}) {
        *ptr = '\0';
        return static_cast<int>(ptr - buffer);
    }
    return std::snprintf(buffer, size, "%d", value);
}

bool WorldToScreen(const float world[3], const float matrix[16], ImVec2& screen) {
    const float clipX = world[0] * matrix[0] + world[1] * matrix[1] + world[2] * matrix[2] + matrix[3];
    const float clipY = world[0] * matrix[4] + world[1] * matrix[5] + world[2] * matrix[6] + matrix[7];
    const float clipW = world[0] * matrix[12] + world[1] * matrix[13] + world[2] * matrix[14] + matrix[15];
    if (!std::isfinite(clipW) || clipW < 0.01f) return false;



    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    screen.x = (displaySize.x * 0.5f) * (1.f + clipX / clipW);
    screen.y = (displaySize.y * 0.5f) * (1.f - clipY / clipW);
    return std::isfinite(screen.x) && std::isfinite(screen.y);
}

bool WorldSegmentToScreenClipped(const float first[3], const float second[3],
                                 const float matrix[16], ImVec2& firstScreen,
                                 ImVec2& secondScreen) {
    struct ClipPoint { float x, y, w; };
    const auto project = [matrix](const float world[3]) {
        return ClipPoint{
            world[0] * matrix[0] + world[1] * matrix[1] + world[2] * matrix[2] + matrix[3],
            world[0] * matrix[4] + world[1] * matrix[5] + world[2] * matrix[6] + matrix[7],
            world[0] * matrix[12] + world[1] * matrix[13] + world[2] * matrix[14] + matrix[15]
        };
    };
    ClipPoint a = project(first);
    ClipPoint b = project(second);
    if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(a.w) ||
        !std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.w))
        return false;
    constexpr float kNearW = .01f;
    if (a.w < kNearW && b.w < kNearW) return false;
    const auto clipNear = [](ClipPoint& behind, const ClipPoint& visible) {
        constexpr float nearW = .01f;
        const float denominator = visible.w - behind.w;
        if (std::abs(denominator) < 1e-6f) return false;
        const float t = std::clamp((nearW - behind.w) / denominator, 0.f, 1.f);
        behind.x += (visible.x - behind.x) * t;
        behind.y += (visible.y - behind.y) * t;
        behind.w = nearW;
        return true;
    };
    if (a.w < kNearW && !clipNear(a, b)) return false;
    if (b.w < kNearW && !clipNear(b, a)) return false;
    const ImVec2 viewport = ImGui::GetIO().DisplaySize;
    firstScreen = ImVec2((viewport.x * .5f) * (1.f + a.x / a.w),
                         (viewport.y * .5f) * (1.f - a.y / a.w));
    secondScreen = ImVec2((viewport.x * .5f) * (1.f + b.x / b.w),
                          (viewport.y * .5f) * (1.f - b.y / b.w));
    return std::isfinite(firstScreen.x) && std::isfinite(firstScreen.y) &&
           std::isfinite(secondScreen.x) && std::isfinite(secondScreen.y);
}

ImU32 Color(const float color[4]) {
    return IM_COL32(static_cast<int>(color[0] * 255.f), static_cast<int>(color[1] * 255.f),
                    static_cast<int>(color[2] * 255.f), static_cast<int>(color[3] * 255.f));
}

ImU32 RgbColor() {
    const float t = static_cast<float>(ImGui::GetTime()) * .35f;
    const auto channel = [t](float phase) {
        return static_cast<int>((std::sin(t + phase) * .5f + .5f) * 255.f);
    };
    return IM_COL32(channel(0.f), channel(2.094f), channel(4.188f), 255);
}

void DrawCornerBox(ImDrawList* draw, const ImVec2& min, const ImVec2& max, ImU32 color, float thickness) {
    const float w = max.x - min.x, h = max.y - min.y;
    const float lx = w * .25f, ly = h * .20f;
    draw->AddLine(min, ImVec2(min.x + lx, min.y), color, thickness);
    draw->AddLine(min, ImVec2(min.x, min.y + ly), color, thickness);
    draw->AddLine(ImVec2(max.x - lx, min.y), ImVec2(max.x, min.y), color, thickness);
    draw->AddLine(ImVec2(max.x, min.y), ImVec2(max.x, min.y + ly), color, thickness);
    draw->AddLine(ImVec2(min.x, max.y - ly), ImVec2(min.x, max.y), color, thickness);
    draw->AddLine(ImVec2(min.x, max.y), ImVec2(min.x + lx, max.y), color, thickness);
    draw->AddLine(ImVec2(max.x - lx, max.y), max, color, thickness);
    draw->AddLine(ImVec2(max.x, max.y - ly), max, color, thickness);
}

void DrawVerticalBar(ImDrawList* draw, float x, float top, float bottom, float fraction, ImU32 color) {
    constexpr float kWidth = 4.f;
    fraction = std::clamp(fraction, 0.f, 1.f);
    draw->AddRectFilled(ImVec2(x - 1.f, top - 1.f), ImVec2(x + kWidth + 1.f, bottom + 1.f), IM_COL32(0, 0, 0, 180));
    draw->AddRectFilled(ImVec2(x, top), ImVec2(x + kWidth, bottom), IM_COL32(18, 18, 18, 235));
    const float filledTop = bottom - (bottom - top) * fraction;
    draw->AddRectFilled(ImVec2(x, filledTop), ImVec2(x + kWidth, bottom), color);
}

void DrawHorizontalBar(ImDrawList* draw, float left, float right, float y, float fraction, ImU32 color) {
    constexpr float kHeight = 4.f;
    fraction = std::clamp(fraction, 0.f, 1.f);
    draw->AddRectFilled(ImVec2(left - 1.f, y - 1.f), ImVec2(right + 1.f, y + kHeight + 1.f), IM_COL32(0, 0, 0, 180));
    draw->AddRectFilled(ImVec2(left, y), ImVec2(right, y + kHeight), IM_COL32(18, 18, 18, 235));
    draw->AddRectFilled(ImVec2(left, y), ImVec2(left + (right - left) * fraction, y + kHeight), color);
}

bool DrawOffscreenArrow(ImDrawList* draw, const float world[3], const float matrix[16],
                        const CS2::Player& player, const CS2::Config& settings) {
    if (!settings.offscreen_arrows || player.distance > settings.offscreen_arrow_max_distance)
        return false;
    const ImVec2 viewport = ImGui::GetIO().DisplaySize;
    if (viewport.x < 10.f || viewport.y < 10.f) return false;
    const float clipX = world[0] * matrix[0] + world[1] * matrix[1] + world[2] * matrix[2] + matrix[3];
    const float clipY = world[0] * matrix[4] + world[1] * matrix[5] + world[2] * matrix[6] + matrix[7];
    const float clipW = world[0] * matrix[12] + world[1] * matrix[13] + world[2] * matrix[14] + matrix[15];
    if (!std::isfinite(clipX) || !std::isfinite(clipY) || !std::isfinite(clipW)) return false;
    const float safeW = std::abs(clipW) > .001f ? clipW : (clipW < 0.f ? -.001f : .001f);
    float nx = clipX / safeW;
    float ny = -clipY / safeW;
    if (clipW < 0.f) { nx = -nx; ny = -ny; }
    const bool onScreen = clipW > .001f && std::abs(nx) <= 1.f && std::abs(ny) <= 1.f;
    float length = std::sqrt(nx * nx + ny * ny);
    if (!std::isfinite(length)) return false;
    if (length < .001f) {
        nx = 0.f;
        ny = -1.f;
        length = 1.f;
    }
    nx /= length; ny /= length;
    const float size = std::clamp(settings.offscreen_arrow_size, 6.f, 32.f);
    float fovRadius = settings.aim_fov;
    if (settings.aim_dynamic_fov) {
        fovRadius = settings.aim_fov_curve_value[0];
        for (std::size_t i = 1; i < std::size(settings.aim_fov_curve_distance); ++i) {
            const float left = settings.aim_fov_curve_distance[i - 1];
            const float right = (std::max)(settings.aim_fov_curve_distance[i], left + .01f);
            if (player.distance <= right) {
                const float t = std::clamp((player.distance - left) / (right - left), 0.f, 1.f);
                fovRadius = std::lerp(settings.aim_fov_curve_value[i - 1],
                                      settings.aim_fov_curve_value[i], t);
                break;
            }
            fovRadius = settings.aim_fov_curve_value[i];
        }
    }
    const float ringRadius = std::clamp(
        fovRadius + settings.offscreen_arrow_margin,
        size * 2.f,
        (std::min)(viewport.x, viewport.y) * .5f - size * 2.f);
    if (onScreen) {
        const float projectedX = clipX / safeW * viewport.x * .5f;
        const float projectedY = clipY / safeW * viewport.y * .5f;
        if (std::sqrt(projectedX * projectedX + projectedY * projectedY) <= fovRadius)
            return false;
    }
    const ImVec2 tip(viewport.x * .5f + nx * ringRadius,
                     viewport.y * .5f + ny * ringRadius);
    const ImVec2 side(-ny, nx);
    const ImVec2 base(tip.x - nx * size, tip.y - ny * size);
    const float* sourceColor = settings.col_offscreen_arrow;
    if (settings.visibility_colors && settings.visible_check && player.visibility_known)
        sourceColor = player.visible ? settings.col_visible : settings.col_occluded;
    float colorValues[4] = { sourceColor[0], sourceColor[1], sourceColor[2],
        std::clamp(sourceColor[3] * settings.offscreen_arrow_opacity, 0.f, 1.f) };
    draw->AddTriangleFilled(tip,
        ImVec2(base.x + side.x * size * .55f, base.y + side.y * size * .55f),
        ImVec2(base.x - side.x * size * .55f, base.y - side.y * size * .55f), Color(colorValues));
    return !onScreen;
}

void DrawSkeleton(ImDrawList* draw, const CS2::Player& player, const float viewMatrix[16],
                  const float positionOffset[3],
                  ImU32 color, ImU32 jointColor, float thickness, bool joints, float bodyHeightPx) {
    if (!player.full_bones_ok) return;

    constexpr std::pair<CS2::BoneSlot, CS2::BoneSlot> kLinks[] = {
        {CS2::BoneSlot::Head, CS2::BoneSlot::Neck}, {CS2::BoneSlot::Neck, CS2::BoneSlot::SpineUpper},
        {CS2::BoneSlot::SpineUpper, CS2::BoneSlot::SpineMiddle},
        {CS2::BoneSlot::SpineMiddle, CS2::BoneSlot::SpineLower},
        {CS2::BoneSlot::SpineLower, CS2::BoneSlot::Pelvis},
        {CS2::BoneSlot::Neck, CS2::BoneSlot::ClavicleLeft},
        {CS2::BoneSlot::ClavicleLeft, CS2::BoneSlot::ShoulderLeft},
        {CS2::BoneSlot::ShoulderLeft, CS2::BoneSlot::ElbowLeft}, {CS2::BoneSlot::ElbowLeft, CS2::BoneSlot::HandLeft},
        {CS2::BoneSlot::Neck, CS2::BoneSlot::ClavicleRight},
        {CS2::BoneSlot::ClavicleRight, CS2::BoneSlot::ShoulderRight},
        {CS2::BoneSlot::ShoulderRight, CS2::BoneSlot::ElbowRight}, {CS2::BoneSlot::ElbowRight, CS2::BoneSlot::HandRight},
        {CS2::BoneSlot::Pelvis, CS2::BoneSlot::HipLeft},
        {CS2::BoneSlot::HipLeft, CS2::BoneSlot::KneeLeft}, {CS2::BoneSlot::KneeLeft, CS2::BoneSlot::AnkleLeft},
        {CS2::BoneSlot::Pelvis, CS2::BoneSlot::HipRight},
        {CS2::BoneSlot::HipRight, CS2::BoneSlot::KneeRight}, {CS2::BoneSlot::KneeRight, CS2::BoneSlot::AnkleRight},
    };
    std::array<bool, CS2::kBoneSlotCount> used{};
    for (const auto& [from, to] : kLinks) {
        used[static_cast<std::size_t>(from)] = true;
        used[static_cast<std::size_t>(to)] = true;
    }
    std::array<ImVec2, CS2::kBoneSlotCount> points{};
    std::array<bool, CS2::kBoneSlotCount> valid{};
    std::array<bool, CS2::kBoneSlotCount> drawn{};
    for (std::size_t index = 0; index < CS2::kBoneSlotCount; ++index) {
        if (!used[index]) continue;
        const float* bone = player.bones[index];
        if (!std::isfinite(bone[0]) || !std::isfinite(bone[1]) || !std::isfinite(bone[2]) ||
            (bone[0] == 0.f && bone[1] == 0.f && bone[2] == 0.f))
            continue;
        const float predictedBone[3] = {
            player.bones[index][0] + positionOffset[0],
            player.bones[index][1] + positionOffset[1],
            player.bones[index][2] + positionOffset[2]
        };
        valid[index] = WorldToScreen(predictedBone, viewMatrix, points[index]);
    }
    for (const auto& [from, to] : kLinks) {
        const auto a = static_cast<std::size_t>(from), b = static_cast<std::size_t>(to);
        if (!valid[a] || !valid[b]) continue;
        const float dx = player.bones[a][0] - player.bones[b][0];
        const float dy = player.bones[a][1] - player.bones[b][1];
        const float dz = player.bones[a][2] - player.bones[b][2];
        const float maxWorldLength = from == CS2::BoneSlot::Head ? 55.f : 100.f;
        if (dx * dx + dy * dy + dz * dz > maxWorldLength * maxWorldLength)
            continue;
        const float sx = points[a].x - points[b].x;
        const float sy = points[a].y - points[b].y;
        const float maxScreenLength = (std::max)(50.f, bodyHeightPx * 0.9f);
        if (sx * sx + sy * sy > maxScreenLength * maxScreenLength)
            continue;
        drawn[a] = drawn[b] = true;
        draw->AddLine(points[a], points[b], IM_COL32(0, 0, 0, 195), thickness + 1.4f);
        draw->AddLine(points[a], points[b], color, thickness);
    }
    if (joints) {
        for (std::size_t index = 0; index < CS2::kBoneSlotCount; ++index) {
            if (!used[index]) continue;
            if (!drawn[index]) continue;
            draw->AddCircleFilled(points[index], 2.3f, IM_COL32(0, 0, 0, 195), 8);
            draw->AddCircleFilled(points[index], 1.4f, jointColor, 8);
        }
    }
}

ImU32 EffectColor(const CS2::Config& settings, const float color[4], ImU32 rgb) {
    return settings.rgb_mode ? rgb : Color(color);
}

void DrawOutlinedText(ImDrawList* draw, ImVec2 pos, ImU32 color, const char* text, const CS2::Config& settings) {
    if (!text || !*text) return;
    if (settings.text_outline) {
        const ImU32 outline = Color(settings.col_text_outline);
        const float t = std::clamp(settings.text_outline_thickness, .5f, 2.f);


        const float fontSize = ImGui::GetFontSize() * std::clamp(settings.layout_text_scale, .65f, 1.6f);
        draw->AddText(nullptr, fontSize, ImVec2(pos.x - t, pos.y - t), outline, text);
        draw->AddText(nullptr, fontSize, ImVec2(pos.x + t, pos.y + t), outline, text);
        if (t >= 1.25f) {
            draw->AddText(nullptr, fontSize, ImVec2(pos.x + t, pos.y - t), outline, text);
            draw->AddText(nullptr, fontSize, ImVec2(pos.x - t, pos.y + t), outline, text);
        }
    }
    draw->AddText(nullptr, ImGui::GetFontSize() * std::clamp(settings.layout_text_scale, .65f, 1.6f), pos, color, text);
}

void DrawExtras(ImDrawList* draw, const CS2::Player& player, const ImVec2& head, const ImVec2& feet,
                const ImVec2& min, const ImVec2& max, const float* viewMatrix,
                const float predictedPosition[3], const CS2::Config& settings, ImU32 rgb,
                uint64_t sessionGeneration) {
    const float height = max.y - min.y;
    const float scale = std::clamp(height / 180.f, .45f, 2.2f);
    const auto visibilityColor = [&](const float configured[4]) {
        if (settings.rgb_mode) return rgb;
        if (settings.visibility_colors && settings.visible_check && player.visibility_known)
            return Color(player.visible ? settings.col_visible : settings.col_occluded);
        return Color(configured);
    };

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    if (settings.snaplines) {
        const float y = settings.snapline_position == 0 ? 0.f : settings.snapline_position == 1 ? displaySize.y * .5f : displaySize.y;
        draw->AddLine(ImVec2(displaySize.x * .5f, y), feet,
                      EffectColor(settings, settings.col_snaplines, rgb), std::clamp(settings.snapline_thickness, .5f, 5.f));
    }
    if (settings.head_dot)
        draw->AddCircle(head, std::clamp(height * .028f, 2.5f, 11.f), visibilityColor(settings.col_head), 20,
                        std::clamp(settings.head_circle_thickness, .5f, 5.f));
    if (settings.head_halo) {
        ImVec2 halo[17]{};
        for (int index = 0; index <= 16; ++index) {
            const float angle = index * 6.28318530718f / 16.f;
            halo[index] = ImVec2(head.x + std::cos(angle) * 17.f * scale,
                head.y - 13.f * scale + std::sin(angle) * 5.f * scale);
        }
        draw->AddPolyline(halo, 17, EffectColor(settings, settings.col_halo, rgb), true, 2.f);
    }
    if (settings.chinese_hat) {
        const float s = std::clamp(settings.chinese_hat_scale, .4f, 2.5f) * scale;
        const ImVec2 a(head.x, head.y - 38.f * s), b(head.x - 28.f * s, head.y - 9.f * s), c(head.x + 28.f * s, head.y - 9.f * s);
        const float phase = static_cast<float>(ImGui::GetTime()) * 1.4f;
        constexpr int kSpokes = 12;
        for (int i = 0; i <= kSpokes; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kSpokes);
            const float x = b.x + (c.x - b.x) * t;
            const ImU32 rainbow = ImColor::HSV(std::fmod(t + phase * .08f, 1.f), .78f, 1.f, .95f);
            draw->AddLine(a, ImVec2(x, b.y), rainbow, 1.8f);
            if (i < kSpokes) {
                const float nextT = static_cast<float>(i + 1) / static_cast<float>(kSpokes);
                const ImU32 rim = ImColor::HSV(std::fmod(nextT + phase * .08f, 1.f), .78f, 1.f, .95f);
                draw->AddLine(ImVec2(x, b.y), ImVec2(b.x + (c.x - b.x) * nextT, b.y), rim, 2.f);
            }
        }
    }
    if (settings.look_direction) {
        const float angle = player.view_yaw * 0.0174532925f;
        const float length = std::clamp(settings.look_direction_length, 20.f, 180.f);
        const float endWorld[3] = { predictedPosition[0] + std::cos(angle) * length,
                                    predictedPosition[1] + std::sin(angle) * length,
                                    predictedPosition[2] + 64.f };
        ImVec2 end{};
        if (WorldToScreen(endWorld, viewMatrix, end))
            draw->AddLine(head, end, EffectColor(settings, settings.col_look, rgb),
                          std::clamp(settings.eye_line_thickness, .5f, 5.f));
    }
    if (settings.trails) {
        struct Point { float world[3]{}; uint64_t sample_ms = 0; double time = 0.; };
        static std::unordered_map<uintptr_t, std::deque<Point>> history;
        static double nextPrune = 0.;
        static uint64_t historyGeneration = 0;
        constexpr size_t kMaxTrailPlayers = 128;
        if (historyGeneration != sessionGeneration) {
            history.clear();
            nextPrune = 0.;
            historyGeneration = sessionGeneration;
        }
        auto& points = history[player.pawn];
        const double now = ImGui::GetTime();


        if (player.position_sample_ms &&
            (points.empty() || points.back().sample_ms != player.position_sample_ms)) {
            Point point{};
            point.world[0] = predictedPosition[0];
            point.world[1] = predictedPosition[1];
            point.world[2] = predictedPosition[2] + 48.f;
            point.sample_ms = player.position_sample_ms;
            point.time = now;
            if (!points.empty()) {
                const float dx = point.world[0] - points.back().world[0];
                const float dy = point.world[1] - points.back().world[1];
                const float dz = point.world[2] - points.back().world[2];
                const float jumpSq = dx * dx + dy * dy + dz * dz;
                if (jumpSq > 225.f * 225.f || point.sample_ms <= points.back().sample_ms ||
                    now - points.back().time > .35)
                    points.clear();
            }
            points.push_back(point);


            constexpr size_t kMaxTrailPoints = 360;
            if (points.size() > kMaxTrailPoints)
                points.pop_front();
        }
        const double duration = std::clamp(static_cast<double>(settings.trail_duration), .2, 2.5);
        while (!points.empty() && now - points.front().time > duration) points.pop_front();
        for (size_t i = 1; i < points.size(); ++i) {
            const float fade = static_cast<float>(1.0 - (now - points[i].time) / duration);
            ImU32 color = EffectColor(settings, settings.col_trail, rgb);
            color = (color & 0x00FFFFFFu) | (static_cast<ImU32>(std::clamp(fade, 0.f, 1.f) * 220.f) << 24);
            ImVec2 from{}, to{};
            if (WorldToScreen(points[i - 1].world, viewMatrix, from) &&
                WorldToScreen(points[i].world, viewMatrix, to))
                draw->AddLine(from, to, color, std::clamp(settings.trail_thickness, 1.f, 7.f));
        }
        if (now >= nextPrune) {
            nextPrune = now + 1.0;
            for (auto it = history.begin(); it != history.end();) {
                if (it->second.empty() || now - it->second.back().time > duration * 2.0)
                    it = history.erase(it);
                else
                    ++it;
            }

            if (history.size() > kMaxTrailPlayers) {

                while (history.size() > kMaxTrailPlayers) {
                    auto oldest = history.begin();
                    for (auto it = std::next(oldest); it != history.end(); ++it) {
                        if (it->second.back().time < oldest->second.back().time)
                            oldest = it;
                    }
                    history.erase(oldest);
                }
            }
        }
    }
}

void DrawPersistentWindow(const char* id, const char* title, float& x, float& y,
                          const ImVec2& fallback, const std::function<void()>& content,
                          float* persistX = nullptr, float* persistY = nullptr) {
    const bool movable = app_settings::menu_open;
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0.f || y < 0.f) {
        x = fallback.x;
        y = fallback.y;
    }
    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(.88f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!movable) flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin(id, nullptr, flags)) {
        ImGui::TextUnformatted(title);
        ImGui::Separator();
        content();
        if (movable) {
            const ImVec2 pos = ImGui::GetWindowPos();
            x = pos.x;
            y = pos.y;




            if (persistX) *persistX = x;
            if (persistY) *persistY = y;
        }
    }
    ImGui::End();
}

void DrawBombTimerWindow(const CS2::Runtime& snapshot, const CS2::Config& settings) {
    if (!settings.bomb_timer) return;

    static float x = -1.f, y = -1.f;
    if (x < 0.f || y < 0.f) { x = settings.bomb_window_x; y = settings.bomb_window_y; }
    const auto& bomb = snapshot.bomb;


    DrawPersistentWindow("##cs2_bomb_timer", "BOMBA", x, y, ImVec2(30.f, 338.f), [&] {
            if (!bomb.planted) {
                ImGui::TextDisabled("Sem bomba plantada");
                return;
            }
            const uint64_t now = GetTickCount64();
            const float age = bomb.sample_timestamp_ms && now >= bomb.sample_timestamp_ms
                ? std::min(static_cast<float>(now - bomb.sample_timestamp_ms) / 1000.f, .50f) : 0.f;
            const float left = std::max(0.f, bomb.blow_time - age);
            ImGui::Text("Explosao: %.1fs", left);
            if (bomb.defusing)
                ImGui::TextColored(ImVec4(.32f, .82f, 1.f, 1.f), "Defuse: %.1fs", std::max(0.f, bomb.defuse_time - age));
            else
                ImGui::TextDisabled("Sem defuse ativo");
            ImGui::ProgressBar(std::clamp(left / 40.f, 0.f, 1.f), ImVec2(170.f, 7.f));
        }, &CS2::config.bomb_window_x, &CS2::config.bomb_window_y);
}

void DrawActiveFeaturesPanel(const CS2::Config& settings) {
    if (!settings.active_features_panel) return;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float scale = std::clamp(settings.active_features_scale, .65f, 1.6f);
    ImGui::SetNextWindowPos(ImVec2(settings.active_features_x * display.x,
                                   settings.active_features_y * display.y), ImGuiCond_Once);
    ImGui::SetNextWindowBgAlpha(std::clamp(settings.active_features_opacity, .15f, 1.f));
    ImGui::SetNextWindowSize(ImVec2(175.f, 0.f));
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar;
    if (!app_settings::menu_open) flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##cs2_active_features", nullptr, flags)) {
        ImGui::SetWindowFontScale(scale);
        ImGui::TextColored(ImVec4(.95f, .72f, .18f, 1.f), "CS2 DMA");
        ImGui::Separator();
        const auto row = [&](const char* label, bool active) {
            if (!active) return;
            ImGui::TextColored(ImVec4(.25f, .90f, .52f, 1.f), "%s ON", label);
        };
        row("Aimbot", settings.aim_enabled);
        row("Triggerbot", settings.trigger_enabled);
        row("Player ESP", settings.esp_enabled);
        row("Offscreen", settings.offscreen_arrows);
        row("Dynamic FOV", settings.aim_dynamic_fov);
        row("C4 Equipped", settings.c4_carrier_esp);
        if (app_settings::menu_open && display.x > 1.f && display.y > 1.f) {
            const ImVec2 pos = ImGui::GetWindowPos();
            CS2::config.active_features_x = std::clamp(pos.x / display.x, 0.f, .95f);
            CS2::config.active_features_y = std::clamp(pos.y / display.y, 0.f, .95f);
        }
        ImGui::SetWindowFontScale(1.f);
    }
    ImGui::End();
}

void DrawSpectatorWindow(const CS2::Runtime& snapshot, const CS2::Config& settings) {
    if (!settings.spectator_list) return;
    static float x = -1.f, y = -1.f;
    if (x < 0.f || y < 0.f) { x = settings.spectator_window_x; y = settings.spectator_window_y; }
    DrawPersistentWindow("##cs2_spectators", "ESPECTADORES", x, y, ImVec2(30.f, 40.f), [&] {
            if (snapshot.spectators.empty()) {
                ImGui::TextDisabled("Ninguem a observar");
                return;
            }
            for (const auto& spectator : snapshot.spectators)
                ImGui::BulletText("%s", spectator.name[0] ? spectator.name : "Jogador");
        }, &CS2::config.spectator_window_x, &CS2::config.spectator_window_y);
}

void DrawRadar2D(const CS2::Runtime& snapshot, const CS2::Config& settings) {
    if (!settings.radar_2d || !snapshot.in_match) return;
    const float size = std::clamp(settings.radar_2d_size, 110.f, 360.f);
    ImGui::SetNextWindowPos(ImVec2(settings.radar_2d_x, settings.radar_2d_y), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(size, size));
    ImGui::SetNextWindowBgAlpha(.86f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize;
    if (!app_settings::menu_open) flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##cs2_radar_2d", nullptr, flags)) {
        const ImVec2 pos = ImGui::GetWindowPos();
        const ImVec2 extent = ImGui::GetWindowSize();
        const ImVec2 center(pos.x + extent.x * .5f, pos.y + extent.y * .5f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRect(pos, ImVec2(pos.x + extent.x, pos.y + extent.y), IM_COL32(218, 177, 47, 145), 7.f);
        dl->AddLine(ImVec2(pos.x + 8.f, center.y), ImVec2(pos.x + extent.x - 8.f, center.y), IM_COL32(218,177,47,45));
        dl->AddLine(ImVec2(center.x, pos.y + 8.f), ImVec2(center.x, pos.y + extent.y - 8.f), IM_COL32(218,177,47,45));
        const float yaw = -snapshot.local_view_yaw * .0174532925f;
        const float cs = std::cos(yaw), sn = std::sin(yaw);
        constexpr float rangeUnits = 2500.f;
        for (const CS2::Player& player : snapshot.players) {
            if (!player.alive || player.is_local || (settings.team_check && player.team == snapshot.local_team)) continue;
            const float dx = player.pos[0] - snapshot.local_pos[0];
            const float dy = player.pos[1] - snapshot.local_pos[1];
            const float rx = dx * cs - dy * sn;
            const float ry = dx * sn + dy * cs;
            const float scale = (extent.x * .43f) / rangeUnits;
            const ImVec2 dot(center.x + ry * scale, center.y - rx * scale);
            if (dot.x < pos.x + 6.f || dot.x > pos.x + extent.x - 6.f ||
                dot.y < pos.y + 6.f || dot.y > pos.y + extent.y - 6.f) continue;
            dl->AddCircleFilled(dot, player.is_bot ? 3.f : 4.f,
                player.team == snapshot.local_team ? IM_COL32(70,155,255,255) : IM_COL32(255,78,70,255), 12);
        }
        dl->AddTriangleFilled(ImVec2(center.x, center.y - 7.f), ImVec2(center.x - 5.f, center.y + 5.f),
                              ImVec2(center.x + 5.f, center.y + 5.f), IM_COL32(235,190,50,255));
        if (app_settings::menu_open) {
            CS2::config.radar_2d_x = pos.x;
            CS2::config.radar_2d_y = pos.y;
        }
    }
    ImGui::End();
}

void DrawDroppedWeapons(ImDrawList* draw, const CS2::Runtime& snapshot,
                        const CS2::Config& settings, const float* viewMatrix) {
    if (!settings.dropped_weapons || !viewMatrix) return;


    constexpr float kMaxDistanceUnits = 50.f * 39.37f;
    for (const auto& weapon : snapshot.dropped_weapons) {
        const float dx = weapon.pos[0] - snapshot.local_pos[0];
        const float dy = weapon.pos[1] - snapshot.local_pos[1];
        const float dz = weapon.pos[2] - snapshot.local_pos[2];
        if (dx * dx + dy * dy + dz * dz > kMaxDistanceUnits * kMaxDistanceUnits) continue;
        ImVec2 screen{};

        if (!WorldToScreen(weapon.pos, viewMatrix, screen)) continue;
        const ImU32 color = Color(settings.col_weapon);
        char label[96]{};
        if (settings.dropped_weapon_ammo && weapon.ammo_clip >= 0)
            std::snprintf(label, sizeof(label), "%s  %d", weapon.name, weapon.ammo_clip);
        else
            std::snprintf(label, sizeof(label), "%s", weapon.name);
        const ImVec2 text = ImGui::CalcTextSize(label);
        const float iconOffset = settings.dropped_weapon_icons ? 10.f : 0.f;
        if (settings.dropped_weapon_icons) {



            ID3D11Device* device = g_overlay_instance ? g_overlay_instance->device : nullptr;
            CS2_WeaponIcons::EnsureLoaded(device);
            ID3D11ShaderResourceView* icon =
                CS2_WeaponIcons::Get(weapon.item_definition);
            int iconW = 0, iconH = 0;
            if (icon)
                CS2_WeaponIcons::GetSize(weapon.item_definition, iconW, iconH);
            if (icon && iconW > 0 && iconH > 0) {
                const float targetH = 26.f;
                const float scale = targetH / static_cast<float>(iconH);
                const ImVec2 iconMax(screen.x - text.x * .5f - 8.f, screen.y + 3.f);
                const ImVec2 iconMin(iconMax.x - static_cast<float>(iconW) * scale,
                                    iconMax.y - targetH);
                draw->AddImage(reinterpret_cast<ImTextureID>(icon), iconMin, iconMax);
            } else {
                draw->AddRectFilled(ImVec2(screen.x - text.x * .5f - 11.f, screen.y - 1.f),
                    ImVec2(screen.x - text.x * .5f - 3.f, screen.y + 5.f), color, 1.f);
            }
        }
        DrawOutlinedText(draw, ImVec2(screen.x - text.x * .5f + iconOffset * .15f, screen.y - 12.f), color, label, settings);
    }
}

}

namespace CS2::ESP {

void DrawPlayers(const Runtime& snapshot, const Config& settings) {

    DrawActiveFeaturesPanel(settings);
    DrawRadar2D(snapshot, settings);

    if (snapshot.in_match) {
        DrawBombTimerWindow(snapshot, settings);
        DrawSpectatorWindow(snapshot, settings);
    }
    if (!settings.esp_enabled || !snapshot.in_match) return;

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const bool hideTeam = settings.team_check;
    const bool useVisibilityColors = settings.visibility_colors;
    const float maxDistance = settings.max_distance;
    const ImU32 rgbColor = settings.rgb_mode ? RgbColor() : 0;
    const uint64_t framePositionSampleMs = snapshot.world_sample_ms
        ? snapshot.world_sample_ms : snapshot.snapshot_timestamp_ms;
    const auto liveness = CS2::AcquireLivenessSnapshot();
    const auto diedSinceSnapshot = [&](const Player& player) {
        if (!liveness || !player.pawn)
            return false;
        for (uint32_t index = 0; index < liveness->count; ++index) {
            const LivenessSample& sample = liveness->players[index];
            if (sample.pawn == player.pawn &&
                sample.generation == snapshot.session_generation &&
                sample.sample_ms >= (player.position_sample_ms ? player.position_sample_ms : framePositionSampleMs))
                return !sample.alive;
        }
        return false;
    };
    const uint64_t nowMs = CS2::SampleClockMs();
    const auto frameCamera = CS2::PredictCameraSnapshot(nowMs);


    static uint64_t measuredGeneration = 0;
    static uint64_t nextDeathPruneMs = 0;
    static std::unordered_map<uintptr_t, uint64_t> lastDrawnMs;
    static std::unordered_map<uintptr_t, uint64_t> measuredDeathMs;
    struct HeldPlayer {
        Player player{};
        uint64_t lastSeenMs = 0;
    };
    static std::unordered_map<uintptr_t, HeldPlayer> heldPlayers;
    constexpr size_t kMaxDrawnMs = 128;
    constexpr size_t kMaxDeathMs = 128;
    if (measuredGeneration != snapshot.session_generation) {
        lastDrawnMs.clear();
        measuredDeathMs.clear();
        heldPlayers.clear();
        measuredGeneration = snapshot.session_generation;
        nextDeathPruneMs = 0;
    }
    if (nowMs >= nextDeathPruneMs) {
        nextDeathPruneMs = nowMs + 2000u;
        for (auto it = lastDrawnMs.begin(); it != lastDrawnMs.end();) {
            if (nowMs > it->second && nowMs - it->second > 10000u) it = lastDrawnMs.erase(it);
            else ++it;
        }
        for (auto it = measuredDeathMs.begin(); it != measuredDeathMs.end();) {
            if (nowMs > it->second && nowMs - it->second > 10000u) it = measuredDeathMs.erase(it);
            else ++it;
        }

        if (lastDrawnMs.size() > kMaxDrawnMs) {
            while (lastDrawnMs.size() > kMaxDrawnMs) {
                auto oldest = lastDrawnMs.begin();
                for (auto it = std::next(oldest); it != lastDrawnMs.end(); ++it) {
                    if (it->second < oldest->second)
                        oldest = it;
                }
                lastDrawnMs.erase(oldest);
            }
        }
        if (measuredDeathMs.size() > kMaxDeathMs) {
            while (measuredDeathMs.size() > kMaxDeathMs) {
                auto oldest = measuredDeathMs.begin();
                for (auto it = std::next(oldest); it != measuredDeathMs.end(); ++it) {
                    if (it->second < oldest->second)
                        oldest = it;
                }
                measuredDeathMs.erase(oldest);
            }
        }
    }
    if (liveness) {
        for (uint32_t i = 0; i < liveness->count; ++i) {
            const auto& dead = liveness->players[i];
            if (dead.alive || !dead.pawn || dead.generation != measuredGeneration)
                continue;
            const auto newer = std::find_if(snapshot.players.begin(), snapshot.players.end(),
                [&](const Player& p) { return p.pawn == dead.pawn && p.position_sample_ms > dead.sample_ms; });
            if (newer != snapshot.players.end())
                continue;
            const auto drawn = lastDrawnMs.find(dead.pawn);
            if (drawn == lastDrawnMs.end() || !measuredDeathMs.emplace(dead.pawn, dead.sample_ms).second)
                continue;
            const float hideMs = drawn->second > dead.sample_ms
                ? static_cast<float>(drawn->second - dead.sample_ms) : 0.f;
            OmniGhost::Gameplay::DmaTelemetry::CS2().pressure.death_to_hide_ms.store(
                hideMs, std::memory_order_relaxed);
            lastDrawnMs.erase(drawn);
        }
    }
    const uint64_t worldSampleMs = snapshot.world_sample_ms
        ? snapshot.world_sample_ms : snapshot.snapshot_timestamp_ms;



    if (!worldSampleMs || nowMs < worldSampleMs || nowMs - worldSampleMs > 1000u)
        return;


    if (!frameCamera.timestamp_ms || nowMs < frameCamera.timestamp_ms ||
        nowMs - frameCamera.timestamp_ms > 250u)
        return;
    const float* frameViewMatrix = frameCamera.view_matrix;
    const uint64_t presentationSampleMs = frameCamera.timestamp_ms;
    const auto presentationDeltaMs = [presentationSampleMs](uint64_t sampleMs) {
        const float deltaMs = presentationSampleMs >= sampleMs
            ? static_cast<float>((std::min)(presentationSampleMs - sampleMs, uint64_t{35}))
            : -static_cast<float>((std::min)(sampleMs - presentationSampleMs, uint64_t{35}));
        return sampleMs ? deltaMs : 0.f;
    };


    std::vector<Player> presentationPlayers;
    presentationPlayers.reserve(snapshot.players.size() + heldPlayers.size());
    for (const Player& player : snapshot.players) {
        if (!player.pawn) continue;
        if (!player.alive || diedSinceSnapshot(player)) {
            heldPlayers.erase(player.pawn);
            continue;
        }
        Player stable = player;
        const auto previous = heldPlayers.find(player.pawn);
        if (previous != heldPlayers.end() &&
            previous->second.player.controller == player.controller) {
            const Player& cached = previous->second.player;
            if (!stable.name[0] && cached.name[0])
                std::snprintf(stable.name, sizeof(stable.name), "%s", cached.name);
            if ((!stable.bones_ok || !stable.full_bones_ok) &&
                cached.bones_ok && cached.full_bones_ok && cached.bone_pose_sample_ms &&
                nowMs >= cached.bone_pose_sample_ms &&
                nowMs - cached.bone_pose_sample_ms <= CS2::kBoneVisualMaxAgeMs) {
                std::memcpy(stable.bones, cached.bones, sizeof(stable.bones));
                const float shift[3] = {
                    stable.pos[0] - cached.pos[0], stable.pos[1] - cached.pos[1],
                    stable.pos[2] - cached.pos[2]
                };
                for (std::size_t bone = 0; bone < CS2::kBoneSlotCount; ++bone) {
                    if (stable.bones[bone][0] == 0.f && stable.bones[bone][1] == 0.f &&
                        stable.bones[bone][2] == 0.f)
                        continue;
                    for (int axis = 0; axis < 3; ++axis)
                        stable.bones[bone][axis] += shift[axis];
                }
                stable.bones_ok = true;
                stable.full_bones_ok = true;
                stable.bones_sample_ms = cached.bones_sample_ms;
                stable.bone_pose_sample_ms = cached.bone_pose_sample_ms;
                stable.bone_origin_sample_ms = stable.position_sample_ms;
            }
        }
        heldPlayers[player.pawn] = HeldPlayer{stable, nowMs};
        presentationPlayers.push_back(stable);
    }
    for (auto it = heldPlayers.begin(); it != heldPlayers.end();) {
        const bool present = std::find_if(snapshot.players.begin(), snapshot.players.end(),
            [&](const Player& player) { return player.pawn == it->first; }) != snapshot.players.end();
        const bool expired = nowMs < it->second.lastSeenMs ||
            nowMs - it->second.lastSeenMs > 450u;
        if (expired || diedSinceSnapshot(it->second.player)) {
            it = heldPlayers.erase(it);
            continue;
        }
        if (!present)
            presentationPlayers.push_back(it->second.player);
        ++it;
    }
    struct HeadHeight {
        uintptr_t controller = 0;
        uint64_t pose_ms = 0;
        uint64_t last_seen_ms = 0;
        float units = 72.f;
    };
    static std::unordered_map<uintptr_t, HeadHeight> headHeights;
    static uint64_t headHeightGeneration = 0;
    static uint64_t nextHeadPruneMs = 0;
    constexpr size_t kMaxHeadHeights = 128;
    if (headHeightGeneration != snapshot.session_generation) {
        headHeights.clear();
        headHeightGeneration = snapshot.session_generation;
        nextHeadPruneMs = 0;
    }
    if (nowMs >= nextHeadPruneMs) {
        nextHeadPruneMs = nowMs + 1000u;
        for (auto it = headHeights.begin(); it != headHeights.end();) {
            if (nowMs - it->second.last_seen_ms > 2000u) it = headHeights.erase(it);
            else ++it;
        }

        if (headHeights.size() > kMaxHeadHeights) {
            while (headHeights.size() > kMaxHeadHeights) {
                auto oldest = headHeights.begin();
                for (auto it = std::next(oldest); it != headHeights.end(); ++it) {
                    if (it->second.last_seen_ms < oldest->second.last_seen_ms)
                        oldest = it;
                }
                headHeights.erase(oldest);
            }
        }
    }
    for (const Player& player : presentationPlayers) {
        if (!player.alive || diedSinceSnapshot(player)) {
            headHeights.erase(player.pawn);
            continue;
        }
        if (!settings.show_bots && player.is_bot) continue;
        measuredDeathMs.erase(player.pawn);
        if (hideTeam && player.team == snapshot.local_team) continue;
        if (!std::isfinite(player.distance) || (maxDistance > 0.f && player.distance > maxDistance)) continue;
        if (!std::isfinite(player.pos[0]) || !std::isfinite(player.pos[1]) || !std::isfinite(player.pos[2])) continue;

        const uint64_t positionSampleMs = player.position_sample_ms
            ? player.position_sample_ms : snapshot.world_sample_ms
                ? snapshot.world_sample_ms : snapshot.snapshot_timestamp_ms;
        const uint64_t rawPositionAgeMs = positionSampleMs && nowMs >= positionSampleMs
            ? nowMs - positionSampleMs : 0;
        const float velocityMagnitude = std::sqrt(player.velocity[0] * player.velocity[0] +
            player.velocity[1] * player.velocity[1] + player.velocity[2] * player.velocity[2]);
        const bool velocityValid = std::isfinite(velocityMagnitude) && velocityMagnitude < 2500.f;
        const float predictionMs = velocityValid ? presentationDeltaMs(positionSampleMs) : 0.f;
        const float predictionSeconds = predictionMs / 1000.f;
        const float* viewMatrix = frameViewMatrix;
        const uint64_t cameraDelta = presentationSampleMs > positionSampleMs
            ? presentationSampleMs - positionSampleMs : positionSampleMs - presentationSampleMs;
        float bonesAge = 0.f;
        if (player.bones_sample_ms && nowMs >= player.bones_sample_ms) {
            bonesAge = static_cast<float>(nowMs - player.bones_sample_ms);
            auto& timing = OmniGhost::Gameplay::DmaTelemetry::CS2();
            if (player.bones_sample_ms < player.position_sample_ms) {
                timing.cache_vmm.cached_bones_used.fetch_add(1, std::memory_order_relaxed);
                float oldMax = timing.cache_vmm.cached_bones_age_max_ms.load(std::memory_order_relaxed);
                while (bonesAge > oldMax && !timing.cache_vmm.cached_bones_age_max_ms.compare_exchange_weak(
                    oldMax, bonesAge, std::memory_order_relaxed)) {}
            }
        }
        OmniGhost::Gameplay::DmaTelemetry::ObservePresentationTiming(
            OmniGhost::Gameplay::DmaTelemetry::CS2(), static_cast<float>(rawPositionAgeMs), bonesAge,
            static_cast<float>(cameraDelta), std::abs(predictionMs));
        const float positionOffset[3] = {
            velocityValid ? player.velocity[0] * predictionSeconds : 0.f,
            velocityValid ? player.velocity[1] * predictionSeconds : 0.f,
            velocityValid ? player.velocity[2] * predictionSeconds : 0.f
        };
        const uint64_t boneOriginSampleMs = player.bone_origin_sample_ms
            ? player.bone_origin_sample_ms : positionSampleMs;
        const float bonePredictionSeconds = velocityValid ? presentationDeltaMs(boneOriginSampleMs) / 1000.f : 0.f;
        const float boneOffset[3] = {
            velocityValid ? player.velocity[0] * bonePredictionSeconds : 0.f,
            velocityValid ? player.velocity[1] * bonePredictionSeconds : 0.f,
            velocityValid ? player.velocity[2] * bonePredictionSeconds : 0.f
        };
        const float predictedPosition[3] = {
            player.pos[0] + positionOffset[0], player.pos[1] + positionOffset[1], player.pos[2] + positionOffset[2]
        };
        if (DrawOffscreenArrow(draw, predictedPosition, viewMatrix, player, settings))
            continue;
        ImVec2 feet{}, head{};



        constexpr uint64_t kBonesVisualMaxAgeMs = CS2::kBoneVisualMaxAgeMs;
        const bool bonesFresh = player.bones_ok && player.bone_pose_sample_ms &&
            nowMs >= player.bone_pose_sample_ms &&
            (nowMs - player.bone_pose_sample_ms) <= kBonesVisualMaxAgeMs;
        const float headDx = player.bones[static_cast<size_t>(BoneSlot::Head)][0] - player.pos[0];
        const float headDy = player.bones[static_cast<size_t>(BoneSlot::Head)][1] - player.pos[1];
        const float headDz = player.bones[static_cast<size_t>(BoneSlot::Head)][2] - player.pos[2];
        const bool headPlausible = std::isfinite(headDx) && std::isfinite(headDy) &&
            std::isfinite(headDz) && headDx * headDx + headDy * headDy < 100.f * 100.f &&
            headDz > 5.f && headDz < 160.f;
        auto [heightIt, insertedHeight] = headHeights.try_emplace(player.pawn);
        auto& headHeight = heightIt->second;
        if (insertedHeight || headHeight.controller != player.controller) {
            headHeight = {};
            headHeight.controller = player.controller;
        }
        const uint64_t heightUpdateDeltaMs = nowMs - headHeight.last_seen_ms;
        headHeight.last_seen_ms = nowMs;
        if (bonesFresh && headPlausible) {
            const float measuredHeight = std::clamp(
                player.bones[static_cast<size_t>(BoneSlot::Head)][2] + boneOffset[2] - predictedPosition[2],
                16.f, 96.f);
            if (!headHeight.pose_ms) {
                headHeight.units = measuredHeight;
            } else if (player.bone_pose_sample_ms > headHeight.pose_ms) {
                const float dtMs = static_cast<float>(player.bone_pose_sample_ms - headHeight.pose_ms);
                const float alpha = 1.f - std::exp(-dtMs / 50.f);
                headHeight.units += (measuredHeight - headHeight.units) * alpha;
            }
            headHeight.pose_ms = player.bone_pose_sample_ms;
        } else if (headHeight.pose_ms && nowMs >= headHeight.pose_ms &&
            nowMs - headHeight.pose_ms > kBonesVisualMaxAgeMs) {
            const float alpha = 1.f - std::exp(-static_cast<float>(heightUpdateDeltaMs) / 120.f);
            headHeight.units += (72.f - headHeight.units) * alpha;
        }


        const float headWorld[3] = {
            predictedPosition[0], predictedPosition[1], predictedPosition[2] + headHeight.units
        };
        ImVec2 boxHead{};
        if (!WorldSegmentToScreenClipped(headWorld, predictedPosition, viewMatrix, boxHead, feet)) {
            // At point-blank range the camera can sit inside the player's vertical
            // segment, putting both synthetic endpoints behind the near plane even
            // though part of the model is still visible. Recover bounds from every
            // currently projectable bone instead of dropping the whole ESP entry.
            float left = FLT_MAX, right = -FLT_MAX, top = FLT_MAX, bottom = -FLT_MAX;
            std::size_t projectedBones = 0;
            if (bonesFresh && player.full_bones_ok) {
                for (std::size_t bone = 0; bone < CS2::kBoneSlotCount; ++bone) {
                    const float boneWorld[3] = {
                        player.bones[bone][0] + boneOffset[0],
                        player.bones[bone][1] + boneOffset[1],
                        player.bones[bone][2] + boneOffset[2]
                    };
                    ImVec2 point{};
                    if (!WorldToScreen(boneWorld, viewMatrix, point)) continue;
                    left = (std::min)(left, point.x);
                    right = (std::max)(right, point.x);
                    top = (std::min)(top, point.y);
                    bottom = (std::max)(bottom, point.y);
                    ++projectedBones;
                }
            }
            if (projectedBones < 2 || bottom - top < 8.f) continue;
            const float centerX = (left + right) * .5f;
            boxHead = ImVec2(centerX, top);
            feet = ImVec2(centerX, bottom);
        }
        head = boxHead;
        if (bonesFresh && headPlausible) {
            const float actualHead[3] = {
                player.bones[static_cast<size_t>(BoneSlot::Head)][0] + boneOffset[0],
                player.bones[static_cast<size_t>(BoneSlot::Head)][1] + boneOffset[1],
                player.bones[static_cast<size_t>(BoneSlot::Head)][2] + boneOffset[2]
            };
            ImVec2 actualHeadScreen{};
            if (WorldToScreen(actualHead, viewMatrix, actualHeadScreen)) head = actualHeadScreen;
        }
        const ImVec2 viewport = ImGui::GetIO().DisplaySize;
        const float height = std::min(feet.y - boxHead.y, viewport.y * 4.f);
        if (!std::isfinite(height) || height < 8.f) continue;
        const float width = height * 0.60f;
        const ImVec2 min(feet.x - width * .5f, boxHead.y - height * .08f);
        const ImVec2 max(feet.x + width * .5f, min.y + height * 1.09f);


        constexpr float kViewportMargin = 240.f;
        if (max.x < -kViewportMargin || min.x > viewport.x + kViewportMargin ||
            max.y < -kViewportMargin || min.y > viewport.y + kViewportMargin)
            continue;
        const float thickness = std::clamp(settings.box_thickness, 0.5f, 5.f);
        if (settings.box || settings.box_corner) {
            draw->AddRect(ImVec2(min.x - 1.f, min.y - 1.f), ImVec2(max.x + 1.f, max.y + 1.f),
                          IM_COL32(0, 0, 0, 150), 0.f, 0, thickness + 1.f);
            const auto boxColor = [&](const float* configured) {
                return settings.rgb_mode ? rgbColor
                    : useVisibilityColors && player.visibility_known ? Color(player.visible ? settings.col_visible : settings.col_occluded)
                    : Color(player.team == snapshot.local_team ? settings.col_team : configured);
            };
            if (settings.box_fill)
                draw->AddRectFilled(min, max, Color(settings.col_box_fill), std::clamp(settings.box_rounding, 0.f, 18.f));
            if (settings.box)
                draw->AddRect(min, max, boxColor(settings.col_box),
                              std::clamp(settings.box_rounding, 0.f, 18.f), 0, thickness);
            if (settings.box_corner || settings.box_style == 3)
                DrawCornerBox(draw, min, max, boxColor(settings.col_box_corner), thickness);
        }
        const auto freeBar = [&](float nx, float ny, float fraction, ImU32 color) {
            const float x = min.x + nx * (max.x - min.x) - 3.f;
            const float centerY = min.y + ny * (max.y - min.y);
            const float halfHeight = (max.y - min.y) * .46f;
            DrawVerticalBar(draw, x, centerY - halfHeight, centerY + halfHeight, fraction, color);
        };
        if (settings.health_bar)
            freeBar(settings.layout_health_x, settings.layout_health_y,
                static_cast<float>(player.health) / 100.f, Color(settings.col_health));
        if (settings.armor_bar)
            freeBar(settings.layout_armor_x, settings.layout_armor_y,
                static_cast<float>(player.armor) / 100.f, Color(settings.col_armor));
        if (settings.skeleton && bonesFresh && headPlausible && player.full_bones_ok) {
            const ImU32 skeletonColor = settings.rgb_mode ? rgbColor
                : useVisibilityColors && player.visibility_known ? Color(player.visible ? settings.col_visible : settings.col_occluded)
                : Color(settings.col_skeleton);
            DrawSkeleton(draw, player, viewMatrix, boneOffset, skeletonColor, Color(settings.col_joints),
                         std::clamp(settings.skeleton_thickness, .5f, 4.f), settings.skeleton_joints, height);
        }
        const auto textSize = [&](const char* text) {
            const float fontSize = ImGui::GetFontSize() * std::clamp(settings.layout_text_scale, .65f, 1.6f);
            return ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.f, text);
        };
        const auto placeText = [&](const char* text, float nx, float ny, ImU32 color) {
            if (!text || !*text) return;
            const ImVec2 size = textSize(text);
            const ImVec2 pos(min.x + nx * (max.x - min.x) - size.x * .5f,
                             min.y + ny * (max.y - min.y) - size.y * .5f);
            DrawOutlinedText(draw, pos, color, text, settings);
        };
        if (settings.name && player.name[0])
            placeText(player.name, settings.layout_name_x, settings.layout_name_y, Color(settings.col_name));
        if (settings.c4_carrier_esp && !snapshot.bomb.planted && player.weapon_def == 49)
            placeText("[C4]", settings.layout_name_x, settings.layout_name_y + .08f, IM_COL32(255, 190, 45, 255));
        if (settings.distance) {
            char distance[32];
            FormatDistance(distance, sizeof(distance), player.distance);
            placeText(distance, settings.layout_distance_x, settings.layout_distance_y, Color(settings.col_distance));
        }
        if (settings.weapon_name && player.weapon[0])
            placeText(player.weapon, settings.layout_weapon_x, settings.layout_weapon_y, Color(settings.col_weapon));
        if (settings.weapon_ammo && settings.weapon_name && player.ammo_clip >= 0) {
            char ammo[32];
            FormatAmmo(ammo, sizeof(ammo), player.ammo_clip, (std::max)(0, player.ammo_reserve));
            placeText(ammo, settings.layout_ammo_x, settings.layout_ammo_y, Color(settings.col_weapon));
        }
        if (settings.health_value) { char hp[16]; FormatInt(hp, sizeof(hp), player.health); DrawOutlinedText(draw, ImVec2(min.x - 25.f, min.y), Color(settings.col_health), hp, settings); }
        if (settings.armor_value && player.armor > 0) { char ap[16]; FormatInt(ap, sizeof(ap), player.armor); DrawOutlinedText(draw, ImVec2(max.x + 8.f, min.y), Color(settings.col_armor), ap, settings); }
        DrawExtras(draw, player, head, feet, min, max, viewMatrix, predictedPosition, settings,
                   rgbColor, snapshot.session_generation);
        lastDrawnMs[player.pawn] = nowMs;
    }
    DrawDroppedWeapons(draw, snapshot, settings, frameViewMatrix);
}

}
