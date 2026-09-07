#pragma warning(disable: 4100 4244)
#include "warzone_aim.h"
#include "../src/gameplay/aim_controller.h"
#include "gameplay/unified_aim.h"
#include "aimbot/aim_type.h"
#include "imgui.h"
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>

namespace Warzone_Aim {
namespace {

OmniGhost::Gameplay::ContinuousAimController g_aim_motion;
int g_active = -1;
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

bool KeyDown(int vk) {
    if (vk <= 0) return false;
    return aim_type::IsDown(vk) || ((GetKeyState(vk) & 0x8000) != 0);
}

void MoveMouse(int dx, int dy) {
    while (dx != 0 || dy != 0) {
        int sx = dx; if (sx > 127) sx = 127; if (sx < -127) sx = -127;
        int sy = dy; if (sy > 127) sy = 127; if (sy < -127) sy = -127;
        aim_type::Move(sx, sy);
        dx -= sx; dy -= sy;
    }
}

void Click() {
    aim_type::LeftClick();
    aim_type::LeftClickRelease();
}

bool Pass(const Warzone::Player& p, const Warzone::Config& cfg) {
    if (!p.valid || p.is_local) return false;
    if (p.health <= 0.f) return false;
    if (cfg.aim_ignore_downed && p.downed) return false;
    if (cfg.aim_ignore_ai && p.ai) return false;
    if (cfg.aim_ignore_team && cfg.team_check && p.team != 0 &&
        p.team == Warzone::runtime.local_team && Warzone::runtime.local_team != 0)
        return false;
    if (p.distance > cfg.aim_max_dist) return false;
    return true;
}

} // namespace

int ActiveTargetIndex() { return g_active; }
const char* DebugStatus() { return g_debug; }

void Run(const Warzone::Runtime& rt, const Warzone::Config& cfg) {
    // Initialize unified aimbot if needed (stub for Publish)
    if (!g_unified_aimbot) {
        g_unified_aimbot = Gameplay::UnifiedAim::CreateAimbotForGame("Warzone");
        Gameplay::UnifiedAim::UnifiedConfig ucfg;
        ucfg.enabled = cfg.aim_enabled;
        ucfg.fov = cfg.aim_fov;
        ucfg.smooth = cfg.aim_smooth;
        g_unified_aimbot->SetConfig(ucfg);
    }

    if (!rt.in_game && rt.players.empty()) {
        g_active = -1;
        g_aim_motion.Reset();
        std::snprintf(g_debug, sizeof(g_debug), "sem dados");
        return;
    }
    if (!cfg.aim_enabled && !cfg.trigger_enabled) {
        g_active = -1;
        g_aim_motion.Reset();
        std::snprintf(g_debug, sizeof(g_debug), "aim/trig OFF");
        return;
    }

    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float cx = ds.x * 0.5f, cy = ds.y * 0.5f;

    if (cfg.trigger_enabled && KeyDown(cfg.trigger_bind)) {
        static auto lastShot = std::chrono::steady_clock::now();
        bool hit = false;
        for (const auto& p : rt.players) {
            if (!Pass(p, cfg)) continue;
            if (cfg.trigger_team_check && p.team == rt.local_team && rt.local_team != 0) continue;
            float sx, sy;
            if (!W2S(p.head, rt.view_matrix, sx, sy)) continue;
            const float d = std::sqrt((sx - cx) * (sx - cx) + (sy - cy) * (sy - cy));
            if (d < 12.f) { hit = true; break; }
        }
        if (hit) {
            const auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastShot).count() >= cfg.trigger_delay_ms) {
                Click();
                lastShot = now;
                std::snprintf(g_debug, sizeof(g_debug), "trigger CLICK");
            }
        }
    }

    if (!cfg.aim_enabled) {
        g_active = -1;
        g_aim_motion.Reset();
        return;
    }

    // Use existing aim logic for actual aiming (unified aimbot is stub in Publish)
    float best_fov = FLT_MAX;
    int best_idx = -1;
    float best_dx = 0, best_dy = 0;
    
    for (size_t i = 0; i < rt.players.size(); ++i) {
        const auto& p = rt.players[i];
        if (!Pass(p, cfg)) continue;
        
        float sx, sy;
        if (!W2S(p.head, rt.view_matrix, sx, sy)) continue;
        
        float dx = sx - cx;
        float dy = sy - cy;
        float fov = std::sqrt(dx * dx + dy * dy);
        
        if (fov < cfg.aim_fov && fov < best_fov) {
            best_fov = fov;
            best_idx = static_cast<int>(i);
            best_dx = dx;
            best_dy = dy;
        }
    }
    
    if (best_idx >= 0) {
        g_active = best_idx;
        
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
        g_active = -1;
        std::snprintf(g_debug, sizeof(g_debug), "aim: no target");
    }

    // Update unified aimbot stub (does nothing in Publish)
    Gameplay::UnifiedAim::AimContext ctx;
    ctx.dt = ImGui::GetIO().DeltaTime;
    g_unified_aimbot->Update(ctx);

    // Keep existing FOV drawing
    if (cfg.aim_draw_fov) {
        if (ImDrawList* dl = ImGui::GetBackgroundDrawList()) {
            dl->AddCircle(ImVec2(cx, cy), cfg.aim_fov,
                IM_COL32((int)(cfg.col_fov[0]*255),(int)(cfg.col_fov[1]*255),
                         (int)(cfg.col_fov[2]*255),(int)(cfg.col_fov[3]*255)), 64, 1.2f);
        }
    }
}

} // namespace Warzone_Aim