#include "config_games.h"
#include "config_core.h"
#include "../Cs2/cs2_game.h"
#include "../Warzone/warzone_game.h"
#include "../Valorant/valorant_game.h"
#include <cstring>
#include <iterator>

namespace config_manager {

// ============================================================
// CS2 Config
// ============================================================
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
    WBool(o, "cs2.weapon_icons", c.weapon_icons);
    WBool(o, "cs2.bomb_timer", c.bomb_timer);
    WBool(o, "cs2.spectator_list", c.spectator_list);
    WFloat(o, "cs2.spectator_window_x", c.spectator_window_x);
    WFloat(o, "cs2.spectator_window_y", c.spectator_window_y);
    WFloat(o, "cs2.bomb_window_x", c.bomb_window_x);
    WFloat(o, "cs2.bomb_window_y", c.bomb_window_y);
    WBool(o, "cs2.smoke_flash", c.smoke_flash);
    WBool(o, "cs2.radar_2d", c.radar_2d);
    WBool(o, "cs2.webradar_enabled", c.webradar_enabled);
    WInt(o, "cs2.webradar_port", c.webradar_port);
    WBool(o, "cs2.webradar_cloudflare", c.webradar_cloudflare);
    WBool(o, "cs2.offscreen_arrows", c.offscreen_arrows);
    WBool(o, "cs2.highlight_aim_target", c.highlight_aim_target);
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
    WBool(o, "cs2.aim_rcs_standalone", c.aim_rcs_standalone);
    WBool(o, "cs2.aim_rcs_auto", c.aim_rcs_auto);
    WFloat(o, "cs2.aim_rcs_x", c.aim_rcs_x);
    WFloat(o, "cs2.aim_rcs_y", c.aim_rcs_y);
    WBool(o, "cs2.trigger_enabled", c.trigger_enabled);
    WBool(o, "cs2.sniper_crosshair", c.sniper_crosshair);
    WBool(o, "cs2.trigger_always_on", c.trigger_always_on);
    WInt(o, "cs2.trigger_bind", c.trigger_bind);
    WInt(o, "cs2.trigger_delay_ms", c.trigger_delay_ms);
    WBool(o, "cs2.trigger_team_check", c.trigger_team_check);
    WBool(o, "cs2.trigger_head_only", c.trigger_head_only);
    WBool(o, "cs2.trigger_scoped_only", c.trigger_scoped_only);
    WBool(o, "cs2.trigger_use_ident", c.trigger_use_ident);
    WBool(o, "cs2.panic_key_enabled", c.panic_key_enabled);
    WInt(o, "cs2.panic_key", c.panic_key);
    WBool(o, "cs2.visibility_colors", c.visibility_colors);
    WBool(o, "cs2.visible_check", c.visible_check);
    WBool(o, "cs2.scope_check", c.scope_check);
    WBool(o, "cs2.sound_esp", c.sound_esp);
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
    WBool(o, "cs2.projectile_timers", c.projectile_timers);
    WBool(o, "cs2.grenade_trail", c.grenade_trail);
    WFloat(o, "cs2.radar_2d_x", c.radar_2d_x);
    WFloat(o, "cs2.radar_2d_y", c.radar_2d_y);
    WFloat(o, "cs2.radar_2d_size", c.radar_2d_size);
    WBool(o, "cs2.skeleton_lod", c.skeleton_lod);
    WFloat(o, "cs2.skeleton_lod_distance", c.skeleton_lod_distance);
    WBool(o, "cs2.c4_carrier", c.c4_carrier);
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
}

bool DeserializeCS2(const std::string& key, const std::string& val) {
    if (key.rfind("cs2.", 0) != 0) return false;
    auto& c = CS2::config;
    auto bi = [&](const char* k) { return key == k; };
    auto toB = [&]() { return val == "1" || val == "true"; };
    auto toF = [&]() { return (float)atof(val.c_str()); };
    auto toI = [&]() { return atoi(val.c_str()); };
    auto toU = [&]() { return (ImU32)strtoul(val.c_str(), nullptr, 10); };

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
    if (bi("cs2.weapon_icons")) { c.weapon_icons = toB(); return true; }
    if (bi("cs2.bomb_timer")) { c.bomb_timer = toB(); return true; }
    if (bi("cs2.spectator_list")) { c.spectator_list = toB(); return true; }
    if (bi("cs2.spectator_window_x")) { c.spectator_window_x = toF(); return true; }
    if (bi("cs2.spectator_window_y")) { c.spectator_window_y = toF(); return true; }
    if (bi("cs2.bomb_window_x")) { c.bomb_window_x = toF(); return true; }
    if (bi("cs2.bomb_window_y")) { c.bomb_window_y = toF(); return true; }
    if (bi("cs2.smoke_flash")) { c.smoke_flash = toB(); return true; }
    if (bi("cs2.radar_2d")) { c.radar_2d = toB(); return true; }
    if (bi("cs2.webradar_enabled")) { c.webradar_enabled = toB(); return true; }
    if (bi("cs2.webradar_port")) { c.webradar_port = toI(); return true; }
    if (bi("cs2.webradar_cloudflare")) { c.webradar_cloudflare = toB(); return true; }
    if (bi("cs2.offscreen_arrows")) { c.offscreen_arrows = toB(); return true; }
    if (bi("cs2.highlight_aim_target")) { c.highlight_aim_target = toB(); return true; }
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
    if (bi("cs2.aim_rcs_standalone")) { c.aim_rcs_standalone = toB(); return true; }
    if (bi("cs2.aim_rcs_auto")) { c.aim_rcs_auto = toB(); return true; }
    if (bi("cs2.aim_rcs_x")) { c.aim_rcs_x = toF(); return true; }
    if (bi("cs2.aim_rcs_y")) { c.aim_rcs_y = toF(); return true; }
    if (bi("cs2.trigger_enabled")) { c.trigger_enabled = toB(); return true; }
    if (bi("cs2.sniper_crosshair")) { c.sniper_crosshair = toB(); return true; }
    if (bi("cs2.trigger_always_on")) { c.trigger_always_on = toB(); return true; }
    if (bi("cs2.trigger_bind")) { c.trigger_bind = toI(); return true; }
    if (bi("cs2.trigger_delay_ms")) { c.trigger_delay_ms = toI(); return true; }
    if (bi("cs2.trigger_team_check")) { c.trigger_team_check = toB(); return true; }
    if (bi("cs2.trigger_head_only")) { c.trigger_head_only = toB(); return true; }
    if (bi("cs2.trigger_scoped_only")) { c.trigger_scoped_only = toB(); return true; }
    if (bi("cs2.trigger_use_ident")) { c.trigger_use_ident = toB(); return true; }
    if (bi("cs2.panic_key_enabled")) { c.panic_key_enabled = toB(); return true; }
    if (bi("cs2.panic_key")) { c.panic_key = toI(); return true; }

    if (bi("cs2.visibility_colors")) { c.visibility_colors = toB(); return true; }
    if (bi("cs2.visible_check")) { c.visible_check = toB(); return true; }
    if (bi("cs2.scope_check")) { c.scope_check = toB(); return true; }
    if (bi("cs2.sound_esp")) { c.sound_esp = toB(); return true; }
    if (bi("cs2.footstep_esp")) { c.footstep_esp = toB(); return true; }
    if (bi("cs2.player_flags")) { c.player_flags = toB(); return true; }
    if (bi("cs2.flag_blind")) { c.flag_blind = toB(); return true; }
    if (bi("cs2.flag_scoped")) { c.flag_scoped = toB(); return true; }
    if (bi("cs2.flag_defusing")) { c.flag_defusing = toB(); return true; }
    if (bi("cs2.flag_kit")) { c.flag_kit = toB(); return true; }
    if (bi("cs2.flag_money")) { c.flag_money = toB(); return true; }
    if (bi("cs2.weapon_ammo")) { c.weapon_ammo = toB(); return true; }
    if (bi("cs2.dropped_weapons")) { c.dropped_weapons = toB(); return true; }
    if (bi("cs2.dropped_weapon_pistols")) { c.dropped_weapon_pistols = toB(); return true; }
    if (bi("cs2.dropped_weapon_smgs")) { c.dropped_weapon_smgs = toB(); return true; }
    if (bi("cs2.dropped_weapon_rifles")) { c.dropped_weapon_rifles = toB(); return true; }
    if (bi("cs2.dropped_weapon_heavy")) { c.dropped_weapon_heavy = toB(); return true; }
    if (bi("cs2.dropped_weapon_icons")) { c.dropped_weapon_icons = toB(); return true; }
    if (bi("cs2.dropped_weapon_ammo")) { c.dropped_weapon_ammo = toB(); return true; }
    if (bi("cs2.projectile_timers")) { c.projectile_timers = toB(); return true; }
    if (bi("cs2.grenade_trail")) { c.grenade_trail = toB(); return true; }
    if (bi("cs2.radar_2d_x")) { c.radar_2d_x = toF(); return true; }
    if (bi("cs2.radar_2d_y")) { c.radar_2d_y = toF(); return true; }
    if (bi("cs2.radar_2d_size")) { c.radar_2d_size = toF(); return true; }
    if (bi("cs2.skeleton_lod")) { c.skeleton_lod = toB(); return true; }
    if (bi("cs2.skeleton_lod_distance")) { c.skeleton_lod_distance = toF(); return true; }
    if (bi("cs2.c4_carrier")) { c.c4_carrier = toB(); return true; }
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
    // Color arrays
    static const char* colorPrefixes[] = {
        "cs2.col_enemy", "cs2.col_team", "cs2.col_skeleton", "cs2.col_box",
        "cs2.col_name", "cs2.col_weapon", "cs2.col_target", "cs2.col_joints",
        "cs2.col_health", "cs2.col_armor", "cs2.col_snaplines", "cs2.col_trail",
        "cs2.col_halo", "cs2.col_fun_effects", "cs2.col_look", "cs2.col_box_corner", "cs2.col_head",
        "cs2.col_distance", "cs2.col_visible", "cs2.col_occluded", "cs2.col_box_fill",
        "cs2.col_text_outline", "cs2.col_flags", "cs2.col_fov"
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
    CS2::config.weapon_icons = true;
    CS2::config.radar_2d = true;
    CS2::config.radar_2d_size = 180.f;
}

// ============================================================
#if 0 // Rust support removed.
// Rust Config
// ============================================================
void SerializeRust(std::ostringstream& o) {
    auto& c = Rust::config;
    WBool(o, "rust.esp_enabled", c.esp_enabled);
    WBool(o, "rust.name", c.name);
    WBool(o, "rust.distance", c.distance);
    WBool(o, "rust.box", c.box);
    WBool(o, "rust.snaplines", c.snaplines);
    WBool(o, "rust.show_sleepers", c.show_sleepers);
    WBool(o, "rust.show_npc", c.show_npc);
    WInt(o, "rust.max_distance", c.max_distance);
    WBool(o, "rust.aim_enabled", c.aim_enabled);
    WBool(o, "rust.aim_draw_fov", c.aim_draw_fov);
    WBool(o, "rust.aim_draw_prediction", c.aim_draw_prediction);
    WBool(o, "rust.aim_draw_line", c.aim_draw_line);
    WBool(o, "rust.aim_prediction", c.aim_prediction);
    WBool(o, "rust.aim_bone_transitions", c.aim_bone_transitions);
    WFloat(o, "rust.aim_fov", c.aim_fov);
    WFloat(o, "rust.aim_smooth", c.aim_smooth);
    WFloat(o, "rust.aim_max_dist", c.aim_max_dist);
    WFloat(o, "rust.aim_interval_ms", c.aim_interval_ms);
    WInt(o, "rust.aim_bind", c.aim_bind);
    WInt(o, "rust.aim_bind2", c.aim_bind2);
    WInt(o, "rust.aim_bind3", c.aim_bind3);
    WInt(o, "rust.aim_bone", c.aim_bone);
    WInt(o, "rust.aim_override_key", c.aim_override_key);
    WFloat(o, "rust.aim_override_smooth", c.aim_override_smooth);
    WFloat(o, "rust.aim_override_fov", c.aim_override_fov);
    WBool(o, "rust.aim_humanize", c.aim_humanize);
    WFloat(o, "rust.aim_deadzone", c.aim_deadzone);
    WFloat(o, "rust.sticky_ms", c.sticky_ms);
    WBool(o, "rust.aim_ignore_sleepers", c.aim_ignore_sleepers);
    WBool(o, "rust.aim_ignore_npc", c.aim_ignore_npc);
    WBool(o, "rust.aim_ignore_wounded", c.aim_ignore_wounded);
    WBool(o, "rust.aim_ignore_knocked", c.aim_ignore_knocked);
    WBool(o, "rust.aim_ignore_team", c.aim_ignore_team);
    WBool(o, "rust.skeleton", c.skeleton);
    WBool(o, "rust.health_bar", c.health_bar);
    WBool(o, "rust.box_corner", c.box_corner);
    WBool(o, "rust.world_esp", c.world_esp);
    WBool(o, "rust.self_esp", c.self_esp);
    WBool(o, "rust.snaplines_center", c.snaplines_center);
    WBool(o, "rust.snaplines_target_only", c.snaplines_target_only);
    WBool(o, "rust.show_wounded", c.show_wounded);
    WBool(o, "rust.show_safezone", c.show_safezone);
    WBool(o, "rust.show_knocked", c.show_knocked);
    WBool(o, "rust.head_dot", c.head_dot);
    WBool(o, "rust.armor_bar", c.armor_bar);
    WBool(o, "rust.weapon_name", c.weapon_name);
    WBool(o, "rust.skeleton_joints", c.skeleton_joints);
    WBool(o, "rust.offscreen_arrows", c.offscreen_arrows);
    WBool(o, "rust.directional_arrows", c.directional_arrows);
    WBool(o, "rust.highlight_aim_target", c.highlight_aim_target);
    WBool(o, "rust.show_team_id", c.show_team_id);
    WBool(o, "rust.show_steam_id", c.show_steam_id);
    WBool(o, "rust.show_flags", c.show_flags);
    WBool(o, "rust.belt_esp", c.belt_esp);
    WBool(o, "rust.distance_gradient", c.distance_gradient);
    WBool(o, "rust.vis_color", c.vis_color);
    WInt(o, "rust.sleeper_max_distance", c.sleeper_max_distance);
    WInt(o, "rust.belt_hotkey", c.belt_hotkey);
    WFloat(o, "rust.box_thickness", c.box_thickness);
    WFloat(o, "rust.skeleton_thickness", c.skeleton_thickness);
    WFloat(o, "rust.snapline_thickness", c.snapline_thickness);
    WFloat(o, "rust.arrow_width", c.arrow_width);
    WFloat(o, "rust.arrow_length", c.arrow_length);
    WInt(o, "rust.box_type", c.box_type);
    WBool(o, "rust.trigger_enabled", c.trigger_enabled);
    WInt(o, "rust.trigger_bind", c.trigger_bind);
    WInt(o, "rust.trigger_delay_ms", c.trigger_delay_ms);
    WBool(o, "rust.hit_chance_ui", c.hit_chance_ui);
    WBool(o, "rust.aim_cat_global", c.aim_cat_global);
    WBool(o, "rust.aim_cat_rifles", c.aim_cat_rifles);
    WBool(o, "rust.aim_cat_smgs", c.aim_cat_smgs);
    WBool(o, "rust.aim_cat_pistols", c.aim_cat_pistols);
    WBool(o, "rust.aim_cat_lmgs", c.aim_cat_lmgs);
    WBool(o, "rust.aim_cat_shotguns", c.aim_cat_shotguns);
    WBool(o, "rust.aim_cat_bows", c.aim_cat_bows);
    WBool(o, "rust.no_recoil", c.no_recoil);
    WInt(o, "rust.recoil_x", c.recoil_x);
    WInt(o, "rust.recoil_y", c.recoil_y);
    WInt(o, "rust.recoil_mode", c.recoil_mode);
    WBool(o, "rust.spider_man", c.spider_man);
    WBool(o, "rust.admin_esp", c.admin_esp);
    WBool(o, "rust.admin_flag", c.admin_flag);
    WBool(o, "rust.bright_nights", c.bright_nights);
    WBool(o, "rust.bright_caves", c.bright_caves);
    WBool(o, "rust.change_time", c.change_time);
    WInt(o, "rust.time_of_day", c.time_of_day);
    WBool(o, "rust.change_fov", c.change_fov);
    WInt(o, "rust.fov_value", c.fov_value);
    WBool(o, "rust.remove_water", c.remove_water);
    WBool(o, "rust.instant_eoka", c.instant_eoka);
    WBool(o, "rust.inventory_esp", c.inventory_esp);
    WBool(o, "rust.vischeck_raycast", c.vischeck_raycast);
    WBool(o, "rust.vischeck_debug_wire", c.vischeck_debug_wire);
    WBool(o, "rust.fortify_export_sleeping_bags", c.fortify_export_sleeping_bags);
    WFloat(o, "rust.fortify_min_distance", c.fortify_min_distance);
    WBool(o, "rust.performance_mode", c.performance_mode);
    WBool(o, "rust.panic_key_enabled", c.panic_key_enabled);
    WInt(o, "rust.panic_key", c.panic_key);
    WBool(o, "rust.radar_2d", c.radar_2d);
    WFloat(o, "rust.radar_size", c.radar_size);
    WFloat(o, "rust.radar_x", c.radar_x);
    WFloat(o, "rust.radar_y", c.radar_y);
    WBool(o, "rust.team_check", c.team_check);
    SerializeColorArray(o, "rust.col_enemy", c.col_enemy);
    SerializeColorArray(o, "rust.col_sleeper", c.col_sleeper);
    SerializeColorArray(o, "rust.col_npc", c.col_npc);
    SerializeColorArray(o, "rust.col_wounded", c.col_wounded);
    SerializeColorArray(o, "rust.col_team", c.col_team);
    SerializeColorArray(o, "rust.col_name", c.col_name);
    SerializeColorArray(o, "rust.col_target", c.col_target);
    SerializeColorArray(o, "rust.col_skeleton", c.col_skeleton);
    SerializeColorArray(o, "rust.col_visible", c.col_visible);
    SerializeColorArray(o, "rust.col_hidden", c.col_hidden);
    SerializeColorArray(o, "rust.col_fov", c.col_fov);
}

bool DeserializeRust(const std::string& key, const std::string& val) {
    if (key.rfind("rust.", 0) != 0) return false;
    auto& c = Rust::config;
    auto bi = [&](const char* k) { return key == k; };
    auto toB = [&]() { return val == "1" || val == "true"; };
    auto toF = [&]() { return (float)atof(val.c_str()); };
    auto toI = [&]() { return atoi(val.c_str()); };

    if (bi("rust.esp_enabled")) { c.esp_enabled = toB(); return true; }
    if (bi("rust.name")) { c.name = toB(); return true; }
    if (bi("rust.distance")) { c.distance = toB(); return true; }
    if (bi("rust.box")) { c.box = toB(); return true; }
    if (bi("rust.snaplines")) { c.snaplines = toB(); return true; }
    if (bi("rust.show_sleepers")) { c.show_sleepers = toB(); return true; }
    if (bi("rust.show_npc")) { c.show_npc = toB(); return true; }
    if (bi("rust.max_distance")) { c.max_distance = toI(); return true; }
    if (bi("rust.aim_enabled")) { c.aim_enabled = toB(); return true; }
    if (bi("rust.aim_draw_fov")) { c.aim_draw_fov = toB(); return true; }
    if (bi("rust.aim_draw_prediction")) { c.aim_draw_prediction = toB(); return true; }
    if (bi("rust.aim_draw_line")) { c.aim_draw_line = toB(); return true; }
    if (bi("rust.aim_prediction")) { c.aim_prediction = toB(); return true; }
    if (bi("rust.aim_bone_transitions")) { c.aim_bone_transitions = toB(); return true; }
    if (bi("rust.aim_fov")) { c.aim_fov = toF(); return true; }
    if (bi("rust.aim_smooth")) { c.aim_smooth = toF(); return true; }
    if (bi("rust.aim_max_dist")) { c.aim_max_dist = toF(); return true; }
    if (bi("rust.aim_interval_ms")) { c.aim_interval_ms = toF(); return true; }
    if (bi("rust.aim_bind")) { c.aim_bind = toI(); return true; }
    if (bi("rust.aim_bind2")) { c.aim_bind2 = toI(); return true; }
    if (bi("rust.aim_bind3")) { c.aim_bind3 = toI(); return true; }
    if (bi("rust.aim_bone")) { c.aim_bone = toI(); return true; }
    if (bi("rust.aim_override_key")) { c.aim_override_key = toI(); return true; }
    if (bi("rust.aim_override_smooth")) { c.aim_override_smooth = toF(); return true; }
    if (bi("rust.aim_override_fov")) { c.aim_override_fov = toF(); return true; }
    if (bi("rust.aim_humanize")) { c.aim_humanize = toB(); return true; }
    if (bi("rust.aim_deadzone")) { c.aim_deadzone = toF(); return true; }
    if (bi("rust.sticky_ms")) { c.sticky_ms = toF(); return true; }
    if (bi("rust.aim_ignore_sleepers")) { c.aim_ignore_sleepers = toB(); return true; }
    if (bi("rust.aim_ignore_npc")) { c.aim_ignore_npc = toB(); return true; }
    if (bi("rust.aim_ignore_wounded")) { c.aim_ignore_wounded = toB(); return true; }
    if (bi("rust.aim_ignore_knocked")) { c.aim_ignore_knocked = toB(); return true; }
    if (bi("rust.aim_ignore_team")) { c.aim_ignore_team = toB(); return true; }
    if (bi("rust.skeleton")) { c.skeleton = toB(); return true; }
    if (bi("rust.health_bar")) { c.health_bar = toB(); return true; }
    if (bi("rust.box_corner")) { c.box_corner = toB(); return true; }
    if (bi("rust.world_esp")) { c.world_esp = toB(); return true; }
    if (bi("rust.team_check")) { c.team_check = toB(); return true; }
    if (bi("rust.self_esp")) { c.self_esp = toB(); return true; }
    if (bi("rust.snaplines_center")) { c.snaplines_center = toB(); return true; }
    if (bi("rust.snaplines_target_only")) { c.snaplines_target_only = toB(); return true; }
    if (bi("rust.show_wounded")) { c.show_wounded = toB(); return true; }
    if (bi("rust.show_safezone")) { c.show_safezone = toB(); return true; }
    if (bi("rust.show_knocked")) { c.show_knocked = toB(); return true; }
    if (bi("rust.head_dot")) { c.head_dot = toB(); return true; }
    if (bi("rust.armor_bar")) { c.armor_bar = toB(); return true; }
    if (bi("rust.weapon_name")) { c.weapon_name = toB(); return true; }
    if (bi("rust.skeleton_joints")) { c.skeleton_joints = toB(); return true; }
    if (bi("rust.offscreen_arrows")) { c.offscreen_arrows = toB(); return true; }
    if (bi("rust.directional_arrows")) { c.directional_arrows = toB(); return true; }
    if (bi("rust.highlight_aim_target")) { c.highlight_aim_target = toB(); return true; }
    if (bi("rust.show_team_id")) { c.show_team_id = toB(); return true; }
    if (bi("rust.show_steam_id")) { c.show_steam_id = toB(); return true; }
    if (bi("rust.show_flags")) { c.show_flags = toB(); return true; }
    if (bi("rust.belt_esp")) { c.belt_esp = toB(); return true; }
    if (bi("rust.distance_gradient")) { c.distance_gradient = toB(); return true; }
    if (bi("rust.vis_color")) { c.vis_color = toB(); return true; }
    if (bi("rust.sleeper_max_distance")) { c.sleeper_max_distance = toI(); return true; }
    if (bi("rust.belt_hotkey")) { c.belt_hotkey = toI(); return true; }
    if (bi("rust.box_thickness")) { c.box_thickness = toF(); return true; }
    if (bi("rust.skeleton_thickness")) { c.skeleton_thickness = toF(); return true; }
    if (bi("rust.snapline_thickness")) { c.snapline_thickness = toF(); return true; }
    if (bi("rust.arrow_width")) { c.arrow_width = toF(); return true; }
    if (bi("rust.arrow_length")) { c.arrow_length = toF(); return true; }
    if (bi("rust.box_type")) { c.box_type = toI(); return true; }
    if (bi("rust.trigger_enabled")) { c.trigger_enabled = toB(); return true; }
    if (bi("rust.trigger_bind")) { c.trigger_bind = toI(); return true; }
    if (bi("rust.trigger_delay_ms")) { c.trigger_delay_ms = toI(); return true; }
    if (bi("rust.hit_chance_ui")) { c.hit_chance_ui = toB(); return true; }
    if (bi("rust.aim_cat_global")) { c.aim_cat_global = toB(); return true; }
    if (bi("rust.aim_cat_rifles")) { c.aim_cat_rifles = toB(); return true; }
    if (bi("rust.aim_cat_smgs")) { c.aim_cat_smgs = toB(); return true; }
    if (bi("rust.aim_cat_pistols")) { c.aim_cat_pistols = toB(); return true; }
    if (bi("rust.aim_cat_lmgs")) { c.aim_cat_lmgs = toB(); return true; }
    if (bi("rust.aim_cat_shotguns")) { c.aim_cat_shotguns = toB(); return true; }
    if (bi("rust.aim_cat_bows")) { c.aim_cat_bows = toB(); return true; }
    if (bi("rust.no_recoil")) { c.no_recoil = toB(); return true; }
    if (bi("rust.recoil_x")) { c.recoil_x = toI(); return true; }
    if (bi("rust.recoil_y")) { c.recoil_y = toI(); return true; }
    if (bi("rust.recoil_mode")) { c.recoil_mode = toI(); return true; }
    if (bi("rust.spider_man")) { c.spider_man = toB(); return true; }
    if (bi("rust.admin_esp")) { c.admin_esp = toB(); return true; }
    if (bi("rust.admin_flag")) { c.admin_flag = toB(); return true; }
    if (bi("rust.bright_nights")) { c.bright_nights = toB(); return true; }
    if (bi("rust.bright_caves")) { c.bright_caves = toB(); return true; }
    if (bi("rust.change_time")) { c.change_time = toB(); return true; }
    if (bi("rust.time_of_day")) { c.time_of_day = toI(); return true; }
    if (bi("rust.change_fov")) { c.change_fov = toB(); return true; }
    if (bi("rust.fov_value")) { c.fov_value = toI(); return true; }
    if (bi("rust.remove_water")) { c.remove_water = toB(); return true; }
    if (bi("rust.instant_eoka")) { c.instant_eoka = toB(); return true; }
    if (bi("rust.inventory_esp")) { c.inventory_esp = toB(); return true; }
    if (bi("rust.vischeck_raycast")) { c.vischeck_raycast = toB(); return true; }
    if (bi("rust.vischeck_debug_wire")) { c.vischeck_debug_wire = toB(); return true; }
    if (bi("rust.fortify_export_sleeping_bags")) { c.fortify_export_sleeping_bags = toB(); return true; }
    if (bi("rust.fortify_min_distance")) { c.fortify_min_distance = toF(); return true; }
    if (bi("rust.performance_mode")) { c.performance_mode = toB(); return true; }
    if (bi("rust.panic_key_enabled")) { c.panic_key_enabled = toB(); return true; }
    if (bi("rust.panic_key")) { c.panic_key = toI(); return true; }
    if (bi("rust.radar_2d")) { c.radar_2d = toB(); return true; }
    if (bi("rust.radar_size")) { c.radar_size = toF(); return true; }
    if (bi("rust.radar_x")) { c.radar_x = toF(); return true; }
    if (bi("rust.radar_y")) { c.radar_y = toF(); return true; }
    static const char* prefixes[] = {"rust.col_enemy","rust.col_sleeper","rust.col_npc","rust.col_wounded","rust.col_team","rust.col_name","rust.col_target","rust.col_skeleton","rust.col_visible","rust.col_hidden","rust.col_fov"};
    float* values[] = {c.col_enemy,c.col_sleeper,c.col_npc,c.col_wounded,c.col_team,c.col_name,c.col_target,c.col_skeleton,c.col_visible,c.col_hidden,c.col_fov};
    for (size_t i = 0; i < std::size(prefixes); ++i) if (DeserializeColorArray(key,val,prefixes[i],values[i])) return true;
    return false;
}

void ResetRustToDefaults() {
    Rust::config = Rust::Config{};
}

void ApplyMinimalRust() {
    ResetRustToDefaults();
    Rust::config.esp_enabled = true;
    Rust::config.box = true;
    Rust::config.health_bar = true;
    Rust::config.name = true;
    Rust::config.distance = true;
    Rust::config.skeleton = false;
    Rust::config.world_esp = false;
    Rust::config.aim_enabled = false;
    Rust::config.trigger_enabled = false;
}

void ApplyVisualRust() {
    ApplyMinimalRust();
    Rust::config.skeleton = true;
    Rust::config.weapon_name = true;
    Rust::config.show_flags = true;
    Rust::config.offscreen_arrows = true;
    Rust::config.radar_2d = true;
}

 #endif

// ============================================================
// Warzone Config
// ============================================================
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
}

bool DeserializeWarzone(const std::string& key, const std::string& val) {
    if (key.rfind("warzone.", 0) != 0) return false;
    auto& c = Warzone::config;
    auto bi = [&](const char* k) { return key == k; };
    auto toB = [&]() { return val == "1" || val == "true"; };
    auto toF = [&]() { return (float)atof(val.c_str()); };
    auto toI = [&]() { return atoi(val.c_str()); };

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

// ============================================================
// Valorant Config
// ============================================================
void SerializeValorant(std::ostringstream& o) {
    auto& c = Valorant::config;
    WBool(o, "valorant.esp_enabled", c.esp_enabled);
    WBool(o, "valorant.self_esp", c.self_esp);
    WBool(o, "valorant.visibility_colors", c.visibility_colors);
    WBool(o, "valorant.team_check", c.team_check);
    WBool(o, "valorant.box", c.box);
    WBool(o, "valorant.box_corner", c.box_corner);
    WBool(o, "valorant.skeleton", c.skeleton);
    WBool(o, "valorant.health_bar", c.health_bar);
    WBool(o, "valorant.armor_bar", c.armor_bar);
    WBool(o, "valorant.name", c.name);
    WBool(o, "valorant.distance", c.distance);
    WBool(o, "valorant.head_dot", c.head_dot);
    WBool(o, "valorant.snaplines", c.snaplines);
    WBool(o, "valorant.weapon_name", c.weapon_name);
    WFloat(o, "valorant.max_distance", c.max_distance);
    WBool(o, "valorant.aim_enabled", c.aim_enabled);
    WBool(o, "valorant.aim_draw_fov", c.aim_draw_fov);
    WFloat(o, "valorant.aim_fov", c.aim_fov);
    WFloat(o, "valorant.aim_smooth", c.aim_smooth);
    WFloat(o, "valorant.aim_max_dist", c.aim_max_dist);
    WInt(o, "valorant.aim_bone", c.aim_bone);
    WInt(o, "valorant.aim_bind", c.aim_bind);
    WInt(o, "valorant.aim_bind2", c.aim_bind2);
    WFloat(o, "valorant.aim_deadzone", c.aim_deadzone);
    WFloat(o, "valorant.sticky_ms", c.sticky_ms);
    WBool(o, "valorant.aim_prediction", c.aim_prediction);
    WBool(o, "valorant.aim_humanize", c.aim_humanize);
    WBool(o, "valorant.trigger_enabled", c.trigger_enabled);
    WInt(o, "valorant.trigger_bind", c.trigger_bind);
    WInt(o, "valorant.trigger_delay_ms", c.trigger_delay_ms);
    WBool(o, "valorant.trigger_team_check", c.trigger_team_check);
    WBool(o, "valorant.trigger_head_only", c.trigger_head_only);
    WBool(o, "valorant.performance_mode", c.performance_mode);
    WBool(o, "valorant.show_fps", c.show_fps);
    WInt(o, "valorant.max_actors", c.max_actors);
    WBool(o, "valorant.skeleton_joints", c.skeleton_joints);
    WFloat(o, "valorant.skeleton_lod_distance", c.skeleton_lod_distance);
    WBool(o, "valorant.skeleton_lod", c.skeleton_lod);
    WBool(o, "valorant.bone_draw_arms", c.bone_draw_arms);
    WBool(o, "valorant.bone_draw_legs", c.bone_draw_legs);
    WBool(o, "valorant.show_makcu_status", c.show_makcu_status);
    WBool(o, "valorant.highlight_aim_target", c.highlight_aim_target);
    SerializeColorArray(o, "valorant.col_enemy", c.col_enemy);
    SerializeColorArray(o, "valorant.col_team", c.col_team);
    SerializeColorArray(o, "valorant.col_skeleton", c.col_skeleton);
    SerializeColorArray(o, "valorant.col_box", c.col_box);
    SerializeColorArray(o, "valorant.col_name", c.col_name);
    SerializeColorArray(o, "valorant.col_weapon", c.col_weapon);
    SerializeColorArray(o, "valorant.col_target", c.col_target);
    SerializeColorArray(o, "valorant.col_joints", c.col_joints);
    SerializeColorArray(o, "valorant.col_health", c.col_health);
    SerializeColorArray(o, "valorant.col_armor", c.col_armor);
    SerializeColorArray(o, "valorant.col_snaplines", c.col_snaplines);
    SerializeColorArray(o, "valorant.col_fov", c.col_fov);
}

bool DeserializeValorant(const std::string& key, const std::string& val) {
    if (key.rfind("valorant.", 0) != 0) return false;
    auto& c = Valorant::config;
    auto bi = [&](const char* k) { return key == k; };
    auto toB = [&]() { return val == "1" || val == "true"; };
    auto toF = [&]() { return (float)atof(val.c_str()); };
    auto toI = [&]() { return atoi(val.c_str()); };

    if (bi("valorant.esp_enabled")) { c.esp_enabled = toB(); return true; }
    if (bi("valorant.self_esp")) { c.self_esp = toB(); return true; }
    if (bi("valorant.visibility_colors")) { c.visibility_colors = toB(); return true; }
    if (bi("valorant.team_check")) { c.team_check = toB(); return true; }
    if (bi("valorant.box")) { c.box = toB(); return true; }
    if (bi("valorant.box_corner")) { c.box_corner = toB(); return true; }
    if (bi("valorant.skeleton")) { c.skeleton = toB(); return true; }
    if (bi("valorant.health_bar")) { c.health_bar = toB(); return true; }
    if (bi("valorant.armor_bar")) { c.armor_bar = toB(); return true; }
    if (bi("valorant.name")) { c.name = toB(); return true; }
    if (bi("valorant.distance")) { c.distance = toB(); return true; }
    if (bi("valorant.head_dot")) { c.head_dot = toB(); return true; }
    if (bi("valorant.snaplines")) { c.snaplines = toB(); return true; }
    if (bi("valorant.weapon_name")) { c.weapon_name = toB(); return true; }
    if (bi("valorant.max_distance")) { c.max_distance = toF(); return true; }
    if (bi("valorant.aim_enabled")) { c.aim_enabled = toB(); return true; }
    if (bi("valorant.aim_draw_fov")) { c.aim_draw_fov = toB(); return true; }
    if (bi("valorant.aim_fov")) { c.aim_fov = toF(); return true; }
    if (bi("valorant.aim_smooth")) { c.aim_smooth = toF(); return true; }
    if (bi("valorant.aim_max_dist")) { c.aim_max_dist = toF(); return true; }
    if (bi("valorant.aim_bone")) { c.aim_bone = toI(); return true; }
    if (bi("valorant.aim_bind")) { c.aim_bind = toI(); return true; }
    if (bi("valorant.aim_bind2")) { c.aim_bind2 = toI(); return true; }
    if (bi("valorant.aim_deadzone")) { c.aim_deadzone = toF(); return true; }
    if (bi("valorant.sticky_ms")) { c.sticky_ms = toF(); return true; }
    if (bi("valorant.aim_prediction")) { c.aim_prediction = toB(); return true; }
    if (bi("valorant.aim_humanize")) { c.aim_humanize = toB(); return true; }
    if (bi("valorant.trigger_enabled")) { c.trigger_enabled = toB(); return true; }
    if (bi("valorant.trigger_bind")) { c.trigger_bind = toI(); return true; }
    if (bi("valorant.trigger_delay_ms")) { c.trigger_delay_ms = toI(); return true; }
    if (bi("valorant.trigger_team_check")) { c.trigger_team_check = toB(); return true; }
    if (bi("valorant.trigger_head_only")) { c.trigger_head_only = toB(); return true; }
    if (bi("valorant.performance_mode")) { c.performance_mode = toB(); return true; }
    if (bi("valorant.show_fps")) { c.show_fps = toB(); return true; }
    if (bi("valorant.max_actors")) { c.max_actors = toI(); return true; }
    if (bi("valorant.skeleton_joints")) { c.skeleton_joints = toB(); return true; }
    if (bi("valorant.skeleton_lod_distance")) { c.skeleton_lod_distance = toF(); return true; }
    if (bi("valorant.skeleton_lod")) { c.skeleton_lod = toB(); return true; }
    if (bi("valorant.bone_draw_arms")) { c.bone_draw_arms = toB(); return true; }
    if (bi("valorant.bone_draw_legs")) { c.bone_draw_legs = toB(); return true; }
    if (bi("valorant.show_makcu_status")) { c.show_makcu_status = toB(); return true; }
    if (bi("valorant.highlight_aim_target")) { c.highlight_aim_target = toB(); return true; }
    static const char* prefixes[] = {"valorant.col_enemy","valorant.col_team","valorant.col_skeleton","valorant.col_box","valorant.col_name","valorant.col_weapon","valorant.col_target","valorant.col_joints","valorant.col_health","valorant.col_armor","valorant.col_snaplines","valorant.col_fov"};
    float* values[] = {c.col_enemy,c.col_team,c.col_skeleton,c.col_box,c.col_name,c.col_weapon,c.col_target,c.col_joints,c.col_health,c.col_armor,c.col_snaplines,c.col_fov};
    for (size_t i = 0; i < std::size(prefixes); ++i) if (DeserializeColorArray(key,val,prefixes[i],values[i])) return true;
    return false;
}

void ResetValorantToDefaults() {
    Valorant::config = Valorant::Config{};
}

void ApplyMinimalValorant() {
    ResetValorantToDefaults();
    Valorant::config.esp_enabled = true;
    Valorant::config.box = true;
    Valorant::config.health_bar = true;
    Valorant::config.name = true;
    Valorant::config.distance = true;
    Valorant::config.skeleton = false;
    Valorant::config.aim_enabled = false;
    Valorant::config.trigger_enabled = false;
}

void ApplyVisualValorant() {
    ApplyMinimalValorant();
    Valorant::config.skeleton = true;
    Valorant::config.armor_bar = true;
    Valorant::config.weapon_name = true;
}

// ============================================================
// FiveM (Shared) Config
// ============================================================
void SerializeFiveM(std::ostringstream& o) {
    (void)o;
    // Handled by config_esp.cpp, config_aim.cpp, config_vehicle.cpp, config_friends.cpp, config_aimtype.cpp
}

bool DeserializeFiveM(const std::string& key, const std::string& val) {
    (void)key; (void)val;
    // Handled by respective modules
    return false;
}

// ============================================================
// Color Array Helpers
// ============================================================
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
    int idx = atoi(idxStr.c_str());
    if (idx >= 0 && idx < 4) {
        value[idx] = (float)atof(val.c_str());
        return true;
    }
    return false;
}

} // namespace config_manager
