#include "warzone_esp.h"
#include "warzone_aim.h"
#include "../src/gameplay/esp_core.h"
#include "imgui.h"
#include <Windows.h>
#include <cmath>
#include <cstdio>

namespace Warzone_ESP {
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

ImU32 Col4(const float c[4]) {
    return IM_COL32((int)(c[0]*255),(int)(c[1]*255),(int)(c[2]*255),(int)(c[3]*255));
}

static void DrawPlayerBox(ImDrawList* dl, const Warzone::Player& p, const Warzone::Config& cfg,
                          const float rt_view_matrix[16], float hx, float hy, float sx, float sy,
                          const float* bc) {
    (void)rt_view_matrix;
    (void)p;
    (void)sx;
    ImU32 col = Col4(bc);
    const float h = std::fabs(sy - hy);
    const float w = h * 0.45f;
    const float x0 = hx - w * 0.5f, x1 = hx + w * 0.5f;

    if (cfg.box) {
        if (cfg.box_corner)
            OmniGhost::Gameplay::EspCore::DrawCornerBox(dl, ImVec2(x0, hy), ImVec2(x1, sy), col, 1.5f);
        else
            dl->AddRect(ImVec2(x0, hy), ImVec2(x1, sy), col, 0.f, 0, 1.4f);
    }
    if (cfg.head_dot) dl->AddCircleFilled(ImVec2(hx, hy), 3.f, col);
    if (cfg.health_bar) {
        const float t = std::clamp(p.health / 100.f, 0.f, 1.f);
        dl->AddRectFilled(ImVec2(x0 - 5.f, sy), ImVec2(x0 - 2.f, hy), IM_COL32(40,40,40,180));
        dl->AddRectFilled(ImVec2(x0 - 5.f, sy - h * t), ImVec2(x0 - 2.f, sy), IM_COL32(65,220,90,220));
    }
    if (cfg.armor_bar && p.armor > 0.f) {
        const float t = std::clamp(p.armor / 100.f, 0.f, 1.f);
        ImU32 armorCol = Col4(cfg.col_armor);
        dl->AddRectFilled(ImVec2(x1 + 2.f, sy), ImVec2(x1 + 5.f, hy), IM_COL32(40,40,40,180));
        dl->AddRectFilled(ImVec2(x1 + 2.f, sy - h * t), ImVec2(x1 + 5.f, sy), armorCol);
    }
}

static void DrawPlayerSkeleton(ImDrawList* dl, const Warzone::Player& p, const Warzone::Config& cfg,
                               const Warzone::Runtime& rt, const float rt_view_matrix[16],
                               const float* bc, int aimIdx) {
    (void)bc;
    if (!cfg.skeleton || !p.bones_ok) return;

    ImU32 skelCol = Col4(cfg.col_skeleton);
    if (aimIdx >= 0 && aimIdx < (int)rt.players.size() && &p == &rt.players[aimIdx])
        skelCol = Col4(cfg.col_target);
    else if (cfg.team_check && rt.local_team != 0 && p.team == rt.local_team)
        skelCol = Col4(cfg.col_team);

    struct BoneLink { int from, to; };
    static const BoneLink links[] = {
        {7, 6}, {6, 5}, {5, 4}, {4, 3}, {3, 2},
        {6, 13}, {13, 14}, {14, 15}, {15, 16}, {16, 17},
        {6, 18}, {18, 19}, {19, 20}, {20, 21}, {21, 22},
        {2, 23}, {23, 24}, {24, 25}, {25, 26},
        {2, 27}, {27, 28}, {28, 29}, {29, 30},
    };

    for (const auto& link : links) {

        if ((p.bones_mask & (1u << link.from)) == 0) continue;
        if ((p.bones_mask & (1u << link.to)) == 0) continue;


        assert(p.bones_ok && "bones_ok must be true when drawing skeleton");

        float bx1, by1, bx2, by2;
        if (W2S(p.bones[link.from], rt_view_matrix, bx1, by1) &&
            W2S(p.bones[link.to], rt_view_matrix, bx2, by2)) {
            dl->AddLine(ImVec2(bx1, by1), ImVec2(bx2, by2), skelCol, 1.5f);
        }
    }
}

static void DrawPlayerNameDist(ImDrawList* dl, const Warzone::Player& p, const Warzone::Config& cfg,
                               const Warzone::Runtime& rt, const float rt_view_matrix[16],
                               float hx, float hy, float sx, float sy, int playerCount) {
    (void)rt;
    (void)rt_view_matrix;
    (void)sx;
    const bool dense = cfg.esp_auto_hide_names &&
        playerCount > (cfg.esp_density_limit > 0 ? cfg.esp_density_limit : 40);
    const bool show_name = cfg.name && !dense;
    const bool show_dist = cfg.distance;
    if (!show_name && !show_dist) return;

    const float h = std::fabs(sy - hy);
    const float x0 = hx - h * 0.45f * 0.5f;

    char line[96]{};
    if (show_name && show_dist)
        std::snprintf(line, sizeof(line), "%s [%.0fm]", p.name[0] ? p.name : "?", p.distance);
    else if (show_name)
        std::snprintf(line, sizeof(line), "%s", p.name[0] ? p.name : "?");
    else
        std::snprintf(line, sizeof(line), "%.0fm", p.distance);
    dl->AddText(ImVec2(x0, hy - 14.f), IM_COL32(255,255,255,220), line);
}

}

void Draw(const Warzone::Runtime& rt, const Warzone::Config& cfg) {
    if (!cfg.esp_enabled) return;
    if (!rt.matrix_ok && rt.players.empty()) return;
    const uint64_t now = GetTickCount64();
    if (rt.camera_sample_ms && now > rt.camera_sample_ms && (now - rt.camera_sample_ms) > 250u)
        return;
    if (rt.world_sample_ms && now > rt.world_sample_ms && (now - rt.world_sample_ms) > 400u)
        return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;

    const int aimIdx = Warzone_Aim::ActiveTargetIndex();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float cx = ds.x * 0.5f, cy = ds.y * 0.5f;
    const float* rt_view_matrix = rt.view_matrix;

    for (int i = 0; i < (int)rt.players.size(); ++i) {
        const auto& p = rt.players[i];
        if (!p.valid || !p.alive || p.is_local) continue;
        if (cfg.ignore_downed && p.downed) continue;
        if (cfg.ignore_ai && p.ai) continue;
        if (cfg.team_check && rt.local_team != 0 && p.team == rt.local_team) continue;
        if (p.distance > (float)cfg.max_distance) continue;

        float sx, sy, hx, hy;
        if (!W2S(p.pos, rt_view_matrix, sx, sy)) continue;
        if (!W2S(p.head, rt_view_matrix, hx, hy)) { hx = sx; hy = sy - 40.f; }
        const float h = std::fabs(sy - hy);
        if (h < 4.f) continue;

        const float* bc = (i == aimIdx) ? cfg.col_target : cfg.col_enemy;

        DrawPlayerBox(dl, p, cfg, rt_view_matrix, hx, hy, sx, sy, bc);
        DrawPlayerSkeleton(dl, p, cfg, rt, rt_view_matrix, bc, aimIdx);
        DrawPlayerNameDist(dl, p, cfg, rt, rt_view_matrix, hx, hy, sx, sy, (int)rt.players.size());

        if (cfg.snaplines) {
            dl->AddLine(ImVec2(cx, cy), ImVec2(sx, sy), IM_COL32(255,255,255,90), 1.f);
        }
    }
}

}
