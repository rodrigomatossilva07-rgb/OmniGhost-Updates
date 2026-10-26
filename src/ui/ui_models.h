#pragma once

#include "third_party/ImGui/imgui.h"
#include "gameplay/esp_config_base.h"

#include <cstdint>
#include <string>

namespace esp {

    enum class LayoutAnchor : int {
        Top = 0,
        Bottom,
        Left,
        Right
    };

    struct Config : OmniGhost::Gameplay::EspConfigBase {

        bool enabled = false;

        bool joints = false;
        bool head_circle = false;
        bool box_2d = true;
        bool filled_box = false;
        bool corner_box = false;
        bool player_name = true;
        float max_esp_distance = 300.f;
        bool self_esp = false;
        bool show_dead = false;
        bool show_knocked = false;
        bool team_check = false;
        bool npc_esp = false;
        bool triangle_radar = false;
        bool blip_esp = false;
        bool waypoint_line = true;
        bool square_radar = false;
        float triangle_radar_radius = 80.f;

        bool offscreen_arrows = false;
        float offscreen_arrow_size = 13.f;
        float offscreen_arrow_margin = 38.f;
        float offscreen_arrow_max_distance = 300.f;
        float offscreen_arrow_opacity = .85f;
        bool offscreen_arrow_distance = true;
        bool offscreen_arrow_name = false;
        int offscreen_arrow_style = 0;
        ImU32 color_offscreen_arrow = IM_COL32(255, 196, 48, 220);

        bool active_features_panel = false;
        float active_panel_x = .018f;
        float active_panel_y = .18f;
        float active_panel_scale = 1.f;
        float active_panel_opacity = .86f;
        bool active_panel_compact = false;
        bool active_panel_show_esp = true;
        bool active_panel_show_aim = true;
        bool active_panel_show_trigger = true;
        bool active_panel_show_radar = true;

        LayoutAnchor layout_name = LayoutAnchor::Top;
        LayoutAnchor layout_health = LayoutAnchor::Left;
        LayoutAnchor layout_armor = LayoutAnchor::Right;
        LayoutAnchor layout_weapon = LayoutAnchor::Bottom;
        LayoutAnchor layout_distance = LayoutAnchor::Bottom;
        float layout_name_x = .50f, layout_name_y = .10f;
        float layout_health_x = .18f, layout_health_y = .50f;
        float layout_armor_x = .82f, layout_armor_y = .50f;
        float layout_weapon_x = .50f, layout_weapon_y = .86f;
        float layout_distance_x = .50f, layout_distance_y = .94f;
        float layout_spacing = 4.f;
        float layout_text_scale = 1.f;
        bool layout_snap = true;
        int circle_type = 0;
        int snapline_pos = 0;



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


        bool radar_enabled = false;
        float radar_range = 100.0f;
        float radar_size = 110.0f;
        float radar_pos_x = 0.88f;
        float radar_pos_y = 0.78f;
        float radar_2d_size = 110.0f;
        bool web_radar_enabled = false;
        bool web_radar_cloudflare = false;
        int web_radar_port = 8765;
        float web_radar_distance = 1500.f;
        int web_radar_max_players = 64;
        int web_radar_max_vehicles = 64;
        float web_radar_update_hz = 10.f;
        int web_radar_mode = 0; // 0 equilibrado, 1 cobertura, 2 desempenho


        bool skeleton_lod = true;

        float lod_full_dist = 30.0f;
        float lod_high_dist = 80.0f;
        float lod_med_dist = 150.0f;
    };

}

namespace aimbot {

    enum class DynamicFovMode : int {
        Static = 0,
        Distance = 1
    };

    enum class WeaponRangeCategory : int {
        Custom = 0,
        Melee,
        Pistol,
        Smg,
        Shotgun,
        Rifle,
        Sniper,
        Heavy
    };

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
        bool visible_check = false;
        bool closest_to_crosshair = true;

        bool aimbot_enabled = false;
        bool show_fov = true;
        ImU32 fov_color = IM_COL32(0, 200, 255, 180);
        int aimbot_bind = 0x02;
        int aimbot_bind2 = 0;
        float fov_size = 100.0f;
        DynamicFovMode dynamic_fov_mode = DynamicFovMode::Static;
        float dynamic_fov_min = 35.f;
        float dynamic_fov_max = 140.f;
        float dynamic_fov_near_distance = 10.f;
        float dynamic_fov_far_distance = 250.f;
        float dynamic_fov_curve = 1.f;
        float dynamic_fov_transition_speed = 10.f;
        float max_distance = 300.0f;
        float smooth_x = 0.0f;
        float smooth_y = 0.0f;

        bool humanize = false;
        bool threat_priority = true;
        bool velocity_prediction = true;
        bool sticky_aim = true;
        float sticky_retention = .35f;
        float sticky_tolerance = 18.f;
        float switch_delay = .16f;
        bool switch_immediately_if_invalid = true;

        bool respect_weapon_range = false;
        bool trigger_respect_weapon_range = true;
        WeaponRangeCategory weapon_range_category = WeaponRangeCategory::Custom;
        float weapon_range_custom = 300.f;
        float weapon_range_melee = 300.f;
        float weapon_range_pistol = 300.f;
        float weapon_range_smg = 300.f;
        float weapon_range_shotgun = 300.f;
        float weapon_range_rifle = 300.f;
        float weapon_range_sniper = 300.f;
        float weapon_range_heavy = 300.f;

        bool trigger_enabled = false;
        bool trigger_always_on = true;
        int trigger_bind = 0;
        bool trigger_head_only = true;
        bool trigger_show_fov = false;
        float trigger_fov = 25.0f;
        float trigger_delay = 0.05f;
        float trigger_random_extra = 0.04f;
        bool trigger_burst = false;
        float trigger_burst_duration = .18f;
        float trigger_burst_interval = .35f;
        float trigger_cooldown = .08f;
        bool lock_beep = false;
        bool crosshair_enabled = false;
        float crosshair_size = 8.0f;
        float crosshair_gap = 4.0f;
        ImU32 crosshair_color = IM_COL32(0, 255, 180, 220);
        bool recoil_pattern_visual = false;
    };

}

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

}

namespace vehicle_esp {

    struct Config {
        bool enabled = false;

        bool box_3d = false;
        bool snaplines = false;
        bool distance = true;
        bool show_occupants = false;
        bool ignore_occupied = false;
        bool lock_status = true;
        bool show_speed = false;

        float max_distance = 200.0f;

        ImU32 color_box_3d = IM_COL32(0, 255, 100, 255);
        ImU32 color_snaplines = IM_COL32(255, 200, 50, 200);
        ImU32 color_distance = IM_COL32(200, 200, 100, 255);
        ImU32 color_unlocked = IM_COL32(50, 255, 80, 255);
        ImU32 color_locked = IM_COL32(255, 50, 50, 255);
        ImU32 color_occupants = IM_COL32(100, 200, 255, 255);

        int snapline_pos = 0;



    };

}

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

}

namespace config_manager {

}
