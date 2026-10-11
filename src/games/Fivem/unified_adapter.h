#pragma once

#include "gameplay/unified_config.h"
#include "gameplay/unified_optional_thread.h"
#include "gameplay/unified_esp.h"
#include "../../../games/Fivem/esp/esp.h"

namespace FiveM {

// Adapter for FiveM esp::Config -> UnifiedConfig::FiveMConfig
class UnifiedConfigAdapter {
public:
    static OmniGhost::Gameplay::UnifiedConfig::FiveMConfig& Get() {
        static OmniGhost::Gameplay::UnifiedConfig::FiveMConfig unified;
        return unified;
    }
    
    static void SyncToUnified(const esp::Config& src) {
        auto& dst = Get();
        
        dst.enabled = src.enabled;
        dst.self_esp = src.self_esp;
        dst.visible_check = src.visible_check;
        dst.visibility_colors = src.visibility_colors;
        dst.team_check = src.team_check;
        dst.show_dead = src.show_dead;
        dst.show_knocked = src.show_knocked;
        dst.npc_esp = src.npc_esp;
        dst.box_2d = src.box_2d;
        dst.corner_box = src.corner_box;
        dst.filled_box = src.filled_box;
        dst.skeleton = src.skeleton;
        dst.joints = src.joints;
        dst.head_circle = src.head_circle;
        dst.health_bar = src.health_bar;
        dst.armor_bar = src.armor_bar;
        dst.weapon_name = src.weapon_name;
        dst.player_name = src.player_name;
        dst.player_id = src.player_id;
        dst.distance = src.distance;
        dst.snaplines = src.snaplines;
        dst.head_halo = src.head_halo;
        dst.chinese_hat = src.chinese_hat;
        dst.angel_wings = src.angel_wings;
        dst.devil_horns = src.devil_horns;
        dst.floating_crown = src.floating_crown;
        dst.trails = src.trails;
        dst.look_direction = src.look_direction;
        dst.hit_marker = src.hit_marker;
        dst.fun_effects_rainbow = src.fun_effects_rainbow;
        dst.box_thickness = src.box_thickness;
        dst.skeleton_thickness = src.skeleton_thickness;
        dst.head_circle_thickness = src.head_circle_thickness;
        dst.snapline_thickness = src.snapline_thickness;
        dst.eye_line_thickness = src.eye_line_thickness;
        dst.max_esp_distance = src.max_esp_distance;
        dst.chinese_hat_scale = src.chinese_hat_scale;
        dst.fun_effects_scale = src.fun_effects_scale;
        dst.trail_duration = src.trail_duration;
        dst.trail_thickness = src.trail_thickness;
        dst.look_direction_length = src.look_direction_length;
        dst.snapline_pos = src.snapline_pos;
        dst.circle_type = src.circle_type;
        dst.rgb_mode = src.rgb_mode;
        dst.radar_enabled = src.radar_enabled;
        dst.triangle_radar = src.triangle_radar;
        dst.blip_esp = src.blip_esp;
        dst.waypoint_line = src.waypoint_line;
        dst.square_radar = src.square_radar;
        dst.triangle_radar_radius = src.triangle_radar_radius;
        dst.aimbot_enabled = src.aimbot_enabled;
        dst.trigger_enabled = src.trigger_enabled;
        dst.visible_check_aim = src.visible_check;
        
        // Colors - convert ImU32 to float[4]
        auto u32ToFloat = [](ImU32 c, float out[4]) {
            out[0] = ((c >> 0) & 0xFF) / 255.f;
            out[1] = ((c >> 8) & 0xFF) / 255.f;
            out[2] = ((c >> 16) & 0xFF) / 255.f;
            out[3] = ((c >> 24) & 0xFF) / 255.f;
        };
        
        u32ToFloat(src.color_visible, dst.color_visible);
        u32ToFloat(src.color_invisible, dst.color_invisible);
        u32ToFloat(src.color_dead, dst.color_dead);
        u32ToFloat(src.color_knocked, dst.color_knocked);
        u32ToFloat(src.color_team, dst.color_team);
        u32ToFloat(src.color_skeleton, dst.color_skeleton);
        u32ToFloat(src.color_skeleton_points, dst.color_skeleton_points);
        u32ToFloat(src.color_head_circle, dst.color_head_circle);
        u32ToFloat(src.color_health, dst.color_health);
        u32ToFloat(src.color_armor, dst.color_armor);
        u32ToFloat(src.color_name, dst.color_name);
        u32ToFloat(src.color_distance, dst.color_distance);
        u32ToFloat(src.color_weapon, dst.color_weapon);
        u32ToFloat(src.color_snaplines, dst.color_snaplines);
        u32ToFloat(src.color_trail, dst.color_trail);
        u32ToFloat(src.color_fun_effects, dst.color_fun_effects);
        u32ToFloat(src.color_look_direction, dst.color_look_direction);
        u32ToFloat(src.color_halo, dst.color_halo);
        u32ToFloat(src.color_box_2d, dst.color_box_2d);
        u32ToFloat(src.color_corner_box, dst.color_corner_box);
        u32ToFloat(src.color_id, dst.color_id);
        u32ToFloat(src.color_npc, dst.color_npc);
    }
    
    static void SyncFromUnified(esp::Config& dst) {
        const auto& src = Get();
        
        dst.enabled = src.enabled;
        dst.self_esp = src.self_esp;
        dst.visible_check = src.visible_check;
        dst.visibility_colors = src.visibility_colors;
        dst.team_check = src.team_check;
        dst.show_dead = src.show_dead;
        dst.show_knocked = src.show_knocked;
        dst.npc_esp = src.npc_esp;
        dst.box_2d = src.box_2d;
        dst.corner_box = src.corner_box;
        dst.filled_box = src.filled_box;
        dst.skeleton = src.skeleton;
        dst.joints = src.joints;
        dst.head_circle = src.head_circle;
        dst.health_bar = src.health_bar;
        dst.armor_bar = src.armor_bar;
        dst.weapon_name = src.weapon_name;
        dst.player_name = src.player_name;
        dst.player_id = src.player_id;
        dst.distance = src.distance;
        dst.snaplines = src.snaplines;
        dst.head_halo = src.head_halo;
        dst.chinese_hat = src.chinese_hat;
        dst.angel_wings = src.angel_wings;
        dst.devil_horns = src.devil_horns;
        dst.floating_crown = src.floating_crown;
        dst.trails = src.trails;
        dst.look_direction = src.look_direction;
        dst.hit_marker = src.hit_marker;
        dst.fun_effects_rainbow = src.fun_effects_rainbow;
        dst.box_thickness = src.box_thickness;
        dst.skeleton_thickness = src.skeleton_thickness;
        dst.head_circle_thickness = src.head_circle_thickness;
        dst.snapline_thickness = src.snapline_thickness;
        dst.eye_line_thickness = src.eye_line_thickness;
        dst.max_esp_distance = src.max_esp_distance;
        dst.chinese_hat_scale = src.chinese_hat_scale;
        dst.fun_effects_scale = src.fun_effects_scale;
        dst.trail_duration = src.trail_duration;
        dst.trail_thickness = src.trail_thickness;
        dst.look_direction_length = src.look_direction_length;
        dst.snapline_pos = src.snapline_pos;
        dst.circle_type = src.circle_type;
        dst.rgb_mode = src.rgb_mode;
        dst.radar_enabled = src.radar_enabled;
        dst.triangle_radar = src.triangle_radar;
        dst.blip_esp = src.blip_esp;
        dst.waypoint_line = src.waypoint_line;
        dst.square_radar = src.square_radar;
        dst.triangle_radar_radius = src.triangle_radar_radius;
        dst.aimbot_enabled = src.aimbot_enabled;
        dst.trigger_enabled = src.trigger_enabled;
        dst.visible_check = src.visible_check_aim;
        
        auto floatToU32 = [](const float c[4]) -> ImU32 {
            return IM_COL32(
                (int)(c[0] * 255), (int)(c[1] * 255), 
                (int)(c[2] * 255), (int)(c[3] * 255)
            );
        };
        
        dst.color_visible = floatToU32(src.color_visible);
        dst.color_invisible = floatToU32(src.color_invisible);
        dst.color_dead = floatToU32(src.color_dead);
        dst.color_knocked = floatToU32(src.color_knocked);
        dst.color_team = floatToU32(src.color_team);
        dst.color_skeleton = floatToU32(src.color_skeleton);
        dst.color_skeleton_points = floatToU32(src.color_skeleton_points);
        dst.color_head_circle = floatToU32(src.color_head_circle);
        dst.color_health = floatToU32(src.color_health);
        dst.color_armor = floatToU32(src.color_armor);
        dst.color_name = floatToU32(src.color_name);
        dst.color_distance = floatToU32(src.color_distance);
        dst.color_weapon = floatToU32(src.color_weapon);
        dst.color_snaplines = floatToU32(src.color_snaplines);
        dst.color_trail = floatToU32(src.color_trail);
        dst.color_fun_effects = floatToU32(src.color_fun_effects);
        dst.color_look_direction = floatToU32(src.color_look_direction);
        dst.color_halo = floatToU32(src.color_halo);
        dst.color_box_2d = floatToU32(src.color_box_2d);
        dst.color_corner_box = floatToU32(src.color_corner_box);
        dst.color_id = floatToU32(src.color_id);
        dst.color_npc = floatToU32(src.color_npc);
    }
    
    // Returns by value: the unified config's GetFeatureFlags() materializes the
    // flags, so a const& would dangle (C4172).
    static OmniGhost::Gameplay::UnifiedESP::FeatureFlags GetFeatureFlags() {
        return Get().GetFeatureFlags();
    }
};

} // namespace FiveM