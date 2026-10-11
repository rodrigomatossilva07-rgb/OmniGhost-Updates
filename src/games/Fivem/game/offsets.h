#pragma once
#include <cstdint>
#include <string>

namespace FiveM {
    namespace offset {
        extern uintptr_t world, replay, viewport, camera, localplayer;
        extern uintptr_t boneList, boneMatrix;
        extern uintptr_t playerInfo, playerHealth, playerPosition;
        extern uintptr_t base;
        extern int buildVersion;

        // CPlayerInfo: cheatoffsets net_id=0x88; some older dumps used 0xE8
        extern uintptr_t playerInfo_netId;
        extern uintptr_t playerInfo_netId_alt;

        // Ped-relative fields verified for b3258
        extern uintptr_t weaponManager;
        extern uintptr_t playerArmor;
        extern uintptr_t playerArmorAlt1;
        extern uintptr_t playerArmorAlt2;
        extern uintptr_t playerMaxHealth;
        extern uintptr_t pedVehicle;
        extern uintptr_t playerInfo_wanted;
        extern uintptr_t playerInfo_name;

        // Vehicle fields (cheatoffsets 3258/3751/3788)
        extern uintptr_t vehicleLock;
        extern uintptr_t vehicleDriver;
        extern uintptr_t vehicleDriverNew;
        extern uintptr_t vehicleEngineHp;
        extern uintptr_t vehicleBodyHp;
        extern uintptr_t vehicleFuel;
        extern uintptr_t vehicleGear;
        extern uintptr_t vehicleRPM;
        extern uintptr_t vehicleModelInfo;
        extern uintptr_t pedVelocity;
        extern uintptr_t pedConfigFlags;

        // CVehicle relative (live probe b3258 in-vehicle)
        extern uintptr_t vehicleHandling;
        extern uintptr_t vehicleGravity;

        // Weapon-chain fields are loaded from data/fivem_offsets.json.
        extern uintptr_t weaponMgr_currentWeapon;
        extern uintptr_t weaponInfo_hash;
        // CWeaponInfo relative fields verified for b3258
        extern uintptr_t weaponInfo_recoil;
        extern uintptr_t weaponInfo_spread;
        extern uintptr_t weaponInfo_damage;
        extern uintptr_t weaponInfo_range;

        // Camera-relative fields are loaded from data/fivem_offsets.json.
        extern uintptr_t cam_position;
        extern uintptr_t cam_direction;
        extern uintptr_t world_localPlayer;
        extern uintptr_t boneFrag3258;
        extern uintptr_t boneFrag3751;
        extern uintptr_t boneSkeleton;
        extern uintptr_t boneSkeletonStride;
        extern uintptr_t boneSkeletonTranslation;
        extern uintptr_t boneArrayBase;
        extern uintptr_t boneSize;
        extern uintptr_t pedVelocityAlt;
        extern uintptr_t vehicleList;
        extern uintptr_t vehicleCount;
        extern uintptr_t vehicleEntryStride;
        extern uintptr_t vehiclePosition;
        extern uintptr_t vehicleMatrix;
        extern uintptr_t viewportMatrix;
        extern uintptr_t replayPedInterface;
        extern uintptr_t pedInterfaceList;
        extern uintptr_t replayVehicleInterface;
        extern uintptr_t replayVehicleInterfaceAlt;
        extern uintptr_t waypointBlipPosition;
        extern uintptr_t waypointBlipSprite;

        // Module-relative extras for b3258
        extern uintptr_t b3258_networkPlayerMgr;
        extern uintptr_t b3258_objectPool;
        extern uintptr_t b3258_pickupMgr;
        extern uintptr_t b3258_waypoint;
        extern uintptr_t b3258_globalPtr;
        extern uintptr_t b3258_skySettings;
        extern uintptr_t pedVisibilityOffset;
        extern uintptr_t blip_list;
        extern uintptr_t aim_cped;
        extern uintptr_t bullet;
        extern uintptr_t network_player_mgr;
        extern uintptr_t object_pool;
        extern uintptr_t waypoint;
    }

    // Structure to hold offset configurations for different builds.
    // Module RVAs (world/replay/object_pool/...) are relative to game base.
    // Field offsets (playerInfo/bone/health/position/ped_visible_flag) are relative to entity.
    // Populated from data/fivem_offsets.json (source of truth).
    struct BuildOffsets {
        int build = 0;
        uintptr_t world_offset = 0;
        uintptr_t replay_offset = 0;
        uintptr_t viewport_offset = 0;
        uintptr_t camera_offset = 0;
        uintptr_t playerInfo_offset = 0;
        uintptr_t boneList_offset = 0;
        uintptr_t boneMatrix_offset = 0;
        uintptr_t playerHealth_offset = 0;
        uintptr_t playerPosition_offset = 0;
        // Module RVAs — optional extras (0 = unavailable for this build)
        uintptr_t object_pool_offset = 0;
        uintptr_t network_player_mgr_offset = 0;
        uintptr_t blip_list_offset = 0;
        uintptr_t waypoint_offset = 0;
        uintptr_t aim_cped_offset = 0;
        uintptr_t ped_pool_offset = 0;
        uintptr_t vehicle_pool_offset = 0;
        uintptr_t ped_visibility_offset = 0;
    };

    // Load / reload builds from data/fivem_offsets.json (or embedded snapshot).
    // Returns number of builds loaded. Safe to call multiple times.
    int LoadOffsetsFromJson(const char* explicit_path = nullptr);

    // Get the explicit FiveM build encoded in the process name.
    int GetBuildVersion();

    // Check if offsets are supported for current build
    bool IsBuildSupported();

    // Returns config for build if present in the JSON table; nullptr otherwise.
    const BuildOffsets* GetOffsetsForBuild(int build);

    /*
    * OFFSET REFERENCE GUIDE:
    *
    * world_offset         - CWorld pointer (contains all game entities)
    * replay_offset        - CReplayInterface module pointer
    * viewport_offset      - CViewport module pointer
    * camera_offset        - CCamera pointer (camera data)
    * playerInfo_offset    - Offset from ped to CPlayerInfo
    * boneList_offset      - Offset from ped to bone list
    * boneMatrix_offset    - Offset from ped to bone matrix (usually 0x60)
    * playerHealth_offset  - Offset from ped to health value (usually 0x280)
    * playerPosition_offset- Offset from ped to position vector (usually 0x90)
    */
}

namespace FiveM {
// Lobby-safe: world pointer from build table must resolve.
bool SoftProbeLobbyOffsets();

// Returns true if any build in the offsets table has critical offsets (world + viewport)
bool HasAnyCriticalOffsets();
}
