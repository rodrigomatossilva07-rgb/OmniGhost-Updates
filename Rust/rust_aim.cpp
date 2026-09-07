#include "rust_aim.h"
#include "rust_game.h"
#include "rust_config.h"
#include "../src/gameplay/aim_controller.h"
#include "gameplay/unified_aim.h"
#include "aimbot/aim_type.h"
#include "../src/makcu/makcu_wrapper.h"
#include "imgui.h"
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>

namespace Rust_Aim {
namespace {

OmniGhost::Gameplay::ContinuousAimController g_aim_motion;
int g_active_target = -1;
int g_last_target = -1;
char g_debug[128] = "aim idle";

// Unified aimbot instance (stub for Publish builds)
static std::unique_ptr<Gameplay::UnifiedAim::UnifiedAimbot> g_unified_aimbot;

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

    // Initialize unified aimbot if needed (stub for Publish)
    if (!g_unified_aimbot) {
        g_unified_aimbot = Gameplay::UnifiedAim::CreateAimbotForGame("Rust");
        Gameplay::UnifiedAim::UnifiedConfig ucfg;
        ucfg.enabled = cfg.aim_enabled;
        ucfg.fov = cfg.aim_fov;
        ucfg.smooth = cfg.aim_smooth;
        g_unified_aimbot->SetConfig(ucfg);
    }

    // Triggerbot: real hardware click when head is under crosshair
    const ImVec2 ds0 = ImGui::GetIO().DisplaySize;
    const float cx0 = ds0.x * 0.5f;
    const float cy0 = ds0.y * 0.5f;
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

    // Use existing aim controller for actual aiming (unified aimbot is stub in Publish)
    float best_fov = FLT_MAX;
    int best_idx = -1;
    float best_dx = 0, best_dy = 0;
    
    for (size_t i = 0; i < rt.players.size(); ++i) {
        const auto& p = rt.players[i];
        if (!PassAimFilters(p, cfg)) continue;
        
        float sx, sy;
        if (!W2S(p.head, rt.view_matrix, sx, sy)) continue;
        
        float dx = sx - cx0;
        float dy = sy - cy0;
        float fov = std::sqrt(dx * dx + dy * dy);
        
        if (fov < cfg.aim_fov && fov < best_fov) {
            best_fov = fov;
            best_idx = static_cast<int>(i);
            best_dx = dx;
            best_dy = dy;
        }
    }
    
    if (best_idx >= 0) {
        g_active_target = best_idx;
        
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
        g_active_target = -1;
        std::snprintf(g_debug, sizeof(g_debug), "aim: no target");
    }

    // Update unified aimbot stub (does nothing in Publish)
    Gameplay::UnifiedAim::AimContext ctx;
    ctx.dt = ImGui::GetIO().DeltaTime;
    g_unified_aimbot->Update(ctx);
    
    // Handle triggerbot (keep existing for now)
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
}

} // namespace Rust_Aim