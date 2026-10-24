#include "rust_esp.h"
#include "rust_game.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace Rust::ESP {
namespace {

bool WorldToScreen(const float world[3], const float matrix[16], ImVec2& screen) {
    const float w = matrix[3] * world[0] + matrix[7] * world[1] +
                    matrix[11] * world[2] + matrix[15];
    if (!std::isfinite(w) || w <= 0.001f) return false;
    const float x = (matrix[0] * world[0] + matrix[4] * world[1] +
                     matrix[8] * world[2] + matrix[12]) / w;
    const float y = (matrix[1] * world[0] + matrix[5] * world[1] +
                     matrix[9] * world[2] + matrix[13]) / w;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    screen = ImVec2(display.x * 0.5f * (1.0f + x),
                    display.y * 0.5f * (1.0f - y));
    return std::isfinite(screen.x) && std::isfinite(screen.y);
}

ImU32 Color(const float value[4]) {
    return ImGui::ColorConvertFloat4ToU32(
        ImVec4(value[0], value[1], value[2], value[3]));
}

}

void Draw(const Runtime& snapshot, const Config& settings) {
    if (!settings.esp_enabled || !settings.box_2d || !snapshot.camera_ok ||
        !snapshot.entity_source_ok || !snapshot.acquisition_ok)
        return;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (!draw) return;
    for (const Player& player : snapshot.players) {
        if (!player.alive || player.local || player.distance > settings.max_distance)
            continue;
        ImVec2 feet{}, head{};
        if (!WorldToScreen(player.feet, snapshot.view_matrix, feet) ||
            !WorldToScreen(player.head, snapshot.view_matrix, head))
            continue;
        const float height = std::fabs(feet.y - head.y);
        if (height < 4.0f) continue;
        const float width = height * 0.48f;
        const ImVec2 minimum(head.x - width * 0.5f, head.y);
        const ImVec2 maximum(head.x + width * 0.5f, feet.y);
        if (settings.box_fill)
            draw->AddRectFilled(minimum, maximum, Color(settings.box_fill_color),
                                settings.box_rounding);
        draw->AddRect(minimum, maximum, Color(settings.box_color),
                      settings.box_rounding, 0, settings.box_thickness);
    }
}

}
