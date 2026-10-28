#pragma once

#include "rust_config.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace Rust {

struct Player {
    uintptr_t address = 0;
    float feet[3]{};
    float head[3]{};
    float distance = 0.0f;
    float health = 0.0f;
    bool alive = false;
    bool local = false;
};

struct Runtime {
    std::uint32_t pid = 0;
    uintptr_t game_assembly = 0;
    uintptr_t unity_player = 0;
    uintptr_t entity_realm = 0;
    uintptr_t entity_list = 0;
    uintptr_t local_player = 0;
    uintptr_t camera = 0;
    uintptr_t camera_matrix_offset = 0;
    std::uint32_t entity_count = 0;
    std::uint32_t valid_player_count = 0;
    std::uint32_t transform_failures = 0;
    std::string collection_status = "Não validada";
    std::string transform_status = "Não validado";
    std::string camera_status = "Não validada";
    float view_matrix[16]{};
    std::vector<Player> players;
    std::uint64_t last_probe_ms = 0;
    std::uint64_t last_acquisition_attempt_ms = 0;
    std::uint64_t last_world_sample_ms = 0;
    std::uint64_t last_camera_sample_ms = 0;
    std::uint64_t frames = 0;
    std::uint64_t read_failures = 0;
    bool device_ok = false;
    bool process_ok = false;
    bool modules_ok = false;
    bool offset_catalog_ok = false;
    bool entity_source_ok = false;
    bool camera_ok = false;
    bool acquisition_ok = false;
    std::string status = "Rust inativo";
};

extern Config config;
extern Runtime runtime;
extern std::atomic_bool ready;

bool Attach();
void Shutdown() noexcept;
void Tick();
bool IsGameProcessAlive();
bool ValidateLiveOffsets();
const char* StatusLine();

}
