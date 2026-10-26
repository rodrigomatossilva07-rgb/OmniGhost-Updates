#pragma once

#include "gameplay/esp_config_base.h"

namespace CS2 {

enum class EspLayoutAnchor : int { Top = 0, Bottom = 1, Left = 2, Right = 3 };

struct Config : OmniGhost::Gameplay::EspConfigBase {

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
    bool c4_carrier_esp = false;
    bool offscreen_arrows = false;
    float offscreen_arrow_size = 14.f;
    float offscreen_arrow_margin = 34.f;
    float offscreen_arrow_max_distance = 300.f;
    float offscreen_arrow_opacity = .90f;
    float col_offscreen_arrow[4] = { 0.95f, 0.72f, 0.18f, 0.90f };
    bool active_features_panel = false;
    float active_features_x = .015f;
    float active_features_y = .18f;
    float active_features_scale = 1.f;
    float active_features_opacity = .88f;
    bool active_features_compact = true;
    int layout_name_anchor = static_cast<int>(EspLayoutAnchor::Top);
    int layout_health_anchor = static_cast<int>(EspLayoutAnchor::Left);
    int layout_armor_anchor = static_cast<int>(EspLayoutAnchor::Right);
    int layout_weapon_anchor = static_cast<int>(EspLayoutAnchor::Bottom);
    int layout_distance_anchor = static_cast<int>(EspLayoutAnchor::Bottom);
    int layout_ammo_anchor = static_cast<int>(EspLayoutAnchor::Bottom);
    float layout_spacing = 4.f;
    float layout_text_scale = 1.f;
    float layout_name_x = .50f, layout_name_y = .10f;
    float layout_health_x = .18f, layout_health_y = .50f;
    float layout_armor_x = .82f, layout_armor_y = .50f;
    float layout_weapon_x = .50f, layout_weapon_y = .86f;
    float layout_distance_x = .50f, layout_distance_y = .94f;
    float layout_ammo_x = .50f, layout_ammo_y = .78f;
    bool radar_2d = false;
    float radar_2d_x = 18.f;
    float radar_2d_y = 18.f;
    float radar_2d_size = 160.f;
    bool web_radar_enabled = false;
    bool web_radar_cloudflare = false;
    int web_radar_port = 8766;
    int web_radar_max_players = 64;
    float web_radar_update_hz = 20.f;
    bool hotkey_overlay = false;
    bool panic_key_enabled = true;
    int panic_key = 0x23;
    bool stream_proof = false;
    bool show_makcu_status = true;
    int max_entities = 32;
    bool auto_entity_cap = true;
    bool show_fps = true;
    bool auto_dump_on_mismatch = true;
    char server_config_key[64] = {};


    bool aim_enabled = false;
    bool aim_draw_fov = true;
    bool aim_fov_rgb = false;
    int aim_fov_style = 0;
    bool aim_dynamic_fov = false;
    float aim_fov = 80.f;
    float aim_fov_min = 25.f;
    float aim_fov_curve_distance[5] = { 0.f, 25.f, 60.f, 120.f, 250.f };
    float aim_fov_curve_value[5] = { 25.f, 40.f, 70.f, 110.f, 160.f };
    float aim_smooth = 12.f;
    float aim_max_dist = 150.f;
    int aim_bone = 0;
    int aim_bind = 0x02;
    int aim_bind2 = 0;
    int aim_bind3 = 0;
    float aim_deadzone = 2.f;
    float sticky_ms = 120.f;
    bool aim_prediction = false;
    bool aim_humanize = false;
    float aim_humanization = 0.f;
    float prediction_strength = 0.35f;
    bool hit_chance_ui = false;
    float aim_switch_cooldown_ms = 90.f;
    float aim_switch_margin = 0.16f;
    int aim_reaction_min_ms = 30;
    int aim_reaction_max_ms = 80;
    float aim_overshoot_px = 0.55f;
    float aim_micro_jitter_px = 0.06f;
    int aim_motion_interval_ms = 6;
    int aim_max_step = 0;
    float aim_ema_alpha = 0.40f;
    float aim_reversal_damping = 0.16f;
    bool aim_ignore_team = true;
    bool aim_ignore_spectators = false;
    bool aim_ignore_bots = false;
    bool aim_auto_bone = false;
    bool aim_visibility_check = false;
    bool aim_visible_bones_only = false;
    int aim_bone_priority[7] = { 0, 1, 2, 3, 4, 5, 6 };
    bool aim_rcs_standalone = false;
    bool aim_rcs_auto = true;
    float aim_rcs_x = 1.f;
    float aim_rcs_y = 1.f;
    float col_fov[4] = { 0.83f, 0.69f, 0.22f, 0.45f };


    bool trigger_enabled = false;
    bool trigger_always_on = true;
    int trigger_bind = 0x12;
    int trigger_delay_ms = 30;
    bool trigger_team_check = true;
    bool trigger_visibility_check = false;
    bool trigger_scoped_only = false;
    bool trigger_head_only = false;
    bool trigger_use_ident = true;

    int profile = 0;





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
    c.aim_ignore_bots = false;
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

}
