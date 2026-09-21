#include "cs2_esp.h"

#include "../cs2_game.h"
#include "../config/cs2_config.h"
#include "../trajectory/cs2_grenade_simulator.h"
#include "../trajectory/cs2_map_collision_cache.h"
#include "../../ImGui/imgui.h"
#include "../../src/config/app_settings.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
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

    std::array<ImVec2, CS2::kBoneSlotCount> points{};
    std::array<bool, CS2::kBoneSlotCount> valid{};
    for (std::size_t index = 0; index < CS2::kBoneSlotCount; ++index) {
        const float predictedBone[3] = {
            player.bones[index][0] + positionOffset[0],
            player.bones[index][1] + positionOffset[1],
            player.bones[index][2] + positionOffset[2]
        };
        valid[index] = WorldToScreen(predictedBone, viewMatrix, points[index]);
    }

    constexpr std::pair<CS2::BoneSlot, CS2::BoneSlot> kLinks[] = {
        {CS2::BoneSlot::Head, CS2::BoneSlot::Neck}, {CS2::BoneSlot::Neck, CS2::BoneSlot::SpineUpper},
        {CS2::BoneSlot::SpineUpper, CS2::BoneSlot::SpineMiddle}, {CS2::BoneSlot::SpineMiddle, CS2::BoneSlot::SpineLower},
        {CS2::BoneSlot::SpineLower, CS2::BoneSlot::Pelvis},
        {CS2::BoneSlot::Neck, CS2::BoneSlot::ClavicleLeft}, {CS2::BoneSlot::ClavicleLeft, CS2::BoneSlot::ShoulderLeft},
        {CS2::BoneSlot::ShoulderLeft, CS2::BoneSlot::ElbowLeft}, {CS2::BoneSlot::ElbowLeft, CS2::BoneSlot::HandLeft},
        {CS2::BoneSlot::Neck, CS2::BoneSlot::ClavicleRight}, {CS2::BoneSlot::ClavicleRight, CS2::BoneSlot::ShoulderRight},
        {CS2::BoneSlot::ShoulderRight, CS2::BoneSlot::ElbowRight}, {CS2::BoneSlot::ElbowRight, CS2::BoneSlot::HandRight},
        {CS2::BoneSlot::Pelvis, CS2::BoneSlot::HipLeft}, {CS2::BoneSlot::HipLeft, CS2::BoneSlot::KneeLeft},
        {CS2::BoneSlot::KneeLeft, CS2::BoneSlot::AnkleLeft}, {CS2::BoneSlot::Pelvis, CS2::BoneSlot::HipRight},
        {CS2::BoneSlot::HipRight, CS2::BoneSlot::KneeRight}, {CS2::BoneSlot::KneeRight, CS2::BoneSlot::AnkleRight},
    };
    for (const auto& [from, to] : kLinks) {
        const auto a = static_cast<std::size_t>(from), b = static_cast<std::size_t>(to);
        if (!valid[a] || !valid[b]) continue;
        draw->AddLine(points[a], points[b], IM_COL32(0, 0, 0, 195), thickness + 1.4f);
        draw->AddLine(points[a], points[b], color, thickness);
    }
    if (joints) {
        for (std::size_t index = 0; index < CS2::kBoneSlotCount; ++index) {
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
        for (int x = -1; x <= 1; ++x) for (int y = -1; y <= 1; ++y)
            if (x || y) draw->AddText(ImVec2(pos.x + x * t, pos.y + y * t), outline, text);
    }
    draw->AddText(pos, color, text);
}

void DrawExtras(ImDrawList* draw, const CS2::Player& player, const ImVec2& head, const ImVec2& feet,
                const ImVec2& min, const ImVec2& max, const CS2::Config& settings, ImU32 rgb) {
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
        const float length = std::clamp(settings.look_direction_length, 20.f, 180.f) * scale;
        const ImVec2 end(head.x + std::cos(angle) * length, head.y + std::sin(angle) * length);
        draw->AddLine(head, end, EffectColor(settings, settings.col_look, rgb), std::clamp(settings.eye_line_thickness, .5f, 5.f));
    }
    if (settings.trails) {
        struct Point { ImVec2 pos; double time; };
        static std::unordered_map<uintptr_t, std::vector<Point>> history;
        auto& points = history[player.pawn];
        const double now = ImGui::GetTime();
        if (points.empty() || now - points.back().time > .04) points.push_back({feet, now});
        const double duration = std::clamp(static_cast<double>(settings.trail_duration), .2, 2.5);
        while (!points.empty() && now - points.front().time > duration) points.erase(points.begin());
        for (size_t i = 1; i < points.size(); ++i) {
            const float fade = static_cast<float>(1.0 - (now - points[i].time) / duration);
            ImU32 color = EffectColor(settings, settings.col_trail, rgb);
            color = (color & 0x00FFFFFFu) | (static_cast<ImU32>(std::clamp(fade, 0.f, 1.f) * 220.f) << 24);
            draw->AddLine(points[i - 1].pos, points[i].pos, color, std::clamp(settings.trail_thickness, 1.f, 7.f));
        }
    }
}

void DrawSoundEsp(ImDrawList* draw, const CS2::Player& player, const ImVec2& feet,
                  const CS2::Config& settings, ImU32 rgb) {
    if (!settings.sound_esp || !player.pawn) return;

    struct Ripple {
        uint64_t shot_ms = 0;
        ImVec2 position{};
        uint64_t last_seen_ms = 0;
    };
    static std::unordered_map<uintptr_t, Ripple> ripples;

    const uint64_t now = GetTickCount64();
    auto& ripple = ripples[player.pawn];
    ripple.last_seen_ms = now;
    if (player.last_shot_ms > ripple.shot_ms) {
        ripple.shot_ms = player.last_shot_ms;
        ripple.position = feet;
    }

    constexpr float kLifetimeMs = 680.f;
    if (!ripple.shot_ms || now < ripple.shot_ms ||
        static_cast<float>(now - ripple.shot_ms) > kLifetimeMs)
        return;

    const float progress = static_cast<float>(now - ripple.shot_ms) / kLifetimeMs;
    const ImU32 base = EffectColor(settings, settings.col_fun_effects, rgb);
    for (int ring = 0; ring < 3; ++ring) {
        const float phase = progress - static_cast<float>(ring) * .18f;
        if (phase < 0.f || phase > 1.f) continue;
        const float alpha = (1.f - phase) * .82f;
        const float radius = 10.f + phase * 42.f;
        const ImU32 color = (base & 0x00FFFFFFu) |
            (static_cast<ImU32>(std::clamp(alpha * 255.f, 0.f, 255.f)) << 24);
        draw->AddCircle(ripple.position, radius, color, 28, 1.5f);
    }

    // A pawn can disappear after a disconnect or map change. Keep this tiny
    // presentation cache bounded even if that happens mid-animation.
    if (ripples.size() > 96) {
        for (auto it = ripples.begin(); it != ripples.end();) {
            if (now - it->second.last_seen_ms > 5000)
                it = ripples.erase(it);
            else
                ++it;
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

void DrawFootstepEsp(ImDrawList* draw, const CS2::Player& player, const ImVec2& feet,
                     const CS2::Config& settings, ImU32 /*rgb*/) {
    if (!settings.footstep_esp || !player.pawn) return;

    struct Step {
        uint64_t began_ms = 0;
        ImVec2 position{};
        bool left = false;
    };
    struct FootprintHistory {
        uint64_t last_step_ms = 0;
        uint64_t last_seen_ms = 0;
        bool next_left = false;
        std::vector<Step> steps;
    };
    static std::unordered_map<uintptr_t, FootprintHistory> footprints;

    const uint64_t now = GetTickCount64();
    auto& history = footprints[player.pawn];
    history.last_seen_ms = now;
    // The data collector treats every speed above stationary jitter as motion,
    // so this includes slow and silent walking as requested.
    if (player.is_moving && (now - history.last_step_ms >= 245)) {
        history.last_step_ms = now;
        history.next_left = !history.next_left;
        history.steps.push_back({now, feet, history.next_left});
    }

    constexpr float kLifetimeMs = 1000.f;
    while (!history.steps.empty() && now - history.steps.front().began_ms > kLifetimeMs)
        history.steps.erase(history.steps.begin());
    for (const Step& step : history.steps) {
        const float age = static_cast<float>(now - step.began_ms) / kLifetimeMs;
        const float fade = (1.f - age) * (1.f - age); // smooth one-second fade
        const ImU32 gold = IM_COL32(218, 165, 32, static_cast<int>(fade * 238.f));
        const float angle = player.view_yaw * 0.0174532925f + (step.left ? -.16f : .16f);
        DrawFootprint(draw, step.position, angle, 1.f + age * .10f, gold);
    }

    if (footprints.size() > 64) {
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
    auto& mutableSettings = const_cast<CS2::Config&>(settings);
    const auto& bomb = snapshot.bomb;
    DrawPersistentWindow("##cs2_bomb_timer", "BOMBA", mutableSettings.bomb_window_x,
        mutableSettings.bomb_window_y, ImVec2(30.f, 338.f), [&] {
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
    auto& mutableSettings = const_cast<CS2::Config&>(settings);
    DrawPersistentWindow("##cs2_spectators", "ESPECTADORES", mutableSettings.spectator_window_x,
        mutableSettings.spectator_window_y, ImVec2(30.f, 40.f), [&] {
            if (snapshot.spectators.empty()) {
                ImGui::TextDisabled("Ninguem a observar");
                return;
            }
            for (const auto& spectator : snapshot.spectators)
                ImGui::BulletText("%s", spectator.name[0] ? spectator.name : "Jogador");
        });
}

const char* ProjectileLabel(CS2::ProjectileKind kind) {
    switch (kind) {
    case CS2::ProjectileKind::Flash: return "Flash";
    case CS2::ProjectileKind::Smoke: return "Smoke";
    case CS2::ProjectileKind::HE: return "HE Grenade";
    case CS2::ProjectileKind::Molotov: return "Molotov";
    case CS2::ProjectileKind::Incendiary: return "Incendiary";
    case CS2::ProjectileKind::Decoy: return "Decoy";
    default: return "";
    }
}

const char* ProjectileIcon(CS2::ProjectileKind kind) {
    switch (kind) {
    case CS2::ProjectileKind::Flash: return "FL";
    case CS2::ProjectileKind::Smoke: return "SM";
    case CS2::ProjectileKind::HE: return "HE";
    case CS2::ProjectileKind::Molotov: return "MO";
    case CS2::ProjectileKind::Incendiary: return "IN";
    case CS2::ProjectileKind::Decoy: return "DE";
    default: return "";
    }
}

CS2::Trajectory::GrenadePhysics ProjectilePhysics(CS2::ProjectileKind kind) {
    CS2::Trajectory::GrenadePhysics physics{};
    switch (kind) {
    case CS2::ProjectileKind::Flash:
    case CS2::ProjectileKind::HE: physics.detonate_seconds=1.5f; physics.restitution=.45f; break;
    case CS2::ProjectileKind::Smoke: physics.detonate_seconds=3.f; physics.restitution=.36f; physics.surface_friction=.64f; break;
    case CS2::ProjectileKind::Molotov:
    case CS2::ProjectileKind::Incendiary: physics.detonate_seconds=2.f; physics.restitution=.30f; physics.surface_friction=.58f; break;
    case CS2::ProjectileKind::Decoy: physics.detonate_seconds=2.f; physics.restitution=.42f; break;
    default: break;
    }
    return physics;
}

struct CachedProjectilePath {
    uint64_t sample_timestamp_ms{};
    CS2::Trajectory::TrajectoryResult result{};
};

float ProjectileEffectRadius(CS2::ProjectileKind kind) {
    // Preview radii are deliberately conservative visual guides, not damage
    // guarantees. They are drawn only for a trajectory that already has a
    // collision-geometry impact prediction.
    switch (kind) {
    case CS2::ProjectileKind::Flash: return 750.f;
    case CS2::ProjectileKind::HE: return 350.f;
    case CS2::ProjectileKind::Smoke: return 144.f;
    case CS2::ProjectileKind::Molotov:
    case CS2::ProjectileKind::Incendiary: return 160.f;
    case CS2::ProjectileKind::Decoy: return 128.f;
    default: return 0.f;
    }
}

void DrawProjectedEffectRadius(ImDrawList* draw, CS2::Trajectory::Vec3 center,
                               float radius, const float* view_matrix, ImU32 color) {
    if (radius <= 0.f) return;
    constexpr int segments = 24;
    ImVec2 previous{};
    bool have_previous = false;
    for (int i = 0; i <= segments; ++i) {
        const float angle = (static_cast<float>(i) / segments) * 6.283185307f;
        const float point[3]{ center.x + std::cos(angle) * radius,
                              center.y + std::sin(angle) * radius, center.z + 2.f };
        ImVec2 screen{};
        if (WorldToScreen(point, view_matrix, screen)) {
            if (have_previous) draw->AddLine(previous, screen, color, 1.15f);
            previous = screen;
            have_previous = true;
        } else {
            have_previous = false;
        }
    }
}

void DrawProjectilePath(ImDrawList* draw, const CS2::Projectile& projectile,
                        const float* view_matrix, ImU32 color) {
    static std::unordered_map<uintptr_t, CachedProjectilePath> paths;
    auto world = CS2::Trajectory::CollisionCache().WorldSnapshot();
    auto& cached = paths[projectile.entity];
    if (cached.sample_timestamp_ms != projectile.sample_timestamp_ms) {
        cached.sample_timestamp_ms = projectile.sample_timestamp_ms;
        const CS2::Trajectory::Vec3 origin{ projectile.pos[0], projectile.pos[1], projectile.pos[2] };
        const CS2::Trajectory::Vec3 velocity{ projectile.velocity[0], projectile.velocity[1], projectile.velocity[2] };
        const auto physics = ProjectilePhysics(projectile.kind);
        cached.result = world && world->Ready()
            ? CS2::Trajectory::SimulateGrenade(*world, origin, velocity, physics)
            : CS2::Trajectory::SimulateBallistic(origin, velocity, physics);
    }
    ImVec2 previous{};
    bool have_previous = false;
    for (const auto& point : cached.result.points) {
        const float world_point[3]{ point.x, point.y, point.z };
        ImVec2 screen{};
        if (WorldToScreen(world_point, view_matrix, screen)) {
            if (have_previous) draw->AddLine(previous, screen, color, 1.5f);
            previous = screen;
            have_previous = true;
        } else {
            have_previous = false;
        }
    }
    if (paths.size() > 64) {
        for (auto it = paths.begin(); it != paths.end();) {
            if (it->second.sample_timestamp_ms + 1000 < projectile.sample_timestamp_ms)
                it = paths.erase(it);
            else ++it;
        }
    }
}

void DrawProjectiles(ImDrawList* draw, const CS2::Runtime& snapshot,
                     const CS2::Config& settings, const float* view_matrix, ImU32 rgb) {
    if ((!settings.grenade_trail && !settings.projectile_timers) || !view_matrix) return;
    const ImU32 color = EffectColor(settings, settings.col_fun_effects, rgb);
    for (const auto& projectile : snapshot.projectiles) {
        const char* label = ProjectileLabel(projectile.kind);
        const char* icon = ProjectileIcon(projectile.kind);
        if (!*label || !*icon) continue;
        // A held grenade is attached close to its owner. Once thrown it moves
        // away from every player, so hide the planning line immediately rather
        // than leaving a second line following the projectile in flight.
        const float dx = projectile.pos[0] - snapshot.local_pos[0];
        const float dy = projectile.pos[1] - snapshot.local_pos[1];
        const float dz = projectile.pos[2] - snapshot.local_pos[2];
        const bool held_by_local = dx * dx + dy * dy + dz * dz < 160.f * 160.f;
        if (settings.grenade_trail && held_by_local) DrawProjectilePath(draw, projectile, view_matrix, color);
        if ((settings.grenade_trail || settings.projectile_timers) && projectile.world_effect_active) {
            ImVec2 effect_screen{};
            if (WorldToScreen(projectile.pos, view_matrix, effect_screen)) {
                char timer[32]{};
                const uint64_t now = GetTickCount64();
                const float remaining = projectile.world_effect_expires_ms > now
                    ? static_cast<float>(projectile.world_effect_expires_ms - now) / 1000.f : 0.f;
                std::snprintf(timer, sizeof(timer), "%.1fs", remaining);
                DrawOutlinedText(draw, ImVec2(effect_screen.x + 12.f, effect_screen.y - 7.f), color, timer, settings);
                DrawProjectedEffectRadius(draw, { projectile.pos[0], projectile.pos[1], projectile.pos[2] },
                    ProjectileEffectRadius(projectile.kind), view_matrix, color);
            }
        }
    }
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
    constexpr float kMaxDistanceUnits = 500.f * 39.37f;
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
    const bool useVisibilityColors = settings.visibility_colors && settings.visible_check;
    const float maxDistance = settings.max_distance;
    const ImU32 rgbColor = settings.rgb_mode ? RgbColor() : 0;
    // The entity snapshot is intentionally lower-rate and coherent. The view
    // matrix has its own fast lane, so use its newest published value here to
    // keep ESP attached while the local player turns the camera.
    const auto camera = AcquireCameraSnapshot();
    const float* viewMatrix = camera && camera->timestamp_ms
        ? camera->view_matrix : snapshot.view_matrix;
    const auto liveness = AcquireLivenessSnapshot();
    const auto diedSinceSnapshot = [&](uintptr_t pawn) {
        if (!liveness || liveness->timestamp_ms <= snapshot.snapshot_timestamp_ms || !pawn)
            return false;
        for (uint32_t index = 0; index < liveness->count; ++index) {
            const LivenessSample& sample = liveness->players[index];
            if (sample.pawn == pawn)
                return !sample.alive;
        }
        return false;
    };
    const uint64_t nowMs = GetTickCount64();
    const uint64_t snapshotAgeMs = snapshot.snapshot_timestamp_ms && nowMs >= snapshot.snapshot_timestamp_ms
        ? (std::min)(nowMs - snapshot.snapshot_timestamp_ms, uint64_t{28}) : 0;
    const float predictionSeconds = static_cast<float>(snapshotAgeMs) / 1000.f;
    for (const Player& player : snapshot.players) {
        if (!player.alive || player.is_local) continue;
        if (diedSinceSnapshot(player.pawn)) continue;
        if (hideTeam && player.team == snapshot.local_team) continue;
        if (!std::isfinite(player.distance) || (maxDistance > 0.f && player.distance > maxDistance)) continue;
        if (!std::isfinite(player.pos[0]) || !std::isfinite(player.pos[1]) || !std::isfinite(player.pos[2])) continue;

        const float positionOffset[3] = {
            std::isfinite(player.velocity[0]) ? player.velocity[0] * predictionSeconds : 0.f,
            std::isfinite(player.velocity[1]) ? player.velocity[1] * predictionSeconds : 0.f,
            std::isfinite(player.velocity[2]) ? player.velocity[2] * predictionSeconds : 0.f
        };
        const float predictedPosition[3] = {
            player.pos[0] + positionOffset[0], player.pos[1] + positionOffset[1], player.pos[2] + positionOffset[2]
        };
        ImVec2 feet{}, head{};
        if (!WorldToScreen(predictedPosition, viewMatrix, feet)) continue;
        float headWorld[3] = { predictedPosition[0], predictedPosition[1], predictedPosition[2] + 72.f };
        if (!WorldToScreen(headWorld, viewMatrix, head)) continue;

        const float height = feet.y - head.y;
        if (!std::isfinite(height) || height < 8.f || height > 4000.f) continue;
        const float width = height * 0.60f;
        const ImVec2 min(feet.x - width * .5f, head.y - height * .08f);
        const ImVec2 max(feet.x + width * .5f, min.y + height * 1.09f);
        const float thickness = std::clamp(settings.box_thickness, 0.5f, 5.f);
        if (settings.box || settings.box_corner) {
            draw->AddRect(ImVec2(min.x - 1.f, min.y - 1.f), ImVec2(max.x + 1.f, max.y + 1.f),
                          IM_COL32(0, 0, 0, 150), 0.f, 0, thickness + 1.f);
            const float* elementColor = settings.box_corner ? settings.col_box_corner : settings.col_box;
            const ImU32 color = settings.rgb_mode ? rgbColor
                : useVisibilityColors ? Color(player.spotted ? settings.col_visible : settings.col_occluded)
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
                : useVisibilityColors ? Color(player.spotted ? settings.col_visible : settings.col_occluded)
                : Color(settings.col_skeleton);
            DrawSkeleton(draw, player, viewMatrix, positionOffset, skeletonColor, Color(settings.col_joints),
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
        DrawExtras(draw, player, head, feet, min, max, settings, rgbColor);
        DrawSoundEsp(draw, player, feet, settings, rgbColor);
        DrawFootstepEsp(draw, player, feet, settings, rgbColor);
        DrawPlayerFlags(draw, player, min, settings, rgbColor);
    }
    DrawDroppedWeapons(draw, snapshot, settings, viewMatrix);
    DrawProjectiles(draw, snapshot, settings, viewMatrix, rgbColor);
}

} // namespace CS2::ESP
