#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace Apex {

struct Player {
    uintptr_t address = 0;
    float pos[3]{};
    float head[3]{};
    float distance = 0.0f;
    float health = 0.0f;
    float shield = 0.0f;
    bool alive = false;
    bool visible = false;
    int team = 0;
    int life_state = 0;
    char name[64]{};
    char weapon[32]{};
};

struct Runtime {
    std::uint32_t pid = 0;
    uintptr_t game_base = 0;
    uintptr_t client_dll = 0;
    uintptr_t engine_dll = 0;
    uintptr_t entity_list = 0;
    uintptr_t local_player = 0;
    uintptr_t view_render = 0;
    uintptr_t client_state = 0;
    uintptr_t view_matrix = 0;
    uintptr_t camera_origin = 0;
    uintptr_t view_angles = 0;
    uintptr_t name_list = 0;
    uintptr_t weapon_names = 0;
    float local_pos[3]{};
    float cam_pos[3]{};
    float view_matrix[16]{};
    std::vector<Player> players;
    std::uint32_t player_count = 0;
    std::uint32_t entity_count = 0;
    std::uint64_t frames = 0;
    std::uint64_t read_failures = 0;
    std::uint64_t world_sample_ms = 0;
    std::uint64_t camera_sample_ms = 0;
    bool device_ok = false;
    bool process_ok = false;
    bool modules_ok = false;
    bool offset_catalog_ok = false;
    bool entity_source_ok = false;
    bool camera_ok = false;
    bool acquisition_ok = false;
    std::string status = "Apex inativo";
    std::string collection_status = "Nao validada";
    std::string camera_status = "Nao validada";
};

extern Runtime runtime;
extern std::atomic_bool ready;

bool Attach();
void Shutdown() noexcept;
void Tick();
bool IsGameProcessAlive();
bool ValidateLiveOffsets();
const char* StatusLine();

} // namespace Apex