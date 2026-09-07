#include "rust_esp.h"
#include "rust_aim.h"
#include "gameplay/esp_core.h"
#include "gameplay/esp_optimizer.h"
#include "../ImGui/imgui.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace Rust_ESP {
namespace {
bool W2S(const float world[3], const float vm[16], float& sx, float& sy) {
    const float w = vm[3]*world[0] + vm[7]*world[1] + vm[11]*world[2] + vm[15];
    if (w < 0.001f) return false;
    const float inv = 1.f / w;
    const float x = (vm[0]*world[0] + vm[4]*world[1] + vm[8]*world[2] + vm[12]) * inv;
    const float y = (vm[1]*world[0] + vm[5]*world[1] + vm[9]*world[2] + vm[13]) * inv;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    sx = (ds.x * 0.5f) * (1.f + x);
    sy = (ds.y * 0.5f) * (1.f - y);
    return std::isfinite(sx) && std::isfinite(sy);
}
ImU32 Col4(const float c[4]) {
    return IM_COL32((int)(c[0]*255),(int)(c[1]*255),(int)(c[2]*255),(int)(c[3]*255));
}
} // namespace

void Draw(const Rust::Runtime& rt, const Rust::Config& cfg) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!dl || !rt.in_game) return;

    // World ESP (independent of player ESP toggle)
    const bool anyWorld =
        cfg.world_esp || cfg.ore_esp || cfg.crate_esp || cfg.stash_esp || cfg.tc_esp ||
        cfg.turret_esp || cfg.vehicle_esp || cfg.animal_esp || cfg.airdrop_esp ||
        cfg.item_drops_esp || cfg.collectables_esp || cfg.corpse_esp;
    if (anyWorld && !rt.world_entities.empty()) {
        for (const auto& we : rt.world_entities) {
            if (!we.valid) continue;
            if (we.distance > (float)cfg.world_max_distance) continue;
            float sx = 0.f, sy = 0.f;
            if (!W2S(we.pos, rt.view_matrix, sx, sy)) continue;
            ImU32 col = IM_COL32(200, 200, 200, 230);
            switch (we.kind) {
            case Rust::WorldKind::Ore: col = IM_COL32(220, 180, 60, 240); break;
            case Rust::WorldKind::Crate: col = IM_COL32(80, 180, 255, 240); break;
            case Rust::WorldKind::Stash: col = IM_COL32(180, 100, 255, 240); break;
            case Rust::WorldKind::TC: col = IM_COL32(255, 120, 80, 240); break;
            case Rust::WorldKind::Turret: col = IM_COL32(255, 60, 60, 240); break;
            case Rust::WorldKind::Vehicle: col = IM_COL32(90, 220, 140, 240); break;
            case Rust::WorldKind::Animal: col = IM_COL32(200, 160, 90, 240); break;
            case Rust::WorldKind::AirDrop: col = IM_COL32(255, 220, 40, 250); break;
            case Rust::WorldKind::Collectable: col = IM_COL32(120, 220, 120, 230); break;
            case Rust::WorldKind::ItemDrop: col = IM_COL32(220, 220, 220, 230); break;
            case Rust::WorldKind::Corpse: col = IM_COL32(160, 80, 80, 230); break;
            default: break;
            }
            dl->AddCircleFilled(ImVec2(sx, sy), 3.0f, col);
            char line[80]{};
            if (cfg.world_show_name && cfg.world_show_distance)
                std::snprintf(line, sizeof(line), "%s [%.0fm]", we.name, we.distance);
            else if (cfg.world_show_name)
                std::snprintf(line, sizeof(line), "%s", we.name);
            else if (cfg.world_show_distance)
                std::snprintf(line, sizeof(line), "%.0fm", we.distance);
            if (line[0]) {
                ImVec2 ts = ImGui::CalcTextSize(line);
                dl->AddText(ImVec2(sx - ts.x * 0.5f + 1.f, sy + 5.f + 1.f), IM_COL32(0, 0, 0, 180), line);
                dl->AddText(ImVec2(sx - ts.x * 0.5f, sy + 5.f), col, line);
            }
        }
    }

    if (!cfg.esp_enabled) return;
    const int aimIdx = Rust_Aim::ActiveTargetIndex();
    int maxDist = cfg.performance_mode ? (std::min)(cfg.max_distance, 180) : cfg.max_distance;

    for (int i = 0; i < (int)rt.players.size(); ++i) {
        const auto& p = rt.players[i];
        if (!p.valid) continue;
        if (p.sleeping && !cfg.show_sleepers) continue;
        if (p.npc && !cfg.show_npc) continue;
        if (p.wounded && !cfg.show_wounded) continue;
        if (cfg.team_check && rt.local_team != 0 && p.team_id == rt.local_team) continue;
        if (p.distance > (float)maxDist) continue;

        float sx, sy, hx, hy;
        if (!W2S(p.pos, rt.view_matrix, sx, sy)) continue;
        if (!W2S(p.head, rt.view_matrix, hx, hy)) { hx=sx; hy=sy-40.f; }
        const float h = std::fabs(sy-hy), w = h*0.45f;
        if (h < 4.f) continue;

        const float* bc = cfg.col_enemy;
        if (p.sleeping) bc = cfg.col_sleeper;
        else if (p.npc) bc = cfg.col_npc;
        else if (p.wounded) bc = cfg.col_wounded;
        ImU32 col = Col4(bc);
        if (cfg.highlight_aim_target && i == aimIdx) col = Col4(cfg.col_target);

        const float x0 = hx - w*0.5f, x1 = hx + w*0.5f;
        if (cfg.box) {
            if (cfg.box_corner) OmniGhost::Gameplay::EspCore::DrawCornerBox(
                dl, ImVec2(x0, hy), ImVec2(x1, sy), col, 1.5f);
            else dl->AddRect(ImVec2(x0,hy), ImVec2(x1,sy), col, 0.f, 0, 1.4f);
        }
        if (cfg.head_dot) dl->AddCircleFilled(ImVec2(hx,hy), 3.f, col);
        if (cfg.skeleton) {
            ImU32 sc = Col4(cfg.col_skeleton);
            auto line = [&](ImVec2 a, ImVec2 b) {
                if (a.x == 0 && a.y == 0) return;
                if (b.x == 0 && b.y == 0) return;
                dl->AddLine(a, b, sc, 1.5f);
            };
            // Prefer real bone world positions when CachePlayers filled them
            if (p.bones_ok && p.bone_count >= 8) {
                ImVec2 sp[16]{};
                bool ok[16]{};
                for (int b = 0; b < p.bone_count && b < 16; ++b) {
                    float bx, by;
                    if (W2S(p.bones[b], rt.view_matrix, bx, by)) {
                        sp[b] = ImVec2(bx, by); ok[b] = true;
                    }
                }
                // Indices from FillPlayerBones layout
                auto L = [&](int a, int b) {
                    if (ok[a] && ok[b]) line(sp[a], sp[b]);
                };
                L(0,1); L(1,2); L(2,3);          // head-neck-chest-pelvis
                L(1,4); L(1,5);                    // shoulders
                L(4,6); L(5,7);                    // arms (if present)
                L(3,8); L(3,9);                    // hips
                L(8,10); L(9,11); L(10,12); L(11,13); // legs
                // fallback pairs for 10-bone proportion layout
                if (p.bone_count == 10) {
                    L(0,1); L(1,2); L(2,3);
                    L(1,4); L(1,5);
                    L(3,6); L(3,7);
                    L(6,8); L(7,9);
                }
                if (cfg.skeleton_joints) {
                    const ImU32 jc = IM_COL32(80, 220, 90, 255);
                    for (int b = 0; b < p.bone_count && b < 16; ++b) {
                        if (!ok[b]) continue;
                        dl->AddCircleFilled(sp[b], 2.4f, jc, 8);
                        dl->AddCircle(sp[b], 2.4f, IM_COL32(0,0,0,160), 8, 1.f);
                    }
                }
            } else {
                // Screen-space proportion fallback
                const float midY = hy + (sy - hy) * 0.35f;
                const float pelY = hy + (sy - hy) * 0.55f;
                const float shW = w * 0.55f;
                const float hipW = w * 0.35f;
                ImVec2 neck(hx, hy + h * 0.12f);
                ImVec2 chest(hx, midY);
                ImVec2 pelvis(hx, pelY);
                ImVec2 lsh(hx - shW, midY - 2.f), rsh(hx + shW, midY - 2.f);
                ImVec2 lhip(hx - hipW, pelY), rhip(hx + hipW, pelY);
                ImVec2 lft(sx - hipW * 0.8f, sy), rft(sx + hipW * 0.8f, sy);
                line(ImVec2(hx,hy), neck); line(neck, chest); line(chest, pelvis);
                line(neck, lsh); line(neck, rsh);
                line(pelvis, lhip); line(pelvis, rhip);
                line(lhip, lft); line(rhip, rft);
                if (cfg.skeleton_joints) {
                    const ImU32 jc = IM_COL32(80, 220, 90, 255);
                    for (ImVec2 pt : { ImVec2(hx,hy), neck, chest, pelvis, lsh, rsh, lhip, rhip, lft, rft }) {
                        dl->AddCircleFilled(pt, 2.4f, jc, 8);
                        dl->AddCircle(pt, 2.4f, IM_COL32(0,0,0,160), 8, 1.f);
                    }
                }
            }
        }
        if (cfg.snaplines) {
            const ImVec2 ds = ImGui::GetIO().DisplaySize;
            dl->AddLine(ImVec2(ds.x*0.5f, cfg.snaplines_center ? ds.y*0.5f : ds.y), ImVec2(sx,sy), col, 1.f);
        }
        if (cfg.health_bar) {
            float t = std::clamp(p.health/(p.max_health>1.f?p.max_health:100.f), 0.f, 1.f);
            OmniGhost::Gameplay::EspCore::DrawHealthBar(
                dl, ImVec2(x0 - 5.f, hy), ImVec2(x0 - 2.f, sy), t);
        }
        float textY = hy - 16.f;
        if (cfg.name && p.name[0]) {
            char line[96];
            if (cfg.show_team_id && p.team_id) std::snprintf(line,sizeof(line),"%s [%d]", p.name, p.team_id);
            else std::snprintf(line,sizeof(line),"%s", p.name);
            ImVec2 ts = ImGui::CalcTextSize(line);
            dl->AddText(ImVec2(hx-ts.x*0.5f+1, textY+1), IM_COL32(0,0,0,180), line);
            dl->AddText(ImVec2(hx-ts.x*0.5f, textY), Col4(cfg.col_name), line);
            textY -= 14.f;
        }
        if (cfg.show_flags) {
            char fl[48]{};
            size_t used = 0;
            auto append = [&](const char* s) {
                const int n = std::snprintf(fl + used, sizeof(fl) - used, "%s", s);
                if (n > 0) used = (std::min)(sizeof(fl) - 1, used + (size_t)n);
            };
            if (p.sleeping) append("SLEEP ");
            if (p.wounded) append("WOUND ");
            if (p.aiming) append("AIM ");
            if (fl[0]) {
                ImVec2 ts = ImGui::CalcTextSize(fl);
                dl->AddText(ImVec2(hx - ts.x * 0.5f, textY), IM_COL32(220, 200, 120, 220), fl);
                textY -= 12.f;
            }
        }
        if (cfg.distance) {
            char db[32]; std::snprintf(db,sizeof(db),"%.0fm", p.distance);
            ImVec2 ts=ImGui::CalcTextSize(db);
            dl->AddText(ImVec2(hx-ts.x*0.5f, sy+3.f), col, db);
        }
    }
}
} // namespace Rust_ESP
