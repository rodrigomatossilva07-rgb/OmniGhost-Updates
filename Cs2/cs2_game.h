#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "config/cs2_config.h"
#include "gameplay/snapshot_exchange.h"

namespace CS2 {

struct Offsets {
    uintptr_t dwEntityList = 0;
    uintptr_t dwLocalPlayerPawn = 0;
    uintptr_t dwLocalPlayerController = 0;
    uintptr_t dwViewMatrix = 0;
    uintptr_t dwViewAngles = 0;
    uintptr_t dwGlobalVars = 0;
    uintptr_t dwBuildNumber = 0;
    uintptr_t dwPlantedC4 = 0;

    uintptr_t m_iHealth = 0x34C;
    uintptr_t m_iTeamNum = 0x3E7;
    uintptr_t m_pGameSceneNode = 0x330;
    uintptr_t m_fFlags = 0x3F4;
    uintptr_t m_vOldOrigin = 0x13B8;
    uintptr_t m_hPlayerPawn = 0x914;
    uintptr_t m_hObserverPawn = 0x918;
    uintptr_t m_pObserverServices = 0x1220; // C_CSPlayerPawn → CPlayer_ObserverServices*
    uintptr_t m_hObserverTarget = 0x4C;   // CPlayer_ObserverServices
    uintptr_t m_iPawnHealth = 0x920;
    uintptr_t m_iPawnArmor = 0x924;
    uintptr_t m_iszPlayerName = 0x6F4;
    uintptr_t m_sSanitizedPlayerName = 0x868;
    uintptr_t m_steamID = 0x780;
    uintptr_t m_bPawnIsAlive = 0x91C;
    // C_CSPlayerPawn fields for build 14174
    uintptr_t m_ArmorValue = 0x1CA4;
    uintptr_t m_angEyeAngles = 0x3350;
    uintptr_t m_iIDEntIndex = 0x342C;
    uintptr_t m_vecAbsOrigin = 0xC8;
    uintptr_t m_vecVelocity = 0x1D8;
    uintptr_t m_bDormant = 0x103;
    uintptr_t m_modelState = 0x140;
    uintptr_t BoneArray = 0x1C0;
    uintptr_t m_pClippingWeapon = 0x13A0;
    uintptr_t m_bIsScoped = 0x1C78;
    // EntitySpottedState_t on C_CSPlayerPawn (cheatoffsets ~2026-08)
    uintptr_t m_entitySpottedState = 0x1C60;
    uintptr_t m_bSpotted = 0x8; // within EntitySpottedState_t
    uintptr_t m_flFlashDuration = 0x1428;
    // Optional recoil fields. They are refreshed from the schema dump when
    // available; zero keeps RCS disabled safely on incompatible builds.
    uintptr_t m_iShotsFired = 0;
    uintptr_t m_aimPunchAngle = 0;
    // Current CS2 builds expose aim punch through a pawn component. Keep the
    // legacy direct angle for compatibility with older offset dumps.
    // C_PlantedC4 fields for build 14174
    uintptr_t m_bBombTicking = 0x11A0;
    uintptr_t m_flC4Blow = 0x11D0;
    uintptr_t m_flTimerLength = 0x11D8;
    uintptr_t m_bBombDefused = 0x11F4;
    uintptr_t m_bBeingDefused = 0x11DC;
    uintptr_t m_flDefuseCountDown = 0x11F0;
    uintptr_t m_hBombDefuser = 0x11F8;
    // Weapon chain
    uintptr_t m_pWeaponServices = 0x1208;
    uintptr_t m_pItemServices = 0x1210;
    uintptr_t m_bHasDefuser = 0x48;
    uintptr_t m_hActiveWeapon = 0x60;
    uintptr_t m_AttributeManager = 0x1200;
    uintptr_t m_Item = 0x50;
    uintptr_t m_iItemDefinitionIndex = 0x1BA;
    uintptr_t m_iClip1 = 0x18D0;            // C_BasePlayerWeapon (schema may override)
    uintptr_t m_pReserveAmmo = 0x18D8;      // optional
    uintptr_t m_iAccount = 0x8F0;           // CCSPlayerController
    uintptr_t m_bIsDefusing = 0x1C9A;       // C_CSPlayerPawn optional
    bool loaded = false;

    bool Validate(std::string* reason = nullptr) const noexcept;
};

enum class BoneSlot : std::size_t {
    Head, Neck, SpineUpper, SpineMiddle, SpineLower, Pelvis,
    ClavicleLeft, ShoulderLeft, ElbowLeft, HandLeft,
    ClavicleRight, ShoulderRight, ElbowRight, HandRight,
    HipLeft, KneeLeft, AnkleLeft, HipRight, KneeRight, AnkleRight,
    Count
};

inline constexpr std::size_t kBoneSlotCount =
    static_cast<std::size_t>(BoneSlot::Count);

struct Player {
    uintptr_t controller = 0;
    uintptr_t pawn = 0;
    uintptr_t scene = 0;
    uintptr_t bone_base = 0;
    uint8_t bone_layout = 0;
    int health = 0;
    int armor = 0;
    int team = 0;
    bool alive = false;
    bool is_local = false;
    bool is_bot = false;
    bool is_scoped = false;
    bool is_flashed = false;
    bool is_spectator = false;
    float pos[3]{};
    float velocity[3]{};
    float view_yaw = 0.f;
    float head[3]{};
    char name[64]{};
    char weapon[32]{};
    int weapon_def = 0;
    float distance = 0.f;
    // Expanded skeleton slots (close-range detail + LOD-safe far):
    //  0 head  1 neck  2 spine2  3 spine1  4 spine0  5 pelvis
    //  6 clav_l  7 shoulder_l  8 elbow_l  9 hand_l
    // 10 clav_r 11 shoulder_r 12 elbow_r 13 hand_r
    // 14 hip_l  15 knee_l  16 ankle_l
    // 17 hip_r  18 knee_r  19 ankle_r
    float bones[kBoneSlotCount][3]{};
    bool bones_ok = false;          // at least head/neck/chest/stomach are valid
    bool full_bones_ok = false;     // complete 20-slot pose is valid for skeleton/body trigger
    bool spotted = true; // m_bSpotted (EntitySpottedState_t)
    int ent_index = 0;
    uint64_t steam_id = 0;
    // Extended status (flags / ammo / movement)
    int ammo_clip = -1;
    int ammo_reserve = -1;
    int money = -1;
    bool has_defuser = false;
    bool is_defusing = false;
    bool is_moving = false;
    float move_speed = 0.f;
    uint64_t last_shot_ms = 0;
    int shots_fired = 0;
};

struct BombState {
    bool planted = false;
    bool defused = false;
    bool defusing = false;
    float blow_time = 0.f;
    float defuse_time = 0.f;
    uint32_t defuser_handle = 0;
    char defuser_name[64]{};
    float pos[3]{};
    uint64_t sample_timestamp_ms = 0;
};

struct Runtime {
    uintptr_t client_base = 0;
    uintptr_t engine_base = 0;
    uintptr_t entity_list_addr = 0;
    uintptr_t entity_list_entry = 0;
    uintptr_t local_pawn = 0;
    uintptr_t local_controller = 0;
    int local_team = 0;
    int local_health = 100;
    float local_pos[3]{};
    float local_vel[3]{};
    float local_angles[3]{};
    float local_view_yaw = 0.f;
    bool local_scoped = false;
    bool local_has_defuser = false;
    int local_crosshair_entity = 0;
    int local_shots_fired = 0;
    uint64_t local_last_shot_ms = 0;
    float view_matrix[16]{};
    char map_name[64]{};
    bool in_match = false;
    uintptr_t controller_stride = 0x70;
    uintptr_t pawn_stride = 0x70;
    BombState bomb{};
    int player_count = 0;
    int enemy_count = 0;
    int controller_count = 0;
    int pawn_count = 0;
    int spectator_count = 0;
    uintptr_t spectator_target = 0;
    char spectator_target_name[64]{};
    uint32_t build_number = 0;
    uint64_t frames = 0;
    uint64_t read_fails = 0;
    uint64_t snapshot_timestamp_ms = 0;
    uint64_t snapshot_drops = 0;
    float acquisition_hz = 0.f;
    float acquisition_ms = 0.f;
    float processing_ms = 0.f;
    float snapshot_interval_ms = 0.f;
    float fps = 0.f;
    int entity_cap_used = 0;
    bool offsets_self_test_ok = false;
    std::vector<Player> players;
    std::vector<Player> spectators;
};

using RuntimeSnapshotLease = OmniGhost::Gameplay::SnapshotExchange<Runtime, 4>::ReadLease;

struct CameraSnapshot {
    float view_matrix[16]{};
    uint64_t timestamp_ms = 0;
};
using CameraSnapshotLease = OmniGhost::Gameplay::SnapshotExchange<CameraSnapshot>::ReadLease;

// A tiny, high-priority life-state lane. It is published immediately after
// the existing core health scatter, before optional bones and metadata.
struct LivenessSample {
    uintptr_t pawn = 0;
    bool alive = false;
};
struct LivenessSnapshot {
    std::array<LivenessSample, 64> players{};
    uint32_t count = 0;
    uint64_t timestamp_ms = 0;
};
using LivenessSnapshotLease = OmniGhost::Gameplay::SnapshotExchange<LivenessSnapshot>::ReadLease;

// Lightweight motion lane used only to keep the visual anchored between full
// entity/bone scans.  Fixed storage avoids allocations in the fast thread.
struct MotionSample {
    uintptr_t pawn = 0;
    float pos[3]{};
};
struct MotionSnapshot {
    std::array<MotionSample, 64> players{};
    uint32_t count = 0;
    uint64_t timestamp_ms = 0;
};
using MotionSnapshotLease = OmniGhost::Gameplay::SnapshotExchange<MotionSnapshot>::ReadLease;

extern Offsets offsets;
extern Config config;
extern Runtime runtime;
extern bool ready;
extern std::string status;
extern std::string offsets_source;

bool ResolveDataPath(std::string& out_dir);
bool LoadOffsetsFromJson(const char* path = nullptr);
bool WaitForProcess(int timeout_sec = 120);
bool Attach();
bool RecoverCriticalOffsets();
void RunFrame();
void EnsureAcquisitionStarted();
void StopAcquisition();
void SubmitAcquisitionConfig(const Config& next) noexcept;
[[nodiscard]] RuntimeSnapshotLease AcquireRuntimeSnapshot();
[[nodiscard]] CameraSnapshotLease AcquireCameraSnapshot();
[[nodiscard]] LivenessSnapshotLease AcquireLivenessSnapshot();
[[nodiscard]] MotionSnapshotLease AcquireMotionSnapshot();
[[nodiscard]] bool AcquisitionRunning() noexcept;
void SetPresentationFps(float fps) noexcept;
void Shutdown();
const char* StatusLine();
int PlayerCount();
bool SoftProbeLobbyOffsets();
bool SelfTestOffsets();
bool ValidateLiveOffsets();
bool ReinitDma();
// True while cs2.exe is still visible to the DMA / local process list.
bool IsGameProcessAlive();

} // namespace CS2
