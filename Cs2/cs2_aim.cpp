#pragma warning(disable: 4100 4244)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "cs2_aim.h"
#include "Memory/Memory.h"
#include "../Fivem/aimbot/aim_type.h"
#include "gameplay/aim_controller.h"
#include "gameplay/smooth_curves.h"
#include "gameplay/prediction.h"
#include "imgui.h"
#include <Windows.h>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <array>
#include <unordered_map>

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
Gameplay::SmoothCurves::AimHumanizer g_humanizer;
Gameplay::SmoothCurves::SmoothCurveEvaluator g_curve_evaluator;

struct WeaponRecoilPattern {
    std::array<std::pair<float, float>, 30> offsets{};
    int length = 0;
    float scale = 1.f;
};

static std::unordered_map<int, WeaponRecoilPattern> g_recoil_patterns;
static std::array<std::pair<float, float>, 30> g_current_pattern{};
static int g_shots_fired = 0;
static int g_last_shots_fired = 0;
static bool g_patterns_loaded = false;

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
        case 1: selected = p.bones[1]; break;
        case 2: selected = p.bones[2]; break;
        case 3: selected = p.bones[5]; break;
        case 4:
            out[0] = (p.bones[15][0] + p.bones[18][0]) * 0.5f;
            out[1] = (p.bones[15][1] + p.bones[18][1]) * 0.5f;
            out[2] = (p.bones[15][2] + p.bones[18][2]) * 0.5f;
            return;
        default: selected = p.bones[0]; break;
        }
    }
    out[0] = selected[0];
    out[1] = selected[1];
    out[2] = selected[2];
}

bool IsBoneVisible(const CS2::Player& p, int bone, const float* view_matrix) {
    float sx, sy;
    float point[3];
    PickBone(p, bone, point);
    return W2S(point, view_matrix, sx, sy);
}

int SelectBestVisibleBone(const CS2::Player& p, const float* view_matrix, int preferred_bone) {
    if (preferred_bone >= 0 && preferred_bone <= 4 && IsBoneVisible(p, preferred_bone, view_matrix))
        return preferred_bone;

    static const int bone_priority[] = { 0, 1, 2, 3, 4 };
    for (int bone : bone_priority) {
        if (IsBoneVisible(p, bone, view_matrix))
            return bone;
    }
    return preferred_bone;
}

void MoveMouse(int dx, int dy) {
    if (dx == 0 && dy == 0) return;

    int left_x = dx;
    int left_y = dy;
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

float WeaponThreat(int definition) noexcept {
    switch (definition) {
    case 9: case 11: case 38: case 40: return 1.f;
    case 7: case 8: case 10: case 13: case 16: case 39: case 60: return .75f;
    case 1: case 2: case 3: case 4: case 30: case 32: case 36: case 61: case 63: case 64: return .45f;
    default: return .2f;
    }
}

int AutoSelectBone(const CS2::Player& p, int weapon_def, float distance_m, const float* view_matrix, bool visibility_check, bool auto_bone_enabled, int fallback_bone) {
    if (!auto_bone_enabled) return fallback_bone;

    bool is_sniper = (weapon_def == 9 || weapon_def == 11 || weapon_def == 38 || weapon_def == 40);
    bool is_rifle = (weapon_def == 7 || weapon_def == 8 || weapon_def == 10 || weapon_def == 13 || weapon_def == 16 || weapon_def == 39 || weapon_def == 60);
    bool is_pistol = (weapon_def >= 1 && weapon_def <= 4) || weapon_def == 30 || weapon_def == 32 || weapon_def == 36 || weapon_def == 61 || weapon_def == 63 || weapon_def == 64;

    int preferred = 0;
    if (is_sniper) preferred = 0;
    else if (is_rifle) preferred = (distance_m > 50.f) ? 0 : 1;
    else if (is_pistol) preferred = (distance_m > 30.f) ? 1 : 2;
    else preferred = 0;

    if (visibility_check) {
        return SelectBestVisibleBone(p, view_matrix, preferred);
    }
    return preferred;
}

struct TargetCandidate {
    int index = -1;
    uintptr_t pawn = 0;
    float error_x = 0.f;
    float error_y = 0.f;
    float screen_distance = FLT_MAX;
    float distance_m = 0.f;
    float score = FLT_MAX;
    float velocity_x = 0.f;
    float velocity_y = 0.f;
    float velocity_z = 0.f;
    float prev_velocity_x = 0.f;
    float prev_velocity_y = 0.f;
    float prev_velocity_z = 0.f;
    int weapon_def = 0;
    int bone = 0;
};

static std::unordered_map<uintptr_t, std::array<float, 3>> g_prev_velocities;

TargetCandidate SelectTarget(const CS2::Runtime& rt, const CS2::Config& cfg, float cx, float cy) {
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
        if (cfg.aim_visibility_check && !player.spotted) continue;
        if (player.distance > cfg.aim_max_dist) continue;

        int bone = AutoSelectBone(player, player.weapon_def, player.distance, rt.view_matrix, cfg.aim_visibility_check, cfg.aim_auto_bone, cfg.aim_bone);
        
        float point[3]{};
        PickBone(player, bone, point);

        if (cfg.aim_prediction) {
            float accel_x = player.velocity[0] - g_prev_velocities[player.pawn][0];
            float accel_y = player.velocity[1] - g_prev_velocities[player.pawn][1];
            float accel_z = player.velocity[2] - g_prev_velocities[player.pawn][2];

            float lead = std::clamp((.006f + snapshot_age) * cfg.prediction_strength, 0.f, .045f);
            float accel_factor = std::clamp(cfg.prediction_strength * 0.5f, 0.f, 0.02f);

            point[0] += player.velocity[0] * lead + accel_x * accel_factor;
            point[1] += player.velocity[1] * lead + accel_y * accel_factor;
            point[2] += player.velocity[2] * lead + accel_z * accel_factor;
        }

        g_prev_velocities[player.pawn] = { player.velocity[0], player.velocity[1], player.velocity[2] };

        float sx = 0.f, sy = 0.f;
        if (!W2S(point, rt.view_matrix, sx, sy)) continue;
        const float dx = sx - cx;
        const float dy = sy - cy;
        const float screen = std::sqrt(dx * dx + dy * dy);
        if (screen > EffectiveFov(cfg, player.distance)) continue;

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
            best.velocity_x = player.velocity[0];
            best.velocity_y = player.velocity[1];
            best.velocity_z = player.velocity[2];
            best.weapon_def = player.weapon_def;
            best.bone = bone;
        }
    }
    return best;
}

OmniGhost::Gameplay::AimMotionSettings BuildMotionSettings(const CS2::Config& cfg, float distance_m) noexcept {
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
    settings.prediction = false;
    OmniGhost::Gameplay::ApplyStableDistanceProfile(settings, distance_m);
    return settings;
}

void InitializeHumanizer() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    Gameplay::SmoothCurves::AimHumanizer::HumanizerConfig hcfg;
    hcfg.enabled = true;
    hcfg.micro_jitter = 0.12f;
    hcfg.macro_drift = 0.03f;
    hcfg.reaction_variance = 0.015f;
    hcfg.overshoot_chance = 0.025f;
    hcfg.overshoot_amount = 0.08f;
    hcfg.pause_chance = 0.008f;
    hcfg.pause_duration = 0.04f;
    hcfg.curve_variance = 0.04f;
    hcfg.close_range_multiplier = 0.6f;
    hcfg.mid_range_multiplier = 1.0f;
    hcfg.far_range_multiplier = 1.3f;
    g_humanizer.SetConfig(hcfg);

    Gameplay::SmoothCurves::SmoothCurveConfig ccfg = Gameplay::SmoothCurves::SmoothCurveEvaluator::LegitConfig();
    ccfg.type = Gameplay::SmoothCurves::CurveType::EaseOutCubic;
    ccfg.dynamic_adjustment = true;
    ccfg.distance_factor = 0.03f;
    ccfg.velocity_factor = 0.08f;
    g_curve_evaluator.SetConfig(ccfg);
}

void LoadRecoilPatterns() {
    if (g_patterns_loaded) return;
    g_patterns_loaded = true;

    auto add_pattern = [](int def, std::initializer_list<std::pair<float, float>> offsets, float scale = 1.f) {
        WeaponRecoilPattern& p = g_recoil_patterns[def];
        int i = 0;
        for (auto& o : offsets) {
            if (i >= 30) break;
            p.offsets[i++] = o;
        }
        p.length = i;
        p.scale = scale;
    };

    add_pattern(7, {  // AK-47
        {0,0}, {0.5,2.1}, {1.2,3.8}, {1.8,5.2}, {2.1,6.5}, {2.3,7.8}, {2.4,9.1}, {2.3,10.3}, {2.1,11.4}, {1.8,12.4},
        {1.4,13.3}, {1.0,14.1}, {0.5,14.8}, {0,15.4}, {-0.5,15.9}, {-1.0,16.3}, {-1.5,16.6}, {-2.0,16.8}, {-2.4,16.9}, {-2.8,16.9},
        {-3.1,16.8}, {-3.3,16.6}, {-3.4,16.3}, {-3.4,15.8}, {-3.3,15.2}, {-3.1,14.5}, {-2.8,13.7}, {-2.4,12.8}, {-2.0,11.9}, {-1.5,10.9}
    }, 1.0f);

    add_pattern(8, {  // AUG
        {0,0}, {0.3,1.8}, {0.8,3.2}, {1.2,4.5}, {1.5,5.6}, {1.7,6.6}, {1.8,7.5}, {1.8,8.3}, {1.7,9.0}, {1.5,9.7},
        {1.2,10.3}, {0.8,10.8}, {0.4,11.2}, {0,11.5}, {-0.4,11.7}, {-0.8,11.8}, {-1.2,11.8}, {-1.5,11.7}, {-1.8,11.5}, {-2.0,11.2},
        {-2.2,10.8}, {-2.3,10.3}, {-2.4,9.7}, {-2.4,9.1}, {-2.3,8.4}, {-2.2,7.6}, {-2.0,6.8}, {-1.7,5.9}, {-1.4,5.0}, {-1.0,4.1}
    }, 0.85f);

    add_pattern(9, {  // AWP
        {0,0}, {0,12.0}, {0,24.0}, {0,36.0}, {0,48.0}, {0,60.0}, {0,72.0}, {0,84.0}, {0,96.0}, {0,108.0},
        {0,120.0}, {0,132.0}, {0,144.0}, {0,156.0}, {0,168.0}, {0,180.0}, {0,192.0}, {0,204.0}, {0,216.0}, {0,228.0},
        {0,240.0}, {0,252.0}, {0,264.0}, {0,276.0}, {0,288.0}, {0,300.0}, {0,312.0}, {0,324.0}, {0,336.0}, {0,348.0}
    }, 1.0f);

    add_pattern(10, { // FAMAS
        {0,0}, {0.4,2.0}, {1.0,3.6}, {1.5,5.0}, {1.8,6.2}, {2.0,7.3}, {2.1,8.3}, {2.1,9.2}, {2.0,10.0}, {1.8,10.7},
        {1.5,11.3}, {1.1,11.8}, {0.6,12.2}, {0,12.5}, {-0.5,12.7}, {-1.0,12.8}, {-1.5,12.8}, {-1.9,12.7}, {-2.3,12.5}, {-2.6,12.2},
        {-2.8,11.7}, {-3.0,11.1}, {-3.1,10.4}, {-3.1,9.6}, {-3.0,8.8}, {-2.8,7.9}, {-2.5,7.0}, {-2.1,6.0}, {-1.7,5.1}, {-1.2,4.1}
    }, 0.9f);

    add_pattern(13, { // Galil AR
        {0,0}, {0.6,2.2}, {1.4,4.0}, {2.0,5.5}, {2.4,6.8}, {2.6,8.0}, {2.7,9.1}, {2.7,10.1}, {2.5,11.0}, {2.2,11.8},
        {1.8,12.5}, {1.3,13.1}, {0.7,13.6}, {0,14.0}, {-0.7,14.3}, {-1.4,14.5}, {-2.0,14.6}, {-2.6,14.6}, {-3.1,14.5}, {-3.6,14.3},
        {-4.0,14.0}, {-4.3,13.5}, {-4.5,12.8}, {-4.6,12.1}, {-4.6,11.3}, {-4.5,10.4}, {-4.3,9.5}, {-4.0,8.5}, {-3.6,7.5}, {-3.1,6.5}
    }, 0.95f);

    add_pattern(16, { // M4A4
        {0,0}, {0.4,1.9}, {1.0,3.5}, {1.5,4.9}, {1.8,6.1}, {2.0,7.2}, {2.1,8.1}, {2.1,9.0}, {2.0,9.8}, {1.8,10.5},
        {1.5,11.1}, {1.1,11.6}, {0.6,12.0}, {0,12.3}, {-0.5,12.5}, {-1.0,12.6}, {-1.4,12.6}, {-1.8,12.5}, {-2.1,12.3}, {-2.4,12.0},
        {-2.6,11.6}, {-2.7,11.1}, {-2.8,10.5}, {-2.8,9.8}, {-2.7,9.1}, {-2.5,8.3}, {-2.3,7.5}, {-2.0,6.6}, {-1.6,5.7}, {-1.2,4.7}
    }, 0.9f);

    add_pattern(60, { // M4A1-S
        {0,0}, {0.3,1.7}, {0.8,3.1}, {1.2,4.3}, {1.5,5.4}, {1.7,6.4}, {1.8,7.2}, {1.8,8.0}, {1.7,8.7}, {1.5,9.3},
        {1.2,9.9}, {0.9,10.3}, {0.5,10.7}, {0,11.0}, {-0.4,11.2}, {-0.8,11.3}, {-1.2,11.3}, {-1.5,11.2}, {-1.8,11.0}, {-2.0,10.7},
        {-2.2,10.3}, {-2.3,9.9}, {-2.3,9.3}, {-2.3,8.7}, {-2.2,8.0}, {-2.0,7.3}, {-1.8,6.5}, {-1.5,5.7}, {-1.2,4.9}, {-0.9,4.1}
    }, 0.85f);

    add_pattern(1, { // Desert Eagle
        {0,0}, {0,3.5}, {0,6.8}, {0,9.8}, {0,12.5}, {0,15.0}, {0,17.2}, {0,19.2}, {0,21.0}, {0,22.6},
        {0,24.0}, {0,25.3}, {0,26.4}, {0,27.4}, {0,28.2}, {0,28.9}, {0,29.5}, {0,30.0}, {0,30.4}, {0,30.7},
        {0,30.9}, {0,31.0}, {0,31.1}, {0,31.1}, {0,31.0}, {0,30.9}, {0,30.7}, {0,30.4}, {0,30.1}, {0,29.7}
    }, 1.2f);

    add_pattern(4, { // Glock-18
        {0,0}, {0.2,1.5}, {0.5,2.8}, {0.8,3.9}, {1.0,4.8}, {1.1,5.6}, {1.2,6.3}, {1.2,6.9}, {1.1,7.4}, {1.0,7.8},
        {0.8,8.1}, {0.5,8.3}, {0.2,8.5}, {0,8.6}, {-0.2,8.6}, {-0.5,8.5}, {-0.8,8.4}, {-1.1,8.2}, {-1.4,7.9}, {-1.6,7.5},
        {-1.8,7.1}, {-1.9,6.6}, {-2.0,6.1}, {-2.0,5.5}, {-2.0,4.9}, {-1.9,4.3}, {-1.8,3.7}, {-1.6,3.1}, {-1.4,2.5}, {-1.1,1.9}
    }, 0.7f);

    add_pattern(61, { // USP-S
        {0,0}, {0.2,1.4}, {0.5,2.6}, {0.7,3.6}, {0.9,4.5}, {1.0,5.2}, {1.0,5.9}, {1.0,6.4}, {0.9,6.9}, {0.8,7.3},
        {0.6,7.7}, {0.3,7.9}, {0,8.1}, {-0.3,8.1}, {-0.6,8.1}, {-0.9,8.0}, {-1.2,7.8}, {-1.5,7.5}, {-1.7,7.1}, {-1.9,6.6},
        {-2.0,6.1}, {-2.1,5.5}, {-2.1,4.9}, {-2.1,4.2}, {-2.0,3.6}, {-1.9,2.9}, {-1.7,2.3}, {-1.5,1.7}, {-1.2,1.1}, {-0.9,0.5}
    }, 0.75f);
}

void ApplyRCS(const CS2::Runtime& rt, const CS2::Config& cfg, int weapon_def) {
    if (!cfg.aim_rcs_auto && !cfg.aim_rcs_standalone) return;
    if (!rt.local_pawn) return;

    int shots_fired = 0;
    if (CS2::offsets.m_iShotsFired) {
        mem.Read(rt.local_pawn + CS2::offsets.m_iShotsFired, &shots_fired, sizeof(shots_fired));
    }

    if (shots_fired <= 1) {
        g_shots_fired = 0;
        g_last_shots_fired = 0;
        return;
    }

    if (shots_fired != g_last_shots_fired) {
        g_shots_fired = shots_fired - 1;
        g_last_shots_fired = shots_fired;
    }

    if (g_shots_fired <= 0) return;

    LoadRecoilPatterns();

    auto it = g_recoil_patterns.find(weapon_def);
    if (it == g_recoil_patterns.end()) return;

    const WeaponRecoilPattern& pattern = it->second;
    if (pattern.length == 0) return;

    int idx = std::clamp(g_shots_fired - 1, 0, pattern.length - 1);
    float punch_x = 0.f, punch_y = 0.f;
    if (CS2::offsets.m_aimPunchAngle) {
        mem.Read(rt.local_pawn + CS2::offsets.m_aimPunchAngle, &punch_x, sizeof(punch_x));
        mem.Read(rt.local_pawn + CS2::offsets.m_aimPunchAngle + 4, &punch_y, sizeof(punch_y));
    }

    float pattern_x = pattern.offsets[idx].first * pattern.scale * cfg.aim_rcs_x;
    float pattern_y = pattern.offsets[idx].second * pattern.scale * cfg.aim_rcs_y;

    float rcs_x = -punch_y * cfg.aim_rcs_x + pattern_x;
    float rcs_y = -punch_x * cfg.aim_rcs_y + pattern_y;

    if (std::fabs(rcs_x) > 0.5f || std::fabs(rcs_y) > 0.5f) {
        MoveMouse(static_cast<int>(rcs_x), static_cast<int>(rcs_y));
    }
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

} // namespace

int ActiveTargetIndex() { return g_active_target_idx; }
const char* DebugStatus() { return g_debug; }

void Run(const CS2::Runtime& rt, const CS2::Config& cfg_in) {
    CS2::Config& cfg = const_cast<CS2::Config&>(cfg_in);
    HandlePanic(cfg);

    InitializeHumanizer();

    const bool playable = rt.in_match || !rt.players.empty();
    if (!playable) {
        g_active_target_idx = -1;
        g_target_pawn = 0;
        g_aim_motion.Reset();
        g_humanizer.Reset();
        g_shots_fired = 0;
        g_last_shots_fired = 0;
        std::snprintf(g_debug, sizeof(g_debug), "sem partida");
        return;
    }
    if (!cfg.aim_enabled && !cfg.trigger_enabled && !cfg.aim_rcs_standalone) {
        g_active_target_idx = -1;
        g_target_pawn = 0;
        g_aim_motion.Reset();
        g_humanizer.Reset();
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

    if (cfg.aim_rcs_standalone || (cfg.aim_enabled && cfg.aim_rcs_auto)) {
        ApplyRCS(rt, cfg, local_weapon_definition);
    }

    // A zero definition means the weapon-chain read was unavailable for this
    // snapshot.  Do not turn that transient read failure into a permanent aim
    // block; known utility definitions remain strictly blocked.
    if (local_weapon_definition > 0 && !IsFirearm(local_weapon_definition)) {
        g_active_target_idx = -1;
        g_last_target_idx = -1;
        g_target_pawn = 0;
        g_aim_motion.Reset();
        g_humanizer.Reset();
        std::snprintf(g_debug, sizeof(g_debug), "aim bloqueado: utilitario (%d)", local_weapon_definition);
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
        g_humanizer.Reset();
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
                        (cfg.aim_visibility_check && !held.spotted) || held.distance > cfg.aim_max_dist)
                        break;
                    float point[3]{};
                    int bone = AutoSelectBone(held, held.weapon_def, held.distance, rt.view_matrix, cfg.aim_visibility_check, cfg.aim_auto_bone, cfg.aim_bone);
                    PickBone(held, bone, point);
                    float sx = 0.f, sy = 0.f;
                    if (held.alive && held.health > 0 && W2S(point, rt.view_matrix, sx, sy)) {
                        target.index = static_cast<int>(i);
                        target.pawn = held.pawn;
                        target.error_x = sx - cx;
                        target.error_y = sy - cy;
                        target.screen_distance = std::sqrt(target.error_x * target.error_x + target.error_y * target.error_y);
                        target.distance_m = held.distance;
                        target.bone = bone;
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
        if (motion) {
            ImVec2 raw{static_cast<float>(motion.x), static_cast<float>(motion.y)};
            const auto humanized = g_humanizer.Humanize(raw, target.distance_m, ImGui::GetIO().DeltaTime);
            MoveMouse(static_cast<int>(humanized.x), static_cast<int>(humanized.y));
            std::snprintf(g_debug, sizeof(g_debug),
                "lock %+d,%+d err=%.1f idx=%d bone=%d dist=%.0fm",
                static_cast<int>(humanized.x), static_cast<int>(humanized.y),
                motion.error, target.index, target.bone, target.distance_m);
        } else {
            std::snprintf(g_debug, sizeof(g_debug), "aim: on target (deadzone)");
        }
    } else {
        g_active_target_idx = -1;
        std::snprintf(g_debug, sizeof(g_debug), "aim: no target");
    }

    bool local_scoped = false;
    if (cfg.trigger_scoped_only || cfg.scope_check) {
        for (const auto& p : rt.players) {
            if (p.is_local) { local_scoped = p.is_scoped; break; }
        }
    }

    if (cfg.trigger_enabled) {
        static auto lastShot = std::chrono::steady_clock::now();
        static auto targetEntered = std::chrono::steady_clock::now();
        static uintptr_t triggerTarget = 0;
        const bool triggerActive = cfg.trigger_always_on || cfg.trigger_bind <= 0 ||
            KeyDown(cfg.trigger_bind);
        if (triggerActive) {
            if (!(cfg.trigger_scoped_only && !local_scoped)) {
                bool hit = false;
                uintptr_t hitPawn = 0;
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
                                        if (d < 16.f) { hit = true; hitPawn = p.pawn; }
                                    }
                                } else {
                                    hit = true;
                                    hitPawn = p.pawn;
                                }
                                break;
                            }
                        }
                    }
                }
                if (!hit) {
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
                            if (testPoint(p.head)) { hit = true; hitPawn = p.pawn; break; }
                            continue;
                        }
                        if (p.bones_ok) {
                            for (int bi = 0; bi < 20; ++bi) {
                                if (testPoint(p.bones[bi])) { hit = true; hitPawn = p.pawn; break; }
                            }
                            if (hit) break;
                        } else {
                            if (testPoint(p.head) || testPoint(p.pos)) {
                                hit = true; hitPawn = p.pawn; break;
                            }
                        }
                    }
                }
                if (hit) {
                    const auto now = std::chrono::steady_clock::now();
                    if (hitPawn != triggerTarget) {
                        triggerTarget = hitPawn;
                        targetEntered = now;
                    }
                    const auto reactionMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - targetEntered).count();
                    const auto cooldownMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - lastShot).count();
                    if (reactionMs >= (std::max)(cfg.trigger_delay_ms, 0) && cooldownMs >= 60) {
                        Click();
                        lastShot = now;
                    }
                } else {
                    triggerTarget = 0;
                }
            }
        } else {
            triggerTarget = 0;
        }
    }
}

} // namespace CS2_Aim
