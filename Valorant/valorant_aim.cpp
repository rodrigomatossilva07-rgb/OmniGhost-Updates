#pragma warning(disable: 4100 4244)
#include "valorant_aim.h"
#include "valorant_game.h"
#include "../src/makcu/makcu_wrapper.h"
#include "../Fivem/aimbot/aim_type.h"
#include "../ImGui/imgui.h"
#include "gameplay/unified_aim.h"
#include <memory>

#include <Windows.h>
#include <cmath>
#include <algorithm>
#include <chrono>

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

bool KeyDown(int vk) {
    if (vk <= 0) return false;
    if (makcu_wrapper::IsConnected())
        makcu_wrapper::EnsureButtonMonitoring();
    const bool isMouse = (vk >= 1 && vk <= 6);
    if (isMouse) {
        if (makcu_wrapper::GetButtonState(vk).down)
            return true;
        if (makcu_wrapper::PacketsReceived() == 0) {
            if ((GetAsyncKeyState(vk) & 0x8000) != 0) return true;
            if ((GetKeyState(vk) & 0x8000) != 0) return true;
        }
        return false;
    }
    if (makcu_wrapper::GetButtonState(vk).down) return true;
    if ((GetAsyncKeyState(vk) & 0x8000) != 0) return true;
    return (GetKeyState(vk) & 0x8000) != 0;
}

bool AimHold() {
    bool down = false;
    if (config.aim_bind > 0) down = KeyDown(config.aim_bind);
    if (!down && config.aim_bind2 > 0) down = KeyDown(config.aim_bind2);
    return down;
}

void MoveMouse(int dx, int dy) {
    if (makcu_wrapper::IsConnected()) {
        while (dx != 0 || dy != 0) {
            int sx = dx; if (sx > 127) sx = 127; if (sx < -127) sx = -127;
            int sy = dy; if (sy > 127) sy = 127; if (sy < -127) sy = -127;
            makcu_wrapper::move(sx, sy);
            dx -= sx; dy -= sy;
        }
        return;
    }
    aim_type::Move(dx, dy);
}

int g_sticky = -1;
std::chrono::steady_clock::time_point g_sticky_until{};

// Unified aimbot instance (stub for Publish builds)
static std::unique_ptr<Gameplay::UnifiedAim::UnifiedAimbot> g_unified_aimbot;

} // namespace

void RunAim() {
    // Initialize unified aimbot if needed (stub for Publish)
    if (!g_unified_aimbot) {
        g_unified_aimbot = Gameplay::UnifiedAim::CreateAimbotForGame("Valorant");
        Gameplay::UnifiedAim::UnifiedConfig ucfg;
        ucfg.enabled = config.aim_enabled;
        ucfg.fov = config.aim_fov > 5.f ? config.aim_fov : 70.f;
        ucfg.smooth = config.aim_smooth;
        g_unified_aimbot->SetConfig(ucfg);
    }

    if (!config.aim_enabled || !runtime.in_game)
        return;

    if (makcu_wrapper::IsConnected())
        makcu_wrapper::EnsureButtonMonitoring();

    if (!AimHold()) {
        g_sticky = -1;
        return;
    }

    // Use existing aim logic for actual aiming (unified aimbot is stub in Publish)
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float cx = ds.x * 0.5f;
    const float cy = ds.y * 0.5f;
    const float fov = config.aim_fov > 5.f ? config.aim_fov : 70.f;
    
    float best_fov = FLT_MAX;
    int best_idx = -1;
    float best_dx = 0, best_dy = 0;
    
    for (size_t i = 0; i < runtime.players.size(); ++i) {
        const auto& p = runtime.players[i];
        if (p.is_local) continue;
        if (!p.alive || p.health <= 0) continue;
        if (config.aim_ignore_team && config.team_check && p.team != 0 &&
            p.team == runtime.local_team && runtime.local_team != 0)
            continue;
        if (p.distance > config.aim_max_dist) continue;
        
        float sx, sy;
        if (!W2S(p.head, runtime.view_matrix, sx, sy)) continue;
        
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
        g_sticky = best_idx;
        
        // Apply smoothing
        float smooth = config.aim_smooth > 0 ? config.aim_smooth : 1.0f;
        float mx = best_dx / smooth;
        float my = best_dy / smooth;
        
        // Move mouse
        if (mx != 0 || my != 0) {
            MoveMouse(static_cast<int>(mx), static_cast<int>(my));
        }
    } else {
        g_sticky = -1;
    }

    // Update unified aimbot stub (does nothing in Publish)
    Gameplay::UnifiedAim::AimContext ctx;
    ctx.dt = ImGui::GetIO().DeltaTime;
    g_unified_aimbot->Update(ctx);

    // Keep existing FOV drawing
    if (config.aim_draw_fov) {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        if (dl) {
            ImU32 col = IM_COL32(
                (int)(config.col_fov[0] * 255),
                (int)(config.col_fov[1] * 255),
                (int)(config.col_fov[2] * 255),
                (int)(config.col_fov[3] * 255));
            if (config.aim_fov_style == 1)
                dl->AddRect(ImVec2(cx - fov, cy - fov), ImVec2(cx + fov, cy + fov), col, 0, 0, 1.2f);
            else
                dl->AddCircle(ImVec2(cx, cy), fov, col, 48, 1.2f);
        }
    }
}

} // namespace Valorant