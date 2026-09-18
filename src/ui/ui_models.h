#pragma once

#include "../../ImGui/imgui.h"
#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <filesystem>

namespace esp {

    enum class ESPSubTab : int {
        Main = 0,
        Colors = 1,
        Advanced = 2,
        Filters = 3,
        Profiles = 4
    };

    enum class ColorProfile : int {
        Custom = 0,
        Competitive = 1,
        Streamer = 2,
        Stealth = 3,
        HighContrast = 4,
        ColorBlind = 5
    };

    enum class ColorBlindMode : int {
        None = 0,
        Protanopia = 1,
        Deuteranopia = 2,
        Tritanopia = 3
    };

    struct ColorScheme {
        std::string name;
        ImU32 color_visible = IM_COL32(0, 255, 0, 255);
        ImU32 color_invisible = IM_COL32(255, 55, 55, 255);
        ImU32 color_dead = IM_COL32(255, 50, 50, 255);
        ImU32 color_knocked = IM_COL32(255, 165, 0, 255);
        ImU32 color_team = IM_COL32(0, 150, 255, 255);
        ImU32 color_skeleton = IM_COL32(255, 255, 255, 220);
        ImU32 color_skeleton_points = IM_COL32(255, 200, 50, 255);
        ImU32 color_weapon = IM_COL32(200, 200, 255, 255);
        ImU32 color_box_2d = IM_COL32(255, 255, 255, 220);
        ImU32 color_corner_box = IM_COL32(0, 200, 255, 255);
        ImU32 color_snaplines = IM_COL32(255, 255, 255, 120);
        ImU32 color_name = IM_COL32(255, 255, 255, 255);
        ImU32 color_id = IM_COL32(180, 180, 180, 255);
        ImU32 color_distance = IM_COL32(200, 200, 100, 255);
        ImU32 color_npc = IM_COL32(150, 100, 255, 255);
        ImU32 color_head_circle = IM_COL32(255, 255, 255, 230);
        ImU32 color_health = IM_COL32(65, 220, 90, 255);
        ImU32 color_armor = IM_COL32(70, 150, 255, 255);
        ImU32 color_trail = IM_COL32(212, 175, 55, 210);
        ImU32 color_halo = IM_COL32(255, 226, 138, 230);
        ImU32 color_fun_effects = IM_COL32(255, 184, 46, 242);
        ImU32 color_look_direction = IM_COL32(212, 175, 55, 220);
        bool rgb_mode = false;
        
        // Per-entity colors
        ImU32 color_player = IM_COL32(0, 255, 100, 255);
        ImU32 color_npc_entity = IM_COL32(150, 100, 255, 255);
        ImU32 color_vehicle = IM_COL32(255, 200, 50, 255);
        ImU32 color_loot = IM_COL32(255, 255, 0, 255);
        ImU32 color_special = IM_COL32(255, 0, 255, 255);
    };

    struct ServerProfile {
        std::string server_ip;
        std::string server_name;
        std::string profile_data; // Base64 encoded config
        std::chrono::system_clock::time_point last_used;
        bool auto_apply = true;
    };

    struct Config {
        // All features start OFF until the user enables them or loads a config.
        bool enabled = false;
        bool self_esp = false;
        // The colour state and the visibility filter are independent: colour
        // state keeps every player visible (green/red), while visible_check
        // hides occluded players entirely.
        bool visibility_colors = false;
        bool visible_check = false;
        bool show_dead = false;
        bool show_knocked = false;
        bool team_check = false; // when true: hide friends from ESP

        bool skeleton = false;
        bool skeleton_chams = false; // Filled skeleton with transparency
        float skeleton_chams_alpha = 0.3f;
        bool joints = false; // yellow joint dots on skeleton
        bool head_circle = false;
        bool health_bar = false;
        bool armor_bar = false;
        bool weapon_name = false;
        bool weapon_icon = false;
        bool box_2d = false;
        bool corner_box = false;
        bool filled_box = false;
        bool snaplines = false;
        bool distance = false;
        bool player_name = false;
        bool player_id = false;
        bool npc_esp = false;
        bool show_velocity = false;        // Velocity vectors
        bool show_aim_direction = false;   // Where player is looking
        bool show_reload_indicator = false;
        bool show_platform = false;        // Steam/Epic/Console
        bool show_rank = false;
        bool show_kd_ratio = false;
        bool trails = false;
        bool head_halo = false;
        bool look_direction = false; // Eye Line
        bool chinese_hat = false;
        float chinese_hat_scale = 1.0f;
        bool angel_wings = false;
        bool devil_horns = false;
        bool floating_crown = false;
        bool hit_marker = false;
        bool fun_effects_rainbow = true;
        float fun_effects_scale = 1.0f;
        bool rainbow_trails = true;
        float trail_duration = 0.80f;
        float trail_thickness = 2.0f;
        float look_direction_length = 2.0f;
        float skeleton_thickness = 1.8f;
        float snapline_thickness = 1.5f;
        float head_circle_thickness = 1.6f;
        float box_thickness = 1.8f;
        float eye_line_thickness = 1.6f;
        bool radar_enabled = false;
        bool radar_3d = false;              // 3D radar with height
        bool radar_show_names = false;
        bool radar_show_distance = false;
        bool radar_rotate_with_player = true;
        float radar_height_scale = 0.5f;    // Z-axis compression
        float radar_range = 100.0f;
        float radar_size = 110.0f;
        float radar_pos_x = 0.88f; // fraction of screen
        float radar_pos_y = 0.78f;
        bool skeleton_lod = true;
        float max_esp_distance = 150.0f; // 0..500 m — DMA + draw cutoff (0 = nobody)
        bool triangle_radar = false;
        bool blip_esp = false;
        bool waypoint_line = true;      // arrows around crosshair
        bool square_radar = false;       // old corner minimap (off by default)
        float triangle_radar_radius = 80.f; // base; scales with aim FOV

        // Color system
        ColorProfile color_profile = ColorProfile::Custom;
        ColorBlindMode color_blind_mode = ColorBlindMode::None;
        std::vector<ServerProfile> server_profiles;
        bool auto_save_config = true;
        int config_version = 3;

        ImU32 color_visible = IM_COL32(0, 255, 0, 255);
        ImU32 color_invisible = IM_COL32(255, 55, 55, 255);
        ImU32 color_dead = IM_COL32(255, 50, 50, 255);
        ImU32 color_knocked = IM_COL32(255, 165, 0, 255);
        ImU32 color_team = IM_COL32(0, 150, 255, 255);
        ImU32 color_skeleton = IM_COL32(255, 255, 255, 220);
        ImU32 color_skeleton_points = IM_COL32(255, 200, 50, 255);
        ImU32 color_weapon = IM_COL32(200, 200, 255, 255);
        ImU32 color_box_2d = IM_COL32(255, 255, 255, 220);
        ImU32 color_corner_box = IM_COL32(0, 200, 255, 255);
        ImU32 color_snaplines = IM_COL32(255, 255, 255, 120);
        ImU32 color_name = IM_COL32(255, 255, 255, 255);
        ImU32 color_id = IM_COL32(180, 180, 180, 255);
        ImU32 color_distance = IM_COL32(200, 200, 100, 255);
        ImU32 color_npc = IM_COL32(150, 100, 255, 255);
        ImU32 color_head_circle = IM_COL32(255, 255, 255, 230);
        ImU32 color_health = IM_COL32(65, 220, 90, 255);
        ImU32 color_armor = IM_COL32(70, 150, 255, 255);
        ImU32 color_trail = IM_COL32(212, 175, 55, 210);
        ImU32 color_halo = IM_COL32(255, 226, 138, 230);
        ImU32 color_fun_effects = IM_COL32(255, 184, 46, 242);
        ImU32 color_look_direction = IM_COL32(212, 175, 55, 220);
        
        // Per-entity colors
        ImU32 color_player = IM_COL32(0, 255, 100, 255);
        ImU32 color_npc_entity = IM_COL32(150, 100, 255, 255);
        ImU32 color_vehicle = IM_COL32(255, 200, 50, 255);
        ImU32 color_loot = IM_COL32(255, 255, 0, 255);
        ImU32 color_special = IM_COL32(255, 0, 255, 255);

        bool rgb_mode = false;
        int snapline_pos = 0;
        int circle_type = 0;
        
        // UI State
        ESPSubTab current_sub_tab = ESPSubTab::Main;
        std::string search_filter;
        bool show_advanced = false;
    };

} // namespace esp

namespace aimbot {

    enum class Hitbox : int {
        Head = 0,
        Neck,
        Torso,
        Pelvis,
        Legs
    };

    enum class FovStyle : int {
        Circle = 0,
        Square,
        Cross
    };

    struct Config {
        Hitbox hitbox = Hitbox::Head;
        FovStyle fov_style = FovStyle::Circle;
        bool visible_check = false; // DMA visibility often false-negatives
        bool closest_to_crosshair = true;

        bool aimbot_enabled = false;
        bool show_fov = true;
        bool fov_rgb = false;
        ImU32 fov_color = IM_COL32(0, 200, 255, 180);
        int aimbot_bind = 0x02;
        int aimbot_bind2 = 0;
        float fov_size = 100.0f;
        float max_distance = 300.0f;
        float smooth_x = 0.0f;   // 0 = full snap, 100 = no pull (same as CS2)
        float smooth_y = 0.0f;
        float reaction_time = 0.0f; // no artificial delay by default

        bool humanize = false;
        bool threat_priority = true;
        bool velocity_prediction = true;
        float jitter_amount = 0.35f;
        float acceleration = 0.65f;
        float overshoot_chance = 0.12f;
        float overshoot_scale = 0.08f;
        float min_move_threshold = 0.15f;

        bool silent_enabled = false;
        bool silent_show_fov = false;
        bool silent_fov_rgb = false;
        ImU32 silent_fov_color = IM_COL32(0, 200, 200, 180);
        int silent_bind = 0x02;
        float silent_fov_size = 100.0f;
        float silent_max_distance = 300.0f;
        float silent_hit_chance = 100.0f;
        float silent_miss_chance = 0.0f;
        Hitbox silent_bone = Hitbox::Head;
        bool silent_legit = true;
        bool silent_rage = false;

        bool trigger_enabled = false;
        bool trigger_always_on = true;
        int trigger_bind = 0;
        bool trigger_head_only = true;
        bool trigger_show_fov = false;
        float trigger_fov = 25.0f;
        float trigger_delay = 0.05f;
        float trigger_random_extra = 0.04f;
        bool lock_beep = false;
        bool crosshair_enabled = false;
        float crosshair_size = 8.0f;
        float crosshair_gap = 4.0f;
        ImU32 crosshair_color = IM_COL32(0, 255, 180, 220);
        bool recoil_pattern_visual = false;
    };

} // namespace aimbot

namespace aim_type {

    enum class DeviceType : int {
        None = 0,
        KmboxNet,
        Ferrum,
        Makcu
    };

    struct Config {
        DeviceType active = DeviceType::Makcu;

        bool kmbox_net_enabled = false;
        char kmbox_ip[64] = "192.168.2.188";
        char kmbox_port[16] = "19856";
        char kmbox_uuid[64] = "";
        bool kmbox_net_connected = false;

        bool ferrum_enabled = false;
        char ferrum_com[16] = "";
        int ferrum_baud = 115200;
        bool ferrum_connected = false;

        bool makcu_enabled = true;
        char makcu_com[16] = "";
        int makcu_baud = 115200;
        bool makcu_connected = false;
    };

} // namespace aim_type

namespace vehicle_esp {

    struct Config {
        bool enabled = false;
        bool self_vehicle = false;
        bool anti_fall_moto = false;

        bool box_3d = false;
        bool snaplines = false;
        bool marker = false;
        bool distance = true;
        bool show_occupants = false;
        bool vehicle_name = false;
        bool ignore_occupied = false;
        bool lock_status = true;
        bool show_speed = false;
        bool show_gear = false;
        bool show_engine = false;

        float max_distance = 200.0f;

        ImU32 color_box_3d = IM_COL32(0, 255, 100, 255);
        ImU32 color_snaplines = IM_COL32(255, 200, 50, 200);
        ImU32 color_marker = IM_COL32(255, 220, 50, 255);
        ImU32 color_distance = IM_COL32(200, 200, 100, 255);
        ImU32 color_unlocked = IM_COL32(50, 255, 80, 255);
        ImU32 color_locked = IM_COL32(255, 50, 50, 255);
        ImU32 color_occupants = IM_COL32(100, 200, 255, 255);
        ImU32 color_name = IM_COL32(255, 255, 255, 255);
        ImU32 color_gear = IM_COL32(255, 220, 120, 220);
        ImU32 color_engine = IM_COL32(180, 255, 180, 220);

        bool rgb_mode = false;
        int snapline_pos = 0;

        int unlock_bind = 0;
        int lock_bind = 0;
        int fix_total_bind = 0;
        int fix_motor_bind = 0;
    };

} // namespace vehicle_esp

namespace friends {

    struct FriendEntry {
        std::string name;
        uint32_t id = 0;
        bool is_prox = false;
    };

    struct PlayerEntry {
        std::string name;
        uint32_t id = 0;
        float distance = 0.0f;
        float health_pct = 0.0f;
        uintptr_t ped = 0;
    };

    struct Config {
        bool ignore_aim = true;
        bool ignore_silent = true;
        bool show_friend_esp = false;
        ImU32 friend_color = IM_COL32(0, 180, 255, 255);
        bool add_by_prox = true;
        float prox_max_distance = 50.0f;
    };

} // namespace friends

namespace config_manager {

} // namespace config_manager
