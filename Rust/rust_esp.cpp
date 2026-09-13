#include "rust_esp.h"
#include "rust_entities.h"
#include "imgui.h"
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace Rust {
namespace ESP {
namespace {

bool WorldToScreen(const float m[16], const float pos[3], ImVec2& out, float sw, float sh) {
    const float w = m[12] * pos[0] + m[13] * pos[1] + m[14] * pos[2] + m[15];
    if (w < 0.001f) return false;
    const float inv = 1.f / w;
    const float x = m[0] * pos[0] + m[1] * pos[1] + m[2] * pos[2] + m[3];
    const float y = m[4] * pos[0] + m[5] * pos[1] + m[6] * pos[2] + m[7];
    out.x = (sw * 0.5f) * (1.f + x * inv);
    out.y = (sh * 0.5f) * (1.f - y * inv);
    return out.x > -200 && out.x < sw + 200 && out.y > -200 && out.y < sh + 200;
}

ImU32 Col4(const float c[4]) {
    return IM_COL32(int(c[0] * 255), int(c[1] * 255), int(c[2] * 255), int(c[3] * 255));
}

} // namespace

void Draw(const Runtime& rt, const Config& cfg) {
    if (!cfg.esp_enabled || !rt.attached || !rt.matrix_ok)
        return;

    const auto& players = Entities::Players();
    if (players.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    const float sw = io.DisplaySize.x;
    const float sh = io.DisplaySize.y;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;

    for (const auto& p : players) {
        if (!p.valid || p.is_local) continue;

        float feet[3] = { p.pos[0], p.pos[1], p.pos[2] };
        float head[3] = { p.head[0], p.head[1], p.head[2] };
        ImVec2 sf, shs;
        if (!WorldToScreen(rt.view_matrix, feet, sf, sw, sh)) continue;
        if (!WorldToScreen(rt.view_matrix, head, shs, sw, sh)) continue;

        float h = std::fabs(shs.y - sf.y);
        if (h < 4.f) h = 4.f;
        float w = h * 0.45f;
        ImVec2 boxMin(shs.x - w * 0.5f, shs.y);
        ImVec2 boxMax(shs.x + w * 0.5f, sf.y);

        if (cfg.esp_box) {
            dl->AddRect(boxMin, boxMax, Col4(cfg.col_box), 0.f, 0, 1.5f);
        }
        if (cfg.esp_name || cfg.esp_distance) {
            char line[96];
            if (cfg.esp_name && cfg.esp_distance)
                std::snprintf(line, sizeof(line), "%s [%.0fm]", p.name, p.distance);
            else if (cfg.esp_name)
                std::snprintf(line, sizeof(line), "%s", p.name);
            else
                std::snprintf(line, sizeof(line), "%.0fm", p.distance);
            dl->AddText(ImVec2(boxMin.x, boxMin.y - 14.f), Col4(cfg.col_name), line);
        }
        if (cfg.esp_health) {
            const float ratio = std::clamp(p.health / (p.max_health > 1.f ? p.max_health : 100.f), 0.f, 1.f);
            const float bh = boxMax.y - boxMin.y;
            ImVec2 a(boxMin.x - 5.f, boxMax.y - bh * ratio);
            ImVec2 b(boxMin.x - 3.f, boxMax.y);
            dl->AddRectFilled(ImVec2(a.x, boxMin.y), b, IM_COL32(0, 0, 0, 140));
            dl->AddRectFilled(a, b, IM_COL32(int((1.f - ratio) * 255), int(ratio * 255), 40, 220));
        }
    }
}

} // namespace ESP
} // namespace Rust
