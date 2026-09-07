#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include "rust_config.h"

namespace Rust {

struct Offsets {
    // TypeInfo RVAs (GameAssembly.dll) - data/rust_offsets.json Aug 2026
    uintptr_t BasePlayer_TypeInfo = 0;
    uintptr_t LocalPlayer_TypeInfo = 0;
    uintptr_t MainCamera_TypeInfo = 0x115BB318;
    uintptr_t BaseNetworkable_TypeInfo = 0x115B1A70;
    uintptr_t TOD_Sky_TypeInfo = 0;
    uintptr_t OcclusionCulling_TypeInfo = 0;
    uintptr_t ConVar_Admin_TypeInfo = 0;
    uintptr_t ConVar_Graphics_TypeInfo = 0;
    uintptr_t Il2CppHandle_RVA = 0x11A88F30;

    uintptr_t playerFlags = 0x6D0;
    uintptr_t modelState = 0; // not in current dump - resolve via flags/model
    uintptr_t movement = 0x510;
    uintptr_t inventory = 0x308;
    uintptr_t clActiveItem = 0x580;
    uintptr_t playerModel = 0x2E8;
    uintptr_t displayName = 0x390;
    uintptr_t currentTeam = 0x550;
    uintptr_t userId = 0;
    uintptr_t playerEyes = 0x708;
    uintptr_t heldEntity = 0x5D0;
    uintptr_t visiblePlayerList = 0x10;

    uintptr_t _health = 0x2B4;
    uintptr_t _maxHealth = 0x2B8;
    uintptr_t lifestate = 0x2A8;

    uintptr_t modelPosition = 0x148; // player_model.position
    uintptr_t boneTransforms = 0x50;
    uintptr_t boneCount = 0;
    uintptr_t isNpc = 0;
    uintptr_t newVelocity = 0x164; // player_model.velocity

    uintptr_t groundAngle = 0xC4;
    uintptr_t groundAngleNew = 0xC8;
    uintptr_t maxAngleWalking = 0xD0;

    uintptr_t staticFields = 0xB8;
    uintptr_t staticFieldsAlt = 0x90;
    uintptr_t mainCamera = 0x0;
    uintptr_t cameraGameObject = 0x30;
    uintptr_t viewMatrix = 0x2FC;

    uintptr_t bufferList = 0x10;
    uintptr_t buffer = 0x10;
    uintptr_t bufferSize = 0x18;
    uintptr_t destroyed = 0x40;

    uintptr_t containerBelt = 0x30;
    uintptr_t itemList = 0x48;
    uintptr_t itemListContents = 0x10;
    uintptr_t itemListSize = 0x18;

    uintptr_t recoil = 0x2D8;
    uintptr_t recoilYawMin = 0x18;
    uintptr_t recoilYawMax = 0x1C;
    uintptr_t recoilPitchMin = 0x20;
    uintptr_t recoilPitchMax = 0x24;
    uintptr_t successFraction = 0x3A0;

    bool loaded = false;
    char source[48] = "none"; // embedded-json | embedded-seed | json | fallback | none
};

enum class PlayerFlags : uint32_t {
    IsAdmin = 4,
    Sleeping = 16,
    Spectating = 32,
    Wounded = 64,
    Connected = 256,
    Aiming = 16384,
    SafeZone = 131072,
};

// Snapshot of one BasePlayer as consumed by ESP / aim / UI tables.
struct Player {
    uintptr_t address = 0;      // BasePlayer*
    uintptr_t model = 0;        // BasePlayer.playerModel
    bool valid = false;
    bool is_local = false;
    bool destroyed = false;
    bool sleeping = false;
    bool wounded = false;
    bool npc = false;
    bool aiming = false;
    bool safezone = false;
    int team_id = 0;
    uint32_t flags = 0;
    float health = 0.f;
    float max_health = 100.f;
    float pos[3]{};
    float head[3]{};
    float chest[3]{};
    float velocity[3]{};
    float distance = 0.f;
    char name[64]{};
    // World-space bone positions filled by the entity cache (see rust_entities.cpp
    // for the slot layout: 0 head 1 neck 2 chest 3 pelvis 4/5 shoulders 6/7 hands
    // 8/9 hips 10/11 knees 12/13 feet).
    float bones[16][3]{};
    int bone_count = 0;
    bool bones_ok = false;
    uintptr_t bone_array = 0;   // Transform* array used for per-frame refresh
    uint32_t bone_array_count = 0;
};

enum class WorldKind : int {
    Unknown = 0,
    Ore,
    Crate,
    Stash,
    TC,
    Turret,
    Vehicle,
    Animal,
    AirDrop,
    Collectable,
    ItemDrop,
    Corpse,
    Generic,
};

struct WorldEntity {
    uintptr_t address = 0;
    uintptr_t transform = 0;
    bool valid = false;
    WorldKind kind = WorldKind::Unknown;
    float pos[3]{};
    float distance = 0.f;
    char name[48]{};
};

struct Runtime {
    uintptr_t game_assembly = 0;
    uintptr_t local_player = 0;
    int local_team = 0;
    int local_health = 100;
    float local_pos[3]{};
    float local_vel[3]{};
    float local_angles[3]{};
    bool local_scoped = false;
    float view_matrix[16]{};
    std::vector<Player> players;
    std::vector<WorldEntity> world_entities;
    int player_count = 0;
    int world_count = 0;
    bool in_game = false;
    bool matrix_ok = false;
    bool list_ok = false;
    bool self_test_ok = false;
    uint64_t frames = 0;
    uint64_t last_cache_ms = 0;
    uint64_t last_pos_ms = 0;
    uint64_t last_world_ms = 0;
    char status[192] = "idle";
};

// ── Backend state model (separate axes: engine / device / process / runtime) ──

enum class BackendState : int {
    Idle = 0,      // nothing started
    Starting,      // worker opening device / attaching
    Waiting,       // device OK, waiting for process / ProcInfo / runtime
    Ready,         // attached and runtime resolved
    Error,         // hard failure (device / dependencies)
    Cancelled,     // user cancelled the operation
};

enum class BackendReason : int {
    None = 0,
    DeviceOpenFailed,
    DeviceDataPathFailed,
    VmmInitializationFailed,
    VmmSessionRecoveryFailed,
    PluginInitializationFailed,
    ProcessNotFound,
    ProcInfoGenerating,
    ProcInfoTimeout,
    ProcInfoStuck,
    ProcInfoCancelled,
    MappingUnavailable,
    ModuleValidationFailed,
    RuntimeUnavailable,
    OperationSuperseded,
    DependencyMismatch,
    BackendBusy,
};

enum class DevicePhase : int {
    Closed = 0,
    Opening,
    Open,
    Lost,
};

enum class ProcessPhase : int {
    Detached = 0,
    Searching,
    Found,
    Attaching,
    Attached,
    Waiting,
};

enum class RuntimePhase : int {
    Idle = 0,
    Resolving,
    Ready,
    Unavailable,
};

struct BackendSnapshot {
    BackendState state = BackendState::Idle;
    BackendReason reason = BackendReason::None;
    DevicePhase device = DevicePhase::Closed;
    ProcessPhase process = ProcessPhase::Detached;
    RuntimePhase runtime = RuntimePhase::Idle;
    bool busy = false;
    uint64_t operation_id = 0;
    uint64_t operation_elapsed_ms = 0;
    char operation[48]{};
    uint64_t session_generation = 0;
    uint64_t process_generation = 0;
    int procinfo_state = 0;
    int procinfo_progress = -1;
    bool procinfo_recovery_recommended = false;
    // Diagnostic extras surfaced by Misc/Debug pages (optional, default 0/false).
    uint64_t procinfo_dtb_size = 0;
    bool vmm_maintenance = false;
    uint64_t blocked_data_calls = 0;
};

extern Offsets offsets;
extern Config config;
extern Runtime runtime;
extern bool ready;
extern std::string status;

extern std::atomic<int> device_phase;
extern std::atomic<int> process_phase;
extern std::atomic<int> runtime_phase;
extern std::atomic<int> backend_state;
extern std::atomic_bool backend_busy;

// ── Lifecycle ─────────────────────────────────────────────────────────────────
// Synchronous attach: opens the DMA device, waits for RustClient.exe and resolves
// GameAssembly.dll. Returns false on hard failure or when cancelled; a Waiting
// result leaves backend_state == Waiting and ready == false.
bool Attach();
// Starts Attach() on the backend worker thread. Returns false when busy.
bool StartBackendAsync();
// Soft process rebind (FixCr3) without reopening the FPGA; rebuilds the VMM
// session only when the physical data path fails or ProcInfo stays at 0%.
bool ReinitDma();
bool ReinitDmaAsync();
bool ReinitDmaCore();
void CancelBackendOperation();
void Shutdown();
void RunFrame();
bool IsGameProcessAlive();
// Sleeps in small slices; returns false as soon as cancellation is requested.
bool SleepCancelable(unsigned long ms);
uint64_t BeginBackendOperation(const char* name);
void EndBackendOperation(uint64_t id, const char* outcome);
BackendSnapshot GetBackendSnapshot();
int64_t MonotonicMilliseconds();

const char* BackendStateName();
const char* BackendReasonName(BackendReason reason);
const char* DevicePhaseName();
const char* ProcessPhaseName();
const char* RuntimePhaseName();

// ── Offsets / validation ──────────────────────────────────────────────────────
bool LoadOffsetsFromJson(const char* path);
void ApplyEmbeddedDefaults();
bool ReloadOffsets();
bool SelfTest();
bool SoftProbeLobbyOffsets();
bool ValidateLiveOffsets();
void DeepProbeChains();

} // namespace Rust
