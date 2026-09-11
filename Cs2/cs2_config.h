#pragma once

namespace CS2 {

struct Config {
    // Safe defaults: everything OFF on first run
    bool esp_enabled = false;
    bool self_esp = false;      // draw local player
    // Spotted-state based visibility (m_entitySpottedState + m_bSpotted)
    bool visibility_colors = true;
    bool visible_check = false;
    bool box = false;
    bool box_corner = false;
    bool skeleton = false;
    bool skeleton_joints = false; // joint dots like reference ESP
    bool health_bar = false;
    bool armor_bar = false;
    bool name = false;
    bool distance = false;
    bool team_check = true;
    bool show_bots = true;
    bool head_dot = false;
    bool trails = false;
    bool head_halo = false;
    bool look_direction = false; // Eye Line
    bool chinese_hat = false;
    bool rainbow_trails = true;
    float trail_duration = 0.80f;
    float trail_thickness = 2.0f;
    float look_direction_length = 90.f;
    float skeleton_thickness = 1.85f;
    float snapline_thickness = 1.5f;
    float head_circle_thickness = 1.5f;
    float box_thickness = 1.8f;
    float eye_line_thickness = 1.6f;
    bool snaplines = false;
    bool bomb_timer = false;
    bool spectator_list = false;
    bool smoke_flash = false;
    bool scope_check = false;
    bool recoil_visual = false;
    bool sound_esp = false;
    bool grenade_trail = false;
    bool radar_2d = false;
    bool webradar_enabled = false;
    int webradar_port = 8080;
    bool webradar_cloudflare = false;
    float radar_2d_x = 18.f;
    float radar_2d_y = 18.f;
    float radar_2d_size = 160.f;
    bool skeleton_lod = true;
    float max_distance = 300.f;
    float skeleton_lod_distance = 70.f; // more aggressive default
    bool weapon_icons = false;
    bool offscreen_arrows = false;
    bool highlight_aim_target = true;
    bool c4_carrier = false;
    bool distance_feet = false; // m by default
    bool hotkey_overlay = false;
    bool panic_key_enabled = true;
    int panic_key = 0x23; // END
    bool stream_proof = false;
    bool bone_draw_arms = true;
    bool bone_draw_legs = true;
    bool performance_mode = false;
    bool vsync = false;
    bool show_makcu_status = true;

    float col_enemy[4]    = { 0.90f, 0.25f, 0.25f, 1.f };
    float col_team[4]     = { 0.25f, 0.75f, 0.95f, 1.f };
    float col_skeleton[4] = { 0.95f, 0.85f, 0.35f, 1.f };
    float col_box[4]      = { 0.90f, 0.25f, 0.25f, 1.f };
    float col_name[4]     = { 1.f, 1.f, 1.f, 1.f };
    float col_weapon[4]   = { 0.85f, 0.85f, 0.90f, 1.f };
    float col_target[4]   = { 1.f, 0.85f, 0.15f, 1.f };
    float col_joints[4]   = { 0.27f, 0.90f, 0.37f, 1.f };
    float col_health[4]   = { 0.25f, 0.86f, 0.35f, 1.f };
    float col_armor[4]    = { 0.27f, 0.59f, 1.00f, 1.f };
    float col_snaplines[4]= { 0.83f, 0.69f, 0.22f, 0.55f };
    float col_trail[4]    = { 0.83f, 0.69f, 0.22f, 0.82f };
    float col_halo[4]     = { 1.00f, 0.89f, 0.54f, 0.90f };
    float col_look[4]     = { 0.83f, 0.69f, 0.22f, 0.86f };
    float col_head[4]     = { 0.95f, 0.85f, 0.35f, 1.f };
    float col_distance[4] = { 0.85f, 0.85f, 0.90f, 1.f };
    float col_visible[4]  = { 0.27f, 0.90f, 0.37f, 1.f };
    float col_occluded[4] = { 0.90f, 0.25f, 0.25f, 1.f };

    bool aim_enabled = false;
    bool aim_draw_fov = true;
    bool aim_fov_rgb = false;
    int aim_fov_style = 0; // 0 circle 1 square 2 cross
    bool aim_dynamic_fov = false;
    float aim_fov = 80.f;
    float aim_fov_min = 25.f;
    float aim_smooth = 12.f; // 0 = instant snap, 100 = no pull
    float aim_max_dist = 150.f;
    int aim_bone = 0;
    int aim_bind = 0x01;   // LMB — change in UI (click bind → press key)
    int aim_bind2 = 0x02;  // RMB
    int aim_bind3 = 0;     // optional
    float aim_deadzone = 2.f;
    float sticky_ms = 120.f;
    bool aim_prediction = false;
    bool aim_humanize = true;
    float prediction_strength = 0.35f;
    bool hit_chance_ui = false;
    float aim_switch_cooldown_ms = 90.f;
    bool aim_ignore_team = true;
    bool aim_ignore_spectators = false;
    bool aim_ignore_bots = true;
    float col_fov[4] = { 0.83f, 0.69f, 0.22f, 0.45f };

    bool trigger_enabled = false;
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
    c.aim_smooth = 35.f; // legit: slower pull
    c.aim_deadzone = 3.f;
    c.aim_prediction = false;
    c.aim_dynamic_fov = true;
    c.sticky_ms = 100.f;
    c.aim_switch_cooldown_ms = 120.f;
    c.trigger_enabled = false;
    c.aim_draw_fov = true;
    c.esp_enabled = true;
    c.box = true;
    c.skeleton = true;
    c.health_bar = true;
    c.name = true;
    c.distance = true;
    c.team_check = true;
    c.skeleton_lod = true;
    c.skeleton_lod_distance = 60.f;
    c.max_entities = 24;
    c.highlight_aim_target = true;
}

inline void ApplyRageProfile(Config& c) {
    c.profile = 2;
    c.aim_enabled = true;
    c.aim_fov = 180.f;
    c.aim_smooth = 0.f; // rage: full snap
    c.aim_deadzone = 0.f;
    c.aim_prediction = true;
    c.prediction_strength = 0.55f;
    c.aim_dynamic_fov = false;
    c.sticky_ms = 200.f;
    c.aim_switch_cooldown_ms = 40.f;
    c.trigger_enabled = true;
    c.trigger_delay_ms = 10;
    c.trigger_head_only = false;
    c.aim_draw_fov = true;
    c.esp_enabled = true;
    c.box = true;
    c.skeleton = true;
    c.health_bar = true;
    c.armor_bar = true;
    c.name = true;
    c.distance = true;
    c.weapon_icons = true;
    c.snaplines = true;
    c.team_check = true;
    c.skeleton_lod = false;
    c.max_entities = 48;
    c.highlight_aim_target = true;
    c.offscreen_arrows = true;
}

bool SaveConfig(const char* name = "cs2_default");
bool LoadConfig(const char* name = "cs2_default");
bool SelfTestOffsets();

} // namespace CS2
