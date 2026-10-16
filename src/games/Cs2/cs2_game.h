#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "config/cs2_config.h"
#include "gameplay/snapshot_exchange.h"
#include "gameplay/unified_esp.h"

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
    uintptr_t entity_identity_stride = 0;
    uintptr_t entity_identity_pointer = 0;
    uintptr_t entity_identity_name = 0;
    uintptr_t entity_page_size = 0;
    uintptr_t entity_page_table_offset = 0;
    uintptr_t global_vars_current_time = 0;
    uintptr_t global_vars_current_map = 0;

    uintptr_t m_iHealth = 0;
    uintptr_t m_iTeamNum = 0;
    uintptr_t m_pGameSceneNode = 0;
    uintptr_t m_fFlags = 0;
    uintptr_t m_vOldOrigin = 0;
    uintptr_t m_hPlayerPawn = 0;
    uintptr_t m_hObserverPawn = 0;
    uintptr_t m_pObserverServices = 0;
    uintptr_t m_hObserverTarget = 0;
    uintptr_t m_iPawnHealth = 0;
    uintptr_t m_iPawnArmor = 0;
    uintptr_t m_iszPlayerName = 0;
    uintptr_t m_sSanitizedPlayerName = 0;
    uintptr_t m_steamID = 0;
    uintptr_t m_bPawnIsAlive = 0;
    uintptr_t m_ArmorValue = 0;
    uintptr_t m_angEyeAngles = 0;
    uintptr_t m_iIDEntIndex = 0;
    uintptr_t m_vecAbsOrigin = 0;
    uintptr_t m_vecVelocity = 0;
    uintptr_t m_bDormant = 0;
    uintptr_t m_modelState = 0;
    uintptr_t BoneArray = 0;
    uintptr_t m_pClippingWeapon = 0;
    uintptr_t m_bIsScoped = 0;

    uintptr_t m_entitySpottedState = 0;
    uintptr_t m_bSpotted = 0;
    uintptr_t m_flFlashDuration = 0;


    uintptr_t m_iShotsFired = 0;
    uintptr_t m_aimPunchAngle = 0;



    uintptr_t m_bBombTicking = 0;
    uintptr_t m_flC4Blow = 0;
    uintptr_t m_flTimerLength = 0;
    uintptr_t m_bBombDefused = 0;
    uintptr_t m_bBeingDefused = 0;
    uintptr_t m_flDefuseCountDown = 0;
    uintptr_t m_hBombDefuser = 0;

    uintptr_t m_pWeaponServices = 0;
    uintptr_t m_pItemServices = 0;
    uintptr_t m_bHasDefuser = 0;
    uintptr_t m_hActiveWeapon = 0;
    uintptr_t m_AttributeManager = 0;
    uintptr_t m_Item = 0;
    uintptr_t m_iItemDefinitionIndex = 0;
    uintptr_t m_iClip1 = 0;
    uintptr_t m_pReserveAmmo = 0;
    uintptr_t m_pInGameMoneyServices = 0;
    uintptr_t m_iAccount = 0;
    uintptr_t m_bIsDefusing = 0;
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

static_assert(kBoneSlotCount == 20, "BoneSlot enum must have exactly 20 entries");







inline constexpr uintptr_t kEntityStridePrimary = 0x78;
inline constexpr uintptr_t kEntityStrideAlternate = 0x70;
struct BoneMapping {
    int jointIndex[kBoneSlotCount];
    bool resolved = false;

    constexpr BoneMapping() {
        for (std::size_t i = 0; i < kBoneSlotCount; ++i)
            jointIndex[i] = -1;
    }



    bool ResolveFromModel(uintptr_t clientBase, uintptr_t modelBase);
};


extern BoneMapping g_boneMapping;

inline int GetBoneJointIndex(BoneSlot slot) {
    return g_boneMapping.jointIndex[static_cast<std::size_t>(slot)];
}



inline constexpr uint64_t kBoneVisualMaxAgeMs = 160u;



inline constexpr int kBoneReliabilityHoldMs = static_cast<int>(kBoneVisualMaxAgeMs);

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


    uint64_t position_sample_ms = 0;
    uint64_t bones_sample_ms = 0;
    uint64_t bone_pose_sample_ms = 0;
    uint64_t bone_origin_sample_ms = 0;
    float view_yaw = 0.f;
    float head[3]{};
    char name[64]{};
    char weapon[32]{};
    int weapon_def = 0;
    float distance = 0.f;






    float bones[kBoneSlotCount][3]{};
    bool bones_ok = false;
    bool full_bones_ok = false;
    bool spotted = true;
    bool visibility_known = false;
    bool visible = false;
    int ent_index = 0;
    uint64_t steam_id = 0;

    int ammo_clip = -1;
    int ammo_reserve = -1;
    int money = -1;
    bool has_defuser = false;
    bool is_defusing = false;
    bool is_moving = false;
    float move_speed = 0.f;
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

struct DroppedWeapon {
    uintptr_t entity = 0;
    float pos[3]{};
    int item_definition = 0;
    int ammo_clip = -1;
    uint64_t sample_timestamp_ms = 0;
    char name[48]{};
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
    int local_weapon_definition = 0;
    bool local_scoped = false;
    bool local_has_defuser = false;
    int local_crosshair_entity = 0;
    int local_shots_fired = 0;
    float local_aim_punch[2]{};
    uint64_t local_aim_sample_ms = 0;
    uint64_t local_last_shot_ms = 0;
    float view_matrix[16]{};
    char map_name[64]{};
    bool in_match = false;
    uintptr_t controller_stride = 0;
    uintptr_t pawn_stride = 0;
    BombState bomb{};
    std::vector<DroppedWeapon> dropped_weapons;
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
    uint64_t session_generation = 0;


    uint64_t world_sample_ms = 0;
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


    Runtime() = default;
    Runtime(Runtime&& other) noexcept
        : client_base(other.client_base), engine_base(other.engine_base),
          entity_list_addr(other.entity_list_addr), entity_list_entry(other.entity_list_entry),
          local_pawn(other.local_pawn), local_controller(other.local_controller),
          local_team(other.local_team), local_health(other.local_health),
          local_pos{other.local_pos[0], other.local_pos[1], other.local_pos[2]},
          local_vel{other.local_vel[0], other.local_vel[1], other.local_vel[2]},
          local_angles{other.local_angles[0], other.local_angles[1], other.local_angles[2]},
          local_view_yaw(other.local_view_yaw), local_weapon_definition(other.local_weapon_definition),
          local_scoped(other.local_scoped), local_has_defuser(other.local_has_defuser),
          local_crosshair_entity(other.local_crosshair_entity), local_shots_fired(other.local_shots_fired),
          local_aim_punch{other.local_aim_punch[0], other.local_aim_punch[1]},
          local_aim_sample_ms(other.local_aim_sample_ms), local_last_shot_ms(other.local_last_shot_ms),
          view_matrix{other.view_matrix[0], other.view_matrix[1], other.view_matrix[2], other.view_matrix[3],
                       other.view_matrix[4], other.view_matrix[5], other.view_matrix[6], other.view_matrix[7],
                       other.view_matrix[8], other.view_matrix[9], other.view_matrix[10], other.view_matrix[11],
                       other.view_matrix[12], other.view_matrix[13], other.view_matrix[14], other.view_matrix[15]},
          map_name{0}, in_match(other.in_match),
          controller_stride(other.controller_stride), pawn_stride(other.pawn_stride),
          bomb(std::move(other.bomb)), dropped_weapons(std::move(other.dropped_weapons)),
          player_count(other.player_count), enemy_count(other.enemy_count),
          controller_count(other.controller_count), pawn_count(other.pawn_count),
          spectator_count(other.spectator_count), spectator_target(other.spectator_target),
          spectator_target_name{0}, build_number(other.build_number),
          frames(other.frames), read_fails(other.read_fails),
          snapshot_timestamp_ms(other.snapshot_timestamp_ms), session_generation(other.session_generation),
          world_sample_ms(other.world_sample_ms), snapshot_drops(other.snapshot_drops),
          acquisition_hz(other.acquisition_hz), acquisition_ms(other.acquisition_ms),
          processing_ms(other.processing_ms), snapshot_interval_ms(other.snapshot_interval_ms),
          fps(other.fps), entity_cap_used(other.entity_cap_used),
          offsets_self_test_ok(other.offsets_self_test_ok),
          players(std::move(other.players)), spectators(std::move(other.spectators)) {
        std::copy_n(other.map_name, 64, map_name);
        std::copy_n(other.spectator_target_name, 64, spectator_target_name);
    }

    Runtime& operator=(Runtime&& other) noexcept {
        if (this != &other) {
            client_base = other.client_base;
            engine_base = other.engine_base;
            entity_list_addr = other.entity_list_addr;
            entity_list_entry = other.entity_list_entry;
            local_pawn = other.local_pawn;
            local_controller = other.local_controller;
            local_team = other.local_team;
            local_health = other.local_health;
            local_pos[0] = other.local_pos[0]; local_pos[1] = other.local_pos[1]; local_pos[2] = other.local_pos[2];
            local_vel[0] = other.local_vel[0]; local_vel[1] = other.local_vel[1]; local_vel[2] = other.local_vel[2];
            local_angles[0] = other.local_angles[0]; local_angles[1] = other.local_angles[1]; local_angles[2] = other.local_angles[2];
            local_view_yaw = other.local_view_yaw;
            local_weapon_definition = other.local_weapon_definition;
            local_scoped = other.local_scoped;
            local_has_defuser = other.local_has_defuser;
            local_crosshair_entity = other.local_crosshair_entity;
            local_shots_fired = other.local_shots_fired;
            local_aim_punch[0] = other.local_aim_punch[0]; local_aim_punch[1] = other.local_aim_punch[1];
            local_aim_sample_ms = other.local_aim_sample_ms;
            local_last_shot_ms = other.local_last_shot_ms;
            for (int i = 0; i < 16; ++i) view_matrix[i] = other.view_matrix[i];
            std::copy_n(other.map_name, 64, map_name);
            in_match = other.in_match;
            controller_stride = other.controller_stride;
            pawn_stride = other.pawn_stride;
            bomb = std::move(other.bomb);
            dropped_weapons = std::move(other.dropped_weapons);
            player_count = other.player_count;
            enemy_count = other.enemy_count;
            controller_count = other.controller_count;
            pawn_count = other.pawn_count;
            spectator_count = other.spectator_count;
            spectator_target = other.spectator_target;
            std::copy_n(other.spectator_target_name, 64, spectator_target_name);
            build_number = other.build_number;
            frames = other.frames;
            read_fails = other.read_fails;
            snapshot_timestamp_ms = other.snapshot_timestamp_ms;
            session_generation = other.session_generation;
            world_sample_ms = other.world_sample_ms;
            snapshot_drops = other.snapshot_drops;
            acquisition_hz = other.acquisition_hz;
            acquisition_ms = other.acquisition_ms;
            processing_ms = other.processing_ms;
            snapshot_interval_ms = other.snapshot_interval_ms;
            fps = other.fps;
            entity_cap_used = other.entity_cap_used;
            offsets_self_test_ok = other.offsets_self_test_ok;
            players = std::move(other.players);
            spectators = std::move(other.spectators);
        }
        return *this;
    }
};

// namespace CS2 is defined above; the unified-adapter framework was retired
// (CS2-019/FIVEM-014 cleanup). Feature flags are now computed inline in the
// world pipeline using the UnifiedESP field model directly.
using RuntimeSnapshotLease = OmniGhost::Gameplay::SnapshotExchange<Runtime, 4>::ReadLease;

struct CameraSnapshot {
    float view_matrix[16]{};
    uint64_t timestamp_ms = 0;
};
using CameraSnapshotLease = OmniGhost::Gameplay::SnapshotExchange<CameraSnapshot>::ReadLease;



struct LivenessSample {
    uintptr_t pawn = 0;
    bool alive = false;


    uint64_t sample_ms = 0;
    uint64_t generation = 0;
};
struct LivenessSnapshot {
    std::array<LivenessSample, 64> players{};
    uint32_t count = 0;
    uint64_t timestamp_ms = 0;
    uint64_t generation = 0;
};
using LivenessSnapshotLease = OmniGhost::Gameplay::SnapshotExchange<LivenessSnapshot>::ReadLease;

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
void RunFrame();
void EnsureAcquisitionStarted();
void StopAcquisition();
void SubmitAcquisitionConfig(const Config& next) noexcept;
[[nodiscard]] RuntimeSnapshotLease AcquireRuntimeSnapshot();
[[nodiscard]] CameraSnapshotLease AcquireCameraSnapshot();
[[nodiscard]] CameraSnapshot SelectCameraSnapshot(uint64_t world_sample_ms);
[[nodiscard]] LivenessSnapshotLease AcquireLivenessSnapshot();
[[nodiscard]] bool AcquisitionRunning() noexcept;

[[nodiscard]] int DmaPressureLevel() noexcept;
[[nodiscard]] float LastSlowDmaMs() noexcept;
[[nodiscard]] float LastWeaponPhaseMs() noexcept;
[[nodiscard]] float LastAmmoPhaseMs() noexcept;
[[nodiscard]] float LastFlagsPhaseMs() noexcept;
void SetPresentationFps(float fps) noexcept;
void Shutdown();
const char* StatusLine();
int PlayerCount();
bool SoftProbeLobbyOffsets();
bool SelfTestOffsets();
bool ValidateLiveOffsets();
bool ReinitDma();

bool IsGameProcessAlive();



bool GiveCameraPriorityBeforeOptionalDma();

}
