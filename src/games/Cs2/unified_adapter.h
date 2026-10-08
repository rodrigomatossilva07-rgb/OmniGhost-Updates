#pragma once

#include "gameplay/unified_config.h"
#include "gameplay/unified_optional_thread.h"
#include "gameplay/unified_esp.h"
#include "config/cs2_config.h"

namespace CS2 {

// Adapter that bridges CS2::Config with UnifiedConfig::CS2Config
class UnifiedConfigAdapter {
public:
    static OmniGhost::Gameplay::UnifiedConfig::CS2Config& Get() {
        static OmniGhost::Gameplay::UnifiedConfig::CS2Config unified;
        return unified;
    }
    
    // Sync from CS2::config to unified
    static void SyncToUnified(const CS2::Config& src) {
        auto& dst = Get();
        
        // Visuals
        dst.esp_enabled = src.esp_enabled;
        dst.box = src.box; dst.corner_box = src.box_corner; dst.box_fill = src.box_fill;
        dst.box_thickness = src.box_thickness; dst.box_rounding = src.box_rounding;
        dst.skeleton = src.skeleton; dst.skeleton_joints = src.skeleton_joints;
        dst.skeleton_thickness = src.skeleton_thickness;
        dst.head_dot = src.head_dot; dst.head_halo = src.head_halo;
        dst.head_circle_thickness = src.head_circle_thickness;
        dst.health_bar = src.health_bar; dst.armor_bar = src.armor_bar;
        dst.health_value = src.health_value; dst.armor_value = src.armor_value;
        dst.name = src.name; dst.distance = src.distance; dst.weapon_name = src.weapon_name; dst.weapon_ammo = src.weapon_ammo;
        dst.text_outline = src.text_outline; dst.text_outline_thickness = src.text_outline_thickness;
        dst.snaplines = src.snaplines; dst.snapline_position = src.snapline_position; dst.snapline_thickness = src.snapline_thickness;
        dst.look_direction = src.look_direction; dst.eye_line_thickness = src.eye_line_thickness; dst.look_direction_length = src.look_direction_length;
        dst.chinese_hat = src.chinese_hat; dst.angel_wings = src.angel_wings; dst.devil_horns = src.devil_horns; dst.floating_crown = src.floating_crown;
        dst.trails = src.trails; dst.chinese_hat_scale = src.chinese_hat_scale; dst.fun_effects_scale = src.fun_effects_scale;
        dst.trail_duration = src.trail_duration; dst.trail_thickness = src.trail_thickness;
        dst.rgb_mode = src.rgb_mode; dst.visibility_colors = src.visibility_colors; dst.visible_check = src.visible_check; dst.team_check = src.team_check;
        dst.show_bots = src.show_bots;
        dst.max_distance = src.max_distance;
        dst.bomb_timer = src.bomb_timer; dst.spectator_list = src.spectator_list; dst.radar_2d = src.radar_2d;
        dst.webradar_enabled = src.webradar_enabled; dst.webradar_cloudflare = src.webradar_cloudflare; dst.webradar_port = src.webradar_port;
        dst.telemetry_enabled = src.telemetry_enabled; dst.performance_mode = src.performance_mode; dst.hotkey_overlay = src.hotkey_overlay;
        dst.optional_lane_budget_ms = src.optional_lane_budget_ms;
        
        // Colors
        for (int i = 0; i < 4; ++i) {
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
            dst.col_distance[i] = src.col_distance[i];
            dst.col_weapon[i] = src.col_weapon[i];
            dst.col_snaplines[i] = src.col_snaplines[i];
            dst.col_trail[i] = src.col_trail[i];
            dst.col_fun_effects[i] = src.col_fun_effects[i];
            dst.col_look[i] = src.col_look[i];
            dst.col_text_outline[i] = src.col_text_outline[i];
            dst.col_visible[i] = src.col_visible[i];
            dst.col_occluded[i] = src.col_occluded[i];
            dst.col_team[i] = src.col_team[i];
            dst.col_target[i] = src.col_target[i];
        }
        
        // Misc
        dst.bomb_timer = src.bomb_timer; dst.spectator_list = src.spectator_list; dst.radar_2d = src.radar_2d;
        dst.webradar_enabled = src.webradar_enabled; dst.webradar_cloudflare = src.webradar_cloudflare; dst.webradar_port = src.webradar_port;
        dst.telemetry_enabled = src.telemetry_enabled; dst.performance_mode = src.performance_mode; dst.hotkey_overlay = src.hotkey_overlay;
        dst.optional_lane_budget_ms = src.optional_lane_budget_ms;
        
        // Aim (only needed for RequiredFields)
        dst.aim_enabled = src.aim_enabled; dst.trigger_enabled = src.trigger_enabled;
        dst.aim_visibility_check = src.aim_visibility_check; dst.trigger_head_only = src.trigger_head_only;
        dst.trigger_scoped_only = src.trigger_scoped_only; dst.aim_prediction = src.aim_prediction;
    }
    
    // Sync from unified to CS2::config
    static void SyncFromUnified(CS2::Config& dst) {
        const auto& src = Get();
        
        dst.esp_enabled = src.esp_enabled;
        dst.box = src.box; dst.corner_box = src.box_corner; dst.box_fill = src.box_fill;
        dst.box_thickness = src.box_thickness; dst.box_rounding = src.box_rounding;
        dst.skeleton = src.skeleton; dst.skeleton_joints = src.skeleton_joints;
        dst.skeleton_thickness = src.skeleton_thickness;
        dst.head_dot = src.head_dot; dst.head_halo = src.head_halo;
        dst.head_circle_thickness = src.head_circle_thickness;
        dst.health_bar = src.health_bar; dst.armor_bar = src.armor_bar;
        dst.health_value = src.health_value; dst.armor_value = src.armor_value;
        dst.name = src.name; dst.distance = src.distance; dst.weapon_name = src.weapon_name; dst.weapon_ammo = src.weapon_ammo;
        dst.text_outline = src.text_outline; dst.text_outline_thickness = src.text_outline_thickness;
        dst.snaplines = src.snaplines; dst.snapline_position = src.snapline_position; dst.snapline_thickness = src.snapline_thickness;
        dst.look_direction = src.look_direction; dst.eye_line_thickness = src.eye_line_thickness; dst.look_direction_length = src.look_direction_length;
        dst.chinese_hat = src.chinese_hat; dst.angel_wings = src.angel_wings; dst.devil_horns = src.devil_horns; dst.floating_crown = src.floating_crown;
        dst.trails = src.trails; dst.chinese_hat_scale = src.chinese_hat_scale; dst.fun_effects_scale = src.fun_effects_scale;
        dst.trail_duration = src.trail_duration; dst.trail_thickness = src.trail_thickness;
        dst.rgb_mode = src.rgb_mode; dst.visibility_colors = src.visibility_colors; dst.visible_check = src.visible_check; dst.team_check = src.team_check;
        dst.show_bots = src.show_bots;
        dst.max_distance = src.max_distance;
        dst.bomb_timer = src.bomb_timer; dst.spectator_list = src.spectator_list; dst.radar_2d = src.radar_2d;
        dst.webradar_enabled = src.webradar_enabled; dst.webradar_cloudflare = src.webradar_cloudflare; dst.webradar_port = src.webradar_port;
        dst.telemetry_enabled = src.telemetry_enabled; dst.performance_mode = src.performance_mode; dst.hotkey_overlay = src.hotkey_overlay;
        dst.optional_lane_budget_ms = src.optional_lane_budget_ms;
        
        for (int i = 0; i < 4; ++i) {
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
            dst.col_distance[i] = src.col_distance[i];
            dst.col_weapon[i] = src.col_weapon[i];
            dst.col_snaplines[i] = src.col_snaplines[i];
            dst.col_trail[i] = src.col_trail[i];
            dst.col_fun_effects[i] = src.col_fun_effects[i];
            dst.col_look[i] = src.col_look[i];
            dst.col_text_outline[i] = src.col_text_outline[i];
            dst.col_visible[i] = src.col_visible[i];
            dst.col_occluded[i] = src.col_occluded[i];
            dst.col_team[i] = src.col_team[i];
            dst.col_target[i] = src.col_target[i];
        }
        
        dst.bomb_timer = src.bomb_timer; dst.spectator_list = src.spectator_list; dst.radar_2d = src.radar_2d;
        dst.webradar_enabled = src.webradar_enabled; dst.webradar_cloudflare = src.webradar_cloudflare; dst.webradar_port = src.webradar_port;
        dst.telemetry_enabled = src.telemetry_enabled; dst.performance_mode = src.performance_mode; dst.hotkey_overlay = src.hotkey_overlay;
        dst.optional_lane_budget_ms = src.optional_lane_budget_ms;
        
        dst.aim_enabled = src.aim_enabled; dst.trigger_enabled = src.trigger_enabled;
        dst.aim_visibility_check = src.aim_visibility_check; dst.trigger_head_only = src.trigger_head_only;
        dst.trigger_scoped_only = src.trigger_scoped_only; dst.aim_prediction = src.aim_prediction;
    }
    
    // Get FeatureFlags for RequiredFields()
    static OmniGhost::Gameplay::UnifiedESP::FeatureFlags GetFeatureFlags() {
        return Get().GetFeatureFlags();
    }
};

// CS2 Optional Thread using unified base class
class CS2OptionalThread : public OmniGhost::Gameplay::UnifiedESP::OptionalThread {
protected:
    void DoWork(OmniGhost::Gameplay::UnifiedESP::OptionalJob&& job, 
                OmniGhost::Gameplay::UnifiedESP::OptionalResult& result) override {
        // Delegate to existing CS2 optional enrichment
        // This will be called from the unified thread
        // For now, we keep the existing CS2 optional logic in cs2_game.cpp
        // and just call it from here if needed
    }
};

} // namespace CS2