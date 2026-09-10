#pragma warning(disable: 4100 4244)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "cs2_aim.h"
#include "Memory/Memory.h"
#include "../Fivem/aimbot/aim_type.h"
#include "gameplay/aim_controller.h"
#include "gameplay/unified_aim.h"
#include "imgui.h"
#include <Windows.h>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>

namespace CS2_Aim {
namespace {

int g_last_target_idx = -1;
int g_active_target_idx = -1;
std::chrono::steady_clock::time_point g_last_switch{};
char g_debug[128] = "aim idle";
int g_frames_sem_bind = 0;
OmniGhost::Gameplay::ContinuousAimController g_aim_motion;

// Unified aimbot instance (stub for Publish builds)
static std::unique_ptr<Gameplay::UnifiedAim::UnifiedAimbot> g_unified_aimbot;

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

    // Initialize unified aimbot if needed (stub for Publish)
    if (!g_unified_aimbot) {
        g_unified_aimbot = Gameplay::UnifiedAim::CreateAimbotForGame("CS2");
        Gameplay::UnifiedAim::UnifiedConfig ucfg;
        ucfg.enabled = cfg.aim_enabled;
        ucfg.fov = cfg.aim_fov > 1.f ? cfg.aim_fov : 80.f;
        ucfg.smooth = cfg.aim_smooth;
        g_unified_aimbot->SetConfig(ucfg);
    }

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
    const float fov = EffectiveFov(cfg, 0.f); // distance not available here, use base fov

    // Use existing aim logic for actual aiming (unified aimbot is stub in Publish)
    float best_fov = FLT_MAX;
    int best_idx = -1;
    float best_dx = 0, best_dy = 0;
    
    for (size_t i = 0; i < rt.players.size(); ++i) {
        const auto& p = rt.players[i];
        if (p.is_local) continue;
        if (!p.alive || p.health <= 0) continue;
        if (cfg.aim_ignore_team && cfg.team_check && p.team == rt.local_team && rt.local_team >= 2) continue;
        if (p.distance > cfg.aim_max_dist) continue;
        
        float sx, sy;
        float bone_pos[3];
        PickBone(p, 0, bone_pos); // head
        if (!W2S(bone_pos, rt.view_matrix, sx, sy)) continue;
        
        float dx = sx - cx;
        float dy = sy - cy;
        float dist = std::sqrt(dx * dx + dy * dy);
        
        if (dist < fov && dist < best_fov) {
            best_fov = dist;
            best_idx = static_cast<int>(i);
            best_dx = dx;
            best_dy = dy;
        }
    }
    
    if (best_idx >= 0) {
        g_active_target_idx = best_idx;
        
        // Apply smoothing
        float smooth = cfg.aim_smooth > 0 ? cfg.aim_smooth : 1.0f;
        float mx = best_dx / smooth;
        float my = best_dy / smooth;
        
        // Update debug
        std::snprintf(g_debug, sizeof(g_debug), "pull %+d,%+d d=%.0f idx=%d",
            static_cast<int>(mx), static_cast<int>(my), best_fov, best_idx);
        
        // Move mouse
        if (mx != 0 || my != 0) {
            MoveMouse(static_cast<int>(mx), static_cast<int>(my));
        }
    } else {
        g_active_target_idx = -1;
        std::snprintf(g_debug, sizeof(g_debug), "aim: no target");
    }

    // Update unified aimbot stub (does nothing in Publish)
    Gameplay::UnifiedAim::AimContext ctx;
    ctx.dt = ImGui::GetIO().DeltaTime;
    g_unified_aimbot->Update(ctx);

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
}

} // namespace CS2_Aim
