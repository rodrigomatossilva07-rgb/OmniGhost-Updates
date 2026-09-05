#pragma once

namespace Rust {

struct Config {
    // ── ESP players (core + blurred-style extras) ────────────────
    bool esp_enabled = false;
    bool self_esp = false;
    bool name = true;
    bool distance = true;
    bool box = true;
    bool box_corner = false;
    bool snaplines = false;
    bool snaplines_center = false;
    bool snaplines_target_only = false;
    bool show_sleepers = false;
    bool show_npc = false;
    bool show_wounded = true;
    bool show_safezone = true;
    bool show_knocked = true;
    bool head_dot = false;
    bool health_bar = true;
    bool armor_bar = false;
    bool weapon_name = true;
    bool skeleton = false;
    bool skeleton_joints = true;
    bool offscreen_arrows = false;
    bool directional_arrows = false;
    bool highlight_aim_target = true;
    bool team_check = true;          // ignore same team
    bool show_team_id = false;
    bool show_steam_id = false;
    bool show_flags = true;          // aiming/sprinting/mounted text
    bool belt_esp = false;
    bool distance_gradient = true;
    bool vis_color = false;          // colors by visibility (when vischeck data exists)
    int max_distance = 300;
    int sleeper_max_distance = 200;
    int belt_hotkey = 0x09;          // TAB
    float box_thickness = 1.0f;
    float skeleton_thickness = 1.0f;
    float snapline_thickness = 1.5f;
    float arrow_width = 10.f;
    float arrow_length = 20.f;
    int box_type = 0;                // 0 none ui, 1 full, 2 corner (mirrors toggles)

    float col_enemy[4]   = { 0.95f, 0.25f, 0.20f, 1.f };
    float col_sleeper[4] = { 0.55f, 0.55f, 0.60f, 1.f };
    float col_npc[4]     = { 0.90f, 0.70f, 0.20f, 1.f };
    float col_wounded[4] = { 1.f, 0.55f, 0.15f, 1.f };
    float col_team[4]    = { 0.25f, 0.85f, 0.35f, 1.f };
    float col_name[4]    = { 1.f, 1.f, 1.f, 1.f };
    float col_target[4]  = { 1.f, 0.85f, 0.20f, 1.f };
    float col_skeleton[4] = { 0.95f, 0.95f, 0.95f, 0.90f };
    float col_visible[4] = { 0.25f, 0.90f, 0.40f, 1.f };
    float col_hidden[4]  = { 0.95f, 0.30f, 0.25f, 1.f };

    // ── World ESP ────────────────────────────────────────────────
    bool world_esp = false;
    bool ore_esp = false;
    bool crate_esp = false;
    bool stash_esp = false;
    bool tc_esp = false;
    bool turret_esp = false;
    bool vehicle_esp = false;
    bool animal_esp = false;
    bool airdrop_esp = false;
    bool item_drops_esp = false;
    bool collectables_esp = false;
    bool corpse_esp = false;
    bool world_show_name = true;
    bool world_show_distance = true;
    int world_max_distance = 400;
    int world_toggle_key = 0;
    int world_distance_cycle_key = 0;

    // Per-item world filters (prefab/shortname substring, case-insensitive).
    // When enabled AND category parent is on, entity is drawn.
    bool w_airdrop = true;
    bool w_attackheli = true;
    bool w_barrel = true;
    bool w_basiccrate = true;
    bool w_bear = true;
    bool w_bike = true;
    bool w_blueberry = true;
    bool w_blueprint = true;
    bool w_boar = true;
    bool w_bodybag = true;
    bool w_bradley = true;
    bool w_buriedstash = true;
    bool w_chicken = true;
    bool w_coffin = true;
    bool w_sulfur = true;
    bool w_metal_ore = true;
    bool w_stone_ore = true;
    bool w_hemp = true;
    bool w_wood_pile = true;
    bool w_toolcupboard = true;
    bool w_autoturret = true;
    bool w_shotguntrap = true;
    bool w_flameturret = true;
    bool w_minicopter = true;
    bool w_scrapheli = true;
    bool w_rowboat = true;
    bool w_rhib = true;
    bool w_horse = true;
    bool w_wolf = true;
    bool w_stag = true;
    bool w_elite_crate = true;
    bool w_military_crate = true;
    bool w_locked_crate = true;
    bool w_hackable_crate = true;
    bool w_oil_barrel = true;
    bool w_diesel = true;

    // ── Aim (blurred-inspired + CS2 model) ────────────────────────
    bool aim_enabled = false;
    bool aim_draw_fov = true;
    bool aim_draw_line = false;
    bool aim_draw_prediction = false;
    bool aim_dynamic_fov = false;
    bool aim_sync_fps = true;
    bool aim_bone_transitions = true;
    float aim_fov = 90.f;
    float aim_fov_min = 25.f;
    float aim_smooth = 25.f;         // 0 = snap, 100 = none — default mid for human feel
    float aim_max_dist = 200.f;
    float aim_interval_ms = 5.f;
    float prediction_point_size = 1.0f;
    int aim_bind = 0x02;             // RMB default (common legit)
    int aim_bind2 = 0;
    int aim_bind3 = 0;
    float aim_deadzone = 1.5f;
    float sticky_ms = 140.f;
    float switch_cooldown_ms = 80.f;
    int aim_bone = 0;                // 0 head, 1 chest, 2 pelvis, 3 random
    bool aim_ignore_sleepers = true;
    bool aim_ignore_wounded = false;
    bool aim_ignore_knocked = true;
    bool aim_ignore_npc = true;
    bool aim_ignore_team = true;
    bool aim_ignore_helicopters = true;
    bool aim_ignore_drones = true;
    bool aim_prediction = true;
    float prediction_strength = 0.35f;
    bool aim_humanize = true;        // on by default for human track
    float humanize_amount = 0.35f;
    bool trigger_enabled = false;
    int trigger_bind = 0x06;         // XButton2
    int trigger_delay_ms = 40;
    bool hit_chance_ui = false;
    float col_fov[4] = { 0.83f, 0.69f, 0.22f, 0.45f };

    // Per-weapon category enable (UI + future weapon-class filter)
    bool aim_cat_global = true;
    bool aim_cat_rifles = true;
    bool aim_cat_smgs = true;
    bool aim_cat_pistols = true;
    bool aim_cat_lmgs = true;
    bool aim_cat_shotguns = true;
    bool aim_cat_bows = true;

    // Override key (temporary alternate smooth/fov while held)
    int aim_override_key = 0;
    float aim_override_smooth = 25.f;
    float aim_override_fov = 100.f;

    // ── Misc writes / utilities ──────────────────────────────────
    bool no_recoil = false;
    int recoil_x = 25;
    int recoil_y = 25;
    int recoil_mode = 0;             // 0 during aim, 1 always
    bool spider_man = false;
    bool admin_esp = false;
    bool admin_flag = false;
    bool bright_nights = false;
    bool bright_caves = false;
    bool change_time = false;
    int time_of_day = 12;
    bool change_fov = false;
    int fov_value = 100;
    bool remove_water = false;
    bool instant_eoka = false;
    bool inventory_esp = false;
    bool vischeck_raycast = false;
    bool vischeck_debug_wire = false;
    bool fortify_export_sleeping_bags = true;
    float fortify_min_distance = 0.f;

    // ── Web radar ────────────────────────────────────────────────

    // ── System ───────────────────────────────────────────────────
    bool performance_mode = false;
    bool panic_key_enabled = false;
    int panic_key = 0x23;            // END
    bool radar_2d = false;
    float radar_size = 160.f;
    float radar_x = 40.f;
    float radar_y = 40.f;
};

extern Config config;

} // namespace Rust
