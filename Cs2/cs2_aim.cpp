#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "cs2_aim.h"
#include "Memory/Memory.h"
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

float EffectiveFov(const CS2::Config& cfg, float distance_m) {
    float fov = cfg.aim_fov > 1.f ? cfg.aim_fov : 80.f;
    if (!cfg.aim_dynamic_fov) return fov;
    const float t = std::clamp(distance_m / 40.f, 0.f, 1.f);
    const float minF = cfg.aim_fov_min > 5.f ? cfg.aim_fov_min : 20.f;
    return minF + (fov - minF) * t;
}

void HandlePanic(CS2::Config& cfg) {
    if (!cfg.panic_key_enabled || cfg.panic_key == 0) return;
    static bool was_down = false;
    const bool down = KeyDown(cfg.panic_key);
    if (down && !was_down) {
        cfg.aim_enabled = false;
        cfg.trigger_enabled = false;
        cfg.esp_enabled = false;
        cfg.webradar_enabled = false;
        cfg.radar_2d = false;
        cfg.bomb_timer = false;
        MessageBeep(MB_OK);
        std::snprintf(g_debug, sizeof(g_debug), "PANIC — tudo OFF");
    }
    was_down = down;
}

} // namespace

int ActiveTargetIndex() { return g_active_target_idx; }
const char* DebugStatus() { return g_debug; }

void Run(const CS2::Runtime& rt, const CS2::Config& cfg_in) {
    CS2::Config& cfg = const_cast<CS2::Config&>(cfg_in);
    HandlePanic(cfg);

    const bool playable = rt.in_match || !rt.players.empty();
    if (!playable) {
        g_active_target_idx = -1;
        g_aim_motion.Reset();
        std::snprintf(g_debug, sizeof(g_debug), "sem partida");
        return;
    }
    if (!cfg.aim_enabled && !cfg.trigger_enabled) {
        g_active_target_idx = -1;
        g_aim_motion.Reset();
        std::snprintf(g_debug, sizeof(g_debug), "aim/trig OFF");
        return;
    }

    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    if (ds.x < 10.f || ds.y < 10.f) {
        std::snprintf(g_debug, sizeof(g_debug), "display invalido");
        return;
    }
    const float cx = ds.x * 0.5f;
    const float cy = ds.y * 0.5f;

    bool local_scoped = false;
    if (cfg.trigger_scoped_only || cfg.scope_check) {
        for (const auto& p : rt.players) {
            if (p.is_local) { local_scoped = p.is_scoped; break; }
        }
    }

    // ── Triggerbot ────────────────────────────────────────────────────────
    if (cfg.trigger_enabled) {
        static auto lastShot = std::chrono::steady_clock::now();
        if (KeyDown(cfg.trigger_bind)) {
            if (!(cfg.trigger_scoped_only && !local_scoped)) {
                bool hit = false;
                if (cfg.trigger_use_ident && rt.local_pawn && CS2::offsets.m_iIDEntIndex) {
                    int idEnt = 0;
                    if (mem.Read(rt.local_pawn + CS2::offsets.m_iIDEntIndex, &idEnt, sizeof(idEnt))
                        && idEnt > 0) {
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
                    for (const auto& p : rt.players) {
                        if (p.is_local) continue;
                        if (cfg.trigger_team_check && p.team == rt.local_team) continue;
                        if (!p.alive) continue;
                        float sx, sy;
                        const float* bone = cfg.trigger_head_only ? p.head
                            : (p.bones_ok ? p.bones[0] : p.head);
                        if (!W2S(bone, rt.view_matrix, sx, sy)) continue;
                        const float d = std::sqrt((sx - cx) * (sx - cx) + (sy - cy) * (sy - cy));
                        if (d < (cfg.trigger_head_only ? 14.f : 10.f)) { hit = true; break; }
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

    if (!cfg.aim_enabled) {
        g_active_target_idx = -1;
        g_aim_motion.Reset();
        return;
    }

    const bool key = AimKeyDown(cfg);
    if (!key) {
        g_active_target_idx = -1;
        g_last_target_idx = -1;
        g_aim_motion.Reset();
        ++g_frames_sem_bind;
        std::snprintf(g_debug, sizeof(g_debug), "sem bind | %s | VK=%d",
            aim_type::StatusText(), cfg.aim_bind);
        return;
    }
    g_frames_sem_bind = 0;

    static int sticky_index = -1;
    static auto sticky_until = std::chrono::steady_clock::now();
    // Sub-pixel residual — smooth moves accumulate fractions instead of truncating
    const auto now = std::chrono::steady_clock::now();

    float bestDist = 1e9f;
    float bestSX = 0.f, bestSY = 0.f;
    int bestIdx = -1;
    bool found = false;
    int candidates = 0;
    int rejected_fov = 0;
    int rejected_w2s = 0;
    int skipped_team = 0;
    int skipped_dead = 0;
    int skipped_far = 0;

    auto evaluate = [&](const CS2::Player& p, int idx) {
        if (p.is_local) return;
        // Only skip same team when team_check is on AND local_team is a real
        // CT/T value (2/3). local_team==0 means the read failed — do not
        // filter everyone as "same team".
        if (cfg.team_check && rt.local_team >= 2 && p.team == rt.local_team) {
            ++skipped_team;
            return;
        }
        if (p.health <= 0) {
            ++skipped_dead;
            return;
        }
        if (p.distance > cfg.aim_max_dist) {
            ++skipped_far;
            return;
        }
        ++candidates;

        float bone_pos[3];
        PickBone(p, cfg.aim_bone, bone_pos);
        if (!std::isfinite(bone_pos[0]) || (bone_pos[0] == 0.f && bone_pos[1] == 0.f && bone_pos[2] == 0.f)) {
            bone_pos[0] = p.pos[0];
            bone_pos[1] = p.pos[1];
            bone_pos[2] = p.pos[2] + 70.f;
        }

        if (cfg.aim_prediction) {
            // Lead time scales with distance + horizontal speed (more natural)
            const float hspd = std::sqrt(p.velocity[0] * p.velocity[0] + p.velocity[1] * p.velocity[1]);
            if (hspd > 1.f || std::fabs(p.velocity[2]) > 1.f) {
                const float distFactor = std::clamp(p.distance / 35.f, 0.35f, 1.6f);
                const float t = cfg.prediction_strength * 0.07f * distFactor;
                bone_pos[0] += p.velocity[0] * t;
                bone_pos[1] += p.velocity[1] * t;
                bone_pos[2] += p.velocity[2] * t * 0.55f; // less vertical lead
            }
        }

        float sx, sy;
        if (!W2S(bone_pos, rt.view_matrix, sx, sy)) {
            // Fallback: head, then origin+eye height
            if (!W2S(p.head, rt.view_matrix, sx, sy)) {
                float origin_eye[3] = { p.pos[0], p.pos[1], p.pos[2] + 64.f };
                if (!W2S(origin_eye, rt.view_matrix, sx, sy)) {
                    ++rejected_w2s;
                    return;
                }
            }
        }
        const float d = std::sqrt((sx - cx) * (sx - cx) + (sy - cy) * (sy - cy));
        float fov = EffectiveFov(cfg, p.distance);
        // Once locked on this target, give a small FOV hysteresis so micro
        // movement / prediction doesn't drop the target every other frame.
        if (idx == sticky_index && sticky_index >= 0)
            fov *= 1.18f;
        if (d > fov) {
            ++rejected_fov;
            return;
        }
        if (d < bestDist) {
            bestDist = d;
            bestSX = sx;
            bestSY = sy;
            bestIdx = idx;
            found = true;
        }
    };

    if (cfg.sticky_ms > 0.f && sticky_index >= 0 &&
        now < sticky_until && sticky_index < (int)rt.players.size()) {
        evaluate(rt.players[sticky_index], sticky_index);
    }

    for (int i = 0; i < (int)rt.players.size(); ++i)
        evaluate(rt.players[i], i);

    if (!found) {
        sticky_index = -1;
        g_active_target_idx = -1;
        g_aim_motion.Reset();
        if (rt.players.empty()) {
            std::snprintf(g_debug, sizeof(g_debug), "sem players (entity list?)");
        } else if (candidates == 0) {
            std::snprintf(g_debug, sizeof(g_debug),
                "sem cand p=%d team=%d dead=%d far=%d lt=%d",
                (int)rt.players.size(), skipped_team, skipped_dead, skipped_far, rt.local_team);
        } else {
            std::snprintf(g_debug, sizeof(g_debug),
                "fora FOV (cand=%d w2s=%d fov=%d)",
                candidates, rejected_w2s, rejected_fov);
        }
        return;
    }

    // Anti-snap cooldown between targets
    if (g_last_target_idx >= 0 && bestIdx != g_last_target_idx && cfg.aim_switch_cooldown_ms > 0.f) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_last_switch).count();
        if (ms < (int)cfg.aim_switch_cooldown_ms) {
            found = false;
            bestDist = 1e9f;
            evaluate(rt.players[g_last_target_idx], g_last_target_idx);
            if (!found) {
                g_active_target_idx = -1;
                return;
            }
        } else {
            g_last_switch = now;
        }
    } else if (bestIdx != g_last_target_idx) {
        g_last_switch = now;
    }

    g_last_target_idx = bestIdx;
    sticky_index = bestIdx;
    sticky_until = now + std::chrono::milliseconds((int)std::max(0.f, cfg.sticky_ms));
    g_active_target_idx = bestIdx;

    g_aim_motion.SetTarget(static_cast<std::uint64_t>(rt.players[bestIdx].pawn));
    OmniGhost::Gameplay::AimMotionSettings motion_settings{};
    motion_settings.smooth = cfg.aim_smooth;
    motion_settings.humanize = cfg.aim_humanize;
    motion_settings.deadzone = cfg.aim_deadzone;
    motion_settings.minimum_strength = 0.05f;
    OmniGhost::Gameplay::ApplyStableDistanceProfile(
        motion_settings, rt.players[bestIdx].distance);
    const auto motion = g_aim_motion.Step(bestSX - cx, bestSY - cy, motion_settings);
    if (!motion) {
        std::snprintf(g_debug, sizeof(g_debug), "locked d=%.1f tgt=%d", motion.error, bestIdx);
        return;
    }
    MoveMouse(motion.x, motion.y);
    std::snprintf(g_debug, sizeof(g_debug), "pull %+d,%+d d=%.0f sm=%.0f tgt=%d",
        motion.x, motion.y, motion.error, cfg.aim_smooth, bestIdx);
    return;
}

} // namespace CS2_Aim
