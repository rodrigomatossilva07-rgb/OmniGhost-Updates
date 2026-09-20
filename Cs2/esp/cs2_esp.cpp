#include "cs2_esp.h"

#include "../cs2_game.h"
#include "../config/cs2_config.h"
#include "../../ImGui/imgui.h"

#include <algorithm>
#include <cmath>

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

} // namespace

namespace CS2::ESP {

void DrawPlayers(const Runtime& runtime, const Config& config) {
    if (!config.esp_enabled || !config.box || !runtime.in_match) return;

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    for (const Player& player : runtime.players) {
        if (!player.alive || player.is_local || player.team == runtime.local_team) continue;

        ImVec2 feet{}, head{};
        if (!WorldToScreen(player.pos, runtime.view_matrix, feet)) continue;
        float headWorld[3] = { player.pos[0], player.pos[1], player.pos[2] + 72.f };
        if (!WorldToScreen(headWorld, runtime.view_matrix, head)) continue;

        const float height = feet.y - head.y;
        if (!std::isfinite(height) || height < 8.f || height > 4000.f) continue;
        const float width = height * 0.60f;
        const ImVec2 min(feet.x - width * .5f, head.y - height * .08f);
        const ImVec2 max(feet.x + width * .5f, min.y + height * 1.09f);
        const float thickness = (std::max)(0.5f, config.box_thickness);
        draw->AddRect(ImVec2(min.x - 1.f, min.y - 1.f), ImVec2(max.x + 1.f, max.y + 1.f),
                      IM_COL32(0, 0, 0, 150), 0.f, 0, thickness + 1.f);
        draw->AddRect(min, max, Color(config.col_box), 0.f, 0, thickness);
    }
}

} // namespace CS2::ESP
