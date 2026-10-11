#pragma once

#include "gameplay/unified_config.h"
#include "gameplay/unified_esp.h"
#include "fortnite_game.h"

namespace Fortnite {

// Adapter for Fortnite::Config -> UnifiedConfig::FortniteConfig
class UnifiedConfigAdapter {
public:
    static OmniGhost::Gameplay::UnifiedConfig::FortniteConfig& Get() {
        static OmniGhost::Gameplay::UnifiedConfig::FortniteConfig unified;
        return unified;
    }
    
    static void SyncToUnified(const Fortnite::Config& src) {
        auto& dst = Get();
        
        dst.esp_enabled = src.esp_enabled;
        dst.self_esp = src.self_esp;
        dst.show_bots = src.show_bots;
        dst.visible_check = src.visible_check;
        dst.team_check = src.team_check;
        dst.box = src.box;
        dst.corner_box = src.corner_box;
        dst.box_fill = src.box_fill;
        dst.skeleton = src.skeleton;
        dst.skeleton_joints = src.skeleton_joints;
        dst.head_dot = src.head_dot;
        dst.head_halo = src.head_halo;
        dst.health_bar = src.health_bar;
        dst.armor_bar = src.armor_bar;
        dst.health_value = src.health_value;
        dst.armor_value = src.armor_value;
        dst.name = src.name;
        dst.distance = src.distance;
        dst.weapon_name = src.weapon_name;
        dst.weapon_ammo = src.weapon_ammo;
        dst.text_outline = src.text_outline;
        dst.text_outline_thickness = src.text_outline_thickness;
        dst.snaplines = src.snaplines;
        dst.snapline_position = src.snapline_position;
        dst.snapline_thickness = src.snapline_thickness;
        dst.look_direction = src.look_direction;
        dst.eye_line_thickness = src.eye_line_thickness;
        dst.look_direction_length = src.look_direction_length;
        dst.chinese_hat = src.chinese_hat;
        dst.angel_wings = src.angel_wings;
        dst.devil_horns = src.devil_horns;
        dst.floating_crown = src.floating_crown;
        dst.trails = src.trails;
        dst.hit_marker = src.hit_marker;
        dst.box_thickness = src.box_thickness;
        dst.box_rounding = src.box_rounding;
        dst.skeleton_thickness = src.skeleton_thickness;
        dst.head_circle_thickness = src.head_circle_thickness;
        dst.snapline_thickness = src.snapline_thickness;
        dst.eye_line_thickness = src.eye_line_thickness;
        dst.max_distance = src.max_distance;
        dst.rgb_mode = src.rgb_mode;
        dst.visibility_colors = src.visibility_colors;
        
        for (int i = 0; i < 4; ++i) {
            dst.col_enemy[i] = src.col_enemy[i];
            dst.col_team[i] = src.col_team[i];
            dst.col_box[i] = src.col_box[i];
            dst.col_box_corner[i] = src.col_box_corner[i];
            dst.col_box_fill[i] = src.col_box_fill[i];
            dst.col_skeleton[i] = src.col_skeleton[i];
            dst.col_joints[i] = src.col_joints[i];
            dst.col_head[i] = src.col_head[i];
            dst.col_halo[i] = src.col_halo[i];
            dst.col_health[i] = src.col_health[i];
            dst.col_armor[i] = src.col_armor[i];
            dst.col_name[i] = src.col_name[i];
            dst.col_local[i] = src.col_local[i];
            dst.col_distance[i] = src.col_distance[i];
            dst.col_weapon[i] = src.col_weapon[i];
            dst.col_snaplines[i] = src.col_snaplines[i];
            dst.col_trail[i] = src.col_trail[i];
            dst.col_fun_effects[i] = src.col_fun_effects[i];
            dst.col_look[i] = src.col_look[i];
            dst.col_text_outline[i] = src.col_text_outline[i];
            dst.col_visible[i] = src.col_visible[i];
            dst.col_occluded[i] = src.col_occluded[i];
        }
    }
    
    static void SyncFromUnified(Fortnite::Config& dst) {
        const auto& src = Get();
        
        dst.esp_enabled = src.esp_enabled;
        dst.self_esp = src.self_esp;
        dst.show_bots = src.show_bots;
        dst.visible_check = src.visible_check;
        dst.team_check = src.team_check;
        dst.box = src.box;
        dst.corner_box = src.corner_box;
        dst.box_fill = src.box_fill;
        dst.skeleton = src.skeleton;
        dst.skeleton_joints = src.skeleton_joints;
        dst.head_dot = src.head_dot;
        dst.head_halo = src.head_halo;
        dst.health_bar = src.health_bar;
        dst.armor_bar = src.armor_bar;
        dst.health_value = src.health_value;
        dst.armor_value = src.armor_value;
        dst.name = src.name;
        dst.distance = src.distance;
        dst.weapon_name = src.weapon_name;
        dst.weapon_ammo = src.weapon_ammo;
        dst.text_outline = src.text_outline;
        dst.text_outline_thickness = src.text_outline_thickness;
        dst.snaplines = src.snaplines;
        dst.snapline_position = src.snapline_position;
        dst.snapline_thickness = src.snapline_thickness;
        dst.look_direction = src.look_direction;
        dst.eye_line_thickness = src.eye_line_thickness;
        dst.look_direction_length = src.look_direction_length;
        dst.chinese_hat = src.chinese_hat;
        dst.angel_wings = src.angel_wings;
        dst.devil_horns = src.devil_horns;
        dst.floating_crown = src.floating_crown;
        dst.trails = src.trails;
        dst.hit_marker = src.hit_marker;
        dst.box_thickness = src.box_thickness;
        dst.box_rounding = src.box_rounding;
        dst.skeleton_thickness = src.skeleton_thickness;
        dst.head_circle_thickness = src.head_circle_thickness;
        dst.snapline_thickness = src.snapline_thickness;
        dst.eye_line_thickness = src.eye_line_thickness;
        dst.max_distance = src.max_distance;
        dst.rgb_mode = src.rgb_mode;
        dst.visibility_colors = src.visibility_colors;
        
        for (int i = 0; i < 4; ++i) {
            dst.col_enemy[i] = src.col_enemy[i];
            dst.col_team[i] = src.col_team[i];
            dst.col_box[i] = src.col_box[i];
            dst.col_box_corner[i] = src.col_box_corner[i];
            dst.col_box_fill[i] = src.col_box_fill[i];
            dst.col_skeleton[i] = src.col_skeleton[i];
            dst.col_joints[i] = src.col_joints[i];
            dst.col_head[i] = src.col_head[i];
            dst.col_halo[i] = src.col_halo[i];
            dst.col_health[i] = src.col_health[i];
            dst.col_armor[i] = src.col_armor[i];
            dst.col_name[i] = src.col_name[i];
            dst.col_local[i] = src.col_local[i];
            dst.col_distance[i] = src.col_distance[i];
            dst.col_weapon[i] = src.col_weapon[i];
            dst.col_snaplines[i] = src.col_snaplines[i];
            dst.col_trail[i] = src.col_trail[i];
            dst.col_fun_effects[i] = src.col_fun_effects[i];
            dst.col_look[i] = src.col_look[i];
            dst.col_text_outline[i] = src.col_text_outline[i];
            dst.col_visible[i] = src.col_visible[i];
            dst.col_occluded[i] = src.col_occluded[i];
        }
    }
    
    static const OmniGhost::Gameplay::UnifiedESP::FeatureFlags& GetFeatureFlags() {
        return Get().GetFeatureFlags();
    }
};

} // namespace Fortnite