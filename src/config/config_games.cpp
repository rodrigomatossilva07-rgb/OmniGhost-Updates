#include "config_games.h"
#include "config_core.h"
#include "../src/games/Cs2/cs2_game.h"
#include "../src/games/Warzone/warzone_game.h"
#include "../src/games/Fortnite/fortnite_game.h"
#include <cstring>
#include <iterator>

namespace config_manager {




void SerializeCS2(std::ostringstream& o) {
    auto& c = CS2::config;
    WBool(o, "cs2.esp_enabled", c.esp_enabled);
    WBool(o, "cs2.self_esp", c.self_esp);
    WBool(o, "cs2.rgb_mode", c.rgb_mode);
    WBool(o, "cs2.box", c.box);
    WBool(o, "cs2.skeleton", c.skeleton);
    WBool(o, "cs2.skeleton_joints", c.skeleton_joints);
    WBool(o, "cs2.box_corner", c.box_corner);
    WBool(o, "cs2.box_fill", c.box_fill);
    WBool(o, "cs2.health_value", c.health_value);
    WBool(o, "cs2.armor_value", c.armor_value);
    WBool(o, "cs2.text_outline", c.text_outline);
    WInt(o, "cs2.box_style", c.box_style);
    WInt(o, "cs2.snapline_position", c.snapline_position);
    WFloat(o, "cs2.box_rounding", c.box_rounding);
    WFloat(o, "cs2.text_outline_thickness", c.text_outline_thickness);
    WBool(o, "cs2.health_bar", c.health_bar);
    WBool(o, "cs2.armor_bar", c.armor_bar);
    WBool(o, "cs2.name", c.name);
    WBool(o, "cs2.distance", c.distance);
    WBool(o, "cs2.team_check", c.team_check);
    WBool(o, "cs2.show_bots", c.show_bots);
    WBool(o, "cs2.head_dot", c.head_dot);
    WBool(o, "cs2.trails", c.trails);
    WBool(o, "cs2.head_halo", c.head_halo);
    WBool(o, "cs2.look_direction", c.look_direction);
    WBool(o, "cs2.chinese_hat", c.chinese_hat);
    WFloat(o, "cs2.chinese_hat_scale", c.chinese_hat_scale);
    WBool(o, "cs2.angel_wings", c.angel_wings);
    WBool(o, "cs2.devil_horns", c.devil_horns);
    WBool(o, "cs2.floating_crown", c.floating_crown);
    WBool(o, "cs2.fun_effects_rainbow", c.fun_effects_rainbow);
    WFloat(o, "cs2.fun_effects_scale", c.fun_effects_scale);
    WBool(o, "cs2.hit_marker", c.hit_marker);
    WBool(o, "cs2.rainbow_trails", c.rainbow_trails);
    WFloat(o, "cs2.trail_duration", c.trail_duration);
    WFloat(o, "cs2.trail_thickness", c.trail_thickness);
    WFloat(o, "cs2.look_direction_length", c.look_direction_length);
    WFloat(o, "cs2.skeleton_thickness", c.skeleton_thickness);
    WFloat(o, "cs2.snapline_thickness", c.snapline_thickness);
    WFloat(o, "cs2.head_circle_thickness", c.head_circle_thickness);
    WFloat(o, "cs2.box_thickness", c.box_thickness);
    WFloat(o, "cs2.eye_line_thickness", c.eye_line_thickness);
    WBool(o, "cs2.snaplines", c.snaplines);
    WBool(o, "cs2.bomb_timer", c.bomb_timer);
    WBool(o, "cs2.spectator_list", c.spectator_list);
    WFloat(o, "cs2.spectator_window_x", c.spectator_window_x);
    WFloat(o, "cs2.spectator_window_y", c.spectator_window_y);
    WFloat(o, "cs2.bomb_window_x", c.bomb_window_x);
    WFloat(o, "cs2.bomb_window_y", c.bomb_window_y);
    WBool(o, "cs2.radar_2d", c.radar_2d);
    WBool(o, "cs2.webradar_enabled", c.webradar_enabled);
    WInt(o, "cs2.webradar_port", c.webradar_port);
    WBool(o, "cs2.webradar_cloudflare", c.webradar_cloudflare);
    WBool(o, "cs2.performance_mode", c.performance_mode);
    WBool(o, "cs2.telemetry_enabled", c.telemetry_enabled);
    WFloat(o, "cs2.max_distance", c.max_distance);
    WInt(o, "cs2.max_entities", c.max_entities);
    WBool(o, "cs2.aim_enabled", c.aim_enabled);
    WBool(o, "cs2.aim_draw_fov", c.aim_draw_fov);
    WBool(o, "cs2.aim_fov_rgb", c.aim_fov_rgb);
    WInt(o, "cs2.aim_fov_style", c.aim_fov_style);
    WBool(o, "cs2.aim_dynamic_fov", c.aim_dynamic_fov);
    WFloat(o, "cs2.aim_fov", c.aim_fov);
    WFloat(o, "cs2.aim_fov_min", c.aim_fov_min);
    for (int i = 0; i < 5; ++i) {
        const std::string distanceKey = std::string("cs2.aim_fov_curve_distance.") + std::to_string(i);
        const std::string valueKey = std::string("cs2.aim_fov_curve_value.") + std::to_string(i);
        WFloat(o, distanceKey.c_str(), c.aim_fov_curve_distance[i]);
        WFloat(o, valueKey.c_str(), c.aim_fov_curve_value[i]);
    }
    WFloat(o, "cs2.aim_smooth", c.aim_smooth);
    WFloat(o, "cs2.aim_max_dist", c.aim_max_dist);
    WInt(o, "cs2.aim_bone", c.aim_bone);
    WInt(o, "cs2.aim_bind", c.aim_bind);
    WInt(o, "cs2.aim_bind2", c.aim_bind2);
    WInt(o, "cs2.aim_bind3", c.aim_bind3);
    WBool(o, "cs2.aim_humanize", c.aim_humanize);
    WFloat(o, "cs2.aim_humanization", c.aim_humanization);
    WFloat(o, "cs2.aim_deadzone", c.aim_deadzone);
    WFloat(o, "cs2.sticky_ms", c.sticky_ms);
    WBool(o, "cs2.aim_prediction", c.aim_prediction);
    WFloat(o, "cs2.prediction_strength", c.prediction_strength);
    WBool(o, "cs2.hit_chance_ui", c.hit_chance_ui);
    WFloat(o, "cs2.aim_switch_cooldown_ms", c.aim_switch_cooldown_ms);
    WFloat(o, "cs2.aim_switch_margin", c.aim_switch_margin);
    WInt(o, "cs2.aim_reaction_min_ms", c.aim_reaction_min_ms);
    WInt(o, "cs2.aim_reaction_max_ms", c.aim_reaction_max_ms);
    WFloat(o, "cs2.aim_overshoot_px", c.aim_overshoot_px);
    WFloat(o, "cs2.aim_micro_jitter_px", c.aim_micro_jitter_px);
    WInt(o, "cs2.aim_motion_interval_ms", c.aim_motion_interval_ms);
    WInt(o, "cs2.aim_max_step", c.aim_max_step);
    WFloat(o, "cs2.aim_ema_alpha", c.aim_ema_alpha);
    WFloat(o, "cs2.aim_reversal_damping", c.aim_reversal_damping);
    WBool(o, "cs2.aim_ignore_team", c.aim_ignore_team);
    WBool(o, "cs2.aim_ignore_spectators", c.aim_ignore_spectators);
    WBool(o, "cs2.aim_ignore_bots", c.aim_ignore_bots);
    WBool(o, "cs2.aim_auto_bone", c.aim_auto_bone);
    WBool(o, "cs2.aim_visibility_check", c.aim_visibility_check);
    WBool(o, "cs2.aim_visible_bones_only", c.aim_visible_bones_only);
    for (int i = 0; i < 7; ++i) {
        const std::string key = std::string("cs2.aim_bone_priority.") + std::to_string(i);
        WInt(o, key.c_str(), c.aim_bone_priority[i]);
    }
    WBool(o, "cs2.aim_rcs_standalone", c.aim_rcs_standalone);
    WBool(o, "cs2.aim_rcs_auto", c.aim_rcs_auto);
    WFloat(o, "cs2.aim_rcs_x", c.aim_rcs_x);
    WFloat(o, "cs2.aim_rcs_y", c.aim_rcs_y);
    WBool(o, "cs2.trigger_enabled", c.trigger_enabled);
    WBool(o, "cs2.trigger_always_on", c.trigger_always_on);
    WInt(o, "cs2.trigger_bind", c.trigger_bind);
    WInt(o, "cs2.trigger_delay_ms", c.trigger_delay_ms);
    WBool(o, "cs2.trigger_team_check", c.trigger_team_check);
    WBool(o, "cs2.trigger_visibility_check", c.trigger_visibility_check);
    WBool(o, "cs2.trigger_head_only", c.trigger_head_only);
    WBool(o, "cs2.trigger_scoped_only", c.trigger_scoped_only);
    WBool(o, "cs2.trigger_use_ident", c.trigger_use_ident);
    WBool(o, "cs2.panic_key_enabled", c.panic_key_enabled);
    WInt(o, "cs2.panic_key", c.panic_key);
    WBool(o, "cs2.visibility_colors", c.visibility_colors);
    WBool(o, "cs2.visible_check", c.visible_check);
    WBool(o, "cs2.scope_check", c.scope_check);
    WBool(o, "cs2.footstep_esp", c.footstep_esp);
    WBool(o, "cs2.player_flags", c.player_flags);
    WBool(o, "cs2.flag_blind", c.flag_blind);
    WBool(o, "cs2.flag_scoped", c.flag_scoped);
    WBool(o, "cs2.flag_defusing", c.flag_defusing);
    WBool(o, "cs2.flag_kit", c.flag_kit);
    WBool(o, "cs2.flag_money", c.flag_money);
    WBool(o, "cs2.weapon_ammo", c.weapon_ammo);
    WBool(o, "cs2.dropped_weapons", c.dropped_weapons);
    WBool(o, "cs2.dropped_weapon_pistols", c.dropped_weapon_pistols);
    WBool(o, "cs2.dropped_weapon_smgs", c.dropped_weapon_smgs);
    WBool(o, "cs2.dropped_weapon_rifles", c.dropped_weapon_rifles);
    WBool(o, "cs2.dropped_weapon_heavy", c.dropped_weapon_heavy);
    WBool(o, "cs2.dropped_weapon_icons", c.dropped_weapon_icons);
    WBool(o, "cs2.dropped_weapon_ammo", c.dropped_weapon_ammo);
    WBool(o, "cs2.c4_carrier_esp", c.c4_carrier_esp);
    WBool(o, "cs2.offscreen_arrows", c.offscreen_arrows);
    WFloat(o, "cs2.offscreen_arrow_size", c.offscreen_arrow_size);
    WFloat(o, "cs2.offscreen_arrow_margin", c.offscreen_arrow_margin);
    WFloat(o, "cs2.offscreen_arrow_max_distance", c.offscreen_arrow_max_distance);
    WFloat(o, "cs2.offscreen_arrow_opacity", c.offscreen_arrow_opacity);
    WBool(o, "cs2.active_features_panel", c.active_features_panel);
    WFloat(o, "cs2.active_features_x", c.active_features_x);
    WFloat(o, "cs2.active_features_y", c.active_features_y);
    WFloat(o, "cs2.active_features_scale", c.active_features_scale);
    WFloat(o, "cs2.active_features_opacity", c.active_features_opacity);
    WBool(o, "cs2.active_features_compact", c.active_features_compact);
    WInt(o, "cs2.layout_name_anchor", c.layout_name_anchor);
    WInt(o, "cs2.layout_health_anchor", c.layout_health_anchor);
    WInt(o, "cs2.layout_armor_anchor", c.layout_armor_anchor);
    WInt(o, "cs2.layout_weapon_anchor", c.layout_weapon_anchor);
    WInt(o, "cs2.layout_distance_anchor", c.layout_distance_anchor);
    WInt(o, "cs2.layout_ammo_anchor", c.layout_ammo_anchor);
    WFloat(o, "cs2.layout_spacing", c.layout_spacing);
    WFloat(o, "cs2.layout_text_scale", c.layout_text_scale);
    WFloat(o, "cs2.layout_name_x", c.layout_name_x); WFloat(o, "cs2.layout_name_y", c.layout_name_y);
    WFloat(o, "cs2.layout_health_x", c.layout_health_x); WFloat(o, "cs2.layout_health_y", c.layout_health_y);
    WFloat(o, "cs2.layout_armor_x", c.layout_armor_x); WFloat(o, "cs2.layout_armor_y", c.layout_armor_y);
    WFloat(o, "cs2.layout_weapon_x", c.layout_weapon_x); WFloat(o, "cs2.layout_weapon_y", c.layout_weapon_y);
    WFloat(o, "cs2.layout_distance_x", c.layout_distance_x); WFloat(o, "cs2.layout_distance_y", c.layout_distance_y);
    WFloat(o, "cs2.layout_ammo_x", c.layout_ammo_x); WFloat(o, "cs2.layout_ammo_y", c.layout_ammo_y);
    WFloat(o, "cs2.radar_2d_x", c.radar_2d_x);
    WFloat(o, "cs2.radar_2d_y", c.radar_2d_y);
    WFloat(o, "cs2.radar_2d_size", c.radar_2d_size);
    WBool(o, "cs2.skeleton_lod", c.skeleton_lod);
    WFloat(o, "cs2.skeleton_lod_distance", c.skeleton_lod_distance);
    WBool(o, "cs2.distance_feet", c.distance_feet);
    WBool(o, "cs2.hotkey_overlay", c.hotkey_overlay);
    WBool(o, "cs2.stream_proof", c.stream_proof);
    WBool(o, "cs2.bone_draw_arms", c.bone_draw_arms);
    WBool(o, "cs2.bone_draw_legs", c.bone_draw_legs);
    WBool(o, "cs2.vsync", c.vsync);
    WBool(o, "cs2.show_makcu_status", c.show_makcu_status);
    WBool(o, "cs2.auto_entity_cap", c.auto_entity_cap);
    WBool(o, "cs2.show_fps", c.show_fps);
    WBool(o, "cs2.auto_dump_on_mismatch", c.auto_dump_on_mismatch);
    WInt(o, "cs2.profile", c.profile);
    WStr(o, "cs2.server_config_key", c.server_config_key);
    SerializeColorArray(o, "cs2.col_enemy", c.col_enemy);
    SerializeColorArray(o, "cs2.col_team", c.col_team);
    SerializeColorArray(o, "cs2.col_skeleton", c.col_skeleton);
    SerializeColorArray(o, "cs2.col_box", c.col_box);
    SerializeColorArray(o, "cs2.col_box_corner", c.col_box_corner);
    SerializeColorArray(o, "cs2.col_name", c.col_name);
    SerializeColorArray(o, "cs2.col_weapon", c.col_weapon);
    SerializeColorArray(o, "cs2.col_target", c.col_target);
    SerializeColorArray(o, "cs2.col_joints", c.col_joints);
    SerializeColorArray(o, "cs2.col_health", c.col_health);
    SerializeColorArray(o, "cs2.col_armor", c.col_armor);
    SerializeColorArray(o, "cs2.col_snaplines", c.col_snaplines);
    SerializeColorArray(o, "cs2.col_trail", c.col_trail);
    SerializeColorArray(o, "cs2.col_halo", c.col_halo);
    SerializeColorArray(o, "cs2.col_fun_effects", c.col_fun_effects);
    SerializeColorArray(o, "cs2.col_look", c.col_look);
    SerializeColorArray(o, "cs2.col_head", c.col_head);
    SerializeColorArray(o, "cs2.col_distance", c.col_distance);
    SerializeColorArray(o, "cs2.col_visible", c.col_visible);
    SerializeColorArray(o, "cs2.col_occluded", c.col_occluded);
    SerializeColorArray(o, "cs2.col_box_fill", c.col_box_fill);
    SerializeColorArray(o, "cs2.col_text_outline", c.col_text_outline);
    SerializeColorArray(o, "cs2.col_flags", c.col_flags);
    SerializeColorArray(o, "cs2.col_fov", c.col_fov);
    SerializeColorArray(o, "cs2.col_offscreen_arrow", c.col_offscreen_arrow);
}

bool DeserializeCS2(const std::string& key, const std::string& val) {
    if (key.rfind("cs2.", 0) != 0) return false;
    auto& c = CS2::config;
    auto bi = [&](const char* k) { return key == k; };
    auto toB = [&]() -> bool { return config_manager::toB(val); };
    auto toF = [&]() -> float { return config_manager::toF(val); };
    auto toI = [&]() -> int { return config_manager::toI(val); };
    auto toU = [&]() -> ImU32 { return config_manager::toU(val); };

    if (bi("cs2.esp_enabled")) { c.esp_enabled = toB(); return true; }
    if (bi("cs2.self_esp")) { c.self_esp = toB(); return true; }
    if (bi("cs2.rgb_mode")) { c.rgb_mode = toB(); return true; }
    if (bi("cs2.box")) { c.box = toB(); return true; }
    if (bi("cs2.skeleton")) { c.skeleton = toB(); return true; }
    if (bi("cs2.skeleton_joints")) { c.skeleton_joints = toB(); return true; }
    if (bi("cs2.box_corner")) { c.box_corner = toB(); return true; }
    if (bi("cs2.box_fill")) { c.box_fill = toB(); return true; }
    if (bi("cs2.health_value")) { c.health_value = toB(); return true; }
    if (bi("cs2.armor_value")) { c.armor_value = toB(); return true; }
    if (bi("cs2.text_outline")) { c.text_outline = toB(); return true; }
    if (bi("cs2.box_style")) { c.box_style = toI(); return true; }
    if (bi("cs2.snapline_position")) { c.snapline_position = toI(); return true; }
    if (bi("cs2.box_rounding")) { c.box_rounding = toF(); return true; }
    if (bi("cs2.text_outline_thickness")) { c.text_outline_thickness = toF(); return true; }
    if (bi("cs2.health_bar")) { c.health_bar = toB(); return true; }
    if (bi("cs2.armor_bar")) { c.armor_bar = toB(); return true; }
    if (bi("cs2.name")) { c.name = toB(); return true; }
    if (bi("cs2.distance")) { c.distance = toB(); return true; }
    if (bi("cs2.team_check")) { c.team_check = toB(); return true; }
    if (bi("cs2.show_bots")) { c.show_bots = toB(); return true; }
    if (bi("cs2.head_dot")) { c.head_dot = toB(); return true; }
    if (bi("cs2.trails")) { c.trails = toB(); return true; }
    if (bi("cs2.head_halo")) { c.head_halo = toB(); return true; }
    if (bi("cs2.look_direction")) { c.look_direction = toB(); return true; }
    if (bi("cs2.chinese_hat")) { c.chinese_hat = toB(); return true; }
    if (bi("cs2.chinese_hat_scale")) { c.chinese_hat_scale = toF(); return true; }
    if (bi("cs2.angel_wings")) { c.angel_wings = toB(); return true; }
    if (bi("cs2.devil_horns")) { c.devil_horns = toB(); return true; }
    if (bi("cs2.floating_crown")) { c.floating_crown = toB(); return true; }
    if (bi("cs2.fun_effects_rainbow")) { c.fun_effects_rainbow = toB(); return true; }
    if (bi("cs2.fun_effects_scale")) { c.fun_effects_scale = toF(); return true; }
    if (bi("cs2.hit_marker")) { c.hit_marker = toB(); return true; }
    if (bi("cs2.rainbow_trails")) { c.rainbow_trails = toB(); return true; }
    if (bi("cs2.trail_duration")) { c.trail_duration = toF(); return true; }
    if (bi("cs2.trail_thickness")) { c.trail_thickness = toF(); return true; }
    if (bi("cs2.look_direction_length")) { c.look_direction_length = toF(); return true; }
    if (bi("cs2.skeleton_thickness")) { c.skeleton_thickness = toF(); return true; }
    if (bi("cs2.snapline_thickness")) { c.snapline_thickness = toF(); return true; }
    if (bi("cs2.head_circle_thickness")) { c.head_circle_thickness = toF(); return true; }
    if (bi("cs2.box_thickness")) { c.box_thickness = toF(); return true; }
    if (bi("cs2.eye_line_thickness")) { c.eye_line_thickness = toF(); return true; }
    if (bi("cs2.snaplines")) { c.snaplines = toB(); return true; }
    if (bi("cs2.bomb_timer")) { c.bomb_timer = toB(); return true; }
    if (bi("cs2.spectator_list")) { c.spectator_list = toB(); return true; }
    if (bi("cs2.spectator_window_x")) { c.spectator_window_x = toF(); return true; }
    if (bi("cs2.spectator_window_y")) { c.spectator_window_y = toF(); return true; }
    if (bi("cs2.bomb_window_x")) { c.bomb_window_x = toF(); return true; }
    if (bi("cs2.bomb_window_y")) { c.bomb_window_y = toF(); return true; }
    if (bi("cs2.radar_2d")) { c.radar_2d = toB(); return true; }
    if (bi("cs2.webradar_enabled")) { c.webradar_enabled = toB(); return true; }
    if (bi("cs2.webradar_port")) { c.webradar_port = toI(); return true; }
    if (bi("cs2.webradar_cloudflare")) { c.webradar_cloudflare = toB(); return true; }
    if (bi("cs2.performance_mode")) { c.performance_mode = toB(); return true; }
    if (bi("cs2.telemetry_enabled")) { c.telemetry_enabled = toB(); return true; }
    if (bi("cs2.max_distance")) { c.max_distance = toF(); return true; }
    if (bi("cs2.max_entities")) { c.max_entities = toI(); return true; }
    if (bi("cs2.aim_enabled")) { c.aim_enabled = toB(); return true; }
    if (bi("cs2.aim_draw_fov")) { c.aim_draw_fov = toB(); return true; }
    if (bi("cs2.aim_fov_rgb")) { c.aim_fov_rgb = toB(); return true; }
    if (bi("cs2.aim_fov_style")) { c.aim_fov_style = toI(); return true; }
    if (bi("cs2.aim_dynamic_fov")) { c.aim_dynamic_fov = toB(); return true; }
    if (bi("cs2.aim_fov")) { c.aim_fov = toF(); return true; }
    if (bi("cs2.aim_fov_min")) { c.aim_fov_min = toF(); return true; }
    for (int i = 0; i < 5; ++i) {
        if (key == std::string("cs2.aim_fov_curve_distance.") + std::to_string(i)) { c.aim_fov_curve_distance[i] = toF(); return true; }
        if (key == std::string("cs2.aim_fov_curve_value.") + std::to_string(i)) { c.aim_fov_curve_value[i] = toF(); return true; }
    }
    if (bi("cs2.aim_smooth")) { c.aim_smooth = toF(); return true; }
    if (bi("cs2.aim_max_dist")) { c.aim_max_dist = toF(); return true; }
    if (bi("cs2.aim_bone")) { c.aim_bone = toI(); return true; }
    if (bi("cs2.aim_bind")) { c.aim_bind = toI(); return true; }
    if (bi("cs2.aim_bind2")) { c.aim_bind2 = toI(); return true; }
    if (bi("cs2.aim_bind3")) { c.aim_bind3 = toI(); return true; }
    if (bi("cs2.aim_humanize")) { c.aim_humanize = toB(); return true; }
    if (bi("cs2.aim_humanization")) { c.aim_humanization = toF(); c.aim_humanize = c.aim_humanization > 0.5f; return true; }
    if (bi("cs2.aim_deadzone")) { c.aim_deadzone = toF(); return true; }
    if (bi("cs2.sticky_ms")) { c.sticky_ms = toF(); return true; }
    if (bi("cs2.aim_prediction")) { c.aim_prediction = toB(); return true; }
    if (bi("cs2.prediction_strength")) { c.prediction_strength = toF(); return true; }
    if (bi("cs2.hit_chance_ui")) { c.hit_chance_ui = toB(); return true; }
    if (bi("cs2.aim_switch_cooldown_ms")) { c.aim_switch_cooldown_ms = toF(); return true; }
    if (bi("cs2.aim_switch_margin")) { c.aim_switch_margin = toF(); return true; }
    if (bi("cs2.aim_reaction_min_ms")) { c.aim_reaction_min_ms = toI(); return true; }
    if (bi("cs2.aim_reaction_max_ms")) { c.aim_reaction_max_ms = toI(); return true; }
    if (bi("cs2.aim_overshoot_px")) { c.aim_overshoot_px = toF(); return true; }
    if (bi("cs2.aim_micro_jitter_px")) { c.aim_micro_jitter_px = toF(); return true; }
    if (bi("cs2.aim_motion_interval_ms")) { c.aim_motion_interval_ms = toI(); return true; }
    if (bi("cs2.aim_max_step")) { c.aim_max_step = toI(); return true; }
    if (bi("cs2.aim_ema_alpha")) { c.aim_ema_alpha = toF(); return true; }
    if (bi("cs2.aim_reversal_damping")) { c.aim_reversal_damping = toF(); return true; }
    if (bi("cs2.aim_ignore_team")) { c.aim_ignore_team = toB(); return true; }
    if (bi("cs2.aim_ignore_spectators")) { c.aim_ignore_spectators = toB(); return true; }
    if (bi("cs2.aim_ignore_bots")) { c.aim_ignore_bots = toB(); return true; }
    if (bi("cs2.aim_auto_bone")) { c.aim_auto_bone = toB(); return true; }
    if (bi("cs2.aim_visibility_check")) { c.aim_visibility_check = toB(); return true; }
    if (bi("cs2.aim_visible_bones_only")) { c.aim_visible_bones_only = toB(); return true; }
    for (int i = 0; i < 7; ++i) {
        if (key == std::string("cs2.aim_bone_priority.") + std::to_string(i)) { c.aim_bone_priority[i] = toI(); return true; }
    }
    if (bi("cs2.aim_rcs_standalone")) { c.aim_rcs_standalone = toB(); return true; }
    if (bi("cs2.aim_rcs_auto")) { c.aim_rcs_auto = toB(); return true; }
    if (bi("cs2.aim_rcs_x")) { c.aim_rcs_x = toF(); return true; }
    if (bi("cs2.aim_rcs_y")) { c.aim_rcs_y = toF(); return true; }
    if (bi("cs2.trigger_enabled")) { c.trigger_enabled = toB(); return true; }
    if (bi("cs2.trigger_always_on")) { c.trigger_always_on = toB(); return true; }
    if (bi("cs2.trigger_bind")) { c.trigger_bind = toI(); return true; }
    if (bi("cs2.trigger_delay_ms")) { c.trigger_delay_ms = toI(); return true; }
    if (bi("cs2.trigger_team_check")) { c.trigger_team_check = toB(); return true; }
    if (bi("cs2.trigger_visibility_check")) { c.trigger_visibility_check = toB(); return true; }
    if (bi("cs2.trigger_head_only")) { c.trigger_head_only = toB(); return true; }
    if (bi("cs2.trigger_scoped_only")) { c.trigger_scoped_only = toB(); return true; }
    if (bi("cs2.trigger_use_ident")) { c.trigger_use_ident = toB(); return true; }
    if (bi("cs2.panic_key_enabled")) { c.panic_key_enabled = toB(); return true; }
    if (bi("cs2.panic_key")) { c.panic_key = toI(); return true; }

    if (bi("cs2.visibility_colors")) { c.visibility_colors = toB(); return true; }
    if (bi("cs2.visible_check")) { c.visible_check = toB(); return true; }
    if (bi("cs2.scope_check")) { c.scope_check = toB(); return true; }
    if (bi("cs2.footstep_esp")) { c.footstep_esp = toB(); return true; }
    if (bi("cs2.player_flags")) { c.player_flags = toB(); return true; }
    if (bi("cs2.flag_blind")) { c.flag_blind = toB(); return true; }
    if (bi("cs2.flag_scoped")) { c.flag_scoped = toB(); return true; }
    if (bi("cs2.flag_defusing")) { c.flag_defusing = toB(); return true; }
    if (bi("cs2.flag_kit")) { c.flag_kit = toB(); return true; }
    if (bi("cs2.flag_money")) { c.flag_money = toB(); return true; }
    if (bi("cs2.weapon_ammo")) { c.weapon_ammo = toB(); return true; }


    if (bi("cs2.dropped_weapons")) { c.dropped_weapons = false; return true; }
    if (bi("cs2.dropped_weapon_pistols")) { c.dropped_weapon_pistols = toB(); return true; }
    if (bi("cs2.dropped_weapon_smgs")) { c.dropped_weapon_smgs = toB(); return true; }
    if (bi("cs2.dropped_weapon_rifles")) { c.dropped_weapon_rifles = toB(); return true; }
    if (bi("cs2.dropped_weapon_heavy")) { c.dropped_weapon_heavy = toB(); return true; }
    if (bi("cs2.dropped_weapon_icons")) { c.dropped_weapon_icons = toB(); return true; }
    if (bi("cs2.dropped_weapon_ammo")) { c.dropped_weapon_ammo = toB(); return true; }
    if (bi("cs2.c4_carrier_esp")) { c.c4_carrier_esp = toB(); return true; }
    if (bi("cs2.offscreen_arrows")) { c.offscreen_arrows = toB(); return true; }
    if (bi("cs2.offscreen_arrow_size")) { c.offscreen_arrow_size = toF(); return true; }
    if (bi("cs2.offscreen_arrow_margin")) { c.offscreen_arrow_margin = toF(); return true; }
    if (bi("cs2.offscreen_arrow_max_distance")) { c.offscreen_arrow_max_distance = toF(); return true; }
    if (bi("cs2.offscreen_arrow_opacity")) { c.offscreen_arrow_opacity = toF(); return true; }
    if (bi("cs2.active_features_panel")) { c.active_features_panel = toB(); return true; }
    if (bi("cs2.active_features_x")) { c.active_features_x = toF(); return true; }
    if (bi("cs2.active_features_y")) { c.active_features_y = toF(); return true; }
    if (bi("cs2.active_features_scale")) { c.active_features_scale = toF(); return true; }
    if (bi("cs2.active_features_opacity")) { c.active_features_opacity = toF(); return true; }
    if (bi("cs2.active_features_compact")) { c.active_features_compact = toB(); return true; }
    if (bi("cs2.layout_name_anchor")) { c.layout_name_anchor = toI(); return true; }
    if (bi("cs2.layout_health_anchor")) { c.layout_health_anchor = toI(); return true; }
    if (bi("cs2.layout_armor_anchor")) { c.layout_armor_anchor = toI(); return true; }
    if (bi("cs2.layout_weapon_anchor")) { c.layout_weapon_anchor = toI(); return true; }
    if (bi("cs2.layout_distance_anchor")) { c.layout_distance_anchor = toI(); return true; }
    if (bi("cs2.layout_ammo_anchor")) { c.layout_ammo_anchor = toI(); return true; }
    if (bi("cs2.layout_spacing")) { c.layout_spacing = toF(); return true; }
    if (bi("cs2.layout_text_scale")) { c.layout_text_scale = toF(); return true; }
    if (bi("cs2.layout_name_x")) { c.layout_name_x = toF(); return true; }
    if (bi("cs2.layout_name_y")) { c.layout_name_y = toF(); return true; }
    if (bi("cs2.layout_health_x")) { c.layout_health_x = toF(); return true; }
    if (bi("cs2.layout_health_y")) { c.layout_health_y = toF(); return true; }
    if (bi("cs2.layout_armor_x")) { c.layout_armor_x = toF(); return true; }
    if (bi("cs2.layout_armor_y")) { c.layout_armor_y = toF(); return true; }
    if (bi("cs2.layout_weapon_x")) { c.layout_weapon_x = toF(); return true; }
    if (bi("cs2.layout_weapon_y")) { c.layout_weapon_y = toF(); return true; }
    if (bi("cs2.layout_distance_x")) { c.layout_distance_x = toF(); return true; }
    if (bi("cs2.layout_distance_y")) { c.layout_distance_y = toF(); return true; }
    if (bi("cs2.layout_ammo_x")) { c.layout_ammo_x = toF(); return true; }
    if (bi("cs2.layout_ammo_y")) { c.layout_ammo_y = toF(); return true; }
    if (bi("cs2.radar_2d_x")) { c.radar_2d_x = toF(); return true; }
    if (bi("cs2.radar_2d_y")) { c.radar_2d_y = toF(); return true; }
    if (bi("cs2.radar_2d_size")) { c.radar_2d_size = toF(); return true; }
    if (bi("cs2.skeleton_lod")) { c.skeleton_lod = toB(); return true; }
    if (bi("cs2.skeleton_lod_distance")) { c.skeleton_lod_distance = toF(); return true; }
    if (bi("cs2.distance_feet")) { c.distance_feet = toB(); return true; }
    if (bi("cs2.hotkey_overlay")) { c.hotkey_overlay = toB(); return true; }
    if (bi("cs2.stream_proof")) { c.stream_proof = toB(); return true; }
    if (bi("cs2.bone_draw_arms")) { c.bone_draw_arms = toB(); return true; }
    if (bi("cs2.bone_draw_legs")) { c.bone_draw_legs = toB(); return true; }
    if (bi("cs2.vsync")) { c.vsync = toB(); return true; }
    if (bi("cs2.show_makcu_status")) { c.show_makcu_status = toB(); return true; }
    if (bi("cs2.auto_entity_cap")) { c.auto_entity_cap = toB(); return true; }
    if (bi("cs2.show_fps")) { c.show_fps = toB(); return true; }
    if (bi("cs2.auto_dump_on_mismatch")) { c.auto_dump_on_mismatch = toB(); return true; }
    if (bi("cs2.profile")) { c.profile = toI(); return true; }
    if (bi("cs2.server_config_key")) { strncpy_s(c.server_config_key, val.c_str(), _TRUNCATE); return true; }

    static const char* colorPrefixes[] = {
        "cs2.col_enemy", "cs2.col_team", "cs2.col_skeleton", "cs2.col_box",
        "cs2.col_name", "cs2.col_weapon", "cs2.col_target", "cs2.col_joints",
        "cs2.col_health", "cs2.col_armor", "cs2.col_snaplines", "cs2.col_trail",
        "cs2.col_halo", "cs2.col_fun_effects", "cs2.col_look", "cs2.col_box_corner", "cs2.col_head",
        "cs2.col_distance", "cs2.col_visible", "cs2.col_occluded", "cs2.col_box_fill",
        "cs2.col_text_outline", "cs2.col_flags", "cs2.col_fov", "cs2.col_offscreen_arrow"
    };
    for (const char* prefix : colorPrefixes) {
        auto pick = [&]() -> decltype(c.col_enemy)& {
            if (std::strcmp(prefix, "cs2.col_enemy") == 0) return c.col_enemy;
            if (std::strcmp(prefix, "cs2.col_team") == 0) return c.col_team;
            if (std::strcmp(prefix, "cs2.col_skeleton") == 0) return c.col_skeleton;
            if (std::strcmp(prefix, "cs2.col_box") == 0) return c.col_box;
            if (std::strcmp(prefix, "cs2.col_box_corner") == 0) return c.col_box_corner;
            if (std::strcmp(prefix, "cs2.col_name") == 0) return c.col_name;
            if (std::strcmp(prefix, "cs2.col_weapon") == 0) return c.col_weapon;
            if (std::strcmp(prefix, "cs2.col_target") == 0) return c.col_target;
            if (std::strcmp(prefix, "cs2.col_joints") == 0) return c.col_joints;
            if (std::strcmp(prefix, "cs2.col_health") == 0) return c.col_health;
            if (std::strcmp(prefix, "cs2.col_armor") == 0) return c.col_armor;
            if (std::strcmp(prefix, "cs2.col_snaplines") == 0) return c.col_snaplines;
            if (std::strcmp(prefix, "cs2.col_trail") == 0) return c.col_trail;
            if (std::strcmp(prefix, "cs2.col_halo") == 0) return c.col_halo;
            if (std::strcmp(prefix, "cs2.col_fun_effects") == 0) return c.col_fun_effects;
            if (std::strcmp(prefix, "cs2.col_look") == 0) return c.col_look;
            if (std::strcmp(prefix, "cs2.col_head") == 0) return c.col_head;
            if (std::strcmp(prefix, "cs2.col_distance") == 0) return c.col_distance;
            if (std::strcmp(prefix, "cs2.col_visible") == 0) return c.col_visible;
            if (std::strcmp(prefix, "cs2.col_occluded") == 0) return c.col_occluded;
            if (std::strcmp(prefix, "cs2.col_box_fill") == 0) return c.col_box_fill;
            if (std::strcmp(prefix, "cs2.col_text_outline") == 0) return c.col_text_outline;
            if (std::strcmp(prefix, "cs2.col_flags") == 0) return c.col_flags;
            if (std::strcmp(prefix, "cs2.col_offscreen_arrow") == 0) return c.col_offscreen_arrow;
            return c.col_fov;
        };
        if (DeserializeColorArray(key, val, prefix, pick())) {
            return true;
        }
    }
    return false;
}

void ResetCS2ToDefaults() {
    CS2::config = CS2::Config{};
}

void ApplyMinimalCS2() {
    ResetCS2ToDefaults();
    CS2::config.esp_enabled = true;
    CS2::config.box = true;
    CS2::config.health_bar = true;
    CS2::config.name = true;
    CS2::config.team_check = true;
    CS2::config.aim_enabled = false;
    CS2::config.trigger_enabled = false;
}

void ApplyVisualCS2() {
    ApplyMinimalCS2();
    CS2::config.skeleton = true;
    CS2::config.distance = true;
    CS2::config.radar_2d = true;
    CS2::config.radar_2d_size = 180.f;
}





void SerializeWarzone(std::ostringstream& o) {
    auto& c = Warzone::config;
    WBool(o, "warzone.esp_enabled", c.esp_enabled);
    WBool(o, "warzone.team_check", c.team_check);
    WBool(o, "warzone.name", c.name);
    WBool(o, "warzone.distance", c.distance);
    WBool(o, "warzone.box", c.box);
    WBool(o, "warzone.box_corner", c.box_corner);
    WBool(o, "warzone.skeleton", c.skeleton);
    WBool(o, "warzone.health_bar", c.health_bar);
    WBool(o, "warzone.snaplines", c.snaplines);
    WBool(o, "warzone.head_dot", c.head_dot);
    WBool(o, "warzone.ignore_downed", c.ignore_downed);
    WBool(o, "warzone.ignore_ai", c.ignore_ai);
    WInt(o, "warzone.max_distance", c.max_distance);
    WBool(o, "warzone.aim_enabled", c.aim_enabled);
    WBool(o, "warzone.aim_draw_fov", c.aim_draw_fov);
    WBool(o, "warzone.aim_prediction", c.aim_prediction);
    WBool(o, "warzone.aim_humanize", c.aim_humanize);
    WFloat(o, "warzone.aim_fov", c.aim_fov);
    WFloat(o, "warzone.aim_smooth", c.aim_smooth);
    WFloat(o, "warzone.aim_deadzone", c.aim_deadzone);
    WFloat(o, "warzone.aim_max_dist", c.aim_max_dist);
    WInt(o, "warzone.aim_bind", c.aim_bind);
    WInt(o, "warzone.aim_bind2", c.aim_bind2);
    WInt(o, "warzone.aim_bone", c.aim_bone);
    WBool(o, "warzone.aim_ignore_team", c.aim_ignore_team);
    WBool(o, "warzone.aim_ignore_downed", c.aim_ignore_downed);
    WBool(o, "warzone.aim_ignore_ai", c.aim_ignore_ai);
    WBool(o, "warzone.trigger_enabled", c.trigger_enabled);
    WInt(o, "warzone.trigger_bind", c.trigger_bind);
    WInt(o, "warzone.trigger_delay_ms", c.trigger_delay_ms);
    WBool(o, "warzone.radar_2d", c.radar_2d);
    WFloat(o, "warzone.radar_size", c.radar_size);
    WBool(o, "warzone.performance_mode", c.performance_mode);
    WBool(o, "warzone.radar_only_mode", c.radar_only_mode);
    WBool(o, "warzone.armor_bar", c.armor_bar);
    WBool(o, "warzone.aim_always_on", c.aim_always_on);
    WBool(o, "warzone.trigger_team_check", c.trigger_team_check);
    WInt(o, "warzone.esp_density_limit", c.esp_density_limit);
    WBool(o, "warzone.esp_auto_hide_names", c.esp_auto_hide_names);
    WBool(o, "warzone.player_list_enabled", c.player_list_enabled);
    WBool(o, "warzone.misc_airstrike_alert", c.misc_airstrike_alert);
    WBool(o, "warzone.misc_top250_alert", c.misc_top250_alert);
    WBool(o, "warzone.misc_watched_alert", c.misc_watched_alert);
    WBool(o, "warzone.misc_clan_tags", c.misc_clan_tags);
    WBool(o, "warzone.misc_track_between_matches", c.misc_track_between_matches);
    WBool(o, "warzone.misc_spectator_list", c.misc_spectator_list);
    WStr(o, "warzone.scenario_preset", c.scenario_preset);
    SerializeColorArray(o, "warzone.col_enemy", c.col_enemy);
    SerializeColorArray(o, "warzone.col_team", c.col_team);
    SerializeColorArray(o, "warzone.col_target", c.col_target);
    SerializeColorArray(o, "warzone.col_skeleton", c.col_skeleton);
    SerializeColorArray(o, "warzone.col_armor", c.col_armor);
}

bool DeserializeWarzone(const std::string& key, const std::string& val) {
    if (key.rfind("warzone.", 0) != 0) return false;
    auto& c = Warzone::config;
    auto bi = [&](const char* k) { return key == k; };
    auto toB = [&]() -> bool { return config_manager::toB(val); };
    auto toF = [&]() -> float { return config_manager::toF(val); };
    auto toI = [&]() -> int { return config_manager::toI(val); };

    if (bi("warzone.esp_enabled")) { c.esp_enabled = toB(); return true; }
    if (bi("warzone.team_check")) { c.team_check = toB(); return true; }
    if (bi("warzone.name")) { c.name = toB(); return true; }
    if (bi("warzone.distance")) { c.distance = toB(); return true; }
    if (bi("warzone.box")) { c.box = toB(); return true; }
    if (bi("warzone.box_corner")) { c.box_corner = toB(); return true; }
    if (bi("warzone.skeleton")) { c.skeleton = toB(); return true; }
    if (bi("warzone.health_bar")) { c.health_bar = toB(); return true; }
    if (bi("warzone.snaplines")) { c.snaplines = toB(); return true; }
    if (bi("warzone.head_dot")) { c.head_dot = toB(); return true; }
    if (bi("warzone.ignore_downed")) { c.ignore_downed = toB(); return true; }
    if (bi("warzone.ignore_ai")) { c.ignore_ai = toB(); return true; }
    if (bi("warzone.max_distance")) { c.max_distance = toI(); return true; }
    if (bi("warzone.aim_enabled")) { c.aim_enabled = toB(); return true; }
    if (bi("warzone.aim_draw_fov")) { c.aim_draw_fov = toB(); return true; }
    if (bi("warzone.aim_prediction")) { c.aim_prediction = toB(); return true; }
    if (bi("warzone.aim_humanize")) { c.aim_humanize = toB(); return true; }
    if (bi("warzone.aim_fov")) { c.aim_fov = toF(); return true; }
    if (bi("warzone.aim_smooth")) { c.aim_smooth = toF(); return true; }
    if (bi("warzone.aim_deadzone")) { c.aim_deadzone = toF(); return true; }
    if (bi("warzone.aim_max_dist")) { c.aim_max_dist = toF(); return true; }
    if (bi("warzone.aim_bind")) { c.aim_bind = toI(); return true; }
    if (bi("warzone.aim_bind2")) { c.aim_bind2 = toI(); return true; }
    if (bi("warzone.aim_bone")) { c.aim_bone = toI(); return true; }
    if (bi("warzone.aim_ignore_team")) { c.aim_ignore_team = toB(); return true; }
    if (bi("warzone.aim_ignore_downed")) { c.aim_ignore_downed = toB(); return true; }
    if (bi("warzone.aim_ignore_ai")) { c.aim_ignore_ai = toB(); return true; }
    if (bi("warzone.trigger_enabled")) { c.trigger_enabled = toB(); return true; }
    if (bi("warzone.trigger_bind")) { c.trigger_bind = toI(); return true; }
    if (bi("warzone.trigger_delay_ms")) { c.trigger_delay_ms = toI(); return true; }
    if (bi("warzone.radar_2d")) { c.radar_2d = toB(); return true; }
    if (bi("warzone.radar_size")) { c.radar_size = toF(); return true; }
    if (bi("warzone.performance_mode")) { c.performance_mode = toB(); return true; }
    if (bi("warzone.radar_only_mode")) { c.radar_only_mode = toB(); return true; }
    if (bi("warzone.armor_bar")) { c.armor_bar = toB(); return true; }
    if (bi("warzone.aim_always_on")) { c.aim_always_on = toB(); return true; }
    if (bi("warzone.trigger_team_check")) { c.trigger_team_check = toB(); return true; }
    if (bi("warzone.esp_density_limit")) { c.esp_density_limit = toI(); return true; }
    if (bi("warzone.esp_auto_hide_names")) { c.esp_auto_hide_names = toB(); return true; }
    if (bi("warzone.player_list_enabled")) { c.player_list_enabled = toB(); return true; }
    if (bi("warzone.misc_airstrike_alert")) { c.misc_airstrike_alert = toB(); return true; }
    if (bi("warzone.misc_top250_alert")) { c.misc_top250_alert = toB(); return true; }
    if (bi("warzone.misc_watched_alert")) { c.misc_watched_alert = toB(); return true; }
    if (bi("warzone.misc_clan_tags")) { c.misc_clan_tags = toB(); return true; }
    if (bi("warzone.misc_track_between_matches")) { c.misc_track_between_matches = toB(); return true; }
    if (bi("warzone.misc_spectator_list")) { c.misc_spectator_list = toB(); return true; }
    if (bi("warzone.scenario_preset")) { strncpy_s(c.scenario_preset, val.c_str(), _TRUNCATE); return true; }
    if (DeserializeColorArray(key, val, "warzone.col_enemy", c.col_enemy)) return true;
    if (DeserializeColorArray(key, val, "warzone.col_team", c.col_team)) return true;
    if (DeserializeColorArray(key, val, "warzone.col_target", c.col_target)) return true;
    if (DeserializeColorArray(key, val, "warzone.col_skeleton", c.col_skeleton)) return true;
    if (DeserializeColorArray(key, val, "warzone.col_armor", c.col_armor)) return true;
    return false;
}

void ResetWarzoneToDefaults() {
    Warzone::config = Warzone::Config{};
}

void ApplyMinimalWarzone() {
    ResetWarzoneToDefaults();
    Warzone::config.esp_enabled = true;
    Warzone::config.box = true;
    Warzone::config.health_bar = true;
    Warzone::config.name = true;
    Warzone::config.distance = true;
    Warzone::config.skeleton = false;
    Warzone::config.radar_2d = false;
    Warzone::config.aim_enabled = false;
    Warzone::config.trigger_enabled = false;
    std::strncpy(Warzone::config.scenario_preset, "minimal", sizeof(Warzone::config.scenario_preset) - 1);
    Warzone::config.scenario_preset[sizeof(Warzone::config.scenario_preset) - 1] = '\0';
}

void ApplyVisualWarzone() {
    ApplyMinimalWarzone();
    Warzone::config.skeleton = true;
    Warzone::config.radar_2d = true;
    Warzone::config.radar_size = 180.f;
    Warzone::config.esp_auto_hide_names = true;
    std::strncpy(Warzone::config.scenario_preset, "visuals", sizeof(Warzone::config.scenario_preset) - 1);
    Warzone::config.scenario_preset[sizeof(Warzone::config.scenario_preset) - 1] = '\0';
}









void SerializeFortnite(std::ostringstream& o) {
    auto& c = Fortnite::config;
    WBool(o, "fortnite.esp_enabled", c.esp_enabled);
    WBool(o, "fortnite.show_local_marker", c.show_local_marker);
    WBool(o, "fortnite.show_local_coords", c.show_local_coords);
    WBool(o, "fortnite.show_camera_debug", c.show_camera_debug);
    WBool(o, "fortnite.show_debug_panel", c.show_debug_panel);
    WBool(o, "fortnite.box", c.box);
    WBool(o, "fortnite.box_corner", c.box_corner);
    WBool(o, "fortnite.box_fill", c.box_fill);
    WFloat(o, "fortnite.box_thickness", c.box_thickness);
    WFloat(o, "fortnite.box_rounding", c.box_rounding);
    WBool(o, "fortnite.skeleton", c.skeleton);
    WBool(o, "fortnite.skeleton_joints", c.skeleton_joints);
    WFloat(o, "fortnite.skeleton_thickness", c.skeleton_thickness);
    WBool(o, "fortnite.head_dot", c.head_dot);
    WBool(o, "fortnite.head_halo", c.head_halo);
    WFloat(o, "fortnite.head_circle_thickness", c.head_circle_thickness);
    WBool(o, "fortnite.health_bar", c.health_bar);
    WBool(o, "fortnite.armor_bar", c.armor_bar);
    WBool(o, "fortnite.health_value", c.health_value);
    WBool(o, "fortnite.armor_value", c.armor_value);
    WBool(o, "fortnite.name", c.name);
    WBool(o, "fortnite.distance", c.distance);
    WBool(o, "fortnite.weapon_name", c.weapon_name);
    WBool(o, "fortnite.weapon_ammo", c.weapon_ammo);
    WBool(o, "fortnite.text_outline", c.text_outline);
    WFloat(o, "fortnite.text_outline_thickness", c.text_outline_thickness);
    WBool(o, "fortnite.snaplines", c.snaplines);
    WInt(o, "fortnite.snapline_position", c.snapline_position);
    WFloat(o, "fortnite.snapline_thickness", c.snapline_thickness);
    WBool(o, "fortnite.look_direction", c.look_direction);
    WFloat(o, "fortnite.eye_line_thickness", c.eye_line_thickness);
    WFloat(o, "fortnite.look_direction_length", c.look_direction_length);
    WBool(o, "fortnite.chinese_hat", c.chinese_hat);
    WBool(o, "fortnite.angel_wings", c.angel_wings);
    WBool(o, "fortnite.devil_horns", c.devil_horns);
    WBool(o, "fortnite.floating_crown", c.floating_crown);
    WBool(o, "fortnite.trails", c.trails);
    WFloat(o, "fortnite.chinese_hat_scale", c.chinese_hat_scale);
    WFloat(o, "fortnite.fun_effects_scale", c.fun_effects_scale);
    WBool(o, "fortnite.show_bots", c.show_bots);
    WBool(o, "fortnite.visible_check", c.visible_check);
    WBool(o, "fortnite.team_check", c.team_check);
    WBool(o, "fortnite.self_esp", c.self_esp);
    WBool(o, "fortnite.hit_marker", c.hit_marker);
    WBool(o, "fortnite.aim_enabled", c.aim_enabled);
    WBool(o, "fortnite.aim_visible_only", c.aim_visible_only);
    WBool(o, "fortnite.predict", c.predict);
    WFloat(o, "fortnite.aim_smooth", c.aim_smooth);
    WFloat(o, "fortnite.aim_fov", c.aim_fov);
    WFloat(o, "fortnite.max_distance", c.max_distance);
    WFloat(o, "fortnite.bullet_velocity", c.bullet_velocity);
    WBool(o, "fortnite.show_fps", c.show_fps);
    WBool(o, "fortnite.rgb_mode", c.rgb_mode);
    WBool(o, "fortnite.visibility_colors", c.visibility_colors);
    SerializeColorArray(o, "fortnite.col_enemy", c.col_enemy);
    SerializeColorArray(o, "fortnite.col_team", c.col_team);
    SerializeColorArray(o, "fortnite.col_box", c.col_box);
    SerializeColorArray(o, "fortnite.col_box_corner", c.col_box_corner);
    SerializeColorArray(o, "fortnite.col_box_fill", c.col_box_fill);
    SerializeColorArray(o, "fortnite.col_skeleton", c.col_skeleton);
    SerializeColorArray(o, "fortnite.col_joints", c.col_joints);
    SerializeColorArray(o, "fortnite.col_head", c.col_head);
    SerializeColorArray(o, "fortnite.col_halo", c.col_halo);
    SerializeColorArray(o, "fortnite.col_health", c.col_health);
    SerializeColorArray(o, "fortnite.col_armor", c.col_armor);
    SerializeColorArray(o, "fortnite.col_name", c.col_name);
    SerializeColorArray(o, "fortnite.col_local", c.col_local);
    SerializeColorArray(o, "fortnite.col_distance", c.col_distance);
    SerializeColorArray(o, "fortnite.col_weapon", c.col_weapon);
    SerializeColorArray(o, "fortnite.col_snaplines", c.col_snaplines);
    SerializeColorArray(o, "fortnite.col_trail", c.col_trail);
    SerializeColorArray(o, "fortnite.col_fun_effects", c.col_fun_effects);
    SerializeColorArray(o, "fortnite.col_look", c.col_look);
    SerializeColorArray(o, "fortnite.col_text_outline", c.col_text_outline);
    SerializeColorArray(o, "fortnite.col_visible", c.col_visible);
    SerializeColorArray(o, "fortnite.col_occluded", c.col_occluded);
}

bool DeserializeFortnite(const std::string& key, const std::string& val) {
    if (key.rfind("fortnite.", 0) != 0) return false;
    auto& c = Fortnite::config;
    auto bi = [&](const char* k) { return key == k; };
    auto toB = [&]() -> bool { return config_manager::toB(val); };
    auto toF = [&]() -> float { return config_manager::toF(val); };
    auto toI = [&]() -> int { return config_manager::toI(val); };

    if (bi("fortnite.esp_enabled")) { c.esp_enabled = toB(); return true; }
    if (bi("fortnite.show_local_marker")) { c.show_local_marker = toB(); return true; }
    if (bi("fortnite.show_local_coords")) { c.show_local_coords = toB(); return true; }
    if (bi("fortnite.show_camera_debug")) { c.show_camera_debug = toB(); return true; }
    if (bi("fortnite.show_debug_panel")) { c.show_debug_panel = toB(); return true; }
    if (bi("fortnite.box")) { c.box = toB(); return true; }
    if (bi("fortnite.box_corner")) { c.box_corner = toB(); return true; }
    if (bi("fortnite.box_fill")) { c.box_fill = toB(); return true; }
    if (bi("fortnite.box_thickness")) { c.box_thickness = toF(); return true; }
    if (bi("fortnite.box_rounding")) { c.box_rounding = toF(); return true; }
    if (bi("fortnite.skeleton")) { c.skeleton = toB(); return true; }
    if (bi("fortnite.skeleton_joints")) { c.skeleton_joints = toB(); return true; }
    if (bi("fortnite.skeleton_thickness")) { c.skeleton_thickness = toF(); return true; }
    if (bi("fortnite.head_dot")) { c.head_dot = toB(); return true; }
    if (bi("fortnite.head_halo")) { c.head_halo = toB(); return true; }
    if (bi("fortnite.head_circle_thickness")) { c.head_circle_thickness = toF(); return true; }
    if (bi("fortnite.health_bar")) { c.health_bar = toB(); return true; }
    if (bi("fortnite.armor_bar")) { c.armor_bar = toB(); return true; }
    if (bi("fortnite.health_value")) { c.health_value = toB(); return true; }
    if (bi("fortnite.armor_value")) { c.armor_value = toB(); return true; }
    if (bi("fortnite.name")) { c.name = toB(); return true; }
    if (bi("fortnite.distance")) { c.distance = toB(); return true; }
    if (bi("fortnite.weapon_name")) { c.weapon_name = toB(); return true; }
    if (bi("fortnite.weapon_ammo")) { c.weapon_ammo = toB(); return true; }
    if (bi("fortnite.text_outline")) { c.text_outline = toB(); return true; }
    if (bi("fortnite.text_outline_thickness")) { c.text_outline_thickness = toF(); return true; }
    if (bi("fortnite.snaplines")) { c.snaplines = toB(); return true; }
    if (bi("fortnite.snapline_position")) { c.snapline_position = toI(); return true; }
    if (bi("fortnite.snapline_thickness")) { c.snapline_thickness = toF(); return true; }
    if (bi("fortnite.look_direction")) { c.look_direction = toB(); return true; }
    if (bi("fortnite.eye_line_thickness")) { c.eye_line_thickness = toF(); return true; }
    if (bi("fortnite.look_direction_length")) { c.look_direction_length = toF(); return true; }
    if (bi("fortnite.chinese_hat")) { c.chinese_hat = toB(); return true; }
    if (bi("fortnite.angel_wings")) { c.angel_wings = toB(); return true; }
    if (bi("fortnite.devil_horns")) { c.devil_horns = toB(); return true; }
    if (bi("fortnite.floating_crown")) { c.floating_crown = toB(); return true; }
    if (bi("fortnite.trails")) { c.trails = toB(); return true; }
    if (bi("fortnite.chinese_hat_scale")) { c.chinese_hat_scale = toF(); return true; }
    if (bi("fortnite.fun_effects_scale")) { c.fun_effects_scale = toF(); return true; }
    if (bi("fortnite.show_bots")) { c.show_bots = toB(); return true; }
    if (bi("fortnite.visible_check")) { c.visible_check = toB(); return true; }
    if (bi("fortnite.team_check")) { c.team_check = toB(); return true; }
    if (bi("fortnite.self_esp")) { c.self_esp = toB(); return true; }
    if (bi("fortnite.hit_marker")) { c.hit_marker = toB(); return true; }
    if (bi("fortnite.aim_enabled")) { c.aim_enabled = toB(); return true; }
    if (bi("fortnite.aim_visible_only")) { c.aim_visible_only = toB(); return true; }
    if (bi("fortnite.predict")) { c.predict = toB(); return true; }
    if (bi("fortnite.aim_smooth")) { c.aim_smooth = toF(); return true; }
    if (bi("fortnite.aim_fov")) { c.aim_fov = toF(); return true; }
    if (bi("fortnite.max_distance")) { c.max_distance = toF(); return true; }
    if (bi("fortnite.bullet_velocity")) { c.bullet_velocity = toF(); return true; }
    if (bi("fortnite.show_fps")) { c.show_fps = toB(); return true; }
    if (bi("fortnite.rgb_mode")) { c.rgb_mode = toB(); return true; }
    if (bi("fortnite.visibility_colors")) { c.visibility_colors = toB(); return true; }
    if (DeserializeColorArray(key, val, "fortnite.col_enemy", c.col_enemy)) return true;
    if (DeserializeColorArray(key, val, "fortnite.col_team", c.col_team)) return true;
    if (DeserializeColorArray(key, val, "fortnite.col_box", c.col_box)) return true;
    if (DeserializeColorArray(key, val, "fortnite.col_box_corner", c.col_box_corner)) return true;
    if (DeserializeColorArray(key, val, "fortnite.col_box_fill", c.col_box_fill)) return true;
    if (DeserializeColorArray(key, val, "fortnite.col_skeleton", c.col_skeleton)) return true;
    if (DeserializeColorArray(key, val, "fortnite.col_joints", c.col_joints)) return true;
    if (DeserializeColorArray(key, val, "fortnite.col_head", c.col_head)) return true;
    if (DeserializeColorArray(key, val, "fortnite.col_halo", c.col_halo)) return true;
    if (DeserializeColorArray(key, val, "fortnite.col_health", c.col_health)) return true;
    if (DeserializeColorArray(key, val, "fortnite.col_armor", c.col_armor)) return true;
    if (DeserializeColorArray(key, val, "fortnite.col_name", c.col_name)) return true;
    if (DeserializeColorArray(key, val, "fortnite.col_local", c.col_local)) return true;
    if (DeserializeColorArray(key, val, "fortnite.col_distance", c.col_distance)) return true;
    if (DeserializeColorArray(key, val, "fortnite.col_weapon", c.col_weapon)) return true;
    if (DeserializeColorArray(key, val, "fortnite.col_snaplines", c.col_snaplines)) return true;
    if (DeserializeColorArray(key, val, "fortnite.col_trail", c.col_trail)) return true;
    if (DeserializeColorArray(key, val, "fortnite.col_fun_effects", c.col_fun_effects)) return true;
    if (DeserializeColorArray(key, val, "fortnite.col_look", c.col_look)) return true;
    if (DeserializeColorArray(key, val, "fortnite.col_text_outline", c.col_text_outline)) return true;
    if (DeserializeColorArray(key, val, "fortnite.col_visible", c.col_visible)) return true;
    if (DeserializeColorArray(key, val, "fortnite.col_occluded", c.col_occluded)) return true;
    return false;
}




void SerializeFiveM(std::ostringstream& o) {
    (void)o;

}

bool DeserializeFiveM(const std::string& key, const std::string& val) {
    (void)key; (void)val;

    return false;
}




void SerializeColorArray(std::ostringstream& o, const char* prefix, const float value[4]) {
    for (int i = 0; i < 4; ++i) {
        std::string component = std::string(prefix) + "." + std::to_string(i);
        WFloat(o, component.c_str(), value[i]);
    }
}

bool DeserializeColorArray(const std::string& key, const std::string& val, const char* prefix, float value[4]) {
    if (key.rfind(prefix, 0) != 0) return false;
    size_t dotPos = key.find('.', strlen(prefix));
    if (dotPos == std::string::npos) return false;
    std::string idxStr = key.substr(dotPos + 1);
    int idx = config_manager::toI(idxStr);
    if (idx >= 0 && idx < 4) {
        value[idx] = config_manager::toF(val);
        return true;
    }
    return false;
}

}
