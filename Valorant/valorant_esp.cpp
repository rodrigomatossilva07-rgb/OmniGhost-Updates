#include "valorant_esp.h"
#include "valorant_game.h"
#include "../ImGui/imgui.h"

#include <cmath>
#include <algorithm>

namespace Valorant {
namespace {

bool W2S(const float world[3], const float vm[16], float& sx, float& sy) {
    const float w = vm[3] * world[0] + vm[7] * world[1] + vm[11] * world[2] + vm[15];
    if (w < 0.001f) return false;
    const float inv = 1.f / w;
    const float x = (vm[0] * world[0] + vm[4] * world[1] + vm[8] * world[2] + vm[12]) * inv;
    const float y = (vm[1] * world[0] + vm[5] * world[1] + vm[9] * world[2] + vm[13]) * inv;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    sx = (ds.x * 0.5f) * (1.f + x);
    sy = (ds.y * 0.5f) * (1.f - y);
    return std::isfinite(sx) && std::isfinite(sy);
}

ImU32 Col4(const float c[4], float a = -1.f) {
    float aa = a >= 0.f ? a : c[3];
    return IM_COL32((int)(c[0] * 255), (int)(c[1] * 255), (int)(c[2] * 255), (int)(aa * 255));
}

void DrawCornerBox(ImDrawList* dl, float x0, float y0, float x1, float y1, ImU32 col, float t = 1.5f) {
    float w = x1 - x0, h = y1 - y0;
    float lx = w * 0.25f, ly = h * 0.25f;
    // TL
    dl->AddLine(ImVec2(x0, y0), ImVec2(x0 + lx, y0), col, t);
    dl->AddLine(ImVec2(x0, y0), ImVec2(x0, y0 + ly), col, t);
    // TR
    dl->AddLine(ImVec2(x1, y0), ImVec2(x1 - lx, y0), col, t);
    dl->AddLine(ImVec2(x1, y0), ImVec2(x1, y0 + ly), col, t);
    // BL
    dl->AddLine(ImVec2(x0, y1), ImVec2(x0 + lx, y1), col, t);
    dl->AddLine(ImVec2(x0, y1), ImVec2(x0, y1 - ly), col, t);
    // BR
    dl->AddLine(ImVec2(x1, y1), ImVec2(x1 - lx, y1), col, t);
    dl->AddLine(ImVec2(x1, y1), ImVec2(x1, y1 - ly), col, t);
}

} // namespace

void DrawESP() {
    if (!config.esp_enabled || !runtime.in_game)
        return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (!dl) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float cx = ds.x * 0.5f;

    for (const auto& p : runtime.players) {
        if (!p.alive) continue;
        if (p.is_local && !config.self_esp) continue;
        if (p.distance > config.max_distance) continue;

        float sx = 0, sy = 0, hx = 0, hy = 0;
        if (!W2S(p.pos, runtime.view_matrix, sx, sy)) continue;
        if (!W2S(p.head, runtime.view_matrix, hx, hy)) continue;

        const float h = fabsf(sy - hy) * 2.2f;
        const float w = h * 0.45f;
        const float x0 = hx - w * 0.5f;
        const float x1 = hx + w * 0.5f;
        const float y0 = hy - h * 0.15f;
        const float y1 = y0 + h;

        const bool enemy = !p.is_local && (runtime.local_team == 0 || p.team != runtime.local_team);
        const float* baseCol = enemy ? config.col_enemy : config.col_team;

        if (config.box)
            dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), Col4(config.col_box), 0.f, 0, 1.5f);
        if (config.box_corner)
            DrawCornerBox(dl, x0, y0, x1, y1, Col4(config.col_box));

        if (config.health_bar) {
            float t = (float)p.health / (float)(std::max)(1, p.max_health);
            t = std::clamp(t, 0.f, 1.f);
            const float bx = x0 - 6.f;
            dl->AddRectFilled(ImVec2(bx, y0), ImVec2(bx + 3.f, y1), IM_COL32(20, 20, 20, 180));
            dl->AddRectFilled(ImVec2(bx, y1 - h * t), ImVec2(bx + 3.f, y1), Col4(config.col_health));
        }

        if (config.head_dot)
            dl->AddCircleFilled(ImVec2(hx, hy), 3.f, Col4(baseCol));

        if (config.snaplines)
            dl->AddLine(ImVec2(cx, ds.y), ImVec2(sx, sy), Col4(config.col_snaplines), 1.2f);

        if (config.distance || config.name) {
            char line[64];
            if (config.distance && config.name)
                snprintf(line, sizeof(line), "%.0fm", p.distance / 100.f);
            else if (config.distance)
                snprintf(line, sizeof(line), "%.0fm", p.distance / 100.f);
            else
                snprintf(line, sizeof(line), "Jogador");
            dl->AddText(ImVec2(x0, y1 + 2.f), Col4(config.col_name), line);
        }

        // Simple vertical skeleton proxy when bones not fully resolved
        if (config.skeleton) {
            dl->AddLine(ImVec2(hx, hy), ImVec2(sx, sy), Col4(config.col_skeleton), 1.4f);
            if (config.skeleton_joints) {
                dl->AddCircleFilled(ImVec2(hx, hy), 2.2f, Col4(config.col_joints));
                dl->AddCircleFilled(ImVec2(sx, sy), 2.2f, Col4(config.col_joints));
            }
        }
    }
}

} // namespace Valorant
