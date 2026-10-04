#pragma once

#include <cstdint>
#include <ImGui/imgui.h>

namespace OmniGhost::Gameplay {

// Base ESP configuration shared across all game adapters.
// Game-specific configs should inherit from this and add their own fields.
struct EspConfigBase {
    // Master toggle
    bool esp_enabled = false;

    // Filtering
    bool team_check = true;
    bool show_bots = true;
    bool visible_check = false;
    bool visibility_colors = false;
    bool show_dead = false;
    bool show_knocked = false;
    bool self_esp = false;

    // Core elements
    bool box = true;
    bool box_corner = false;
    bool box_fill = false;
    float box_thickness = 1.8f;
    float box_rounding = 0.f;

    bool skeleton = false;
    bool skeleton_joints = false;
    float skeleton_thickness = 1.85f;

    bool head_dot = false;
    bool head_halo = false;
    float head_circle_thickness = 1.5f;

    bool health_bar = true;
    bool armor_bar = false;
    bool health_value = false;
    bool armor_value = false;

    bool name = true;
    bool distance = true;
    bool weapon_name = false;
    bool weapon_ammo = false;
    bool player_id = false;

    bool text_outline = true;
    float text_outline_thickness = 1.f;

    bool snaplines = false;
    int snapline_position = 2; // 0=top, 1=center, 2=bottom
    float snapline_thickness = 1.5f;

    // Visual effects
    bool look_direction = false;
    float eye_line_thickness = 1.6f;
    float look_direction_length = 90.f;

    bool chinese_hat = false;
    bool angel_wings = false;
    bool devil_horns = false;
    bool floating_crown = false;
    bool trails = false;
    bool hit_marker = false;
    float chinese_hat_scale = 1.f;
    float fun_effects_scale = 1.f;
    float trail_duration = 0.8f;
    float trail_thickness = 2.f;
    bool rainbow_trails = true;
    bool fun_effects_rainbow = true;

    // Radar
    bool radar_2d = false;
    bool radar_2d_draggable = false;
    float radar_2d_x = 18.f;
    float radar_2d_y = 18.f;
    float radar_2d_size = 160.f;

    // Skeleton LOD
    bool skeleton_lod = true;
    float skeleton_lod_distance = 70.f;
    bool bone_draw_arms = true;
    bool bone_draw_legs = true;

    // Distance culling
    float max_distance = 300.f;

    // Performance
    bool performance_mode = false;
    float optional_lane_budget_ms = 6.f;

    // Telemetry
    bool telemetry_enabled = false;

    // Colors (RGBA 0-1)
    float col_enemy[4] = {0.9f, 0.25f, 0.25f, 1.f};
    float col_team[4] = {0.25f, 0.75f, 0.95f, 1.f};
    float col_skeleton[4] = {0.95f, 0.85f, 0.35f, 1.f};
    float col_box[4] = {0.9f, 0.25f, 0.25f, 1.f};
    float col_box_corner[4] = {0.f, 0.78f, 1.f, 1.f};
    float col_box_fill[4] = {0.9f, 0.25f, 0.25f, 0.16f};
    float col_name[4] = {1.f, 1.f, 1.f, 1.f};
    float col_weapon[4] = {0.85f, 0.85f, 0.9f, 1.f};
    float col_target[4] = {1.f, 0.85f, 0.15f, 1.f};
    float col_joints[4] = {0.27f, 0.9f, 0.37f, 1.f};
    float col_health[4] = {0.25f, 0.86f, 0.35f, 1.f};
    float col_armor[4] = {0.27f, 0.59f, 1.f, 1.f};
    float col_snaplines[4] = {0.83f, 0.69f, 0.22f, 0.55f};
    float col_trail[4] = {0.83f, 0.69f, 0.22f, 0.82f};
    float col_halo[4] = {1.f, 0.89f, 0.54f, 0.9f};
    float col_fun_effects[4] = {1.f, 0.72f, 0.18f, 0.95f};
    float col_look[4] = {0.83f, 0.69f, 0.22f, 0.86f};
    float col_head[4] = {0.95f, 0.85f, 0.35f, 1.f};
    float col_distance[4] = {0.85f, 0.85f, 0.9f, 1.f};
    float col_visible[4] = {0.27f, 0.9f, 0.37f, 1.f};
    float col_occluded[4] = {0.9f, 0.25f, 0.25f, 1.f};
    float col_text_outline[4] = {0.f, 0.f, 0.f, 1.f};
    float col_flags[4] = {0.95f, 0.85f, 0.35f, 1.f};
    float col_fov[4] = {0.83f, 0.69f, 0.22f, 0.45f};
    float col_dead[4] = {1.f, 0.2f, 0.2f, 1.f};
    float col_knocked[4] = {1.f, 0.65f, 0.f, 1.f};
    float col_npc[4] = {0.5f, 0.4f, 1.f, 1.f};

    // RGB/Visibility modes
    bool rgb_mode = false;
    bool distance_feet = false;

    // Utility: convert float[4] color to ImU32
    ImU32 ToImU32(const float c[4]) const {
        return IM_COL32(
            static_cast<int>(c[0] * 255.f),
            static_cast<int>(c[1] * 255.f),
            static_cast<int>(c[2] * 255.f),
            static_cast<int>(c[3] * 255.f)
        );
    }
};

} // namespace OmniGhost::Gameplay