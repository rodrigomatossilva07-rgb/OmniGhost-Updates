#include "cs2_esp.h"

#include "../cs2_game.h"
#include "../config/cs2_config.h"
#include "gameplay/dma_telemetry_log.h"
#include "../../ImGui/imgui.h"
#include "../../src/config/app_settings.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>
#include <cstdio>
#include <functional>
#include <unordered_map>
#include <utility>

namespace {

bool WorldToScreen(const float world[3], const float matrix[16], ImVec2& screen) {
    const float clipX = world[0] * matrix[0] + world[1] * matrix[1] + world[2] * matrix[2] + matrix[3];
    const float clipY = world[0] * matrix[4] + world[1] * matrix[5] + world[2] * matrix[6] + matrix[7];
    const float clipW = world[0] * matrix[12] + world[1] * matrix[13] + world[2] * matrix[14] + matrix[15];
    if (!std::isfinite(clipW) || clipW < 0.01f) return false;
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    screen.x = (size.x * 0.5f) * (1.f + clipX / clipW);
    screen.y = (size.y * 0.5f) * (1.f - clipY / clipW);
    return std::isfinite(screen.x) && std::isfinite(screen.y);
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

void DrawSkeleton(ImDrawList* draw, const CS2::Player& player, const float viewMatrix[16],
                  const float positionOffset[3],
                  ImU32 color, ImU32 jointColor, float thickness, bool joints) {
    if (!player.full_bones_ok) return;

    // Compact reference-style rig: exactly 13 visible segments.
    constexpr std::pair<CS2::BoneSlot, CS2::BoneSlot> kLinks[] = {
        {CS2::BoneSlot::Head, CS2::BoneSlot::Neck}, {CS2::BoneSlot::Neck, CS2::BoneSlot::SpineUpper},
        {CS2::BoneSlot::SpineUpper, CS2::BoneSlot::Pelvis},
        {CS2::BoneSlot::Neck, CS2::BoneSlot::ShoulderLeft},
        {CS2::BoneSlot::ShoulderLeft, CS2::BoneSlot::ElbowLeft}, {CS2::BoneSlot::ElbowLeft, CS2::BoneSlot::HandLeft},
        {CS2::BoneSlot::Neck, CS2::BoneSlot::ShoulderRight},
        {CS2::BoneSlot::ShoulderRight, CS2::BoneSlot::ElbowRight}, {CS2::BoneSlot::ElbowRight, CS2::BoneSlot::HandRight},
        {CS2::BoneSlot::Pelvis, CS2::BoneSlot::KneeLeft}, {CS2::BoneSlot::KneeLeft, CS2::BoneSlot::AnkleLeft},
        {CS2::BoneSlot::Pelvis, CS2::BoneSlot::KneeRight}, {CS2::BoneSlot::KneeRight, CS2::BoneSlot::AnkleRight},
    };
    static_assert(sizeof(kLinks) / sizeof(kLinks[0]) == 13,
        "CS2 skeleton must have exactly 13 segments");
    std::array<bool, CS2::kBoneSlotCount> used{};
    for (const auto& [from, to] : kLinks) {
        used[static_cast<std::size_t>(from)] = true;
        used[static_cast<std::size_t>(to)] = true;
    }
    std::array<ImVec2, CS2::kBoneSlotCount> points{};
    std::array<bool, CS2::kBoneSlotCount> valid{};
    for (std::size_t index = 0; index < CS2::kBoneSlotCount; ++index) {
        if (!used[index]) continue;
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
        draw->AddLine(points[a], points[b], IM_COL32(0, 0, 0, 195), thickness + 1.4f);
        draw->AddLine(points[a], points[b], color, thickness);
    }
    if (joints) {
        for (std::size_t index = 0; index < CS2::kBoneSlotCount; ++index) {
            if (!used[index]) continue;
            if (!valid[index]) continue;
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
        draw->AddText(ImVec2(pos.x - t, pos.y), outline, text);
        draw->AddText(ImVec2(pos.x + t, pos.y), outline, text);
        draw->AddText(ImVec2(pos.x, pos.y - t), outline, text);
        draw->AddText(ImVec2(pos.x, pos.y + t), outline, text);
    }
    draw->AddText(pos, color, text);
}

void DrawExtras(ImDrawList* draw, const CS2::Player& player, const ImVec2& head, const ImVec2& feet,
                const ImVec2& min, const ImVec2& max, const float* viewMatrix,
                const float predictedPosition[3], const CS2::Config& settings, ImU32 rgb,
                uint64_t sessionGeneration) {
    const float height = max.y - min.y;
    const float scale = std::clamp(height / 180.f, .45f, 2.2f);
    if (settings.snaplines) {
        const float y = settings.snapline_position == 0 ? 0.f : settings.snapline_position == 1 ? ImGui::GetIO().DisplaySize.y * .5f : ImGui::GetIO().DisplaySize.y;
        draw->AddLine(ImVec2(ImGui::GetIO().DisplaySize.x * .5f, y), feet,
                      EffectColor(settings, settings.col_snaplines, rgb), std::clamp(settings.snapline_thickness, .5f, 5.f));
    }
    if (settings.head_dot)
        draw->AddCircle(head, 10.f * scale, EffectColor(settings, settings.col_head, rgb), 20,
                        std::clamp(settings.head_circle_thickness, .5f, 5.f));
    if (settings.weapon_name && player.weapon[0]) {
        const ImVec2 text = ImGui::CalcTextSize(player.weapon);
        DrawOutlinedText(draw, ImVec2((min.x + max.x - text.x) * .5f, max.y + 4.f), EffectColor(settings, settings.col_weapon, rgb), player.weapon, settings);
    }
    if (settings.head_halo) {
        ImVec2 halo[17]{};
        for (int index = 0; index <= 16; ++index) {
            const float angle = index * 6.28318530718f / 16.f;
            halo[index] = ImVec2(head.x + std::cos(angle) * 17.f * scale,
                head.y - 13.f * scale + std::sin(angle) * 5.f * scale);
        }
        draw->AddPolyline(halo, 17, EffectColor(settings, settings.col_halo, rgb), true, 2.f);
    }
    const ImU32 fx = EffectColor(settings, settings.col_fun_effects, rgb);
    if (settings.chinese_hat) {
        const float s = std::clamp(settings.chinese_hat_scale, .4f, 2.5f) * scale;
        const ImVec2 a(head.x, head.y - 38.f * s), b(head.x - 28.f * s, head.y - 9.f * s), c(head.x + 28.f * s, head.y - 9.f * s);
        draw->AddTriangle(a, b, c, fx, 1.8f); draw->AddLine(b, c, fx, 1.8f);
    }
    if (settings.angel_wings) {
        const float s = std::clamp(settings.fun_effects_scale, .5f, 2.5f) * scale;
        for (int side : {-1, 1}) {
            const float x = head.x + side * 10.f * s;
            draw->AddBezierCubic(ImVec2(x, head.y + 12.f * s), ImVec2(x + side * 35.f * s, head.y - 8.f * s),
                ImVec2(x + side * 42.f * s, head.y + 42.f * s), ImVec2(x + side * 20.f * s, head.y + 60.f * s), fx, 1.8f);
        }
    }
    if (settings.devil_horns) {
        for (int side : {-1, 1}) {
            const float s = scale;
            draw->AddBezierCubic(ImVec2(head.x + side * 6.f * s, head.y - 6.f * s), ImVec2(head.x + side * 27.f * s, head.y - 28.f * s),
                ImVec2(head.x + side * 24.f * s, head.y - 41.f * s), ImVec2(head.x + side * 12.f * s, head.y - 33.f * s), fx, 2.f);
        }
    }
    if (settings.floating_crown) {
        const float y = head.y - (35.f + std::sin(static_cast<float>(ImGui::GetTime()) * 2.f) * 3.f) * scale;
        const float s = scale;
        const ImVec2 points[] = {{head.x - 22.f*s,y}, {head.x - 12.f*s,y - 13.f*s}, {head.x,y - 3.f*s},
            {head.x + 12.f*s,y - 13.f*s}, {head.x + 22.f*s,y}, {head.x + 18.f*s,y + 7.f*s}, {head.x - 18.f*s,y + 7.f*s}};
        draw->AddPolyline(points, 7, fx, true, 1.8f);
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
        if (historyGeneration != sessionGeneration) {
            history.clear();
            nextPrune = 0.;
            historyGeneration = sessionGeneration;
        }
        auto& points = history[player.pawn];
        const double now = ImGui::GetTime();
        // A trail represents acquired world samples, not overlay frames.  Do
        // not manufacture duplicate points while acquisition is unchanged.
        if (player.position_sample_ms &&
            (points.empty() || points.back().sample_ms != player.position_sample_ms)) {
            Point point{};
            std::memcpy(point.world, player.pos, sizeof(point.world));
            point.sample_ms = player.position_sample_ms;
            point.time = now;
            points.push_back(point);
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
        }
    }
}

void DrawFootprint(ImDrawList* draw, const ImVec2& center, float angle, float scale, ImU32 color) {
    // A compact vector shoe-print: sole + heel.  It intentionally needs no
    // texture upload and therefore costs no GPU/resource work per step.
    const auto ellipse = [&](ImVec2 c, float rx, float ry) {
        constexpr int kSegments = 12;
        ImVec2 points[kSegments];
        const float cs = std::cos(angle), sn = std::sin(angle);
        for (int i = 0; i < kSegments; ++i) {
            const float a = (6.283185307f * i) / kSegments;
            const float x = std::cos(a) * rx, y = std::sin(a) * ry;
            points[i] = ImVec2(c.x + x * cs - y * sn, c.y + x * sn + y * cs);
        }
        draw->AddConvexPolyFilled(points, kSegments, color);
    };
    const float cs = std::cos(angle), sn = std::sin(angle);
    const auto offset = [&](float x, float y) {
        return ImVec2(center.x + x * cs - y * sn, center.y + x * sn + y * cs);
    };
    ellipse(offset(0.f, -5.5f * scale), 4.1f * scale, 8.6f * scale);
    ellipse(offset(0.f, 6.6f * scale), 3.3f * scale, 4.5f * scale);
}

void DrawFootstepEsp(ImDrawList* draw, const CS2::Player& player, const float position[3],
                     const float* viewMatrix, const CS2::Config& settings, ImU32 /*rgb*/,
                     uint64_t sessionGeneration) {
    if (!settings.footstep_esp || !player.pawn) return;

    struct Step {
        uint64_t began_ms = 0;
        float position[3]{};
        float yaw = 0.f;
        bool left = false;
    };
    struct FootprintHistory {
        uint64_t last_step_ms = 0;
        uint64_t last_seen_ms = 0;
        bool next_left = false;
        std::deque<Step> steps;
    };
    static std::unordered_map<uintptr_t, FootprintHistory> footprints;
    static uint64_t footprintsGeneration = 0;
    if (footprintsGeneration != sessionGeneration) {
        footprints.clear();
        footprintsGeneration = sessionGeneration;
    }

    const uint64_t now = GetTickCount64();
    auto& history = footprints[player.pawn];
    history.last_seen_ms = now;
    // The data collector treats every speed above stationary jitter as motion,
    // so this includes slow and silent walking as requested.
    if (player.is_moving && (now - history.last_step_ms >= 245)) {
        history.last_step_ms = now;
        history.next_left = !history.next_left;
        Step step{};
        step.began_ms = now;
        std::memcpy(step.position, position, sizeof(step.position));
        step.yaw = player.view_yaw;
        step.left = history.next_left;
        history.steps.push_back(step);
    }

    constexpr float kLifetimeMs = 1000.f;
    while (!history.steps.empty() && now - history.steps.front().began_ms > kLifetimeMs)
        history.steps.pop_front();
    for (const Step& step : history.steps) {
        const float age = static_cast<float>(now - step.began_ms) / kLifetimeMs;
        const float fade = (1.f - age) * (1.f - age); // smooth one-second fade
        const ImU32 gold = IM_COL32(218, 165, 32, static_cast<int>(fade * 238.f));
        ImVec2 projected{};
        if (!WorldToScreen(step.position, viewMatrix, projected)) continue;
        // A world yaw cannot be used directly as a screen-space angle while
        // the camera rotates. Project a short world-space forward vector.
        const float yaw = step.yaw * 0.0174532925f;
        const float forward[3] = {step.position[0] + std::cos(yaw) * 18.f,
                                  step.position[1] + std::sin(yaw) * 18.f,
                                  step.position[2]};
        ImVec2 forwardScreen{};
        const float angle = WorldToScreen(forward, viewMatrix, forwardScreen)
            ? std::atan2(forwardScreen.y - projected.y, forwardScreen.x - projected.x)
            : 0.f;
        DrawFootprint(draw, projected, angle + (step.left ? -.16f : .16f), 1.f + age * .10f, gold);
    }

    static uint64_t nextFootprintPruneMs = 0;
    if (now >= nextFootprintPruneMs) {
        nextFootprintPruneMs = now + 1000;
        for (auto it = footprints.begin(); it != footprints.end();) {
            if (now - it->second.last_seen_ms > 5000)
                it = footprints.erase(it);
            else
                ++it;
        }
    }
}

void DrawPersistentWindow(const char* id, const char* title, float& x, float& y,
                          const ImVec2& fallback, const std::function<void()>& content) {
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
        }
    }
    ImGui::End();
}

void DrawBombTimerWindow(const CS2::Runtime& snapshot, const CS2::Config& settings) {
    if (!settings.bomb_timer || !snapshot.bomb.planted) return;
    // Window placement is render-thread UI state, not mutable gameplay config.
    static float x = -1.f, y = -1.f;
    if (x < 0.f || y < 0.f) { x = settings.bomb_window_x; y = settings.bomb_window_y; }
    const auto& bomb = snapshot.bomb;
    DrawPersistentWindow("##cs2_bomb_timer", "BOMBA", x, y, ImVec2(30.f, 338.f), [&] {
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
        });
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
        });
}

void DrawPlayerFlags(ImDrawList* draw, const CS2::Player& player, const ImVec2& min,
                     const CS2::Config& settings, ImU32 rgb) {
    if (!settings.player_flags) return;
    const ImU32 color = EffectColor(settings, settings.col_flags, rgb);
    float y = min.y;
    const float x = min.x - 8.f;
    const auto add = [&](const char* label) {
        const ImVec2 size = ImGui::CalcTextSize(label);
        DrawOutlinedText(draw, ImVec2(x - size.x, y), color, label, settings);
        y += size.y + 1.f;
    };
    if (settings.flag_blind && player.is_flashed) add("BLIND");
    if (settings.flag_scoped && player.is_scoped) add("SCOPED");
    if (settings.flag_defusing && player.is_defusing) add("DEFUSING");
    if (settings.flag_kit && player.has_defuser) add("KIT");
    if (settings.flag_money && player.money >= 0) {
        char money[24]{};
        std::snprintf(money, sizeof(money), "$%d", player.money);
        add(money);
    }
}

void DrawDroppedWeapons(ImDrawList* draw, const CS2::Runtime& snapshot,
                        const CS2::Config& settings, const float* viewMatrix) {
    if (!settings.dropped_weapons || !viewMatrix) return;
    // World loot is useful only nearby and is deliberately constrained so it
    // never clutters the screen with objects from the other side of the map.
    constexpr float kMaxDistanceUnits = 50.f * 39.37f;
    // A discovery batch has one spatial sample. Choose its coherent camera
    // once instead of locking the camera history for every world item.
    const uint64_t sampleMs = snapshot.dropped_weapons.empty() ? 0u
        : snapshot.dropped_weapons.front().sample_timestamp_ms;
    const auto weaponCamera = CS2::SelectCameraSnapshot(sampleMs);
    const float* weaponMatrix = weaponCamera.timestamp_ms
        ? weaponCamera.view_matrix : viewMatrix;
    for (const auto& weapon : snapshot.dropped_weapons) {
        const float dx = weapon.pos[0] - snapshot.local_pos[0];
        const float dy = weapon.pos[1] - snapshot.local_pos[1];
        const float dz = weapon.pos[2] - snapshot.local_pos[2];
        if (dx * dx + dy * dy + dz * dz > kMaxDistanceUnits * kMaxDistanceUnits) continue;
        ImVec2 screen{};
        if (!WorldToScreen(weapon.pos, weaponMatrix, screen)) continue;
        const ImU32 color = Color(settings.col_weapon);
        char label[96]{};
        if (settings.dropped_weapon_ammo && weapon.ammo_clip >= 0)
            std::snprintf(label, sizeof(label), "%s  %d", weapon.name, weapon.ammo_clip);
        else
            std::snprintf(label, sizeof(label), "%s", weapon.name);
        const ImVec2 text = ImGui::CalcTextSize(label);
        const float iconOffset = settings.dropped_weapon_icons ? 10.f : 0.f;
        if (settings.dropped_weapon_icons) {
            // Small vector icon avoids depending on a particular icon-font
            // glyph/version while making world weapons recognisable at a glance.
            draw->AddRectFilled(ImVec2(screen.x - text.x * .5f - 11.f, screen.y - 1.f),
                ImVec2(screen.x - text.x * .5f - 3.f, screen.y + 5.f), color, 1.f);
        }
        DrawOutlinedText(draw, ImVec2(screen.x - text.x * .5f + iconOffset * .15f, screen.y - 12.f), color, label, settings);
    }
}

} // namespace

namespace CS2::ESP {

void DrawPlayers(const Runtime& snapshot, const Config& settings) {
    // Widgets are independent from the player ESP master switch. This makes
    // them visible/movable in a live match even when the user only wants C4
    // or spectator information.
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
    const auto liveness = AcquireLivenessSnapshot();
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
    const uint64_t nowMs = GetTickCount64();
    const uint64_t worldSampleMs = snapshot.world_sample_ms
        ? snapshot.world_sample_ms : snapshot.snapshot_timestamp_ms;
    if (!worldSampleMs || nowMs < worldSampleMs || nowMs - worldSampleMs > 150u)
        return; // an outage must not project a long-dead world as live ESP
    // Runtime snapshots are produced from one core scatter, so one coherent
    // camera selection covers the whole frame and avoids mutex contention per player.
    const CameraSnapshot frameCamera = SelectCameraSnapshot(framePositionSampleMs);
    const float* frameViewMatrix = frameCamera.timestamp_ms ? frameCamera.view_matrix : snapshot.view_matrix;
    for (const Player& player : snapshot.players) {
        if (!player.alive || player.is_local) continue;
        if (diedSinceSnapshot(player)) {
            const float hideMs = liveness && nowMs >= liveness->timestamp_ms
                ? static_cast<float>(nowMs - liveness->timestamp_ms) : 0.f;
            OmniGhost::Gameplay::DmaTelemetry::CS2().death_to_hide_ms.store(
                hideMs, std::memory_order_relaxed);
            continue;
        }
        if (hideTeam && player.team == snapshot.local_team) continue;
        if (!std::isfinite(player.distance) || (maxDistance > 0.f && player.distance > maxDistance)) continue;
        if (!std::isfinite(player.pos[0]) || !std::isfinite(player.pos[1]) || !std::isfinite(player.pos[2])) continue;

        const uint64_t positionSampleMs = player.position_sample_ms
            ? player.position_sample_ms : snapshot.snapshot_timestamp_ms;
        const uint64_t rawPositionAgeMs = positionSampleMs && nowMs >= positionSampleMs
            ? nowMs - positionSampleMs : 0;
        const float velocityMagnitude = std::sqrt(player.velocity[0] * player.velocity[0] +
            player.velocity[1] * player.velocity[1] + player.velocity[2] * player.velocity[2]);
        const uint64_t predictionCapMs = velocityMagnitude > 0.01f && velocityMagnitude < 2500.f ? 45u : 20u;
        const uint64_t positionAgeMs = (std::min)(rawPositionAgeMs, predictionCapMs);
        const float predictionSeconds = static_cast<float>(positionAgeMs) / 1000.f;
        const float* viewMatrix = frameViewMatrix;
        const uint64_t cameraDelta = frameCamera.timestamp_ms > positionSampleMs
            ? frameCamera.timestamp_ms - positionSampleMs : positionSampleMs - frameCamera.timestamp_ms;
        float bonesAge = 0.f;
        if (player.bones_sample_ms && nowMs >= player.bones_sample_ms) {
            bonesAge = static_cast<float>(nowMs - player.bones_sample_ms);
            auto& timing = OmniGhost::Gameplay::DmaTelemetry::CS2();
            if (player.bones_sample_ms < player.position_sample_ms) {
                timing.cached_bones_used.fetch_add(1, std::memory_order_relaxed);
                float oldMax = timing.cached_bones_age_max_ms.load(std::memory_order_relaxed);
                while (bonesAge > oldMax && !timing.cached_bones_age_max_ms.compare_exchange_weak(
                    oldMax, bonesAge, std::memory_order_relaxed)) {}
            }
        }
        OmniGhost::Gameplay::DmaTelemetry::ObservePresentationTiming(
            OmniGhost::Gameplay::DmaTelemetry::CS2(), static_cast<float>(positionAgeMs), bonesAge,
            static_cast<float>(cameraDelta), static_cast<float>(positionAgeMs));
        const float positionOffset[3] = {
            std::isfinite(player.velocity[0]) ? player.velocity[0] * predictionSeconds : 0.f,
            std::isfinite(player.velocity[1]) ? player.velocity[1] * predictionSeconds : 0.f,
            std::isfinite(player.velocity[2]) ? player.velocity[2] * predictionSeconds : 0.f
        };
        const uint64_t boneOriginAgeMs = player.bone_origin_sample_ms && nowMs >= player.bone_origin_sample_ms
            ? (std::min)(nowMs - player.bone_origin_sample_ms, uint64_t{20}) : positionAgeMs;
        const float bonePredictionSeconds = static_cast<float>(boneOriginAgeMs) / 1000.f;
        const float boneOffset[3] = {
            player.velocity[0] * bonePredictionSeconds, player.velocity[1] * bonePredictionSeconds,
            player.velocity[2] * bonePredictionSeconds
        };
        const float predictedPosition[3] = {
            player.pos[0] + positionOffset[0], player.pos[1] + positionOffset[1], player.pos[2] + positionOffset[2]
        };
        ImVec2 feet{}, head{};
        if (!WorldToScreen(predictedPosition, viewMatrix, feet)) continue;
        float headWorld[3] = { predictedPosition[0], predictedPosition[1], predictedPosition[2] + 72.f };
        // Keep the same head anchor for the entire validated bone lifetime;
        // switching abruptly at an arbitrary 90 ms threshold causes box pops.
        if (player.bones_ok && player.bone_pose_sample_ms && nowMs >= player.bone_pose_sample_ms &&
            nowMs - player.bone_pose_sample_ms <= 180u) {
            headWorld[0] = player.bones[static_cast<size_t>(BoneSlot::Head)][0] + boneOffset[0];
            headWorld[1] = player.bones[static_cast<size_t>(BoneSlot::Head)][1] + boneOffset[1];
            headWorld[2] = player.bones[static_cast<size_t>(BoneSlot::Head)][2] + boneOffset[2];
        }
        if (!WorldToScreen(headWorld, viewMatrix, head)) continue;

        const float height = feet.y - head.y;
        if (!std::isfinite(height) || height < 8.f || height > 4000.f) continue;
        const float width = height * 0.60f;
        const ImVec2 min(feet.x - width * .5f, head.y - height * .08f);
        const ImVec2 max(feet.x + width * .5f, min.y + height * 1.09f);
        // Do not turn valid-but-distant projections into dozens of text and
        // line commands. Keep a margin for entities entering the viewport.
        const ImVec2 viewport = ImGui::GetIO().DisplaySize;
        constexpr float kViewportMargin = 240.f;
        if (max.x < -kViewportMargin || min.x > viewport.x + kViewportMargin ||
            max.y < -kViewportMargin || min.y > viewport.y + kViewportMargin)
            continue;
        const float thickness = std::clamp(settings.box_thickness, 0.5f, 5.f);
        if (settings.box || settings.box_corner) {
            draw->AddRect(ImVec2(min.x - 1.f, min.y - 1.f), ImVec2(max.x + 1.f, max.y + 1.f),
                          IM_COL32(0, 0, 0, 150), 0.f, 0, thickness + 1.f);
            const float* elementColor = settings.box_corner ? settings.col_box_corner : settings.col_box;
            const ImU32 color = settings.rgb_mode ? rgbColor
                : useVisibilityColors && player.visibility_known ? Color(player.visible ? settings.col_visible : settings.col_occluded)
                : Color(player.team == snapshot.local_team ? settings.col_team : elementColor);
            if (settings.box_fill)
                draw->AddRectFilled(min, max, Color(settings.col_box_fill), std::clamp(settings.box_rounding, 0.f, 18.f));
            if (settings.box_corner || settings.box_style == 3)
                DrawCornerBox(draw, min, max, color, thickness);
            else
                draw->AddRect(min, max, color, std::clamp(settings.box_rounding, 0.f, 18.f), 0, thickness);
        }
        if (settings.health_bar)
            DrawVerticalBar(draw, min.x - 7.f, min.y, max.y, static_cast<float>(player.health) / 100.f, Color(settings.col_health));
        if (settings.armor_bar)
            DrawVerticalBar(draw, max.x + 3.f, min.y, max.y, static_cast<float>(player.armor) / 100.f, Color(settings.col_armor));
        if (settings.skeleton) {
            const ImU32 skeletonColor = settings.rgb_mode ? rgbColor
                : useVisibilityColors && player.visibility_known ? Color(player.visible ? settings.col_visible : settings.col_occluded)
                : Color(settings.col_skeleton);
            DrawSkeleton(draw, player, viewMatrix, boneOffset, skeletonColor, Color(settings.col_joints),
                         std::clamp(settings.skeleton_thickness, .5f, 4.f), settings.skeleton_joints);
        }
        if (settings.name && player.name[0]) {
            const ImVec2 size = ImGui::CalcTextSize(player.name);
            DrawOutlinedText(draw, ImVec2((min.x + max.x - size.x) * .5f, min.y - 16.f), Color(settings.col_name), player.name, settings);
        }
        if (settings.distance) {
            char distance[32]{}; std::snprintf(distance, sizeof(distance), "%.0f m", player.distance);
            const ImVec2 size = ImGui::CalcTextSize(distance);
            DrawOutlinedText(draw, ImVec2((min.x + max.x - size.x) * .5f, max.y + (settings.weapon_name ? 20.f : 4.f)), Color(settings.col_distance), distance, settings);
        }
        if (settings.weapon_ammo && player.ammo_clip >= 0) {
            char ammo[32]{}; std::snprintf(ammo, sizeof(ammo), "%d/%d", player.ammo_clip, (std::max)(0, player.ammo_reserve));
            const ImVec2 size = ImGui::CalcTextSize(ammo);
            DrawOutlinedText(draw, ImVec2((min.x + max.x - size.x) * .5f, max.y + 20.f), Color(settings.col_weapon), ammo, settings);
        }
        if (settings.health_value) { char hp[16]{}; std::snprintf(hp, sizeof(hp), "%d", player.health); DrawOutlinedText(draw, ImVec2(min.x - 25.f, min.y), Color(settings.col_health), hp, settings); }
        if (settings.armor_value && player.armor > 0) { char ap[16]{}; std::snprintf(ap, sizeof(ap), "%d", player.armor); DrawOutlinedText(draw, ImVec2(max.x + 8.f, min.y), Color(settings.col_armor), ap, settings); }
        DrawExtras(draw, player, head, feet, min, max, viewMatrix, predictedPosition, settings,
                   rgbColor, snapshot.session_generation);
        DrawFootstepEsp(draw, player, player.pos, viewMatrix, settings, rgbColor,
                        snapshot.session_generation);
        DrawPlayerFlags(draw, player, min, settings, rgbColor);
    }
    DrawDroppedWeapons(draw, snapshot, settings, frameViewMatrix);
}

} // namespace CS2::ESP
