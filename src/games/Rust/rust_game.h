#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include "rust_config.h"

namespace Rust {

struct Offsets {
    uintptr_t BaseNetworkable_TypeInfo = 0;
    uintptr_t MainCamera_TypeInfo = 0;
    uintptr_t Il2CppHandle_RVA = 0;

    uintptr_t staticFields = 0xB8;
    uintptr_t clientEntities = 0x28;
    uintptr_t entityList = 0x10;
    uintptr_t buffer = 0x10;
    uintptr_t bufferSize = 0x18;

    uintptr_t playerModel = 0x2F0;
    uintptr_t playerFlags = 0x6D8;
    uintptr_t displayName = 0x520;
    uintptr_t currentTeam = 0x558;
    uintptr_t clActiveItem = 0x350;
    uintptr_t inventory = 0x3A0;
    uintptr_t playerInput = 0x6F0;
    uintptr_t playerEyes = 0x718;
    uintptr_t movement = 0x348;
    uintptr_t visiblePlayerList = 0x600;

    uintptr_t lifestate = 0x2A8;
    uintptr_t health = 0x2B0;
    uintptr_t maxHealth = 0x240;
    uintptr_t baseModel = 0x1B8;

    uintptr_t modelPosition = 0x1F8;
    uintptr_t modelVelocity = 0x21C;
    uintptr_t isNpc = 0x3DD;

    uintptr_t viewMatrix = 0x30C;
    uintptr_t cameraStatic = 0xB8;
    uintptr_t cameraWrapper = 0x78;
    uintptr_t cameraParent = 0x10;
    uintptr_t cameraObject = 0x10;

    uintptr_t boneTransforms = 0x50;

    bool loaded = false;
    char source[48] = "none";
    char version[32] = {};
};

struct Player {
    uintptr_t address = 0;
    bool valid = false;
    bool is_local = false;
    bool sleeping = false;
    bool wounded = false;
    bool npc = false;
    int team_id = 0;
    float health = 0.f;
    float max_health = 100.f;
    float pos[3]{};
    float head[3]{};
    float distance = 0.f;
    char name[64]{};
};

struct Runtime {
    bool attached = false;
    bool ready = false;
    bool matrix_ok = false;
    // Avoid a Windows SDK dependency in a shared runtime data structure.
    uint32_t pid = 0;
    uintptr_t game_assembly = 0;
    uintptr_t dtb = 0;
    float view_matrix[16]{};
    float local_pos[3]{};
    uintptr_t local_player = 0;
    int entity_count = 0;
    int player_count = 0;
    uint64_t frames = 0;
    char status[160] = "Rust offline";
    char offsets_version[32] = {};
};

extern Offsets offsets;
extern Runtime runtime;
extern bool ready;
extern char status[160];

bool LoadOffsetsFromJson(const char* explicit_path = nullptr);
bool Attach();
void Shutdown();
void RunFrame();
bool IsAlive();
bool ValidateOffsets();
const char* StatusText();

// Entity snapshot for ESP/UI
std::vector<Player> SnapshotPlayers();

} // namespace Rust
