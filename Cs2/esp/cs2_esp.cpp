#include "cs2_esp.h"

#include "../cs2_game.h"
#include "../config/cs2_config.h"
#include "../../ImGui/imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
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
                  ImU32 color, ImU32 jointColor, float thickness, bool joints) {
    if (!player.full_bones_ok) return;

    std::array<ImVec2, CS2::kBoneSlotCount> points{};
    std::array<bool, CS2::kBoneSlotCount> valid{};
    for (std::size_t index = 0; index < CS2::kBoneSlotCount; ++index)
        valid[index] = WorldToScreen(player.bones[index], viewMatrix, points[index]);

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

} // namespace

namespace CS2::ESP {

void DrawPlayers(const Runtime& snapshot, const Config& settings) {
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
    for (const Player& player : snapshot.players) {
        if (!player.alive || player.is_local) continue;
        if (hideTeam && player.team == snapshot.local_team) continue;
        if (!std::isfinite(player.distance) || (maxDistance > 0.f && player.distance > maxDistance)) continue;
        if (!std::isfinite(player.pos[0]) || !std::isfinite(player.pos[1]) || !std::isfinite(player.pos[2])) continue;

        ImVec2 feet{}, head{};
        if (!WorldToScreen(player.pos, viewMatrix, feet)) continue;
        float headWorld[3] = { player.pos[0], player.pos[1], player.pos[2] + 72.f };
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
            if (settings.box_corner)
                DrawCornerBox(draw, min, max, color, thickness);
            else
                draw->AddRect(min, max, color, 0.f, 0, thickness);
        }
        if (settings.health_bar)
            DrawVerticalBar(draw, min.x - 7.f, min.y, max.y, static_cast<float>(player.health) / 100.f, Color(settings.col_health));
        if (settings.armor_bar)
            DrawVerticalBar(draw, max.x + 3.f, min.y, max.y, static_cast<float>(player.armor) / 100.f, Color(settings.col_armor));
        if (settings.skeleton) {
            const ImU32 skeletonColor = settings.rgb_mode ? rgbColor
                : useVisibilityColors ? Color(player.spotted ? settings.col_visible : settings.col_occluded)
                : Color(settings.col_skeleton);
            DrawSkeleton(draw, player, viewMatrix, skeletonColor, Color(settings.col_joints),
                         std::clamp(settings.skeleton_thickness, .5f, 4.f), settings.skeleton_joints);
        }
    }
}

} // namespace CS2::ESP
