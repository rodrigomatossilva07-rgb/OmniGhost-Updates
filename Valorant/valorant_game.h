#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "valorant_config.h"

namespace Valorant {

// Offsets relative to VALORANT-Win64-Shipping.exe image base unless noted.
// Values seed from public UE/Valorant layouts — load data/valorant_offsets.json to refresh.
struct Offsets {
    // Globals (RVA)
    uintptr_t uworld_state = 0x8C8A640;
    uintptr_t uworld_key = 0x38; // relative to uworld_state object
    uintptr_t fname_pool = 0x8AC0D80;

    // UWorld
    uintptr_t persistent_level = 0x38;
    uintptr_t game_state = 0x140;
    uintptr_t owning_game_instance = 0x1A0;
    uintptr_t levels = 0x158;

    // UGameInstance / ULocalPlayer
    uintptr_t local_players = 0x40;
    uintptr_t player_controller = 0x38;
    uintptr_t viewport_client = 0x78;

    // APlayerController
    uintptr_t acknowledged_pawn = 0x460;
    uintptr_t player_camera = 0x478;
    uintptr_t control_rotation = 0x440;

    // APawn / ShooterCharacter
    uintptr_t root_component = 0x230;
    uintptr_t player_state = 0x3F0;
    uintptr_t current_mesh = 0x430;
    uintptr_t damage_handler = 0xA10;
    uintptr_t inventory = 0x9B0;
    uintptr_t dormant = 0x100;

    // USceneComponent
    uintptr_t relative_location = 0x164;
    uintptr_t component_to_world = 0x250;

    // Mesh / bones
    uintptr_t bone_array = 0x5C0;
    uintptr_t bone_count = 0x5C8;

    // Damage / team
    uintptr_t current_health = 0x1B0; // on damage_handler
    uintptr_t max_health = 0x1B4;
    uintptr_t team_component = 0x628; // on player_state
    uintptr_t team_id = 0xF8;         // on team_component

    // Camera
    uintptr_t camera_cache = 0x510;
    uintptr_t camera_pov = 0x10; // within cache entry
    // POV: location 0, rotation 0xC, fov 0x18

    // ULevel actors
    uintptr_t actor_array = 0xA0;
    uintptr_t actor_count = 0xA8;

    bool loaded = false;
};

struct Player {
    uintptr_t actor = 0;
    uintptr_t pawn = 0;
    uintptr_t mesh = 0;
    int health = 0;
    int max_health = 100;
    int shield = 0;
    int team = 0;
    bool alive = false;
    bool is_local = false;
    bool dormant = false;
    float pos[3]{};
    float head[3]{};
    float velocity[3]{};
    float distance = 0.f;
    char name[64]{};
    char agent[32]{};
    // 0 head 1 neck 2 chest 3 pelvis 4-7 arms 8-11 legs
    float bones[16][3]{};
    bool bones_ok = false;
};

struct Runtime {
    bool attached = false;
    bool in_game = false;
    uintptr_t base = 0;
    uintptr_t uworld = 0;
    uintptr_t local_pawn = 0;
    int local_team = 0;
    int local_health = 100;
    float local_pos[3]{};
    float local_vel[3]{};
    float local_angles[3]{};
    bool local_scoped = false;
    float view_matrix[16]{};
    float cam_pos[3]{};
    float cam_rot[3]{};
    float cam_fov = 90.f;
    std::vector<Player> players;
    std::string status;
    std::string process_name = "VALORANT-Win64-Shipping.exe";
};

extern Offsets offsets;
extern Runtime runtime;
extern Config config;

bool Attach();
bool SoftProbeLobbyOffsets();
bool ValidateLiveOffsets();
bool IsGameProcessAlive();
void Detach();
void Tick(); // refresh players + camera
bool LoadOffsetsJson(const char* path);
bool SaveOffsetsJson(const char* path);
const char* StatusText();

} // namespace Valorant
