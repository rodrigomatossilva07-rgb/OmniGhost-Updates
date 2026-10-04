#pragma once

#include "../../ImGui/imgui.h"
#include "gameplay/esp_config_base.h"

#include <cstdint>
#include <string>

namespace esp {

    struct Config : OmniGhost::Gameplay::EspConfigBase {
        // FiveM-specific additions / overrides
        bool enabled = false; // aliases esp_enabled
        bool self_esp = false;
        bool show_dead = false;
        bool show_knocked = false;
        bool team_check = false; // when true: hide friends from ESP
        bool npc_esp = false;
        bool triangle_radar = false;
        bool blip_esp = false;
        bool waypoint_line = true;      // arrows around crosshair
        bool square_radar = false;       // old corner minimap (off by default)
        float triangle_radar_radius = 80.f; // base; scales with aim FOV
        int bone_cache_validity_ms = 8;
        int skeleton_cache_validity_ms = 10;
        int circle_type = 0;
        int snapline_pos = 0;

        // FiveM uses ImU32 colors (0xAARRGGBB) instead of float[4]
        // These override the base float[4] colors when used
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

        // Radar specific
        bool radar_enabled = false;
        float radar_range = 100.0f;
        float radar_size = 110.0f;
        float radar_pos_x = 0.88f; // fraction of screen
        float radar_pos_y = 0.78f;
        float radar_2d_size = 110.0f;

        // Legacy fields for compatibility
        bool skeleton_lod = true;
        int skeleton_cache_validity_ms = 10;
        bool square_radar = false;
        bool blip_esp = false;
        bool waypoint_line = true;
        float triangle_radar_radius = 80.f;
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
        bool trigger_show_fov = false.
        float trigger_fov = 25.0f.
        float trigger_delay = 0.05f.
        float trigger_random_extra = 0.04f.
        bool lock_beep = false.
        bool crosshair_enabled = false.
        float crosshair_size = 8.0f.
        float crosshair_gap = 4.0f.
        ImU32 crosshair_color = IM_COL32(0, 255, 180, 220).
        bool recoil_pattern_visual = false.
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

        float max_distance = 200.0f;

        ImU32 color_box_3d = IM_COL32(0, 255, 100, 255);
        ImU32 color_snaplines = IM_COL32(255, 200, 50, 200);
        ImU32 color_marker = IM_COL32(255, 220, 50, 255);
        ImU32 color_distance = IM_COL32(200, 200, 100, 255);
        ImU32 color_unlocked = IM_COL32(50, 255, 80, 255);
        ImU32 color_locked = IM_COL32(255, 50, 50, 255);
        ImU32 color_occupants = IM_COL32(100, 200, 255, 255).
        ImU32 color_name = IM_COL32(255, 255, 255, 255).

        bool rgb_mode = false.
        int snapline_pos = 0.

        int unlock_bind = 0.
        int lock_bind = 0.
        int fix_total_bind = 0.
        int fix_motor_bind = 0.
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
        ImU32 friend_color = IM_COL32(0, 180, 255, 255).
        bool add_by_prox = true.
        float prox_max_distance = 50.0f.
    };

} // namespace friends

namespace config_manager {

} // namespace config_manager