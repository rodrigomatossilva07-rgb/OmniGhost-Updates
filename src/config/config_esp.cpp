#include "config_esp.h"
#include "config_core.h"
#include "../src/games/Fivem/esp/esp.h"
#include <algorithm>

namespace config_manager {

void SerializeESP(std::ostringstream& o) {
    auto& e = esp::config;
    WBool(o, "esp.enabled", e.enabled);
    WBool(o, "esp.self_esp", e.self_esp);
    WBool(o, "esp.visibility_colors", e.visibility_colors);
    WBool(o, "esp.visible_check", e.visible_check);
    WBool(o, "esp.show_dead", e.show_dead);
    WBool(o, "esp.show_knocked", e.show_knocked);
    WBool(o, "esp.team_check", e.team_check);
    WBool(o, "esp.skeleton", e.skeleton);
    WBool(o, "esp.joints", e.joints);
    WBool(o, "esp.head_circle", e.head_circle);
    WBool(o, "esp.health_bar", e.health_bar);
    WBool(o, "esp.armor_bar", e.armor_bar);
    WBool(o, "esp.weapon_name", e.weapon_name);
    WBool(o, "esp.box_2d", e.box_2d);
    WBool(o, "esp.filled_box", e.filled_box);
    WBool(o, "esp.corner_box", e.corner_box);
    WBool(o, "esp.snaplines", e.snaplines);
    WBool(o, "esp.distance", e.distance);
    WBool(o, "esp.npc_esp", e.npc_esp);
    WBool(o, "esp.trails", e.trails);
    WBool(o, "esp.head_halo", e.head_halo);
    WBool(o, "esp.look_direction", e.look_direction);
    WBool(o, "esp.chinese_hat", e.chinese_hat);
    WFloat(o, "esp.chinese_hat_scale", e.chinese_hat_scale);
    WBool(o, "esp.angel_wings", e.angel_wings);
    WBool(o, "esp.devil_horns", e.devil_horns);
    WBool(o, "esp.floating_crown", e.floating_crown);
    WBool(o, "esp.hit_marker", e.hit_marker);
    WBool(o, "esp.fun_effects_rainbow", e.fun_effects_rainbow);
    WFloat(o, "esp.fun_effects_scale", e.fun_effects_scale);
    WFloat(o, "esp.trail_duration", e.trail_duration);
    WFloat(o, "esp.trail_thickness", e.trail_thickness);
    WFloat(o, "esp.look_direction_length", e.look_direction_length);


    WFloat(o, "esp.box_thickness", e.box_thickness);
    WFloat(o, "esp.skeleton_thickness", e.skeleton_thickness);
    WFloat(o, "esp.head_circle_thickness", e.head_circle_thickness);
    WFloat(o, "esp.snapline_thickness", e.snapline_thickness);
    WFloat(o, "esp.eye_line_thickness", e.eye_line_thickness);
    WBool(o, "esp.rgb_mode", e.rgb_mode);
    WBool(o, "esp.radar_enabled", e.radar_enabled);
    WFloat(o, "esp.radar_range", e.radar_range);
    WFloat(o, "esp.radar_size", e.radar_size);
    WFloat(o, "esp.radar_pos_x", e.radar_pos_x);
    WFloat(o, "esp.radar_pos_y", e.radar_pos_y);
    WBool(o, "esp.web_radar_enabled", e.web_radar_enabled);
    WBool(o, "esp.web_radar_cloudflare", e.web_radar_cloudflare);
    WBool(o, "esp.web_radar_demo", e.web_radar_demo);
    WInt(o, "esp.web_radar_port", e.web_radar_port);
    WBool(o, "esp.skeleton_lod", e.skeleton_lod);
    WBool(o, "esp.triangle_radar", e.triangle_radar);
    WBool(o, "esp.blip_esp", e.blip_esp);
    WBool(o, "esp.waypoint_line", e.waypoint_line);
    WBool(o, "esp.square_radar", e.square_radar);
    WFloat(o, "esp.triangle_radar_radius", e.triangle_radar_radius);
    WBool(o, "esp.offscreen_arrows", e.offscreen_arrows);
    WFloat(o, "esp.offscreen_arrow_size", e.offscreen_arrow_size);
    WFloat(o, "esp.offscreen_arrow_margin", e.offscreen_arrow_margin);
    WFloat(o, "esp.offscreen_arrow_max_distance", e.offscreen_arrow_max_distance);
    WFloat(o, "esp.offscreen_arrow_opacity", e.offscreen_arrow_opacity);
    WBool(o, "esp.offscreen_arrow_distance", e.offscreen_arrow_distance);
    WBool(o, "esp.offscreen_arrow_name", e.offscreen_arrow_name);
    WInt(o, "esp.offscreen_arrow_style", e.offscreen_arrow_style);
    WU32(o, "esp.color_offscreen_arrow", e.color_offscreen_arrow);
    WBool(o, "esp.active_features_panel", e.active_features_panel);
    WFloat(o, "esp.active_panel_x", e.active_panel_x);
    WFloat(o, "esp.active_panel_y", e.active_panel_y);
    WFloat(o, "esp.active_panel_scale", e.active_panel_scale);
    WFloat(o, "esp.active_panel_opacity", e.active_panel_opacity);
    WBool(o, "esp.active_panel_compact", e.active_panel_compact);
    WBool(o, "esp.active_panel_show_esp", e.active_panel_show_esp);
    WBool(o, "esp.active_panel_show_aim", e.active_panel_show_aim);
    WBool(o, "esp.active_panel_show_trigger", e.active_panel_show_trigger);
    WBool(o, "esp.active_panel_show_radar", e.active_panel_show_radar);
    WInt(o, "esp.layout_name", (int)e.layout_name);
    WInt(o, "esp.layout_health", (int)e.layout_health);
    WInt(o, "esp.layout_armor", (int)e.layout_armor);
    WInt(o, "esp.layout_weapon", (int)e.layout_weapon);
    WInt(o, "esp.layout_distance", (int)e.layout_distance);
    WFloat(o, "esp.layout_name_x", e.layout_name_x);
    WFloat(o, "esp.layout_name_y", e.layout_name_y);
    WFloat(o, "esp.layout_health_x", e.layout_health_x);
    WFloat(o, "esp.layout_health_y", e.layout_health_y);
    WFloat(o, "esp.layout_armor_x", e.layout_armor_x);
    WFloat(o, "esp.layout_armor_y", e.layout_armor_y);
    WFloat(o, "esp.layout_weapon_x", e.layout_weapon_x);
    WFloat(o, "esp.layout_weapon_y", e.layout_weapon_y);
    WFloat(o, "esp.layout_distance_x", e.layout_distance_x);
    WFloat(o, "esp.layout_distance_y", e.layout_distance_y);
    WFloat(o, "esp.layout_spacing", e.layout_spacing);
    WFloat(o, "esp.layout_text_scale", e.layout_text_scale);
    WBool(o, "esp.layout_snap", e.layout_snap);
    WBool(o, "esp.player_name", e.player_name);
    WBool(o, "esp.player_id", e.player_id);
    WFloat(o, "esp.max_esp_distance", e.max_esp_distance);
    WInt(o, "esp.snapline_pos", e.snapline_pos);
    WInt(o, "esp.circle_type", e.circle_type);
    WU32(o, "esp.color_visible", e.color_visible);
    WU32(o, "esp.color_invisible", e.color_invisible);
    WU32(o, "esp.color_dead", e.color_dead);
    WU32(o, "esp.color_knocked", e.color_knocked);
    WU32(o, "esp.color_team", e.color_team);
    WU32(o, "esp.color_skeleton", e.color_skeleton);
    WU32(o, "esp.color_skeleton_points", e.color_skeleton_points);
    WU32(o, "esp.color_head_circle", e.color_head_circle);
    WU32(o, "esp.color_health", e.color_health);
    WU32(o, "esp.color_armor", e.color_armor);
    WU32(o, "esp.color_weapon", e.color_weapon);
    WU32(o, "esp.color_box_2d", e.color_box_2d);
    WU32(o, "esp.color_corner_box", e.color_corner_box);
    WU32(o, "esp.color_snaplines", e.color_snaplines);
    WU32(o, "esp.color_name", e.color_name);
    WU32(o, "esp.color_id", e.color_id);
    WU32(o, "esp.color_distance", e.color_distance);
    WU32(o, "esp.color_npc", e.color_npc);
    WU32(o, "esp.color_trail", e.color_trail);
    WU32(o, "esp.color_halo", e.color_halo);
    WU32(o, "esp.color_fun_effects", e.color_fun_effects);
    WU32(o, "esp.color_look_direction", e.color_look_direction);
}

bool DeserializeESP(const std::string& key, const std::string& val) {
    if (key.rfind("esp.", 0) != 0) return false;
    auto& e = esp::config;
    auto bi = [&](const char* k) { return key == k; };
    auto toB = [&]() -> bool { return config_manager::toB(val); };
    auto toF = [&]() -> float { return config_manager::toF(val); };
    auto toI = [&]() -> int { return config_manager::toI(val); };
    auto toU = [&]() -> ImU32 { return config_manager::toU(val); };

    if (bi("esp.enabled")) { e.enabled = toB(); return true; }
    if (bi("esp.self_esp")) { e.self_esp = toB(); return true; }
    if (bi("esp.visibility_colors")) { e.visibility_colors = toB(); return true; }
    if (bi("esp.visible_check")) { e.visible_check = toB(); return true; }
    if (bi("esp.show_dead")) { e.show_dead = toB(); return true; }
    if (bi("esp.show_knocked")) { e.show_knocked = toB(); return true; }
    if (bi("esp.team_check")) { e.team_check = toB(); return true; }
    if (bi("esp.skeleton")) { e.skeleton = toB(); return true; }
    if (bi("esp.joints")) { e.joints = toB(); return true; }
    if (bi("esp.head_circle")) { e.head_circle = toB(); return true; }
    if (bi("esp.health_bar")) { e.health_bar = toB(); return true; }
    if (bi("esp.armor_bar")) { e.armor_bar = toB(); return true; }
    if (bi("esp.weapon_name")) { e.weapon_name = toB(); return true; }
    if (bi("esp.box_2d")) { e.box_2d = toB(); return true; }
    if (bi("esp.filled_box")) { e.filled_box = toB(); return true; }
    if (bi("esp.corner_box")) { e.corner_box = toB(); return true; }
    if (bi("esp.snaplines")) { e.snaplines = toB(); return true; }
    if (bi("esp.distance")) { e.distance = toB(); return true; }
    if (bi("esp.npc_esp")) { e.npc_esp = toB(); return true; }
    if (bi("esp.trails")) { e.trails = toB(); return true; }
    if (bi("esp.head_halo")) { e.head_halo = toB(); return true; }
    if (bi("esp.look_direction")) { e.look_direction = toB(); return true; }
    if (bi("esp.chinese_hat")) { e.chinese_hat = toB(); return true; }
    if (bi("esp.chinese_hat_scale")) { e.chinese_hat_scale = toF(); return true; }
    if (bi("esp.angel_wings")) { e.angel_wings = toB(); return true; }
    if (bi("esp.devil_horns")) { e.devil_horns = toB(); return true; }
    if (bi("esp.floating_crown")) { e.floating_crown = toB(); return true; }
    if (bi("esp.hit_marker")) { e.hit_marker = toB(); return true; }
    if (bi("esp.fun_effects_rainbow")) { e.fun_effects_rainbow = toB(); return true; }
    if (bi("esp.fun_effects_scale")) { e.fun_effects_scale = toF(); return true; }
    if (bi("esp.trail_duration")) { e.trail_duration = toF(); return true; }
    if (bi("esp.trail_thickness")) { e.trail_thickness = toF(); return true; }
    if (bi("esp.look_direction_length")) { e.look_direction_length = toF(); return true; }
    if (bi("esp.rgb_mode")) { e.rgb_mode = toB(); return true; }
    if (bi("esp.radar_enabled")) { e.radar_enabled = toB(); return true; }
    if (bi("esp.radar_range")) { e.radar_range = toF(); return true; }
    if (bi("esp.radar_size")) { e.radar_size = toF(); return true; }
    if (bi("esp.radar_pos_x")) { e.radar_pos_x = toF(); return true; }
    if (bi("esp.radar_pos_y")) { e.radar_pos_y = toF(); return true; }
    if (bi("esp.web_radar_enabled")) { e.web_radar_enabled = toB(); return true; }
    if (bi("esp.web_radar_cloudflare")) { e.web_radar_cloudflare = toB(); return true; }
    if (bi("esp.web_radar_demo")) { e.web_radar_demo = toB(); return true; }
    if (bi("esp.web_radar_port")) { e.web_radar_port = std::clamp(toI(), 1024, 65535); return true; }
    if (bi("esp.skeleton_lod")) { e.skeleton_lod = toB(); return true; }
    if (bi("esp.triangle_radar")) { e.triangle_radar = toB(); return true; }
    if (bi("esp.blip_esp")) { e.blip_esp = toB(); return true; }
    if (bi("esp.waypoint_line")) { e.waypoint_line = toB(); return true; }
    if (bi("esp.square_radar")) { e.square_radar = toB(); return true; }
    if (bi("esp.triangle_radar_radius")) { e.triangle_radar_radius = toF(); return true; }
    if (bi("esp.offscreen_arrows")) { e.offscreen_arrows = toB(); return true; }
    if (bi("esp.offscreen_arrow_size")) { e.offscreen_arrow_size = toF(); return true; }
    if (bi("esp.offscreen_arrow_margin")) { e.offscreen_arrow_margin = toF(); return true; }
    if (bi("esp.offscreen_arrow_max_distance")) { e.offscreen_arrow_max_distance = toF(); return true; }
    if (bi("esp.offscreen_arrow_opacity")) { e.offscreen_arrow_opacity = toF(); return true; }
    if (bi("esp.offscreen_arrow_distance")) { e.offscreen_arrow_distance = toB(); return true; }
    if (bi("esp.offscreen_arrow_name")) { e.offscreen_arrow_name = toB(); return true; }
    if (bi("esp.offscreen_arrow_style")) { e.offscreen_arrow_style = std::clamp(toI(), 0, 2); return true; }
    if (bi("esp.color_offscreen_arrow")) { e.color_offscreen_arrow = toU(); return true; }
    if (bi("esp.active_features_panel")) { e.active_features_panel = toB(); return true; }
    if (bi("esp.active_panel_x")) { e.active_panel_x = toF(); return true; }
    if (bi("esp.active_panel_y")) { e.active_panel_y = toF(); return true; }
    if (bi("esp.active_panel_scale")) { e.active_panel_scale = toF(); return true; }
    if (bi("esp.active_panel_opacity")) { e.active_panel_opacity = toF(); return true; }
    if (bi("esp.active_panel_compact")) { e.active_panel_compact = toB(); return true; }
    if (bi("esp.active_panel_show_esp")) { e.active_panel_show_esp = toB(); return true; }
    if (bi("esp.active_panel_show_aim")) { e.active_panel_show_aim = toB(); return true; }
    if (bi("esp.active_panel_show_trigger")) { e.active_panel_show_trigger = toB(); return true; }
    if (bi("esp.active_panel_show_radar")) { e.active_panel_show_radar = toB(); return true; }
    if (bi("esp.layout_name")) { e.layout_name = (esp::LayoutAnchor)std::clamp(toI(), 0, 3); return true; }
    if (bi("esp.layout_health")) { e.layout_health = (esp::LayoutAnchor)std::clamp(toI(), 0, 3); return true; }
    if (bi("esp.layout_armor")) { e.layout_armor = (esp::LayoutAnchor)std::clamp(toI(), 0, 3); return true; }
    if (bi("esp.layout_weapon")) { e.layout_weapon = (esp::LayoutAnchor)std::clamp(toI(), 0, 3); return true; }
    if (bi("esp.layout_distance")) { e.layout_distance = (esp::LayoutAnchor)std::clamp(toI(), 0, 3); return true; }
    if (bi("esp.layout_name_x")) { e.layout_name_x = std::clamp(toF(), -.20f, 1.20f); return true; }
    if (bi("esp.layout_name_y")) { e.layout_name_y = std::clamp(toF(), -.20f, 1.20f); return true; }
    if (bi("esp.layout_health_x")) { e.layout_health_x = std::clamp(toF(), -.20f, 1.20f); return true; }
    if (bi("esp.layout_health_y")) { e.layout_health_y = std::clamp(toF(), -.20f, 1.20f); return true; }
    if (bi("esp.layout_armor_x")) { e.layout_armor_x = std::clamp(toF(), -.20f, 1.20f); return true; }
    if (bi("esp.layout_armor_y")) { e.layout_armor_y = std::clamp(toF(), -.20f, 1.20f); return true; }
    if (bi("esp.layout_weapon_x")) { e.layout_weapon_x = std::clamp(toF(), -.20f, 1.20f); return true; }
    if (bi("esp.layout_weapon_y")) { e.layout_weapon_y = std::clamp(toF(), -.20f, 1.20f); return true; }
    if (bi("esp.layout_distance_x")) { e.layout_distance_x = std::clamp(toF(), -.20f, 1.20f); return true; }
    if (bi("esp.layout_distance_y")) { e.layout_distance_y = std::clamp(toF(), -.20f, 1.20f); return true; }
    if (bi("esp.layout_spacing")) { e.layout_spacing = toF(); return true; }
    if (bi("esp.layout_text_scale")) { e.layout_text_scale = toF(); return true; }
    if (bi("esp.layout_snap")) { e.layout_snap = toB(); return true; }
    if (bi("esp.player_name")) { e.player_name = toB(); return true; }
    if (bi("esp.player_id")) { e.player_id = toB(); return true; }
    if (bi("esp.max_esp_distance")) { e.max_esp_distance = toF(); return true; }
    if (bi("esp.snapline_pos")) { e.snapline_pos = toI(); return true; }
    if (bi("esp.circle_type")) { e.circle_type = toI(); return true; }

    if (bi("esp.box_thickness")) { e.box_thickness = toF(); return true; }
    if (bi("esp.skeleton_thickness")) { e.skeleton_thickness = toF(); return true; }
    if (bi("esp.head_circle_thickness")) { e.head_circle_thickness = toF(); return true; }
    if (bi("esp.snapline_thickness")) { e.snapline_thickness = toF(); return true; }
    if (bi("esp.eye_line_thickness")) { e.eye_line_thickness = toF(); return true; }
    if (bi("esp.color_visible")) { e.color_visible = toU(); return true; }
    if (bi("esp.color_invisible")) { e.color_invisible = toU(); return true; }
    if (bi("esp.color_dead")) { e.color_dead = toU(); return true; }
    if (bi("esp.color_knocked")) { e.color_knocked = toU(); return true; }
    if (bi("esp.color_team")) { e.color_team = toU(); return true; }
    if (bi("esp.color_skeleton")) { e.color_skeleton = toU(); return true; }
    if (bi("esp.color_skeleton_points")) { e.color_skeleton_points = toU(); return true; }
    if (bi("esp.color_head_circle")) { e.color_head_circle = toU(); return true; }
    if (bi("esp.color_health")) { e.color_health = toU(); return true; }
    if (bi("esp.color_armor")) { e.color_armor = toU(); return true; }
    if (bi("esp.color_weapon")) { e.color_weapon = toU(); return true; }
    if (bi("esp.color_box_2d")) { e.color_box_2d = toU(); return true; }
    if (bi("esp.color_corner_box")) { e.color_corner_box = toU(); return true; }
    if (bi("esp.color_snaplines")) { e.color_snaplines = toU(); return true; }
    if (bi("esp.color_name")) { e.color_name = toU(); return true; }
    if (bi("esp.color_id")) { e.color_id = toU(); return true; }
    if (bi("esp.color_distance")) { e.color_distance = toU(); return true; }
    if (bi("esp.color_npc")) { e.color_npc = toU(); return true; }
    if (bi("esp.color_trail")) { e.color_trail = toU(); return true; }
    if (bi("esp.color_halo")) { e.color_halo = toU(); return true; }
    if (bi("esp.color_fun_effects")) { e.color_fun_effects = toU(); return true; }
    if (bi("esp.color_look_direction")) { e.color_look_direction = toU(); return true; }
    return false;
}

}
