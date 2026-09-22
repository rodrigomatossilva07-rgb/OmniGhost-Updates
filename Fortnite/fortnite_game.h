#pragma once
#include <cstdint>
#include <string>
#include <cmath>

// Fortnite LocalPlayer-only ESP (lobby test) — read-only.
// Target: 42.00-CL-57316517 via GEngine plain pointer (no decrypt).
// Globals: GWorld/GNames from cheatoffsets 42.00; GEngine kept as prior plain RVA
// (cheatoffsets dump had GEngine=0 / not present).
namespace Fortnite {

struct Config {
    bool esp_enabled = true;
    bool show_local_marker = true;
    bool show_local_coords = true;
    bool show_camera_debug = true;
    bool show_debug_panel = true;
    bool box = false;
    bool skeleton = false;
    bool name = true;
    bool distance = true;
    bool health_bar = false;
    bool snaplines = false;
    bool team_check = true;
    bool self_esp = true;
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
    float col_skeleton[4] = {1.f, 1.f, 1.f, 0.85f};
    float col_name[4] = {1.f, 1.f, 1.f, 1.f};
    float col_local[4] = {0.2f, 0.9f, 1.f, 1.f};
};

// GWorld is ENCODED (see gworld_crypto in data/fortnite_offsets.json).
// Decrypt (current dump): world = encoded * world_mul + world_add (uint64 wrap).
// Legacy dumps may still use rotl64(encoded - sub, rol) ^ xor_key.
// Fallback: GEngine -> Viewport -> World.
struct Offsets {
    uintptr_t gworld = 0x1B2C5BA0;   // encoded slot RVA
    uintptr_t gnames = 0;
    uintptr_t gengine = 0;
    uintptr_t process_event = 0;

    // GWorld crypto
    bool gworld_encoded = true;
    // Method: "mul_add" (current) or "rot_xor" (legacy)
    uint64_t gworld_mul = 0x6501B96661E130DDULL;
    uint64_t gworld_add = 0x79D95BD19230E74DULL;
    // Legacy rot_xor fields (kept for older JSON snapshots)
    uint64_t gworld_sub = 0;
    uint32_t gworld_rol = 0;
    uint64_t gworld_xor = 0;

    uintptr_t engine_game_viewport = 0xB70;

    uintptr_t viewport_world = 0x78;
    uintptr_t viewport_game_instance = 0x80;

    uintptr_t world_persistent_level = 0x38;
    uintptr_t world_net_driver = 0x40;
    uintptr_t world_game_state = 0x1C0;
    uintptr_t world_levels = 0x1D8;
    uintptr_t world_owning_game_instance = 0x238;

    uintptr_t level_actor_cluster = 0x88;
    uintptr_t level_world_settings = 0x2C0;
    uintptr_t level_actors = 0x28;

    uintptr_t gi_local_players = 0x38;

    uintptr_t local_player_controller = 0x30;
    uintptr_t local_viewport_client = 0x78;

    uintptr_t pc_acknowledged_pawn = 0x318;
    uintptr_t pc_my_hud = 0x320;
    uintptr_t pc_player_camera_manager = 0x328;

    uintptr_t pawn_player_state = 0x290;
    uintptr_t pawn_controller = 0x2A0;

    uintptr_t character_mesh = 0x2F0;

    uintptr_t actor_root_component = 0x1B0;

    uintptr_t scene_relative_location = 0x140;
    uintptr_t scene_relative_rotation = 0x158;
    uintptr_t scene_component_velocity = 0x188;

    uintptr_t pcm_camera_cache_private = 0x1590;
    uintptr_t pcm_cam_location = 0x15A0;
    uintptr_t pcm_cam_rotation = 0x15B8;
    uintptr_t pcm_cam_fov = 0x15D0;

    uintptr_t game_state_player_array = 0x288;
    uintptr_t fort_pawn_current_weapon = 0x9D0;
    uintptr_t fort_ps_team_index = 0xF69;

    const char* build = "user-dump-2026-09-22";
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
    bool visual_only = false; // false once local chain is active
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

    std::string process_name = "FortniteClient-Win64-Shipping.exe";
    std::string status = "idle";
    std::string last_fail;

    ChainDiag d_engine{}, d_viewport{}, d_world{}, d_gi{}, d_lp{}, d_pc{}, d_pcm{};
};

extern Config config;
extern Offsets offsets;
extern Runtime runtime;

// Loads offsets from data/fortnite_offsets.json (Tester) or embedded RCDATA (Release).
// Edit ONLY data/fortnite_offsets.json to update after a game patch, then rebuild Release.
bool LoadOffsetsFromJson(const char* path = nullptr);
bool ReloadOffsets();

bool Attach();
void Detach();
void Tick();
void DrawESP(); // LocalPlayer marker only
void RunAim();  // no-op
bool IsGameProcessAlive();
bool ValidateLiveOffsets();
const char* StatusText();
void FlushDiagnosticsToLog();

} // namespace Fortnite
