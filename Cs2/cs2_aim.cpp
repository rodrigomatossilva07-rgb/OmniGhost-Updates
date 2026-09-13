#pragma warning(disable: 4100 4244)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "cs2_aim.h"
#include "../Fivem/aimbot/aim_type.h"
#include "gameplay/aim_controller.h"
#include "imgui.h"
#include <Windows.h>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstdio>

namespace CS2_Aim {
namespace {

int g_last_target_idx = -1;
int g_active_target_idx = -1;
std::chrono::steady_clock::time_point g_last_switch{};
std::chrono::steady_clock::time_point g_target_acquired{};
uintptr_t g_target_pawn = 0;
char g_debug[128] = "aim idle";
int g_frames_sem_bind = 0;
OmniGhost::Gameplay::ContinuousAimController g_aim_motion;

bool W2S(const float* world, const float* vm, float& sx, float& sy) {
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    if (ds.x < 10.f || ds.y < 10.f) return false;
    const float clipX = world[0] * vm[0]  + world[1] * vm[1]  + world[2] * vm[2]  + vm[3];
    const float clipY = world[0] * vm[4]  + world[1] * vm[5]  + world[2] * vm[6]  + vm[7];
    const float clipW = world[0] * vm[12] + world[1] * vm[13] + world[2] * vm[14] + vm[15];
    if (!std::isfinite(clipW) || clipW < 0.001f) return false;
    const float inv = 1.f / clipW;
    sx = (ds.x * 0.5f) + (0.5f * clipX * inv * ds.x);
    sy = (ds.y * 0.5f) - (0.5f * clipY * inv * ds.y);
    return std::isfinite(sx) && std::isfinite(sy);
}

void PickBone(const CS2::Player& p, int bone, float out[3]) {
    const float* selected = p.head;
    if (p.bones_ok) {
        switch (bone) {
        case 1: selected = p.bones[1]; break; // neck
        case 2: selected = p.bones[2]; break; // chest / upper spine
        case 3: selected = p.bones[5]; break; // pelvis
        case 4: // stable midpoint between both knees
            out[0] = (p.bones[15][0] + p.bones[18][0]) * 0.5f;
            out[1] = (p.bones[15][1] + p.bones[18][1]) * 0.5f;
            out[2] = (p.bones[15][2] + p.bones[18][2]) * 0.5f;
            return;
        default: selected = p.bones[0]; break; // head
        }
    }
    out[0] = selected[0];
    out[1] = selected[1];
    out[2] = selected[2];
}

// Makcu only accepts -127..127 per packet — split larger moves in one frame.
void MoveMouse(int dx, int dy) {
    if (dx == 0 && dy == 0) return;

    int left_x = dx;
    int left_y = dy;
    // Up to 12 packets ≈ ±1524 px in a single frame (enough for full-screen snap)
    for (int n = 0; n < 12 && (left_x != 0 || left_y != 0); ++n) {
        int sx = left_x; if (sx > 127) sx = 127; if (sx < -127) sx = -127;
        int sy = left_y; if (sy > 127) sy = 127; if (sy < -127) sy = -127;
        if (sx == 0 && sy == 0) break;
        aim_type::Move(sx, sy);
        left_x -= sx;
        left_y -= sy;
    }
}

void Click() {
    aim_type::LeftClick();
    aim_type::LeftClickRelease();
}

bool KeyDown(int vk) {
    if (vk <= 0) return false;
    return aim_type::IsDown(vk) || ((GetKeyState(vk) & 0x8000) != 0);
}

bool AimKeyDown(const CS2::Config& cfg) {
    // Require a real bind — never "always on"
    bool down = false;
    if (cfg.aim_bind > 0)
        down = KeyDown(cfg.aim_bind);
    if (!down && cfg.aim_bind2 > 0)
        down = KeyDown(cfg.aim_bind2);
    return down;
}

bool IsFirearm(int definition) {
    switch (definition) {
    case 1: case 2: case 3: case 4:
    case 7: case 8: case 9: case 10: case 11:
    case 13: case 14: case 16: case 17: case 19:
    case 23: case 24: case 25: case 26: case 27:
    case 28: case 29: case 30: case 32: case 33:
    case 34: case 35: case 36: case 38: case 39:
    case 40: case 60: case 61: case 63: case 64:
        return true;
    default:
        // Includes knives, Zeus, grenades, C4 and unknown/new utility items.
        return false;
    }
}

float EffectiveFov(const CS2::Config& cfg, float distance_m) {
    float fov = cfg.aim_fov > 1.f ? cfg.aim_fov : 80.f;
    if (!cfg.aim_dynamic_fov) return fov;
    const float t = std::clamp(distance_m / 40.f, 0.f, 1.f);
    const float minF = cfg.aim_fov_min > 5.f ? cfg.aim_fov_min : 20.f;
    return minF + (fov - minF) * t;
}

struct TargetCandidate {
    int index = -1;
    uintptr_t pawn = 0;
    float error_x = 0.f;
    float error_y = 0.f;
    float screen_distance = FLT_MAX;
    float distance_m = 0.f;
    float score = FLT_MAX;
};

float WeaponThreat(int definition) noexcept {
    switch (definition) {
    case 9: case 11: case 38: case 40: return 1.f;       // precision rifles
    case 7: case 8: case 10: case 13: case 16: case 39: case 60: return .75f;
    case 1: case 2: case 3: case 4: case 30: case 32: case 36: case 61: case 63: case 64: return .45f;
    default: return .2f;
    }
}

TargetCandidate SelectTarget(const CS2::Runtime& rt, const CS2::Config& cfg,
                             float cx, float cy) {
    TargetCandidate best{};
    const uint64_t now_ms = GetTickCount64();
    const float snapshot_age = rt.snapshot_timestamp_ms && now_ms > rt.snapshot_timestamp_ms
        ? std::clamp(static_cast<float>(now_ms - rt.snapshot_timestamp_ms) * .001f, 0.f, .050f)
        : 0.f;

    for (std::size_t i = 0; i < rt.players.size(); ++i) {
        const auto& player = rt.players[i];
        if (player.is_local || !player.alive || player.health <= 0) continue;
        if (cfg.aim_ignore_team && player.team == rt.local_team && rt.local_team >= 2) continue;
        if (cfg.aim_ignore_bots && player.is_bot) continue;
        if (cfg.aim_ignore_spectators && player.is_spectator) continue;
        if (cfg.visible_check && !player.spotted) continue;
        if (player.distance > cfg.aim_max_dist) continue;

        float point[3]{};
        PickBone(player, cfg.aim_bone, point);
        if (cfg.aim_prediction) {
            // Hitscan CS2 needs only enough lead to compensate acquisition and
            // presentation age.  Keep it short to prevent overshoot.
            const float lead = std::clamp((.006f + snapshot_age) * cfg.prediction_strength,
                                          0.f, .045f);
            point[0] += player.velocity[0] * lead;
            point[1] += player.velocity[1] * lead;
            point[2] += player.velocity[2] * lead;
        }

        float sx = 0.f, sy = 0.f;
        if (!W2S(point, rt.view_matrix, sx, sy)) continue;
        const float dx = sx - cx;
        const float dy = sy - cy;
        const float screen = std::sqrt(dx * dx + dy * dy);
        if (screen > EffectiveFov(cfg, player.distance)) continue;

        // Screen alignment dominates. Distance, visibility, remaining health
        // and weapon threat only break close choices instead of pulling aim
        // away from the crosshair.
        float score = screen;
        score += std::clamp(player.distance, 0.f, 300.f) * .018f;
        score += std::clamp(static_cast<float>(player.health), 0.f, 100.f) * .008f;
        if (!player.spotted) score += 18.f;
        score -= WeaponThreat(player.weapon_def) * 2.f;
        if (player.pawn == g_target_pawn) score *= .78f;

        if (score < best.score) {
            best.index = static_cast<int>(i);
            best.pawn = player.pawn;
            best.error_x = dx;
            best.error_y = dy;
            best.screen_distance = screen;
            best.distance_m = player.distance;
            best.score = score;
        }
    }
    return best;
}

OmniGhost::Gameplay::AimMotionSettings BuildMotionSettings(
        const CS2::Config& cfg, float distance_m) noexcept {
    OmniGhost::Gameplay::AimMotionSettings settings{};
    settings.smooth = cfg.aim_smooth;
    settings.deadzone = cfg.aim_deadzone;
    settings.humanize = cfg.aim_humanize;
    settings.permanent_humanize = cfg.aim_humanize;
    settings.reaction_delay_ms_min = 30;
    settings.reaction_delay_ms_max = 80;
    settings.overshoot_px = .55f;
    settings.micro_jitter_px = .06f;
    settings.minimum_error = .35f;
    settings.prediction = false; // prediction is applied in world space above
    OmniGhost::Gameplay::ApplyStableDistanceProfile(settings, distance_m);
    return settings;
}

void HandlePanic(CS2::Config& cfg) {
    if (!cfg.panic_key_enabled || cfg.panic_key == 0) return;
    static bool was_down = false;
    const bool down = KeyDown(cfg.panic_key);
    if (down && !was_down) {
        cfg.aim_enabled = false;
        cfg.trigger_enabled = false;
        cfg.esp_enabled = false;
        cfg.radar_2d = false;
        cfg.bomb_timer = false;
        MessageBeep(MB_OK);
        std::snprintf(g_debug, sizeof(g_debug), "PANIC — tudo OFF");
    }
    was_down = down;
}

} // namespace (anonymous helpers)

int ActiveTargetIndex() { return g_active_target_idx; }
const char* DebugStatus() { return g_debug; }
void Run(const CS2::Runtime& rt, const CS2::Config& cfg_in) {
    CS2::Config& cfg = const_cast<CS2::Config&>(cfg_in);
    HandlePanic(cfg);

    const bool playable = rt.in_match || !rt.players.empty();
    if (!playable) {
        g_active_target_idx = -1;
        g_target_pawn = 0;
        g_aim_motion.Reset();
        std::snprintf(g_debug, sizeof(g_debug), "sem partida");
        return;
    }
    if (!cfg.aim_enabled && !cfg.trigger_enabled) {
        g_active_target_idx = -1;
        g_target_pawn = 0;
        g_aim_motion.Reset();
        std::snprintf(g_debug, sizeof(g_debug), "aim/trig OFF");
        return;
    }

    int local_weapon_definition = 0;
    for (const auto& player : rt.players) {
        if (player.is_local) {
            local_weapon_definition = player.weapon_def;
            break;
        }
    }
    if (!IsFirearm(local_weapon_definition)) {
        g_active_target_idx = -1;
        g_last_target_idx = -1;
        g_target_pawn = 0;
        g_aim_motion.Reset();
        std::snprintf(g_debug, sizeof(g_debug), "aim bloqueado: utilitario (%d)",
            local_weapon_definition);
        return;
    }

    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    if (ds.x < 10.f || ds.y < 10.f) {
        std::snprintf(g_debug, sizeof(g_debug), "display invalido");
        return;
    }

    const float cx = ds.x * 0.5f;
    const float cy = ds.y * 0.5f;
    const bool aim_key_down = cfg.aim_enabled && AimKeyDown(cfg);
    TargetCandidate target{};
    if (aim_key_down) target = SelectTarget(rt, cfg, cx, cy);

    if (!aim_key_down) {
        g_active_target_idx = -1;
        g_last_target_idx = -1;
        g_target_pawn = 0;
        g_aim_motion.Reset();
        std::snprintf(g_debug, sizeof(g_debug), "aim: aguarda tecla");
    } else if (target.index >= 0) {
        const auto now = std::chrono::steady_clock::now();
        if (g_target_pawn && target.pawn != g_target_pawn) {
            const auto held_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - g_target_acquired).count();
            const float lock_ms = (std::max)(cfg.sticky_ms, cfg.aim_switch_cooldown_ms);
            if (held_ms < static_cast<long long>(lock_ms)) {
                for (std::size_t i = 0; i < rt.players.size(); ++i) {
                    if (rt.players[i].pawn != g_target_pawn) continue;
                    const auto& held = rt.players[i];
                    if (!held.alive || held.health <= 0 ||
                        (cfg.aim_ignore_team && held.team == rt.local_team && rt.local_team >= 2) ||
                        (cfg.aim_ignore_bots && held.is_bot) ||
                        (cfg.aim_ignore_spectators && held.is_spectator) ||
                        (cfg.visible_check && !held.spotted) || held.distance > cfg.aim_max_dist)
                        break;
                    float point[3]{};
                    PickBone(held, cfg.aim_bone, point);
                    float sx = 0.f, sy = 0.f;
                    if (held.alive && held.health > 0 && W2S(point, rt.view_matrix, sx, sy)) {
                        target.index = static_cast<int>(i);
                        target.pawn = held.pawn;
                        target.error_x = sx - cx;
                        target.error_y = sy - cy;
                        target.screen_distance = std::sqrt(target.error_x * target.error_x + target.error_y * target.error_y);
                        target.distance_m = held.distance;
                    }
                    break;
                }
            }
        }
        if (target.pawn != g_target_pawn) {
            g_target_pawn = target.pawn;
            g_target_acquired = now;
            g_last_switch = now;
            g_aim_motion.SetTarget(static_cast<std::uint64_t>(target.pawn));
        }
        g_active_target_idx = target.index;
        g_last_target_idx = target.index;

        const auto settings = BuildMotionSettings(cfg, target.distance_m);
        const auto motion = g_aim_motion.Step(target.error_x, target.error_y, settings);
        if (motion) MoveMouse(motion.x, motion.y);
        std::snprintf(g_debug, sizeof(g_debug), "lock %+d,%+d err=%.1f idx=%d",
            motion.x, motion.y, motion.error, target.index);
    } else {
        g_active_target_idx = -1;
        std::snprintf(g_debug, sizeof(g_debug), "aim: no target");
    }

    // Handle triggerbot separately (keep existing for now)
    bool local_scoped = false;
    if (cfg.trigger_scoped_only || cfg.scope_check) {
        for (const auto& p : rt.players) {
            if (p.is_local) { local_scoped = p.is_scoped; break; }
        }
    }

    if (cfg.trigger_enabled) {
        static auto lastShot = std::chrono::steady_clock::now();
        if (KeyDown(cfg.trigger_bind)) {
            if (!(cfg.trigger_scoped_only && !local_scoped)) {
                bool hit = false;
                if (cfg.trigger_use_ident && rt.local_pawn && CS2::offsets.m_iIDEntIndex) {
                    const int idEnt = rt.local_crosshair_entity;
                    if (idEnt > 0) {
                        for (const auto& p : rt.players) {
                            if (p.is_local) continue;
                            if (cfg.trigger_team_check && p.team == rt.local_team) continue;
                            if (!p.alive || p.health <= 0) continue;
                            if (p.ent_index == idEnt || p.ent_index == (idEnt & 0x7FFF)) {
                                if (cfg.trigger_head_only) {
                                    float sx, sy;
                                    if (W2S(p.head, rt.view_matrix, sx, sy)) {
                                        const float d = std::sqrt((sx - cx) * (sx - cx) + (sy - cy) * (sy - cy));
                                        if (d < 16.f) hit = true;
                                    }
                                } else {
                                    hit = true;
                                }
                                break;
                            }
                        }
                    }
                }
                if (!hit) {
                    // Fire when ANY body part crosses the FOV (screen-center radius).
                    const float triggerFov = cfg.trigger_head_only
                        ? 14.f
                        : (std::max)(18.f, cfg.aim_fov > 1.f ? cfg.aim_fov * 0.35f : 28.f);
                    for (const auto& p : rt.players) {
                        if (p.is_local) continue;
                        if (cfg.trigger_team_check && p.team == rt.local_team) continue;
                        if (!p.alive || p.health <= 0) continue;
                        auto testPoint = [&](const float* w) {
                            float sx = 0.f, sy = 0.f;
                            if (!W2S(w, rt.view_matrix, sx, sy)) return false;
                            const float d = std::sqrt((sx - cx) * (sx - cx) + (sy - cy) * (sy - cy));
                            return d < triggerFov;
                        };
                        if (cfg.trigger_head_only) {
                            if (testPoint(p.head)) { hit = true; break; }
                            continue;
                        }
                        if (p.bones_ok) {
                            for (int bi = 0; bi < 20; ++bi) {
                                if (testPoint(p.bones[bi])) { hit = true; break; }
                            }
                            if (hit) break;
                        } else {
                            if (testPoint(p.head) || testPoint(p.pos)) { hit = true; break; }
                        }
                    }
                }
                if (hit) {
                    const auto now = std::chrono::steady_clock::now();
                    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastShot).count();
                    if (ms >= cfg.trigger_delay_ms) {
                        Click();
                        lastShot = now;
                    }
                }
            }
        }
    }

}

} // namespace CS2_Aim
