#pragma once
#include "gameplay/unified_config.h"
#include "gameplay/unified_esp.h"
#include <third_party/ImGui/imgui.h>

namespace OmniGhost::Gameplay {

// ============================================================
// Generic Config Adapter Template
// Eliminates code duplication across games
// ============================================================

template <typename GameConfig, typename UnifiedGameConfig>
class UnifiedConfigAdapterBase {
public:
    static UnifiedGameConfig& GetUnified() {
        static UnifiedGameConfig unified;
        return unified;
    }

    // Override these in derived classes for custom mappings
    static void MapToUnified(const GameConfig& src, UnifiedGameConfig& dst);
    static void MapFromUnified(const UnifiedGameConfig& src, GameConfig& dst);

    static void SyncToUnified(const GameConfig& src) {
        MapToUnified(src, GetUnified());
    }

    static void SyncFromUnified(GameConfig& dst) {
        MapFromUnified(GetUnified(), dst);
    }

    static FeatureFlags GetFeatureFlags() {
        return GetUnified().GetFeatureFlags();
    }

protected:
    // Color conversion helpers
    static void U32ToFloat(ImU32 c, float out[4]) {
        out[0] = ((c >> 0) & 0xFF) / 255.f;
        out[1] = ((c >> 8) & 0xFF) / 255.f;
        out[2] = ((c >> 16) & 0xFF) / 255.f;
        out[3] = ((c >> 24) & 0xFF) / 255.f;
    }

    static ImU32 FloatToU32(const float c[4]) {
        return IM_COL32(
            int(c[0] * 255), int(c[1] * 255),
            int(c[2] * 255), int(c[3] * 255)
        );
    }
};

// ============================================================
// FiveM Adapter
// ============================================================
#include "esp/esp.h"

class FiveMUnifiedAdapter : public UnifiedConfigAdapterBase<esp::Config, UnifiedConfig::FiveMConfig> {
public:
    static void MapToUnified(const esp::Config& src, UnifiedConfig::FiveMConfig& dst) {
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

        // Colors
        U32ToFloat(src.color_visible, dst.color_visible);
        U32ToFloat(src.color_invisible, dst.color_invisible);
        U32ToFloat(src.color_dead, dst.color_dead);
        U32ToFloat(src.color_knocked, dst.color_knocked);
        U32ToFloat(src.color_team, dst.color_team);
        U32ToFloat(src.color_skeleton, dst.color_skeleton);
        U32ToFloat(src.color_skeleton_points, dst.color_skeleton_points);
        U32ToFloat(src.color_head_circle, dst.color_head_circle);
        U32ToFloat(src.color_health, dst.color_health);
        U32ToFloat(src.color_armor, dst.color_armor);
        U32ToFloat(src.color_name, dst.color_name);
        U32ToFloat(src.color_distance, dst.color_distance);
        U32ToFloat(src.color_weapon, dst.color_weapon);
        U32ToFloat(src.color_snaplines, dst.color_snaplines);
        U32ToFloat(src.color_trail, dst.color_trail);
        U32ToFloat(src.color_fun_effects, dst.color_fun_effects);
        U32ToFloat(src.color_look_direction, dst.color_look_direction);
        U32ToFloat(src.color_halo, dst.color_halo);
        U32ToFloat(src.color_box_2d, dst.color_box_2d);
        U32ToFloat(src.color_corner_box, dst.color_corner_box);
        U32ToFloat(src.color_id, dst.color_id);
        U32ToFloat(src.color_npc, dst.color_npc);
    }

    static void MapFromUnified(const UnifiedConfig::FiveMConfig& src, esp::Config& dst) {
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

        // Colors
        dst.color_visible = FloatToU32(src.color_visible);
        dst.color_invisible = FloatToU32(src.color_invisible);
        dst.color_dead = FloatToU32(src.color_dead);
        dst.color_knocked = FloatToU32(src.color_knocked);
        dst.color_team = FloatToU32(src.color_team);
        dst.color_skeleton = FloatToU32(src.color_skeleton);
        dst.color_skeleton_points = FloatToU32(src.color_skeleton_points);
        dst.color_head_circle = FloatToU32(src.color_head_circle);
        dst.color_health = FloatToU32(src.color_health);
        dst.color_armor = FloatToU32(src.color_armor);
        dst.color_name = FloatToU32(src.color_name);
        dst.color_distance = FloatToU32(src.color_distance);
        dst.color_weapon = FloatToU32(src.color_weapon);
        dst.color_snaplines = FloatToU32(src.color_snaplines);
        dst.color_trail = FloatToU32(src.color_trail);
        dst.color_fun_effects = FloatToU32(src.color_fun_effects);
        dst.color_look_direction = FloatToU32(src.color_look_direction);
        dst.color_halo = FloatToU32(src.color_halo);
        dst.color_box_2d = FloatToU32(src.color_box_2d);
        dst.color_corner_box = FloatToU32(src.color_corner_box);
        dst.color_id = FloatToU32(src.color_id);
        dst.color_npc = FloatToU32(src.color_npc);
    }
};

// ============================================================
// Warzone Adapter
// ============================================================
#include "warzone_config.h"

class WarzoneUnifiedAdapter : public UnifiedConfigAdapterBase<Warzone::Config, UnifiedConfig::WarzoneConfig> {
public:
    static void MapToUnified(const Warzone::Config& src, UnifiedConfig::WarzoneConfig& dst) {
        dst.esp_enabled = src.esp_enabled;
        dst.team_check = src.team_check;
        dst.ignore_downed = src.ignore_downed;
        dst.ignore_ai = src.ignore_ai;
        dst.skeleton = src.skeleton;
        dst.head_dot = src.head_dot;
        dst.health_bar = src.health_bar;
        dst.armor_bar = src.armor_bar;
        dst.name = src.name;
        dst.distance = src.distance;
        dst.box = src.box;
        dst.box_corner = src.box_corner;
        dst.snaplines = src.snaplines;
        dst.max_distance = src.max_distance;
        dst.aim_enabled = src.aim_enabled;
        dst.trigger_enabled = src.trigger_enabled;

        for (int i = 0; i < 4; ++i) {
            dst.col_enemy[i] = src.col_enemy[i];
            dst.col_team[i] = src.col_team[i];
            dst.col_target[i] = src.col_target[i];
            dst.col_skeleton[i] = src.col_skeleton[i];
            dst.col_armor[i] = src.col_armor[i];
        }
    }

    static void MapFromUnified(const UnifiedConfig::WarzoneConfig& src, Warzone::Config& dst) {
        dst.esp_enabled = src.esp_enabled;
        dst.team_check = src.team_check;
        dst.ignore_downed = src.ignore_downed;
        dst.ignore_ai = src.ignore_ai;
        dst.skeleton = src.skeleton;
        dst.head_dot = src.head_dot;
        dst.health_bar = src.health_bar;
        dst.armor_bar = src.armor_bar;
        dst.name = src.name;
        dst.distance = src.distance;
        dst.box = src.box;
        dst.box_corner = src.box_corner;
        dst.snaplines = src.snaplines;
        dst.max_distance = src.max_distance;
        dst.aim_enabled = src.aim_enabled;
        dst.trigger_enabled = src.trigger_enabled;

        for (int i = 0; i < 4; ++i) {
            dst.col_enemy[i] = src.col_enemy[i];
            dst.col_team[i] = src.col_team[i];
            dst.col_target[i] = src.col_target[i];
            dst.col_skeleton[i] = src.col_skeleton[i];
            dst.col_armor[i] = src.col_armor[i];
        }
    }
};

// ============================================================
// CS2 Adapter
// ============================================================
#include "config/cs2_config.h"

class CS2UnifiedAdapter : public UnifiedConfigAdapterBase<CS2::Config, UnifiedConfig::CS2Config> {
public:
    static void MapToUnified(const CS2::Config& src, UnifiedConfig::CS2Config& dst) {
        // Map CS2 config fields
        dst.esp_enabled = src.esp_enabled;
        dst.box = src.box;
        dst.box_corner = src.box_corner;
        dst.skeleton = src.skeleton;
        dst.health_bar = src.health_bar;
        dst.armor_bar = src.armor_bar;
        dst.health_value = src.health_value;
        dst.armor_value = src.armor_value;
        dst.name = src.name;
        dst.distance = src.distance;
        dst.head_dot = src.head_dot;
        dst.weapon_name = src.weapon_name;
        dst.weapon_ammo = src.weapon_ammo;
        dst.snaplines = src.snaplines;
        dst.trails = src.trails;
        dst.head_halo = src.head_halo;
        dst.look_direction = src.look_direction;
        dst.chinese_hat = src.chinese_hat;
        dst.angel_wings = src.angel_wings;
        dst.devil_horns = src.devil_horns;
        dst.floating_crown = src.floating_crown;
        dst.radar_2d = src.radar_2d;
        dst.webradar_enabled = src.webradar_enabled;
        dst.spectator_list = src.spectator_list;
        dst.bomb_timer = src.bomb_timer;
        dst.hotkey_overlay = src.hotkey_overlay;
        dst.aim_enabled = src.aim_enabled;
        dst.trigger_enabled = src.trigger_enabled;
        dst.aim_visibility_check = src.aim_visibility_check;
        dst.trigger_head_only = src.trigger_head_only;
        dst.trigger_scoped_only = src.trigger_scoped_only;
        dst.aim_prediction = src.aim_prediction;
        dst.max_distance = src.max_distance;
        dst.performance_mode = src.performance_mode;
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
            dst.col_team[i] = src.col_team[i];
            dst.col_target[i] = src.col_target[i];
        }
    }

    static void MapFromUnified(const UnifiedConfig::CS2Config& src, CS2::Config& dst) {
        dst.esp_enabled = src.esp_enabled;
        dst.box = src.box;
        dst.box_corner = src.box_corner;
        dst.skeleton = src.skeleton;
        dst.health_bar = src.health_bar;
        dst.armor_bar = src.armor_bar;
        dst.health_value = src.health_value;
        dst.armor_value = src.armor_value;
        dst.name = src.name;
        dst.distance = src.distance;
        dst.head_dot = src.head_dot;
        dst.weapon_name = src.weapon_name;
        dst.weapon_ammo = src.weapon_ammo;
        dst.snaplines = src.snaplines;
        dst.trails = src.trails;
        dst.head_halo = src.head_halo;
        dst.look_direction = src.look_direction;
        dst.chinese_hat = src.chinese_hat;
        dst.angel_wings = src.angel_wings;
        dst.devil_horns = src.devil_horns;
        dst.floating_crown = src.floating_crown;
        dst.radar_2d = src.radar_2d;
        dst.webradar_enabled = src.webradar_enabled;
        dst.spectator_list = src.spectator_list;
        dst.bomb_timer = src.bomb_timer;
        dst.hotkey_overlay = src.hotkey_overlay;
        dst.aim_enabled = src.aim_enabled;
        dst.trigger_enabled = src.trigger_enabled;
        dst.aim_visibility_check = src.aim_visibility_check;
        dst.trigger_head_only = src.trigger_head_only;
        dst.trigger_scoped_only = src.trigger_scoped_only;
        dst.aim_prediction = src.aim_prediction;
        dst.max_distance = src.max_distance;
        dst.performance_mode = src.performance_mode;
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
            dst.col_team[i] = src.col_team[i];
            dst.col_target[i] = src.col_target[i];
        }
    }
};

// ============================================================
// Fortnite Adapter
// ============================================================
#include "fortnite_game.h"

class FortniteUnifiedAdapter : public UnifiedConfigAdapterBase<Fortnite::Config, UnifiedConfig::FortniteConfig> {
public:
    static void MapToUnified(const Fortnite::Config& src, UnifiedConfig::FortniteConfig& dst) {
        dst.esp_enabled = src.esp_enabled;
        dst.show_local_marker = src.show_local_marker;
        dst.show_local_coords = src.show_local_coords;
        dst.show_camera_debug = src.show_camera_debug;
        dst.show_debug_panel = src.show_debug_panel;
        dst.box = src.box;
        dst.box_corner = src.box_corner;
        dst.box_fill = src.box_fill;
        dst.box_thickness = src.box_thickness;
        dst.box_rounding = src.box_rounding;
        dst.skeleton = src.skeleton;
        dst.skeleton_joints = src.skeleton_joints;
        dst.skeleton_thickness = src.skeleton_thickness;
        dst.head_dot = src.head_dot;
        dst.head_halo = src.head_halo;
        dst.head_circle_thickness = src.head_circle_thickness;
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
        dst.chinese_hat_scale = src.chinese_hat_scale;
        dst.fun_effects_scale = src.fun_effects_scale;
        dst.show_bots = src.show_bots;
        dst.visible_check = src.visible_check;
        dst.team_check = src.team_check;
        dst.self_esp = src.self_esp;
        dst.hit_marker = src.hit_marker;
        dst.aim_enabled = src.aim_enabled;
        dst.aim_visible_only = src.aim_visible_only;
        dst.predict = src.predict;
        dst.aim_smooth = src.aim_smooth;
        dst.aim_fov = src.aim_fov;
        dst.max_distance = src.max_distance;
        dst.bullet_velocity = src.bullet_velocity;
        dst.show_fps = src.show_fps;

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

    static void MapFromUnified(const UnifiedConfig::FortniteConfig& src, Fortnite::Config& dst) {
        dst.esp_enabled = src.esp_enabled;
        dst.show_local_marker = src.show_local_marker;
        dst.show_local_coords = src.show_local_coords;
        dst.show_camera_debug = src.show_camera_debug;
        dst.show_debug_panel = src.show_debug_panel;
        dst.box = src.box;
        dst.box_corner = src.box_corner;
        dst.box_fill = src.box_fill;
        dst.box_thickness = src.box_thickness;
        dst.box_rounding = src.box_rounding;
        dst.skeleton = src.skeleton;
        dst.skeleton_joints = src.skeleton_joints;
        dst.skeleton_thickness = src.skeleton_thickness;
        dst.head_dot = src.head_dot;
        dst.head_halo = src.head_halo;
        dst.head_circle_thickness = src.head_circle_thickness;
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
        dst.chinese_hat_scale = src.chinese_hat_scale;
        dst.fun_effects_scale = src.fun_effects_scale;
        dst.show_bots = src.show_bots;
        dst.visible_check = src.visible_check;
        dst.team_check = src.team_check;
        dst.self_esp = src.self_esp;
        dst.hit_marker = src.hit_marker;
        dst.aim_enabled = src.aim_enabled;
        dst.aim_visible_only = src.aim_visible_only;
        dst.predict = src.predict;
        dst.aim_smooth = src.aim_smooth;
        dst.aim_fov = src.aim_fov;
        dst.max_distance = src.max_distance;
        dst.bullet_velocity = src.bullet_velocity;
        dst.show_fps = src.show_fps;

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
};

} // namespace OmniGhost::Gameplay