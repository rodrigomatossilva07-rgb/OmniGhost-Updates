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


        extern uintptr_t playerInfo_netId;
        extern uintptr_t playerInfo_netId_alt;


        extern uintptr_t weaponManager;
        extern uintptr_t playerArmor;
        extern uintptr_t playerArmorAlt1;
        extern uintptr_t playerArmorAlt2;
        extern uintptr_t playerMaxHealth;
        extern uintptr_t pedVehicle;
        extern uintptr_t playerInfo_wanted;
        extern uintptr_t playerInfo_name;


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


        extern uintptr_t vehicleHandling;
        extern uintptr_t vehicleGravity;


        extern uintptr_t weaponMgr_currentWeapon;
        extern uintptr_t weaponInfo_hash;

        extern uintptr_t weaponInfo_recoil;
        extern uintptr_t weaponInfo_spread;
        extern uintptr_t weaponInfo_damage;
        extern uintptr_t weaponInfo_range;


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

        uintptr_t object_pool_offset = 0;
        uintptr_t network_player_mgr_offset = 0;
        uintptr_t blip_list_offset = 0;
        uintptr_t waypoint_offset = 0;
        uintptr_t aim_cped_offset = 0;
        uintptr_t ped_pool_offset = 0;
        uintptr_t vehicle_pool_offset = 0;
        uintptr_t ped_visibility_offset = 0;
    };



    int LoadOffsetsFromJson(const char* explicit_path = nullptr);


    int GetBuildVersion();


    bool IsBuildSupported();


    const BuildOffsets* GetOffsetsForBuild(int build);














}

namespace FiveM {

bool SoftProbeLobbyOffsets();


bool HasAnyCriticalOffsets();
}
