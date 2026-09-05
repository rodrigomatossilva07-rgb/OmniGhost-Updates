#pragma once

namespace Valorant {

// Valorant-specific OmniGhost menu pages drive these runtime configuration flags.
struct Config {
    bool esp_enabled = false;
    bool self_esp = false;
    bool visibility_colors = true;
    bool team_check = true;
    bool box = false;
    bool box_corner = false;
    bool skeleton = false;
    bool skeleton_joints = true;
    bool health_bar = false;
    bool armor_bar = false; // shield
    bool name = false;
    bool distance = false;
    bool head_dot = false;
    bool snaplines = false;
    bool weapon_name = false;
    float max_distance = 250.f;
    float skeleton_lod_distance = 80.f;
    bool skeleton_lod = true;
    bool bone_draw_arms = true;
    bool bone_draw_legs = true;
    bool performance_mode = false;
    bool show_makcu_status = true;
    bool highlight_aim_target = true;

    float col_enemy[4]    = { 0.95f, 0.28f, 0.32f, 1.f };
    float col_team[4]     = { 0.30f, 0.75f, 0.95f, 1.f };
    float col_skeleton[4] = { 0.95f, 0.85f, 0.35f, 1.f };
    float col_box[4]      = { 0.95f, 0.28f, 0.32f, 1.f };
    float col_name[4]     = { 1.f, 1.f, 1.f, 1.f };
    float col_weapon[4]   = { 0.85f, 0.85f, 0.90f, 1.f };
    float col_target[4]   = { 1.f, 0.85f, 0.15f, 1.f };
    float col_joints[4]   = { 0.27f, 0.90f, 0.37f, 1.f };
    float col_health[4]   = { 0.25f, 0.86f, 0.35f, 1.f };
    float col_armor[4]    = { 0.35f, 0.65f, 1.00f, 1.f };
    float col_snaplines[4]= { 0.83f, 0.69f, 0.22f, 0.55f };

    bool aim_enabled = false;
    bool aim_draw_fov = true;
    bool aim_fov_rgb = false;
    int aim_fov_style = 0;
    float aim_fov = 70.f;
    float aim_smooth = 14.f; // 0 = snap, 100 = no pull
    float aim_max_dist = 120.f;
    int aim_bone = 0; // 0 head
    int aim_bind = 0x02;  // RMB default (safer)
    int aim_bind2 = 0;
    float aim_deadzone = 2.f;
    float sticky_ms = 120.f;
    bool aim_prediction = false;
    bool aim_humanize = true;
    float col_fov[4] = { 0.83f, 0.69f, 0.22f, 0.45f };

    bool trigger_enabled = false;
    int trigger_bind = 0x12;
    int trigger_delay_ms = 30;
    bool trigger_team_check = true;
    bool trigger_head_only = true;

    bool show_fps = true;
    int max_actors = 64;
};

extern Config config;

} // namespace Valorant
