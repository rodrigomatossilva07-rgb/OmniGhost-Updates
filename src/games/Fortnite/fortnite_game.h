#pragma once
#include <cstdint>
#include <string>
#include <cmath>
#include <vector>





namespace Fortnite {

struct Config {

    bool esp_enabled = true;
    bool show_local_marker = true;
    bool show_local_coords = true;
    bool show_camera_debug = true;
    bool show_debug_panel = true;
    bool box = false;
    bool box_corner = false;
    bool box_fill = false;
    float box_thickness = 1.8f;
    float box_rounding = 0.f;
    bool skeleton = false;
    bool skeleton_joints = false;
    float skeleton_thickness = 1.8f;
    bool head_dot = false;
    bool head_halo = false;
    float head_circle_thickness = 1.6f;
    bool health_bar = false;
    bool armor_bar = false;
    bool health_value = false;
    bool armor_value = false;
    bool name = true;
    bool distance = true;
    bool weapon_name = false;
    bool weapon_ammo = false;
    bool text_outline = true;
    float text_outline_thickness = 1.f;
    bool snaplines = false;
    int snapline_position = 2;
    float snapline_thickness = 1.5f;
    bool look_direction = false;
    float eye_line_thickness = 1.6f;
    float look_direction_length = 90.f;
    bool chinese_hat = false;
    bool angel_wings = false;
    bool devil_horns = false;
    bool floating_crown = false;
    bool trails = false;
    float chinese_hat_scale = 1.f;
    float fun_effects_scale = 1.f;
    bool show_bots = false;
    bool visible_check = false;
    bool team_check = true;
    bool self_esp = true;
    bool hit_marker = false;


    bool aim_enabled = false;
    bool aim_visible_only = true;
    bool predict = false;
    float aim_smooth = 40.f;
    float aim_fov = 80.f;
    float max_distance = 300.f;
    float bullet_velocity = 80000.f;
    bool show_fps = true;


    float col_enemy[4] = {1.f, 0.25f, 0.25f, 1.f};
    float col_team[4] = {0.25f, 0.85f, 0.35f, 1.f};
    float col_box[4] = {1.f, 1.f, 1.f, 0.9f};
    float col_box_corner[4] = {0.f, 0.78f, 1.f, 1.f};
    float col_box_fill[4] = {1.f, 0.25f, 0.25f, 0.16f};
    float col_skeleton[4] = {0.95f, 0.85f, 0.35f, 1.f};
    float col_joints[4] = {0.27f, 0.9f, 0.37f, 1.f};
    float col_head[4] = {0.95f, 0.85f, 0.35f, 1.f};
    float col_halo[4] = {1.f, 0.89f, 0.54f, 0.9f};
    float col_health[4] = {0.25f, 0.86f, 0.35f, 1.f};
    float col_armor[4] = {0.27f, 0.59f, 1.f, 1.f};
    float col_name[4] = {1.f, 1.f, 1.f, 1.f};
    float col_local[4] = {0.2f, 0.9f, 1.f, 1.f};
    float col_distance[4] = {0.85f, 0.85f, 0.9f, 1.f};
    float col_weapon[4] = {0.85f, 0.85f, 0.9f, 1.f};
    float col_snaplines[4] = {0.83f, 0.69f, 0.22f, 0.55f};
    float col_trail[4] = {0.83f, 0.69f, 0.22f, 0.82f};
    float col_fun_effects[4] = {1.f, 0.72f, 0.18f, 0.95f};
    float col_look[4] = {0.83f, 0.69f, 0.22f, 0.86f};
    float col_text_outline[4] = {0.f, 0.f, 0.f, 1.f};
    float col_visible[4] = {0.27f, 0.9f, 0.37f, 1.f};
    float col_occluded[4] = {0.9f, 0.25f, 0.25f, 1.f};


    bool rgb_mode = false;
    bool visibility_colors = false;
};





struct Offsets {
    uintptr_t gworld = 0;
    uintptr_t gnames = 0;
    uintptr_t gengine = 0;
    uintptr_t process_event = 0;


    bool gworld_encoded = false;

    uint64_t gworld_mul = 0;
    uint64_t gworld_add = 0;

    uint64_t gworld_sub = 0;
    uint32_t gworld_rol = 0;
    uint64_t gworld_xor = 0;

    uintptr_t engine_game_viewport = 0;

    uintptr_t viewport_world = 0;
    uintptr_t viewport_game_instance = 0;

    uintptr_t world_persistent_level = 0;
    uintptr_t world_net_driver = 0;
    uintptr_t world_game_state = 0;
    uintptr_t world_levels = 0;
    uintptr_t world_owning_game_instance = 0;

    uintptr_t level_actor_cluster = 0;
    uintptr_t level_world_settings = 0;
    uintptr_t level_actors = 0;

    uintptr_t gi_local_players = 0;

    uintptr_t local_player_controller = 0;
    uintptr_t local_viewport_client = 0;

    uintptr_t pc_acknowledged_pawn = 0;
    uintptr_t pc_my_hud = 0;
    uintptr_t pc_player_camera_manager = 0;
    uintptr_t pc_spectator_pawn = 0;
    uintptr_t controller_pawn = 0;
    uintptr_t controller_player_state = 0;
    uintptr_t controller_character = 0;
    uintptr_t pcm_view_target = 0;


    uintptr_t pawn_player_state = 0;
    uintptr_t pawn_controller = 0;

    uintptr_t character_mesh = 0;

    uintptr_t actor_root_component = 0;

    uintptr_t scene_relative_location = 0;
    uintptr_t scene_relative_rotation = 0;
    uintptr_t scene_component_velocity = 0;

    uintptr_t pcm_camera_cache_private = 0;
    uintptr_t pcm_cam_location = 0;
    uintptr_t pcm_cam_rotation = 0;
    uintptr_t pcm_cam_fov = 0;

    uintptr_t game_state_player_array = 0;
    uintptr_t player_state_pawn_private = 0;
    uintptr_t fort_ps_current_health = 0;
    uintptr_t fort_pawn_current_weapon = 0;
    uintptr_t fort_ps_team_index = 0;

    const char* build = "";
    const char* cl = "";
};

struct FVectorD {
    double x = 0, y = 0, z = 0;
    bool finite() const {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
    }
};

struct FRotatorD {
    double pitch = 0, yaw = 0, roll = 0;
    bool finite() const {
        return std::isfinite(pitch) && std::isfinite(yaw) && std::isfinite(roll);
    }
};

struct ChainDiag {
    bool read_ok = false;
    size_t bytes_read = 0;
    uintptr_t address = 0;
    uintptr_t value = 0;
    bool canonical = false;
    const char* fail = nullptr;
};

struct Runtime {
    bool attached = false;
    bool visual_only = false;
    bool chain_ok = false;
    bool camera_ok = false;
    bool local_pawn_ok = false;
    bool w2s_ok = false;
    bool gi_match = false;

    uintptr_t base = 0;
    uint32_t pid = 0;
    uint64_t frames = 0;

    uintptr_t engine = 0;
    uintptr_t viewport = 0;
    uintptr_t world = 0;
    uintptr_t viewport_gi = 0;
    uintptr_t world_gi = 0;
    uintptr_t game_instance = 0;
    uintptr_t local_players_data = 0;
    int32_t local_players_count = 0;
    uintptr_t local_player = 0;
    uintptr_t player_controller = 0;
    uintptr_t local_pawn = 0;
    uintptr_t root_component = 0;
    uintptr_t camera_manager = 0;

    FVectorD local_pos{};
    FVectorD cam_loc{};
    FRotatorD cam_rot{};
    float cam_fov = 0.f;

    float screen_x = 0.f;
    float screen_y = 0.f;
    float distance_to_cam = 0.f;




    uint64_t camera_sample_ms = 0;
    uint64_t world_sample_ms = 0;
    uint64_t snapshot_timestamp_ms = 0;

    std::string process_name = "FortniteClient-Win64-Shipping.exe";
    std::string status = "idle";
    std::string last_fail;

    ChainDiag d_engine{}, d_viewport{}, d_world{}, d_gi{}, d_lp{}, d_pc{}, d_pcm{};

    struct Player {
        uintptr_t player_state = 0;
        uintptr_t pawn = 0;
        FVectorD position{};
        float distance_m = 0.f;
        uint8_t team_index = 0;
        bool team_known = false;
    };
    std::vector<Player> players;
    bool player_array_ok = false;
    bool player_data_ok = false;
    int32_t player_array_count = 0;
    int32_t player_array_capacity = 0;
    int32_t players_without_pawn = 0;
    int32_t players_dying = 0;
    int32_t players_out_of_range = 0;
    int32_t players_same_team = 0;
    int32_t players_invalid = 0;
    int32_t players_local = 0;
    std::string player_array_status = "A aguardar GameState";
};

extern Config config;
extern Offsets offsets;
extern Runtime runtime;



bool LoadOffsetsFromJson(const char* path = nullptr);
bool ReloadOffsets();

bool Attach();
void Detach();
void Tick();
void DrawESP();
void RunAim();
bool IsGameProcessAlive();
bool ValidateLiveOffsets();
const char* StatusText();
void FlushDiagnosticsToLog();

}
