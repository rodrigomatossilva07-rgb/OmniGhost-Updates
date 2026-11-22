#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace Warzone {

struct Config {};

struct Player {
    uintptr_t address = 0;
    float pos[3]{};
    float head[3]{};
    float distance = 0.0f;
    float health = 0.0f;
    float armor = 0.0f;
    bool alive = false;
    bool visible = false;
    int team = 0;
    int stance = 0;
    int weapon = 0;
    char name[64]{};
};

struct Runtime {
    std::uint32_t pid = 0;
    uintptr_t game_base = 0;
    uintptr_t ref_def = 0;
    uintptr_t name_array = 0;
    uintptr_t camera_base = 0;
    uintptr_t entity_list = 0;
    uintptr_t local_player = 0;
    float local_pos[3]{};
    float cam_pos[3]{};
    float view_matrix[16]{};
    std::vector<Player> players;
    std::uint32_t player_count = 0;
    std::uint32_t valid_player_count = 0;
    std::uint32_t entity_count = 0;
    std::uint64_t frames = 0;
    std::uint64_t read_failures = 0;
    std::uint64_t world_sample_ms = 0;
    std::uint64_t camera_sample_ms = 0;
    std::uint64_t last_camera_sample_ms = 0;
    std::uint64_t last_world_sample_ms = 0;
    std::uint64_t last_probe_ms = 0;
    std::uint64_t last_acquisition_attempt_ms = 0;
    bool device_ok = false;
    bool process_ok = false;
    bool modules_ok = false;
    bool offset_catalog_ok = false;
    bool entity_source_ok = false;
    bool camera_ok = false;
    bool acquisition_ok = false;
    std::string status = "Warzone inativo";
    std::string collection_status = "Nao validada";
    std::string camera_status = "Nao validada";
    std::string transform_status = "Nao validada";
};

extern Runtime runtime;
extern std::atomic_bool ready;

bool Attach();
void Shutdown() noexcept;
void Tick();
bool IsGameProcessAlive();
bool ValidateLiveOffsets();
const char* StatusLine();

} // namespace Warzone
