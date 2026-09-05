#include "rust_aim.h"
#include "rust_game.h"
#include "rust_config.h"
#include "../src/gameplay/aim_controller.h"
#include "aimbot/aim_type.h"
#include "../src/makcu/makcu_wrapper.h"
#include "imgui.h"
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace Rust_Aim {
namespace {

OmniGhost::Gameplay::ContinuousAimController g_aim_motion;
int g_active_target = -1;
int g_last_target = -1;
char g_debug[128] = "aim idle";

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

void MoveMouse(int dx, int dy) {
    if (makcu_wrapper::IsConnected()) {
        while (dx != 0 || dy != 0) {
            int sx = dx; if (sx > 127) sx = 127; if (sx < -127) sx = -127;
            int sy = dy; if (sy > 127) sy = 127; if (sy < -127) sy = -127;
            aim_type::Move(sx, sy);
            dx -= sx; dy -= sy;
        }
        return;
    }
    aim_type::Move(dx, dy);
}

bool KeyDown(int vk) {
    if (vk <= 0) return false;
    return aim_type::IsDown(vk) || ((GetKeyState(vk) & 0x8000) != 0);
}

bool PassAimFilters(const Rust::Player& p, const Rust::Config& cfg) {
    if (!p.valid) return false;
    if (p.is_local) return false;
    if (p.health <= 0.f) return false;
    if (p.destroyed) return false;
    if (cfg.aim_ignore_sleepers && p.sleeping) return false;
    if (cfg.aim_ignore_npc && p.npc) return false;
    if (cfg.aim_ignore_wounded && p.wounded) return false;
    if (cfg.aim_ignore_knocked && p.wounded && p.health <= 15.f) return false;
    if (cfg.aim_ignore_team && cfg.team_check && p.team_id != 0 &&
        p.team_id == Rust::runtime.local_team && Rust::runtime.local_team != 0)
        return false;
    if (p.distance > cfg.aim_max_dist) return false;
    return true;
}

} // namespace

int ActiveTargetIndex() { return g_active_target; }
const char* DebugStatus() { return g_debug; }

void Click() {
    aim_type::LeftClick();
    aim_type::LeftClickRelease();
}

void Run(const Rust::Runtime& rt, const Rust::Config& cfg) {
    if (!rt.in_game) {
        g_active_target = -1;
        g_aim_motion.Reset();
        std::snprintf(g_debug, sizeof(g_debug), "sem partida");
        return;
    }
    if (!cfg.aim_enabled && !cfg.trigger_enabled) {
        g_active_target = -1;
        g_aim_motion.Reset();
        std::snprintf(g_debug, sizeof(g_debug), "aim/trig OFF");
        return;
    }

    const ImVec2 ds0 = ImGui::GetIO().DisplaySize;
    const float cx0 = ds0.x * 0.5f;
    const float cy0 = ds0.y * 0.5f;

    // Triggerbot: real hardware click when head is under crosshair
    if (cfg.trigger_enabled && KeyDown(cfg.trigger_bind)) {
        static auto lastShot = std::chrono::steady_clock::now();
        bool hit = false;
        for (const auto& p : rt.players) {
            if (!PassAimFilters(p, cfg)) continue;
            float sx, sy;
            if (!W2S(p.head, rt.view_matrix, sx, sy)) continue;
            const float d = std::sqrt((sx - cx0) * (sx - cx0) + (sy - cy0) * (sy - cy0));
            if (d < 12.f) { hit = true; break; }
        }
        if (hit) {
            const auto now = std::chrono::steady_clock::now();
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastShot).count();
            if (ms >= cfg.trigger_delay_ms) {
                Click();
                lastShot = now;
                std::snprintf(g_debug, sizeof(g_debug), "trigger CLICK");
            }
        }
    }

    if (!cfg.aim_enabled) {
        g_active_target = -1;
        g_aim_motion.Reset();
        return;
    }

    // Require a real bind — never always-on
    bool key = false;
    if (cfg.aim_bind > 0)
        key = KeyDown(cfg.aim_bind);
    if (!key && cfg.aim_bind2 > 0)
        key = KeyDown(cfg.aim_bind2);
    if (!key && cfg.aim_bind3 > 0)
        key = KeyDown(cfg.aim_bind3);
    if (!key) {
        g_active_target = -1;
        g_last_target = -1;
        g_aim_motion.Reset();
        std::snprintf(g_debug, sizeof(g_debug), "sem bind | %s | VK=%d",
            aim_type::StatusText(), cfg.aim_bind);
        return;
    }

    const bool override_held = cfg.aim_override_key > 0 && KeyDown(cfg.aim_override_key);
    float active_smooth = override_held ? cfg.aim_override_smooth : cfg.aim_smooth;
    float active_fov = override_held ? cfg.aim_override_fov : cfg.aim_fov;
    if (active_smooth < 0.f) active_smooth = 0.f;
    if (active_smooth > 100.f) active_smooth = 100.f;
    if (active_fov < 5.f) active_fov = 5.f;

    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float cx = ds.x * 0.5f;
    const float cy = ds.y * 0.5f;

    static int sticky_index = -1;
    static std::chrono::steady_clock::time_point sticky_until = std::chrono::steady_clock::now();
    static std::chrono::steady_clock::time_point last_move = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();

    // Optional aim interval (blurred-style) — still continuous when 0/low
    if (cfg.aim_interval_ms > 1.f) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_move).count();
        if (elapsed < (int)cfg.aim_interval_ms && g_active_target >= 0) {
            // Keep sticky target but skip a move this frame (smooth hold)
            if (cfg.aim_draw_fov) {
                ImDrawList* dl = ImGui::GetBackgroundDrawList();
                if (dl) {
                    dl->AddCircle(ImVec2(cx, cy), active_fov,
                        IM_COL32(
                            (int)(cfg.col_fov[0] * 255), (int)(cfg.col_fov[1] * 255),
                            (int)(cfg.col_fov[2] * 255), (int)(cfg.col_fov[3] * 255)),
                        64, 1.2f);
                }
            }
            return;
        }
    }

    float bestDist = 1e9f;
    float bestSX = 0.f, bestSY = 0.f;
    int bestIdx = -1;
    bool found = false;

    auto evaluate = [&](const Rust::Player& p, int idx) {
        if (!PassAimFilters(p, cfg)) return;

        float target[3] = { p.head[0], p.head[1], p.head[2] };
        int bone = cfg.aim_bone;
        if (bone == 3) // random stable per target address
            bone = (int)((p.address >> 4) % 3u);

        switch (bone) {
        case 1: // chest
            target[0] = p.chest[0]; target[1] = p.chest[1]; target[2] = p.chest[2];
            break;
        case 2: // pelvis / body
            target[0] = p.pos[0]; target[1] = p.pos[1] + 0.82f; target[2] = p.pos[2];
            break;
        default:
            break;
        }

        // Soft bone transitions: blend a little toward previous bone when enabled
        if (cfg.aim_bone_transitions && p.bones_ok && p.bone_count >= 3) {
            // slight blend head↔chest reduces pop when bone index changes
            if (bone == 0 && p.chest[0] != 0.f) {
                target[0] = target[0] * 0.92f + p.chest[0] * 0.08f;
                target[1] = target[1] * 0.92f + p.chest[1] * 0.08f;
                target[2] = target[2] * 0.92f + p.chest[2] * 0.08f;
            }
        }

        if (cfg.aim_prediction) {
            const float lead = std::clamp(cfg.prediction_strength, 0.f, 2.f) *
                               std::clamp(p.distance / 120.f, 0.15f, 1.f) * 0.10f;
            target[0] += p.velocity[0] * lead;
            target[1] += p.velocity[1] * lead;
            target[2] += p.velocity[2] * lead;
        }

        float sx, sy;
        if (!W2S(target, rt.view_matrix, sx, sy)) {
            if (!W2S(p.pos, rt.view_matrix, sx, sy)) return;
        }
        const float d = std::sqrt((sx - cx) * (sx - cx) + (sy - cy) * (sy - cy));
        float fov = active_fov > 1.f ? active_fov : 90.f;
        if (idx == sticky_index && sticky_index >= 0)
            fov *= 1.16f; // sticky hysteresis — avoids target thrash
        if (d > fov) return;
        if (d < bestDist) {
            bestDist = d;
            bestSX = sx;
            bestSY = sy;
            bestIdx = idx;
            found = true;
        }
    };

    if (cfg.sticky_ms > 0.f && sticky_index >= 0 && now < sticky_until &&
        sticky_index < (int)rt.players.size())
        evaluate(rt.players[sticky_index], sticky_index);

    for (int i = 0; i < (int)rt.players.size(); ++i)
        evaluate(rt.players[i], i);

    if (!found) {
        sticky_index = -1;
        g_active_target = -1;
        g_aim_motion.Reset();
        std::snprintf(g_debug, sizeof(g_debug), "sem alvo FOV p=%d", (int)rt.players.size());
        if (cfg.aim_draw_fov) {
            ImDrawList* dl = ImGui::GetBackgroundDrawList();
            if (dl) {
                dl->AddCircle(ImVec2(cx, cy), active_fov,
                    IM_COL32(
                        (int)(cfg.col_fov[0] * 255), (int)(cfg.col_fov[1] * 255),
                        (int)(cfg.col_fov[2] * 255), (int)(cfg.col_fov[3] * 255)),
                    64, 1.2f);
            }
        }
        return;
    }

    g_last_target = bestIdx;
    sticky_index = bestIdx;
    {
        const int sticky_ms_i = (int)((cfg.sticky_ms > 0.f) ? cfg.sticky_ms : 0.f);
        sticky_until = now + std::chrono::milliseconds(sticky_ms_i);
    }
    g_active_target = bestIdx;

    g_aim_motion.SetTarget(static_cast<std::uint64_t>(rt.players[bestIdx].address));
    OmniGhost::Gameplay::AimMotionSettings motion_settings{};
    motion_settings.smooth = active_smooth;
    motion_settings.humanize = cfg.aim_humanize;
    motion_settings.deadzone = cfg.aim_deadzone;
    motion_settings.minimum_strength = 0.02f;
    OmniGhost::Gameplay::ApplyStableDistanceProfile(
        motion_settings, rt.players[bestIdx].distance);
    const auto motion = g_aim_motion.Step(bestSX - cx, bestSY - cy, motion_settings);
    last_move = now;

    if (!motion) {
        std::snprintf(g_debug, sizeof(g_debug), "locked d=%.1f tgt=%d", motion.error, bestIdx);
    } else {
        MoveMouse(motion.x, motion.y);
        std::snprintf(g_debug, sizeof(g_debug), "pull %+d,%+d d=%.0f sm=%.0f tgt=%d",
            motion.x, motion.y, motion.error, active_smooth, bestIdx);
    }

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (dl) {
        if (cfg.aim_draw_fov) {
            dl->AddCircle(ImVec2(cx, cy), active_fov,
                IM_COL32(
                    (int)(cfg.col_fov[0] * 255), (int)(cfg.col_fov[1] * 255),
                    (int)(cfg.col_fov[2] * 255), (int)(cfg.col_fov[3] * 255)),
                64, 1.2f);
        }
        if (cfg.aim_draw_line) {
            dl->AddLine(ImVec2(cx, cy), ImVec2(bestSX, bestSY),
                IM_COL32(212, 175, 55, 120), 1.0f);
        }
        if (cfg.aim_draw_prediction) {
            const float r = (std::max)(1.f, cfg.prediction_point_size);
            dl->AddCircleFilled(ImVec2(bestSX, bestSY), r, IM_COL32(255, 210, 80, 200), 10);
        }
    }
}

} // namespace Rust_Aim
