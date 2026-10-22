#pragma warning(disable: 4100 4244)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "cs2_aim.h"
#include "src/games/Fivem/aimbot/aim_type.h"
#include "gameplay/aim_controller.h"
#include "gameplay/smooth_curves.h"
#include "imgui.h"
#include <Windows.h>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <array>
#include <string>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

namespace CS2_Aim {
namespace {

int g_last_target_idx = -1;
int g_active_target_idx = -1;
std::chrono::steady_clock::time_point g_last_switch{};
std::chrono::steady_clock::time_point g_target_acquired{};
uintptr_t g_target_pawn = 0;
char g_debug[128] = "aim idle";
int g_frames_sem_bind = 0;
float g_output_residual_x = 0.f;
float g_output_residual_y = 0.f;

struct TargetDiagnostics {
    int scanned = 0;
    int dead = 0;
    int team = 0;
    int bots = 0;
    int spectators = 0;
    int hidden = 0;
    int distance = 0;
    int projection = 0;
    int fov = 0;
    int eligible = 0;
    int retained = 0;
};

TargetDiagnostics g_target_diag{};

OmniGhost::Gameplay::ContinuousAimController g_aim_motion;
Gameplay::SmoothCurves::AimHumanizer g_humanizer;
Gameplay::SmoothCurves::SmoothCurveEvaluator g_curve_evaluator;

struct WeaponRecoilPattern {
    std::array<std::pair<float, float>, 30> offsets{};
    int length = 0;
    float scale = 1.f;
};

static std::unordered_map<int, WeaponRecoilPattern> g_recoil_patterns;
static std::shared_mutex g_recoil_patterns_mutex;
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
    // CSE-006: gate de frescura — os bones em cache podem ficar até 5 s
    // velhos sob pressão DMA (prune lento); o aim não deve seguir
    // esqueletos congelados. Acima de 250 ms cai para pos + offset de
    // hitbox (72 u ≈ altura da cabeça, o mesmo default do ESP).
    const uint64_t now_ms = GetTickCount64();
    const bool bonesFresh = p.bones_ok && p.bone_pose_sample_ms != 0 &&
        now_ms >= p.bone_pose_sample_ms &&
        (now_ms - p.bone_pose_sample_ms) <= 250u;
    if (bonesFresh) {
        const float* selected = p.bones[0];
        switch (bone) {
        case 1: selected = p.bones[1]; break;
        case 2: selected = p.bones[2]; break;
        case 3: selected = p.bones[4]; break;
        default: selected = p.bones[0]; break;
        }
        out[0] = selected[0];
        out[1] = selected[1];
        out[2] = selected[2];
        return;
    }
    float height = 72.f;
    switch (bone) {
    case 1: height = 62.f; break;
    case 2: height = 46.f; break;
    case 3: height = 36.f; break;
    default: break;
    }
    out[0] = p.pos[0];
    out[1] = p.pos[1];
    out[2] = p.pos[2] + height;
}



float SmoothToStrength(float smooth) {
    if (smooth <= 0.f) return 1.f;
    if (smooth >= 100.f) return 0.f;
    const float t = smooth * 0.01f;

    return std::pow(1.f - t, 1.75f);
}



struct PaidAimController {
    float phase = 0.f;
    float phase2 = 0.f;
    void Reset() { phase = phase2 = 0.f; }

    void Step(float err_x, float err_y, float smooth, float humanization, float dt,
              float deadzone, float& out_x, float& out_y) {
        out_x = out_y = 0.f;
        dt = std::clamp(dt, 1.e-4f, 0.05f);
        const float err = std::sqrt(err_x * err_x + err_y * err_y);

        if (err <= deadzone)
            return;

        const float strength = SmoothToStrength(smooth);
        if (strength <= 1.e-6f)
            return;



        float frac;
        if (strength >= 0.999f) {
            frac = 1.f;
        } else {
            const float rate = 4.f + strength * 22.f;
            frac = 1.f - std::exp(-rate * dt * strength);
        }

        float mx = err_x * frac;
        float my = err_y * frac;



        const float h = std::clamp(humanization, 0.f, 100.f) * 0.01f;
        if (h > 0.001f) {
            phase  += dt * (0.85f + 1.4f * h);
            phase2 += dt * (0.55f + 0.9f * h);
            const float speedVar = 1.f + 0.10f * h * std::sin(phase);
            const float curve    = 0.06f * h * std::sin(phase2);
            const float rx = mx * speedVar - my * curve;
            const float ry = my * speedVar + mx * curve;
            const float over = 0.025f * h * std::sin(phase * 0.37f);
            mx = rx * (1.f + over);
            my = ry * (1.f + over);
        }

        out_x = mx;
        out_y = my;
    }
};

static PaidAimController g_paid_aim;

void MoveMouse(float dx, float dy) {


    const float desired_x = dx + g_output_residual_x;
    const float desired_y = dy + g_output_residual_y;
    int whole_x = static_cast<int>(std::lround(desired_x));
    int whole_y = static_cast<int>(std::lround(desired_y));
    g_output_residual_x = std::clamp(desired_x - static_cast<float>(whole_x), -0.95f, 0.95f);
    g_output_residual_y = std::clamp(desired_y - static_cast<float>(whole_y), -0.95f, 0.95f);
    if (whole_x == 0 && whole_y == 0) return;

    int left_x = whole_x;
    int left_y = whole_y;
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
    // CSE-008: honra as teclas secundárias persistidas (como Warzone/FiveM).
    if (!cfg.aim_enabled) return false;
    if (cfg.aim_bind > 0 && KeyDown(cfg.aim_bind)) return true;
    if (cfg.aim_bind2 > 0 && KeyDown(cfg.aim_bind2)) return true;
    if (cfg.aim_bind3 > 0 && KeyDown(cfg.aim_bind3)) return true;
    return false;
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
    const float distance = std::max(distance_m, 0.f);
    for (std::size_t i = 1; i < std::size(cfg.aim_fov_curve_distance); ++i) {
        const float leftDistance = cfg.aim_fov_curve_distance[i - 1];
        const float rightDistance = std::max(cfg.aim_fov_curve_distance[i], leftDistance + .01f);
        if (distance <= rightDistance) {
            const float t = std::clamp((distance - leftDistance) / (rightDistance - leftDistance), 0.f, 1.f);
            return std::lerp(cfg.aim_fov_curve_value[i - 1], cfg.aim_fov_curve_value[i], t);
        }
    }
    return cfg.aim_fov_curve_value[std::size(cfg.aim_fov_curve_value) - 1];
}

float WeaponThreat(int definition) noexcept {
    switch (definition) {
    case 9: case 11: case 38: case 40: return 1.f;
    case 7: case 8: case 10: case 13: case 16: case 39: case 60: return .75f;
    case 1: case 2: case 3: case 4: case 30: case 32: case 36: case 61: case 63: case 64: return .45f;
    default: return .2f;
    }
}

int AutoSelectBone(const CS2::Player& p, int weapon_def, float distance_m, bool auto_bone_enabled, int fallback_bone) {
    if (!auto_bone_enabled) return fallback_bone;

    bool is_sniper = (weapon_def == 9 || weapon_def == 11 || weapon_def == 38 || weapon_def == 40);
    bool is_rifle = (weapon_def == 7 || weapon_def == 8 || weapon_def == 10 || weapon_def == 13 || weapon_def == 16 || weapon_def == 39 || weapon_def == 60);
    bool is_pistol = (weapon_def >= 1 && weapon_def <= 4) || weapon_def == 30 || weapon_def == 32 || weapon_def == 36 || weapon_def == 61 || weapon_def == 63 || weapon_def == 64;

    int preferred = 0;
    if (is_sniper) preferred = 0;
    else if (is_rifle) preferred = (distance_m > 50.f) ? 0 : 1;
    else if (is_pistol) preferred = (distance_m > 30.f) ? 1 : 2;
    else preferred = 0;




    return preferred;
}

bool BoneVisibilityKnown(const CS2::Player& player, int bone) noexcept {
    if (bone < 0 || bone > 3) return false;
    return (player.bone_visibility_known_mask & static_cast<uint8_t>(1u << bone)) != 0;
}

bool BoneVisible(const CS2::Player& player, int bone) noexcept {
    return BoneVisibilityKnown(player, bone) &&
        (player.bone_visibility_mask & static_cast<uint8_t>(1u << bone)) != 0;
}

int SelectAimBone(const CS2::Player& player, const CS2::Config& cfg, int weaponDefinition) {
    const int fallback = std::clamp(cfg.aim_bone, 0, 3);
    if (cfg.aim_visible_bones_only) {
        for (const int configured : cfg.aim_bone_priority) {
            const int bone = std::clamp(configured, 0, 3);
            if (BoneVisible(player, bone)) return bone;
        }
        return -1;
    }
    return cfg.aim_auto_bone
        ? AutoSelectBone(player, weaponDefinition, player.distance, true, fallback)
        : fallback;
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

struct MotionHistory {
    std::array<float, 3> velocity{};
    std::array<float, 3> acceleration{};
    uint64_t sample_ms = 0;
    bool initialized = false;
};

static std::unordered_map<uintptr_t, MotionHistory> g_motion_history;

void EnsureMotionHistoryCapacity() {
    if (g_motion_history.bucket_count() < 128) g_motion_history.reserve(128);
}

float Length3(const std::array<float, 3>& value) noexcept {
    return std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
}

void ClampMagnitude(std::array<float, 3>& value, float maximum) noexcept {
    const float length = Length3(value);
    if (length <= maximum || length <= 0.0001f) return;
    const float scale = maximum / length;
    value[0] *= scale;
    value[1] *= scale;
    value[2] *= scale;
}




void ApplyRobustPrediction(float point[3], const CS2::Player& player,
                           const CS2::Runtime& rt, const CS2::Config& cfg,
                           uint64_t now_ms) {
    if (!cfg.aim_prediction || !player.pawn) return;
    EnsureMotionHistoryCapacity();

    const uint64_t sample_ms = player.position_sample_ms ? player.position_sample_ms :
        (rt.snapshot_timestamp_ms ? rt.snapshot_timestamp_ms : now_ms);
    if (g_motion_history.size() > 96) {
        for (auto it = g_motion_history.begin(); it != g_motion_history.end();) {
            if (!it->second.sample_ms || now_ms - it->second.sample_ms > 5000u)
                it = g_motion_history.erase(it);
            else
                ++it;
        }
    }
    auto& history = g_motion_history[player.pawn];
    std::array<float, 3> raw_velocity{ player.velocity[0], player.velocity[1], player.velocity[2] };
    std::array<float, 3> filtered_velocity = raw_velocity;
    bool reversal = false;

    if (history.initialized) {
        const float dot = raw_velocity[0] * history.velocity[0] +
                          raw_velocity[1] * history.velocity[1] +
                          raw_velocity[2] * history.velocity[2];
        reversal = dot < -25.f && Length3(raw_velocity) > 20.f && Length3(history.velocity) > 20.f;
        const float velocity_alpha = reversal ? .78f : .42f;
        for (int axis = 0; axis < 3; ++axis)
            filtered_velocity[axis] = history.velocity[axis] * (1.f - velocity_alpha) + raw_velocity[axis] * velocity_alpha;
    }

    if (!history.initialized || history.sample_ms != sample_ms) {
        std::array<float, 3> acceleration{};
        if (history.initialized && sample_ms > history.sample_ms) {
            const float dt = std::clamp(static_cast<float>(sample_ms - history.sample_ms) * .001f, .003f, .100f);
            for (int axis = 0; axis < 3; ++axis)
                acceleration[axis] = (filtered_velocity[axis] - history.velocity[axis]) / dt;
            ClampMagnitude(acceleration, 2400.f);
            if (reversal) acceleration = {};
            else for (int axis = 0; axis < 3; ++axis)
                acceleration[axis] = history.acceleration[axis] * .72f + acceleration[axis] * .28f;
        }
        history.velocity = filtered_velocity;
        history.acceleration = acceleration;
        history.sample_ms = sample_ms;
        history.initialized = true;
    } else {
        filtered_velocity = history.velocity;
    }

    const float snapshot_age = sample_ms && now_ms > sample_ms
        ? std::clamp(static_cast<float>(now_ms - sample_ms) * .001f, 0.f, .050f) : 0.f;
    float lead = std::clamp((.004f + snapshot_age) * cfg.prediction_strength, 0.f, .045f);
    if (reversal) lead *= .35f;
    std::array<float, 3> displacement{};
    const float accel_term = reversal ? 0.f : .5f * lead * lead;
    for (int axis = 0; axis < 3; ++axis)
        displacement[axis] = history.velocity[axis] * lead + history.acceleration[axis] * accel_term;



    ClampMagnitude(displacement, std::clamp(2.f + player.distance * .055f, 4.f, 14.f));
    for (int axis = 0; axis < 3; ++axis) point[axis] += displacement[axis];
}

TargetCandidate SelectTarget(const CS2::Runtime& rt, const CS2::Config& cfg,
                             float cx, float cy, const float* cameraMatrix) {
    TargetCandidate best{};
    TargetCandidate incumbent{};
    g_target_diag = {};
    const uint64_t now_ms = GetTickCount64();

    for (std::size_t i = 0; i < rt.players.size(); ++i) {
        const auto& player = rt.players[i];
        if (player.is_local) continue;
        ++g_target_diag.scanned;
        if (!player.alive || player.health <= 0) { ++g_target_diag.dead; continue; }
        if (cfg.aim_ignore_team && player.team == rt.local_team && rt.local_team >= 2) { ++g_target_diag.team; continue; }
        if (cfg.aim_ignore_bots && player.is_bot) { ++g_target_diag.bots; continue; }
        if (cfg.aim_ignore_spectators && player.is_spectator) { ++g_target_diag.spectators; continue; }
        if (cfg.aim_visibility_check && !player.spotted) { ++g_target_diag.hidden; continue; }
        if (player.distance > cfg.aim_max_dist) { ++g_target_diag.distance; continue; }




        const int bone = SelectAimBone(player, cfg, rt.local_weapon_definition);
        if (bone < 0) { ++g_target_diag.hidden; continue; }

        float point[3]{};
        PickBone(player, bone, point);

        ApplyRobustPrediction(point, player, rt, cfg, now_ms);

        float sx = 0.f, sy = 0.f;
        if (!W2S(point, cameraMatrix, sx, sy)) { ++g_target_diag.projection; continue; }
        const float dx = sx - cx;
        const float dy = sy - cy;
        const float screen = std::sqrt(dx * dx + dy * dy);
        if (screen > EffectiveFov(cfg, player.distance)) { ++g_target_diag.fov; continue; }

        ++g_target_diag.eligible;

        float score = screen;
        score += std::clamp(player.distance, 0.f, 300.f) * .018f;
        score += std::clamp(static_cast<float>(player.health), 0.f, 100.f) * .008f;
        if (!player.spotted) score += 18.f;
        score -= WeaponThreat(player.weapon_def) * 2.f;
        TargetCandidate candidate{};
        candidate.index = static_cast<int>(i);
        candidate.pawn = player.pawn;
        candidate.error_x = dx;
        candidate.error_y = dy;
        candidate.screen_distance = screen;
        candidate.distance_m = player.distance;
        candidate.score = score;
        candidate.velocity_x = player.velocity[0];
        candidate.velocity_y = player.velocity[1];
        candidate.velocity_z = player.velocity[2];
        candidate.weapon_def = player.weapon_def;
        candidate.bone = bone;
        if (candidate.pawn == g_target_pawn) incumbent = candidate;
        if (candidate.score < best.score) best = candidate;
    }




    if (incumbent.index >= 0 && best.pawn != incumbent.pawn) {
        const float required_gain = 3.f + std::clamp(cfg.aim_switch_margin, .02f, .50f) * 32.f;
        if ((incumbent.score - best.score) < required_gain) {
            ++g_target_diag.retained;
            return incumbent;
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
    settings.reaction_delay_ms_min = cfg.aim_reaction_min_ms;
    settings.reaction_delay_ms_max = cfg.aim_reaction_max_ms;
    settings.overshoot_px = cfg.aim_overshoot_px;
    settings.micro_jitter_px = cfg.aim_micro_jitter_px;
    settings.minimum_error = .35f;
    settings.prediction = false;
    OmniGhost::Gameplay::ApplyStableDistanceProfile(settings, distance_m);
    settings.minimum_interval_ms = std::clamp(cfg.aim_motion_interval_ms, 1, 20);
    settings.max_step = std::clamp(cfg.aim_max_step, 0, 48);
    settings.ema_alpha = std::clamp(cfg.aim_ema_alpha, .15f, .85f);
    settings.reversal_damping = std::clamp(cfg.aim_reversal_damping, .05f, .75f);
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
    std::unique_lock lock(g_recoil_patterns_mutex);
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

    add_pattern(7, {
        {0,0}, {0.5,2.1}, {1.2,3.8}, {1.8,5.2}, {2.1,6.5}, {2.3,7.8}, {2.4,9.1}, {2.3,10.3}, {2.1,11.4}, {1.8,12.4},
        {1.4,13.3}, {1.0,14.1}, {0.5,14.8}, {0,15.4}, {-0.5,15.9}, {-1.0,16.3}, {-1.5,16.6}, {-2.0,16.8}, {-2.4,16.9}, {-2.8,16.9},
        {-3.1,16.8}, {-3.3,16.6}, {-3.4,16.3}, {-3.4,15.8}, {-3.3,15.2}, {-3.1,14.5}, {-2.8,13.7}, {-2.4,12.8}, {-2.0,11.9}, {-1.5,10.9}
    }, 1.0f);

    add_pattern(8, {
        {0,0}, {0.3,1.8}, {0.8,3.2}, {1.2,4.5}, {1.5,5.6}, {1.7,6.6}, {1.8,7.5}, {1.8,8.3}, {1.7,9.0}, {1.5,9.7},
        {1.2,10.3}, {0.8,10.8}, {0.4,11.2}, {0,11.5}, {-0.4,11.7}, {-0.8,11.8}, {-1.2,11.8}, {-1.5,11.7}, {-1.8,11.5}, {-2.0,11.2},
        {-2.2,10.8}, {-2.3,10.3}, {-2.4,9.7}, {-2.4,9.1}, {-2.3,8.4}, {-2.2,7.6}, {-2.0,6.8}, {-1.7,5.9}, {-1.4,5.0}, {-1.0,4.1}
    }, 0.85f);

    add_pattern(9, {
        {0,0}, {0,12.0}, {0,24.0}, {0,36.0}, {0,48.0}, {0,60.0}, {0,72.0}, {0,84.0}, {0,96.0}, {0,108.0},
        {0,120.0}, {0,132.0}, {0,144.0}, {0,156.0}, {0,168.0}, {0,180.0}, {0,192.0}, {0,204.0}, {0,216.0}, {0,228.0},
        {0,240.0}, {0,252.0}, {0,264.0}, {0,276.0}, {0,288.0}, {0,300.0}, {0,312.0}, {0,324.0}, {0,336.0}, {0,348.0}
    }, 1.0f);

    add_pattern(10, {
        {0,0}, {0.4,2.0}, {1.0,3.6}, {1.5,5.0}, {1.8,6.2}, {2.0,7.3}, {2.1,8.3}, {2.1,9.2}, {2.0,10.0}, {1.8,10.7},
        {1.5,11.3}, {1.1,11.8}, {0.6,12.2}, {0,12.5}, {-0.5,12.7}, {-1.0,12.8}, {-1.5,12.8}, {-1.9,12.7}, {-2.3,12.5}, {-2.6,12.2},
        {-2.8,11.7}, {-3.0,11.1}, {-3.1,10.4}, {-3.1,9.6}, {-3.0,8.8}, {-2.8,7.9}, {-2.5,7.0}, {-2.1,6.0}, {-1.7,5.1}, {-1.2,4.1}
    }, 0.9f);

    add_pattern(13, {
        {0,0}, {0.6,2.2}, {1.4,4.0}, {2.0,5.5}, {2.4,6.8}, {2.6,8.0}, {2.7,9.1}, {2.7,10.1}, {2.5,11.0}, {2.2,11.8},
        {1.8,12.5}, {1.3,13.1}, {0.7,13.6}, {0,14.0}, {-0.7,14.3}, {-1.4,14.5}, {-2.0,14.6}, {-2.6,14.6}, {-3.1,14.5}, {-3.6,14.3},
        {-4.0,14.0}, {-4.3,13.5}, {-4.5,12.8}, {-4.6,12.1}, {-4.6,11.3}, {-4.5,10.4}, {-4.3,9.5}, {-4.0,8.5}, {-3.6,7.5}, {-3.1,6.5}
    }, 0.95f);

    add_pattern(16, {
        {0,0}, {0.4,1.9}, {1.0,3.5}, {1.5,4.9}, {1.8,6.1}, {2.0,7.2}, {2.1,8.1}, {2.1,9.0}, {2.0,9.8}, {1.8,10.5},
        {1.5,11.1}, {1.1,11.6}, {0.6,12.0}, {0,12.3}, {-0.5,12.5}, {-1.0,12.6}, {-1.4,12.6}, {-1.8,12.5}, {-2.1,12.3}, {-2.4,12.0},
        {-2.6,11.6}, {-2.7,11.1}, {-2.8,10.5}, {-2.8,9.8}, {-2.7,9.1}, {-2.5,8.3}, {-2.3,7.5}, {-2.0,6.6}, {-1.6,5.7}, {-1.2,4.7}
    }, 0.9f);

    add_pattern(60, {
        {0,0}, {0.3,1.7}, {0.8,3.1}, {1.2,4.3}, {1.5,5.4}, {1.7,6.4}, {1.8,7.2}, {1.8,8.0}, {1.7,8.7}, {1.5,9.3},
        {1.2,9.9}, {0.9,10.3}, {0.5,10.7}, {0,11.0}, {-0.4,11.2}, {-0.8,11.3}, {-1.2,11.3}, {-1.5,11.2}, {-1.8,11.0}, {-2.0,10.7},
        {-2.2,10.3}, {-2.3,9.9}, {-2.3,9.3}, {-2.3,8.7}, {-2.2,8.0}, {-2.0,7.3}, {-1.8,6.5}, {-1.5,5.7}, {-1.2,4.9}, {-0.9,4.1}
    }, 0.85f);

    add_pattern(1, {
        {0,0}, {0,3.5}, {0,6.8}, {0,9.8}, {0,12.5}, {0,15.0}, {0,17.2}, {0,19.2}, {0,21.0}, {0,22.6},
        {0,24.0}, {0,25.3}, {0,26.4}, {0,27.4}, {0,28.2}, {0,28.9}, {0,29.5}, {0,30.0}, {0,30.4}, {0,30.7},
        {0,30.9}, {0,31.0}, {0,31.1}, {0,31.1}, {0,31.0}, {0,30.9}, {0,30.7}, {0,30.4}, {0,30.1}, {0,29.7}
    }, 1.2f);

    add_pattern(4, {
        {0,0}, {0.2,1.5}, {0.5,2.8}, {0.8,3.9}, {1.0,4.8}, {1.1,5.6}, {1.2,6.3}, {1.2,6.9}, {1.1,7.4}, {1.0,7.8},
        {0.8,8.1}, {0.5,8.3}, {0.2,8.5}, {0,8.6}, {-0.2,8.6}, {-0.5,8.5}, {-0.8,8.4}, {-1.1,8.2}, {-1.4,7.9}, {-1.6,7.5},
        {-1.8,7.1}, {-1.9,6.6}, {-2.0,6.1}, {-2.0,5.5}, {-2.0,4.9}, {-1.9,4.3}, {-1.8,3.7}, {-1.6,3.1}, {-1.4,2.5}, {-1.1,1.9}
    }, 0.7f);

    add_pattern(61, {
        {0,0}, {0.2,1.4}, {0.5,2.6}, {0.7,3.6}, {0.9,4.5}, {1.0,5.2}, {1.0,5.9}, {1.0,6.4}, {0.9,6.9}, {0.8,7.3},
        {0.6,7.7}, {0.3,7.9}, {0,8.1}, {-0.3,8.1}, {-0.6,8.1}, {-0.9,8.0}, {-1.2,7.8}, {-1.5,7.5}, {-1.7,7.1}, {-1.9,6.6},
        {-2.0,6.1}, {-2.1,5.5}, {-2.1,4.9}, {-2.1,4.2}, {-2.0,3.6}, {-1.9,2.9}, {-1.7,2.3}, {-1.5,1.7}, {-1.2,1.1}, {-0.9,0.5}
    }, 0.75f);
}

std::pair<float, float> CalculateRCS(const CS2::Runtime& rt, const CS2::Config& cfg, int weapon_def) {
    if (!cfg.aim_rcs_auto && !cfg.aim_rcs_standalone) return {};
    if (!rt.local_pawn) return {};



    const uint64_t nowMs = GetTickCount64();
    if (!rt.local_aim_sample_ms || nowMs < rt.local_aim_sample_ms ||
        nowMs - rt.local_aim_sample_ms > 100u)
        return {};
    const int shots_fired = rt.local_shots_fired;

    if (shots_fired <= 1) {
        g_shots_fired = 0;
        g_last_shots_fired = 0;
        return {};
    }

    if (shots_fired != g_last_shots_fired) {
        g_shots_fired = shots_fired - 1;
        g_last_shots_fired = shots_fired;
    }

    if (g_shots_fired <= 0) return {};

    LoadRecoilPatterns();

    WeaponRecoilPattern pattern{};
    {
        std::shared_lock lock(g_recoil_patterns_mutex);
        auto it = g_recoil_patterns.find(weapon_def);
        if (it == g_recoil_patterns.end()) return {};
        pattern = it->second;
    }
    if (pattern.length == 0) return {};

    int idx = std::clamp(g_shots_fired - 1, 0, pattern.length - 1);
    const float punch_x = rt.local_aim_punch[0];
    const float punch_y = rt.local_aim_punch[1];

    float pattern_x = pattern.offsets[idx].first * pattern.scale * cfg.aim_rcs_x;
    float pattern_y = pattern.offsets[idx].second * pattern.scale * cfg.aim_rcs_y;

    float rcs_x = -punch_y * cfg.aim_rcs_x + pattern_x;
    float rcs_y = -punch_x * cfg.aim_rcs_y + pattern_y;

    if (std::fabs(rcs_x) <= 0.08f && std::fabs(rcs_y) <= 0.08f)
        return {};
    return { rcs_x, rcs_y };
}






bool HandlePanic() {
    if (!CS2::config.panic_key_enabled || CS2::config.panic_key == 0) return false;
    static bool was_down = false;
    const bool down = KeyDown(CS2::config.panic_key);
    if (down && !was_down) {
        CS2::config.aim_enabled = false;
        CS2::config.trigger_enabled = false;
        CS2::config.radar_2d = false;
        CS2::config.bomb_timer = false;
        CS2::SubmitAcquisitionConfig(CS2::config);
        MessageBeep(MB_OK);
        std::snprintf(g_debug, sizeof(g_debug), "PANIC — tudo OFF");
        was_down = down;
        return true;
    }
    was_down = down;
    return false;
}

}

int ActiveTargetIndex() { return g_active_target_idx; }
const char* DebugStatus() { return g_debug; }

void Run(const CS2::Runtime& rt, const CS2::Config& cfg_in) {
    static std::string motion_map;
    const std::string current_map = rt.map_name;
    if (current_map != motion_map) {
        g_motion_history.clear();
        motion_map = current_map;
    }


    CS2::Config cfg = cfg_in;
    if (HandlePanic())
        cfg = CS2::config;

    InitializeHumanizer();

    const bool playable = rt.in_match || !rt.players.empty();
    if (!playable) {
        AimKeyDown(CS2::Config{});
        g_active_target_idx = -1;
        g_target_pawn = 0;
        g_paid_aim.Reset();
        g_output_residual_x = g_output_residual_y = 0.f;
        g_shots_fired = 0;
        g_last_shots_fired = 0;
        std::snprintf(g_debug, sizeof(g_debug), "sem partida");
        return;
    }
    if (!cfg.aim_enabled && !cfg.trigger_enabled && !cfg.aim_rcs_standalone) {
        AimKeyDown(cfg);
        g_active_target_idx = -1;
        g_target_pawn = 0;
        g_paid_aim.Reset();
        g_output_residual_x = g_output_residual_y = 0.f;
        std::snprintf(g_debug, sizeof(g_debug), "aim/trig OFF");
        return;
    }

    const int local_weapon_definition = rt.local_weapon_definition;




    if (local_weapon_definition > 0 && !IsFirearm(local_weapon_definition)) {
        g_active_target_idx = -1;
        g_last_target_idx = -1;
        g_target_pawn = 0;
        g_paid_aim.Reset();
        g_output_residual_x = g_output_residual_y = 0.f;
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



    const auto updateCamera = CS2::SelectCameraSnapshot(
        rt.world_sample_ms ? rt.world_sample_ms : rt.snapshot_timestamp_ms);
    const float* updateViewMatrix = updateCamera.timestamp_ms
        ? updateCamera.view_matrix : rt.view_matrix;
    const bool aim_key_down = AimKeyDown(cfg);
    TargetCandidate target{};
    float aim_output_x = 0.f;
    float aim_output_y = 0.f;
    if (aim_key_down) target = SelectTarget(rt, cfg, cx, cy, updateViewMatrix);

    if (cfg.aim_draw_fov) {
        const float previewDistance = target.index >= 0 ? target.distance_m :
            cfg.aim_fov_curve_distance[2];
        const float radius = std::clamp(EffectiveFov(cfg, previewDistance), 1.f, 1000.f);
        const ImU32 color = IM_COL32(
            static_cast<int>(std::clamp(cfg.col_fov[0], 0.f, 1.f) * 255.f),
            static_cast<int>(std::clamp(cfg.col_fov[1], 0.f, 1.f) * 255.f),
            static_cast<int>(std::clamp(cfg.col_fov[2], 0.f, 1.f) * 255.f),
            static_cast<int>(std::clamp(cfg.col_fov[3], 0.f, 1.f) * 255.f));
        ImGui::GetBackgroundDrawList()->AddCircle(ImVec2(cx, cy), radius, color, 96, 1.25f);
    }

    if (!aim_key_down) {
        g_active_target_idx = -1;
        g_last_target_idx = -1;
        g_target_pawn = 0;
        g_paid_aim.Reset();
        g_output_residual_x = g_output_residual_y = 0.f;
        std::snprintf(g_debug, sizeof(g_debug), "aim: aguarda tecla");
    } else if (target.index >= 0) {
        const auto now = std::chrono::steady_clock::now();
        if (g_target_pawn && target.pawn != g_target_pawn) {
            const auto held_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - g_target_acquired).count();


            const float lock_ms = (std::max)(cfg.sticky_ms, cfg.aim_switch_cooldown_ms) +
                std::clamp(cfg.aim_switch_margin, 0.f, .50f) * 200.f;
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
                    const int bone = SelectAimBone(held, cfg, rt.local_weapon_definition);
                    if (bone < 0) break;
                    PickBone(held, bone, point);
                    // CSE-006: o path de retenção sticky não aplicava a
                    // predição robusta ao contrário do path principal.
                    const uint64_t sticky_now_ms = GetTickCount64();
                    ApplyRobustPrediction(point, held, rt, cfg, sticky_now_ms);
                    float sx = 0.f, sy = 0.f;
                    if (held.alive && held.health > 0 && W2S(point, updateViewMatrix, sx, sy)) {
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
            g_paid_aim.Reset();
        }
        g_active_target_idx = target.index;
        g_last_target_idx = target.index;





        const int boneFixed = std::clamp(cfg.aim_bone, 0, 3);
        (void)boneFixed;
        const float dt = ImGui::GetIO().DeltaTime;
        const float deadzone = (std::max)(1.0f, cfg.aim_deadzone > 0.f ? cfg.aim_deadzone : 1.5f);
        float mx = 0.f, my = 0.f;
        g_paid_aim.Step(target.error_x, target.error_y,
                        cfg.aim_smooth,
                        cfg.aim_humanization,
                        dt,
                        deadzone,
                        mx, my);
        aim_output_x = mx;
        aim_output_y = my;
        if (std::fabs(mx) < 0.01f && std::fabs(my) < 0.01f)
            std::snprintf(g_debug, sizeof(g_debug), "aim: on target (deadzone/smooth100)");
        else
            std::snprintf(g_debug, sizeof(g_debug),
                "lock %.1f,%.1f err=%.1f bone=%d smooth=%.0f hum=%.0f",
                mx, my,
                std::sqrt(target.error_x * target.error_x + target.error_y * target.error_y),
                std::clamp(cfg.aim_bone, 0, 3), cfg.aim_smooth, cfg.aim_humanization);
    } else {
        g_active_target_idx = -1;
        g_paid_aim.Reset();
        if (g_target_diag.hidden)
            std::snprintf(g_debug, sizeof(g_debug), "sem alvo: filt_vis=%d fov=%d", g_target_diag.hidden, g_target_diag.fov);
        else if (g_target_diag.fov)
            std::snprintf(g_debug, sizeof(g_debug), "sem alvo: fora FOV=%d", g_target_diag.fov);
        else if (g_target_diag.distance)
            std::snprintf(g_debug, sizeof(g_debug), "sem alvo: distancia=%d", g_target_diag.distance);
        else if (g_target_diag.team)
            std::snprintf(g_debug, sizeof(g_debug), "sem alvo: equipa=%d", g_target_diag.team);
        else if (g_target_diag.scanned == 0)
            std::snprintf(g_debug, sizeof(g_debug), "sem alvo: sem jogadores validos");
        else
            std::snprintf(g_debug, sizeof(g_debug), "sem alvo: projecao=%d", g_target_diag.projection);
    }



    if (cfg.aim_rcs_standalone || (cfg.aim_enabled && cfg.aim_rcs_auto && aim_key_down)) {
        const auto [rcs_x, rcs_y] = CalculateRCS(rt, cfg, local_weapon_definition);
        aim_output_x += rcs_x;
        aim_output_y += rcs_y;
    }
    if (std::fabs(aim_output_x) > 0.001f || std::fabs(aim_output_y) > 0.001f)
        MoveMouse(aim_output_x, aim_output_y);



    const bool local_scoped = rt.local_scoped;

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
                            // CS-011: enquanto a equipa local é desconhecida
                            // (<2), não dispara com o team check ativo — não
                            // falha aberto em colegas nos primeiros ~500 ms.
                            if (cfg.trigger_team_check &&
                                (rt.local_team < 2 || p.team == rt.local_team)) continue;
                            if (!p.alive || p.health <= 0) continue;
                            if (cfg.aim_visibility_check && !p.spotted) continue;
                            if (p.ent_index == idEnt || p.ent_index == (idEnt & 0x7FFF)) {
                                if (cfg.trigger_head_only) {
                                    // CSE-006: cabeça com gate de frescura —
                                    // bones velhos caem para pos + altura.
                                    const uint64_t identNowMs = GetTickCount64();
                                    const bool identBonesFresh = p.bones_ok &&
                                        p.bone_pose_sample_ms != 0 &&
                                        identNowMs >= p.bone_pose_sample_ms &&
                                        (identNowMs - p.bone_pose_sample_ms) <= 250u;
                                    float identHead[3] = { p.head[0], p.head[1], p.head[2] };
                                    if (!identBonesFresh) {
                                        identHead[0] = p.pos[0];
                                        identHead[1] = p.pos[1];
                                        identHead[2] = p.pos[2] + 72.f;
                                    }
                                    float sx, sy;
                                    if (W2S(identHead, updateViewMatrix, sx, sy)) {
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
                        // CS-011: idem — conservador com equipa local desconhecida.
                        if (cfg.trigger_team_check &&
                            (rt.local_team < 2 || p.team == rt.local_team)) continue;
                        if (!p.alive || p.health <= 0) continue;
                        if (cfg.aim_visibility_check && !p.spotted) continue;
                        auto testPoint = [&](const float* w) {
                            float sx = 0.f, sy = 0.f;
                            if (!W2S(w, updateViewMatrix, sx, sy)) return false;
                            const float d = std::sqrt((sx - cx) * (sx - cx) + (sy - cy) * (sy - cy));
                            return d < triggerFov;
                        };
                        // CSE-006: gate de frescura — bones em cache podem
                        // estar até 5 s velhos sob pressão DMA; o trigger
                        // segue a posição atual + altura de hitbox.
                        const uint64_t trigNowMs = GetTickCount64();
                        const bool trigBonesFresh = p.bones_ok && p.bone_pose_sample_ms != 0 &&
                            trigNowMs >= p.bone_pose_sample_ms &&
                            (trigNowMs - p.bone_pose_sample_ms) <= 250u;
                        float headPoint[3] = { p.head[0], p.head[1], p.head[2] };
                        if (!trigBonesFresh) {
                            headPoint[0] = p.pos[0];
                            headPoint[1] = p.pos[1];
                            headPoint[2] = p.pos[2] + 72.f;
                        }
                        if (cfg.trigger_head_only) {
                            if (testPoint(headPoint)) { hit = true; hitPawn = p.pawn; break; }
                            continue;
                        }
                        if (trigBonesFresh) {
                            for (int bi = 0; bi < 20; ++bi) {
                                if (testPoint(p.bones[bi])) { hit = true; hitPawn = p.pawn; break; }
                            }
                            if (hit) break;
                        } else {
                            if (testPoint(headPoint) || testPoint(p.pos)) {
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
                    const int weaponDelayMs = local_weapon_definition == 64 ? 90 : 0;
                    if (reactionMs >= (std::max)(cfg.trigger_delay_ms, 0) + weaponDelayMs && cooldownMs >= 60) {
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

}
