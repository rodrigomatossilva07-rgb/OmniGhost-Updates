#pragma once

#include "gameplay/esp_config_base.h"

namespace CS2 {

struct Config : OmniGhost::Gameplay::EspConfigBase {
    // CS2-specific additions
    int box_style = 0;
    bool vsync = false;
    bool self_esp = false;
    bool show_bots = true;
    bool scope_check = false;
    bool footstep_esp = false;
    bool player_flags = false;
    bool flag_blind = true;
    bool flag_scoped = true;
    bool flag_defusing = true;
    bool flag_kit = true;
    bool flag_money = true;
    bool bomb_timer = false;
    bool spectator_list = false;
    float spectator_window_x = -1.f;
    float spectator_window_y = 40.f;
    float bomb_window_x = -1.f;
    float bomb_window_y = 338.f;
    bool dropped_weapons = false;
    bool dropped_weapon_pistols = true;
    bool dropped_weapon_smgs = true;
    bool dropped_weapon_rifles = true;
    bool dropped_weapon_heavy = true;
    bool dropped_weapon_icons = true;
    bool dropped_weapon_ammo = false;
    bool radar_2d = false;
    bool webradar_enabled = false;
    int webradar_port = 8080;
    bool webradar_cloudflare = false;
    float radar_2d_x = 18.f;
    float radar_2d_y = 18.f;
    float radar_2d_size = 160.f;
    bool hotkey_overlay = false;
    bool panic_key_enabled = true;
    int panic_key = 0x23; // END
    bool stream_proof = false;
    bool show_makcu_status = true;
    int max_entities = 32;
    bool auto_entity_cap = true;
    bool show_fps = true;
    bool auto_dump_on_mismatch = true;
    char server_config_key[64] = {};

    // Aim
    bool aim_enabled = false;
    bool aim_draw_fov = true;
    bool aim_fov_rgb = false;
    int aim_fov_style = 0; // 0 circle 1 square 2 cross
    bool aim_dynamic_fov = false;
    float aim_fov = 80.f;
    float aim_fov_min = 25.f;
    float aim_smooth = 12.f; // 0 = instant snap, 100 = no pull
    float aim_max_dist = 150.f;
    int aim_bone = 0; // aim_point: 0 Head, 1 Neck, 2 Chest, 3 Stomach
    int aim_bind = 0x02;   // RMB by default; configurable in the aim page
    int aim_bind2 = 0;     // legacy secondary binding
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

    // Trigger
    bool trigger_enabled = false;
    bool trigger_always_on = true;
    int trigger_bind = 0x12; // ALT
    int trigger_delay_ms = 30;
    bool trigger_team_check = true;
    bool trigger_scoped_only = false;
    bool trigger_head_only = false;
    bool trigger_use_ident = true; // prefer m_iIDEntIndex

    int profile = 0; // 0 custom, 1 legit, 2 rage

    // Config persistence
    bool SaveConfig(const char* name = "cs2_default");
    bool LoadConfig(const char* name = "cs2_default");
    bool SelfTestOffsets();
};

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
}

extern Config config;

} // namespace CS2
