#pragma once

namespace CS2 {

struct Config {
    // Legacy visual preferences are retained for backwards-compatible config files.
    // They have no renderer or menu in CS2.
    bool esp_enabled = false, self_esp = false, visibility_colors = true, visible_check = false;
    bool box = true, box_corner = false, skeleton = true, skeleton_joints = false;
    bool health_bar = true, armor_bar = false, name = true, distance = true, team_check = true, show_bots = true;
    bool head_dot = false, weapon_name = false, trails = false, rgb_mode = false, head_halo = false, look_direction = false;
    bool box_fill = false, health_value = false, armor_value = false, text_outline = true;
    int box_style = 0, snapline_position = 2; // normal, dynamic, rounded, corner / top, center, bottom
    float box_rounding = 0.f, text_outline_thickness = 1.f;
    bool chinese_hat = false, angel_wings = false, devil_horns = false, floating_crown = false;
    bool fun_effects_rainbow = true, hit_marker = false, rainbow_trails = true, snaplines = false;
    float chinese_hat_scale = 1.f, fun_effects_scale = 1.f, trail_duration = .8f, trail_thickness = 2.f;
    float look_direction_length = 90.f, skeleton_thickness = 1.85f, snapline_thickness = 1.5f;
    float head_circle_thickness = 1.5f, box_thickness = 1.8f, eye_line_thickness = 1.6f;
    bool bomb_timer = false;
    bool spectator_list = false;
    float spectator_window_x = -1.f;
    float spectator_window_y = 40.f;
    float bomb_window_x = -1.f;
    float bomb_window_y = 338.f;
    bool smoke_flash = false, scope_check = false, sound_esp = false, footstep_esp = false;
    bool player_flags = false, weapon_ammo = false, dropped_weapons = false, projectile_timers = false, grenade_trail = false;
    bool radar_2d = false;
    bool webradar_enabled = false;
    int webradar_port = 8080;
    bool webradar_cloudflare = false;
    float radar_2d_x = 18.f;
    float radar_2d_y = 18.f;
    float radar_2d_size = 160.f;
    bool skeleton_lod = true, weapon_icons = false, offscreen_arrows = false, highlight_aim_target = false;
    bool c4_carrier = false, distance_feet = false, bone_draw_arms = true, bone_draw_legs = true;
    float max_distance = 300.f, skeleton_lod_distance = 70.f;
    bool hotkey_overlay = false;
    bool panic_key_enabled = true;
    int panic_key = 0x23; // END
    bool stream_proof = false;
    bool performance_mode = false;
    bool telemetry_enabled = false; // PERF/SPIKE lines -> logs.txt (active features only)
    bool vsync = false;
    bool show_makcu_status = true;

    float col_enemy[4] = {.9f,.25f,.25f,1.f}, col_team[4] = {.25f,.75f,.95f,1.f};
    float col_skeleton[4] = {.95f,.85f,.35f,1.f}, col_box[4] = {.9f,.25f,.25f,1.f};
    float col_box_corner[4] = {0.f,.78f,1.f,1.f}, col_name[4] = {1.f,1.f,1.f,1.f};
    float col_weapon[4] = {.85f,.85f,.9f,1.f}, col_target[4] = {1.f,.85f,.15f,1.f};
    float col_joints[4] = {.27f,.9f,.37f,1.f}, col_health[4] = {.25f,.86f,.35f,1.f};
    float col_armor[4] = {.27f,.59f,1.f,1.f}, col_snaplines[4] = {.83f,.69f,.22f,.55f};
    float col_trail[4] = {.83f,.69f,.22f,.82f}, col_halo[4] = {1.f,.89f,.54f,.9f};
    float col_fun_effects[4] = {1.f,.72f,.18f,.95f}, col_look[4] = {.83f,.69f,.22f,.86f};
    float col_head[4] = {.95f,.85f,.35f,1.f}, col_distance[4] = {.85f,.85f,.9f,1.f};
    float col_visible[4] = {.27f,.9f,.37f,1.f}, col_occluded[4] = {.9f,.25f,.25f,1.f};
    float col_box_fill[4] = {.9f,.25f,.25f,.16f}, col_text_outline[4] = {0.f,0.f,0.f,1.f};


    bool aim_enabled = false;
    bool aim_draw_fov = true;
    bool sniper_crosshair = false;
    bool aim_fov_rgb = false;
    int aim_fov_style = 0; // 0 circle 1 square 2 cross
    bool aim_dynamic_fov = false;
    float aim_fov = 80.f;
    float aim_fov_min = 25.f;
    float aim_smooth = 12.f; // 0 = instant snap, 100 = no pull
    float aim_max_dist = 150.f;
    int aim_bone = 0; // aim_point: 0 Head, 1 Neck, 2 Chest, 3 Stomach
    int aim_bind = 0x01;   // LMB — change in UI (click bind → press key)
    int aim_bind2 = 0x02;  // RMB
    int aim_bind3 = 0;     // optional
    float aim_deadzone = 2.f;
    float sticky_ms = 120.f;
    bool aim_prediction = false;
    bool aim_humanize = false; // derived from aim_humanization > 0
    float aim_humanization = 0.f; // 0..100 — trajectory shape only
    float prediction_strength = 0.35f;
    bool hit_chance_ui = false;
    float aim_switch_cooldown_ms = 90.f;
    float aim_switch_margin = 0.16f;   // retain a valid target unless a new one is clearly better
    int aim_reaction_min_ms = 30;
    int aim_reaction_max_ms = 80;
    float aim_overshoot_px = 0.55f;
    float aim_micro_jitter_px = 0.06f;
    int aim_motion_interval_ms = 6;
    int aim_max_step = 0;              // 0 = adaptive distance profile
    float aim_ema_alpha = 0.40f;
    float aim_reversal_damping = 0.16f;
    bool aim_ignore_team = true;
    bool aim_ignore_spectators = false;
    bool aim_ignore_bots = true;
    bool aim_auto_bone = false;         // fixed aim point only (head/neck/chest/stomach)
    bool aim_visibility_check = false;  // ON: only aim at spotted targets; OFF: aim through walls (spotted-state, not raytrace)
    bool aim_rcs_standalone = false;    // RCS without aimbot
    bool aim_rcs_auto = true;           // auto-detect weapon & apply inverse pattern
    float aim_rcs_x = 1.f;              // horizontal strength multiplier
    float aim_rcs_y = 1.f;              // vertical strength multiplier
    float col_fov[4] = { 0.83f, 0.69f, 0.22f, 0.45f };

    bool trigger_enabled = false;
    bool trigger_always_on = true;
    int trigger_bind = 0x12; // ALT
    int trigger_delay_ms = 30;
    bool trigger_team_check = true;
    bool trigger_scoped_only = false;
    bool trigger_head_only = false;
    bool trigger_use_ident = true; // prefer m_iIDEntIndex

    int profile = 0; // 0 custom, 1 legit, 2 rage

    int max_entities = 32;
    bool auto_entity_cap = true;

    bool show_fps = true;
    bool auto_dump_on_mismatch = true;
    char server_config_key[64] = {};
};

extern Config config;

inline void ApplyLegitProfile(Config& c) {
    c.profile = 1;
    c.aim_enabled = true;
    c.aim_fov = 35.f;
    c.aim_smooth = 35.f;
    c.aim_deadzone = 3.f;
    c.aim_prediction = true;
    c.prediction_strength = 0.25f;
    c.aim_dynamic_fov = true;
    c.aim_fov_min = 15.f;
    c.aim_humanize = true;
    c.aim_humanization = 35.f;
    c.aim_auto_bone = false;
    c.aim_visibility_check = true;
    c.aim_ignore_bots = true;
    c.aim_ignore_spectators = true;
    c.aim_rcs_standalone = false;
    c.aim_rcs_auto = true;
    c.aim_rcs_x = 1.f;
    c.aim_rcs_y = 1.f;
    c.sticky_ms = 100.f;
    c.aim_switch_cooldown_ms = 120.f;
    c.trigger_enabled = false;
    c.aim_draw_fov = true;
    c.team_check = true;
    c.skeleton_lod = true;
    c.skeleton_lod_distance = 60.f;
    c.max_entities = 24;
    c.highlight_aim_target = false;
}

inline void ApplyRageProfile(Config& c) {
    c.profile = 2;
    c.aim_enabled = true;
    c.aim_fov = 180.f;
    c.aim_smooth = 0.f;
    c.aim_deadzone = 0.f;
    c.aim_prediction = true;
    c.prediction_strength = 0.55f;
    c.aim_dynamic_fov = false;
    c.aim_humanize = false;
    c.aim_humanization = 0.f;
    c.aim_auto_bone = false;
    c.aim_visibility_check = false;
    c.aim_ignore_bots = false;
    c.aim_ignore_spectators = false;
    c.aim_rcs_standalone = false;
    c.aim_rcs_auto = true;
    c.aim_rcs_x = 1.f;
    c.aim_rcs_y = 1.f;
    c.sticky_ms = 200.f;
    c.aim_switch_cooldown_ms = 40.f;
    c.trigger_enabled = true;
    c.trigger_delay_ms = 10;
    c.trigger_head_only = false;
    c.aim_draw_fov = true;
    c.team_check = true;
    c.skeleton_lod = false;
    c.max_entities = 48;
    c.highlight_aim_target = false;
    c.offscreen_arrows = true;
}

bool SaveConfig(const char* name = "cs2_default");
bool LoadConfig(const char* name = "cs2_default");
bool SelfTestOffsets();

} // namespace CS2
