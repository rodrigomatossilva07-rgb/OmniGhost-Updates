#include "valorant_aim.h"
#include "valorant_game.h"
#include "../src/makcu/makcu_wrapper.h"
#include "../Fivem/aimbot/aim_type.h"
#include "../ImGui/imgui.h"

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

} // namespace

void RunAim() {
    if (!config.aim_enabled || !runtime.in_game)
        return;

    if (makcu_wrapper::IsConnected())
        makcu_wrapper::EnsureButtonMonitoring();

    if (!AimHold()) {
        g_sticky = -1;
        return;
    }

    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float cx = ds.x * 0.5f;
    const float cy = ds.y * 0.5f;
    const float fov = config.aim_fov > 5.f ? config.aim_fov : 70.f;

    float best = 1e9f;
    float bdx = 0, bdy = 0;
    int bestIdx = -1;
    const auto now = std::chrono::steady_clock::now();

    for (int i = 0; i < (int)runtime.players.size(); ++i) {
        const auto& p = runtime.players[i];
        if (!p.alive || p.is_local) continue;
        if (config.team_check && runtime.local_team != 0 && p.team == runtime.local_team)
            continue;
        if (p.distance > config.aim_max_dist) continue;

        float sx, sy;
        if (!W2S(p.head, runtime.view_matrix, sx, sy)) continue;
        const float dx = sx - cx;
        const float dy = sy - cy;
        const float d = sqrtf(dx * dx + dy * dy);
        if (d > fov) continue;

        // Sticky preference
        float score = d;
        if (i == g_sticky && now < g_sticky_until)
            score *= 0.65f;
        if (score < best) {
            best = score;
            bdx = dx;
            bdy = dy;
            bestIdx = i;
        }
    }

    if (bestIdx < 0) {
        g_sticky = -1;
        return;
    }

    g_sticky = bestIdx;
    g_sticky_until = now + std::chrono::milliseconds((int)config.sticky_ms);

    // CS2-style smooth: 0 = full snap, 100 = no pull
    float smooth = config.aim_smooth;
    if (smooth < 0.f) smooth = 0.f;
    if (smooth > 100.f) smooth = 100.f;
    float strength = (100.f - smooth) / 100.f;
    if (config.aim_humanize && strength > 0.f && strength < 1.f)
        strength = powf(strength, 0.92f);

    if (strength <= 0.0001f)
        return;

    float mx = bdx * strength;
    float my = bdy * strength;

    // Deadzone
    if (fabsf(bdx) < config.aim_deadzone && fabsf(bdy) < config.aim_deadzone)
        return;

    // Cap step
    float maxStep = 28.f;
    if (smooth > 40.f) maxStep = 12.f;
    if (smooth > 70.f) maxStep = 6.f;
    const float len = sqrtf(mx * mx + my * my);
    if (len > maxStep && len > 0.001f) {
        mx *= maxStep / len;
        my *= maxStep / len;
    }

    int ix = (int)lroundf(mx);
    int iy = (int)lroundf(my);
    if (ix == 0 && iy == 0) return;
    MoveMouse(ix, iy);

    // FOV draw
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
