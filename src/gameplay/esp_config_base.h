#pragma once

#include <cstdint>
#include <third_party/ImGui/imgui.h>

namespace OmniGhost::Gameplay {



struct EspConfigBase {

    bool esp_enabled = false;


    bool team_check = true;
    bool show_bots = true;
    bool visible_check = false;
    bool visibility_colors = false;
    bool show_dead = false;
    bool show_knocked = false;
    bool self_esp = false;


    bool box = true;
    bool box_corner = false;
    bool box_fill = false;
    float box_thickness = 1.8f;
    float box_rounding = 0.f;

    bool skeleton = false;
    bool skeleton_joints = false;
    float skeleton_thickness = 1.85f;
    float skeleton_joint_size = 1.4f;
    float skeleton_opacity = 1.f;

    bool head_dot = false;
    bool head_halo = false;
    float head_circle_thickness = 1.5f;
    float head_circle_scale = 1.f;
    int head_circle_segments = 20;

    bool health_bar = true;
    bool armor_bar = false;
    bool health_value = false;
    bool armor_value = false;
    float bar_width = 4.f;
    float bar_thickness = 1.f;
    float bar_spacing = 4.f;

    bool name = true;
    bool distance = true;
    bool weapon_name = false;
    bool weapon_ammo = false;
    bool player_id = false;

    bool text_outline = true;
    float text_outline_thickness = 1.f;

    bool snaplines = false;
    int snapline_position = 2;
    float snapline_thickness = 1.5f;
    float snapline_opacity = 1.f;


    bool look_direction = false;
    float eye_line_thickness = 1.6f;
    float look_direction_length = 90.f;
    float eye_line_opacity = 1.f;

    bool chinese_hat = false;
    bool angel_wings = false;
    bool devil_horns = false;
    bool floating_crown = false;
    bool trails = false;
    bool hit_marker = false;
    float chinese_hat_scale = 1.f;
    float chinese_hat_height = 1.f;
    float chinese_hat_width = 1.f;
    float chinese_hat_rotation_speed = 1.f;
    float fun_effects_scale = 1.f;
    float halo_scale = 1.f;
    float halo_thickness = 2.f;
    float halo_height = 1.f;
    float trail_duration = 0.8f;
    float trail_thickness = 2.f;
    float trail_opacity = 1.f;
    float trail_smoothing = .45f;
    bool rainbow_trails = true;
    bool fun_effects_rainbow = true;


    bool radar_2d = false;
    bool radar_2d_draggable = false;
    float radar_2d_x = 18.f;
    float radar_2d_y = 18.f;
    float radar_2d_size = 160.f;


    bool skeleton_lod = true;
    float skeleton_lod_distance = 70.f;
    bool bone_draw_arms = true;
    bool bone_draw_legs = true;


    float max_distance = 300.f;

    float box_corner_length = .25f;
    float box_fill_opacity = .16f;
    float box_margin = 0.f;
    float offscreen_arrow_thickness = 1.5f;
    bool esp_distance_scaling = true;
    float esp_min_scale = .55f;
    float esp_visual_smoothing = .35f;


    bool performance_mode = false;
    float optional_lane_budget_ms = 6.f;


    bool telemetry_enabled = false;


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


    bool rgb_mode = false;
    bool distance_feet = false;


    ImU32 ToImU32(const float c[4]) const {
        return IM_COL32(
            static_cast<int>(c[0] * 255.f),
            static_cast<int>(c[1] * 255.f),
            static_cast<int>(c[2] * 255.f),
            static_cast<int>(c[3] * 255.f)
        );
    }
};

}
