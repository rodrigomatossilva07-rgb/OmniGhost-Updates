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
        const uintptr_t playerInfo_netId = 0x88;
        const uintptr_t playerInfo_netId_alt = 0xE8;

        // Ped-relative fields verified for b3258
        const uintptr_t weaponManager = 0x10B8;
        const uintptr_t playerArmor = 0x150C;
        const uintptr_t playerMaxHealth = 0x284;
        const uintptr_t pedVehicle = 0x0D10;   // CPed -> CVehicle* (live probe b3258 in-vehicle)
        const uintptr_t visibleFlag = 0x147C;
        const uintptr_t playerInfo_wanted = 0x8E8; // on CPlayerInfo
        const uintptr_t playerInfo_name   = 0x100; // CPlayerInfo+name (cheatoffsets 3258/3751/3788)

        // Vehicle fields (cheatoffsets 3258/3751/3788)
        const uintptr_t vehicleLock     = 0x13C0; // door_lock
        const uintptr_t vehicleDriver   = 0xC98;  // 3258; 3751 uses 0xCA8 — runtime probes both
        const uintptr_t vehicleEngineHp = 0xA48;  // 3258; 3751 uses 0xAF8
        const uintptr_t vehicleBodyHp   = 0x840;
        const uintptr_t vehicleFuel     = 0x8C0;
        const uintptr_t vehicleGear     = 0xFD0;
        const uintptr_t vehicleRPM      = 0x914;
        const uintptr_t vehicleModelInfo= 0x20;
        const uintptr_t pedVelocity     = 0x2F0;
        const uintptr_t pedConfigFlags  = 0x1444;

        // CVehicle relative (live probe b3258 in-vehicle)
        const uintptr_t vehicleHandling = 0x960;
        const uintptr_t vehicleGravity  = 0xC8C;

        // Weapon chain: CPed+0x10B8 -> mgr, +0x20 -> CWeaponInfo, +0x10 hash
        const uintptr_t weaponMgr_currentWeapon = 0x20;
        const uintptr_t weaponInfo_hash = 0x10;
        // CWeaponInfo relative fields verified for b3258
        const uintptr_t weaponInfo_recoil = 0x20;
        const uintptr_t weaponInfo_spread = 0x3C;
        const uintptr_t weaponInfo_damage = 0x84;
        const uintptr_t weaponInfo_range  = 0x100;

        // Camera relative (memory aim): direction Vector3 at +0x3D0
        const uintptr_t cam_position = 0x60;
        const uintptr_t cam_direction = 0x3D0;

        // Module-relative extras for b3258
        const uintptr_t b3258_networkPlayerMgr = 0x1E63C68;
        const uintptr_t b3258_objectPool       = 0x25BFDE8;
        const uintptr_t b3258_pickupMgr        = 0x25B1470;
        const uintptr_t b3258_waypoint         = 0x2EE0288;
        const uintptr_t b3258_globalPtr        = 0x1F73578;
        const uintptr_t b3258_skySettings      = 0x2721250;
        const uintptr_t b3258_framecountlastvisible = 0x5719A3;

        extern uintptr_t framecountlastvisible;
        extern uintptr_t pedVisibilityOffset;
        extern uintptr_t blip_list;
        extern uintptr_t aim_cped;
        extern uintptr_t bullet;
        extern uintptr_t network_player_mgr;
        extern uintptr_t object_pool;
        extern uintptr_t waypoint;
    }

    // Structure to hold offset configurations for different builds
    struct BuildOffsets {
        int build;
        uintptr_t world_offset;
        uintptr_t replay_offset;
        uintptr_t viewport_offset;
        uintptr_t camera_offset;
        uintptr_t playerInfo_offset;
        uintptr_t boneList_offset;
        uintptr_t boneMatrix_offset;
        uintptr_t playerHealth_offset;
        uintptr_t playerPosition_offset;
    };

    // Get the explicit FiveM build encoded in the process name.
    int GetBuildVersion();

    // Check if offsets are supported for current build
    bool IsBuildSupported();

    // Returns only production-verified configurations. Unknown, provisional or
    // placeholder builds always return nullptr.
    const BuildOffsets* GetOffsetsForBuild(int build);

    /*
    * OFFSET REFERENCE GUIDE:
    *
    * world_offset         - CWorld pointer (contains all game entities)
    * replay_offset        - CReplayInterface pointer (ped list at +0x18)
    * viewport_offset      - CViewport pointer (view matrix at +0x24C)
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
}
