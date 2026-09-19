#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include "warzone_config.h"

namespace Warzone {

struct Offsets {
    // globals / module RVAs
    uintptr_t timestamp = 0;
    uintptr_t ref_def = 0;
    uintptr_t name_array = 0;
    uintptr_t camera_base = 0;
    uintptr_t game_mode = 0;
    uintptr_t view_matrix = 0;
    uintptr_t entity_list = 0;
    uintptr_t entity_size = 0;
    uintptr_t max_player_count = 0;
    uintptr_t loot_ptr = 0;
    uintptr_t distribute = 0;
    uintptr_t lobby_data = 0;
    uintptr_t scoreboard = 0;

    // decrypt RVAs
    uintptr_t client_info_enc = 0;
    uintptr_t client_info_key = 0;
    uintptr_t client_base_enc = 0;
    uintptr_t client_base_key = 0;
    uintptr_t bone_enc = 0;
    uintptr_t bone_key = 0;
    uintptr_t stub_client_info = 0;
    uintptr_t stub_client_base = 0;
    uintptr_t stub_bone_base = 0;

    // camera
    uintptr_t camera_pos = 0;
    uintptr_t camera_pos_enc = 0;
    uintptr_t camera_pos_key = 0;

    // local (relative to client_info)
    uintptr_t local_index_off = 0;   // was local_index in old code
    uintptr_t local_index_pos = 0;
    uintptr_t visible_client_bits = 0;
    uintptr_t recoil = 0;

    // player struct
    uintptr_t player_size = 0;
    uintptr_t player_valid = 0;
    uintptr_t player_pos = 0;
    uintptr_t player_pos_data = 0;
    uintptr_t player_team = 0;
    uintptr_t player_stance = 0;
    uintptr_t player_health = 0;
    uintptr_t player_weapon_index = 0;

    // bone
    uintptr_t bone_base_pos = 0;
    uintptr_t bone_size = 0;
    uintptr_t bone_offset = 0;
    int bone_head = 7;
    int bone_chest = 5;

    // name_array entry
    uintptr_t name_array_pos = 0;
    uintptr_t name_entry_size = 0;
    uintptr_t name_entry_name = 0;
    uintptr_t name_entry_health = 0;
    uintptr_t name_entry_alive = 0;

    // legacy aliases kept for LoadOffsets compatibility
    uintptr_t client_info = 0;
    uintptr_t client_base = 0;
    uintptr_t bone_base = 0;
    uintptr_t local_index = 0;
    uintptr_t ref_def_ptr = 0;

    bool loaded = false;
    char source[48] = "none";
};

struct Player {
    uintptr_t address = 0;
    float pos[3]{};
    float head[3]{};
    float chest[3]{};
    float velocity[3]{};
    char name[64]{};
    float distance = 0.f;
    float health = 100.f;
    int team = 0;
    int index = -1;
    bool downed = false;
    bool ai = false;
    bool is_local = false;
    bool valid = false;
    bool alive = false;
    // 0=stand 1=crouch 2=prone (CoD-style). Aim/ESP head height follows this live.
    int stance = 0;
};

struct Runtime {
    uintptr_t module_base = 0;
    uintptr_t local_player = 0;
    int local_team = 0;
    int local_index = -1;
    int local_health = 100;
    float local_pos[3]{};
    float local_vel[3]{};
    float local_angles[3]{};
    bool local_scoped = false;
    float view_matrix[16]{};
    std::vector<Player> players;
    int player_count = 0;
    uint64_t frames = 0;
    bool in_game = false;
    bool matrix_ok = false;
    bool list_ok = false;
    bool decrypt_needed = false;
    bool decrypt_ok = false;
    uint64_t last_bind_attempt_ms = 0;
    int bind_fail_count = 0;
    char status[192] = "Warzone idle";
    char decrypt_detail[160] = "";
};

extern Offsets offsets;
extern Runtime runtime;
extern Config config;
extern bool ready;
extern std::string status;
extern std::atomic_bool backend_busy;

bool Attach();
bool SoftProbeLobbyOffsets();
bool ValidateLiveOffsets();
void Shutdown();
void RunFrame();
bool IsGameProcessAlive();
bool LoadOffsetsFromJson(const char* path = nullptr);
bool ReloadOffsets();
bool InitializeMenuShell();
const char* StatusLine();

} // namespace Warzone
