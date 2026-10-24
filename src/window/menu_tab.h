#pragma once


enum class MenuTab : int {
    TAB_VISUALS = 0,
    TAB_AIM,
    TAB_VEHICLES,
    TAB_OBJECT_ESP,
    TAB_FRIENDS,
    TAB_CONFIGS,
    TAB_SAVECONFIG,
    TAB_STATUS,
    TAB_RADAR,

    TAB_CS2_VISUALS = 100,
    TAB_CS2_AIM,
    TAB_CS2_MISC,
    TAB_CS2_RADAR,
    TAB_CS2_STATUS,
    TAB_CS2_CONFIGS,

    TAB_RUST_VISUALS = 200,
    TAB_RUST_AIM,
    TAB_RUST_MISC,
    TAB_RUST_RADAR,
    TAB_RUST_STATUS,

    TAB_WARZONE_AIM = 300,
    TAB_WARZONE_VISUALS,
    TAB_WARZONE_RADAR,
    TAB_WARZONE_WORLD,
    TAB_WARZONE_PLAYERS,
    TAB_WARZONE_MISC,


    TAB_UNIFIED_AIM = 600,
    TAB_SOUND_ESP,
    TAB_SPECTATOR_LIST,
    TAB_TRIGGERBOT,
    TAB_RECOIL_CONTROL,
    TAB_PREDICTION,
    TAB_VISIBILITY,
    TAB_BONE_SYSTEM,
    TAB_SMOOTH_CURVES,
    TAB_RECOIL_PATTERNS,
    TAB_ENTITY_CACHE,
    TAB_PROFILE_MANAGER,
    TAB_OFFSET_MANAGER,
    TAB_RESOLUTION,
    TAB_GAME_ADAPTER,

    TAB_FORTNITE_VISUALS = 500,
    TAB_FORTNITE_AIM,
    TAB_FORTNITE_STATUS,

};


void DrawVisuals();
void DrawAim();
void DrawVehicles();
void DrawObjectEsp();
void DrawRadar();
void DrawFriends();
void DrawSaveConfigs();
void DrawFiveMStatus();

class Overlay;
void DrawConfigs(Overlay* self);


void DrawCs2Visuals();
void DrawCs2Aim();
void DrawCs2Misc();
void DrawCs2Radar();
void DrawCs2Status();

void DrawRustVisuals();
void DrawRustAim();
void DrawRustMisc();
void DrawRustRadar();
void DrawRustStatus();


void DrawWarzoneAim();
void DrawWarzoneVisuals();
void DrawWarzoneRadar();
void DrawWarzoneWorld();
void DrawWarzonePlayers();
void DrawWarzoneMisc();

void DrawFortniteVisuals();
void DrawFortniteAim();
void DrawFortniteStatus();


void DrawUnifiedAim();
void DrawSoundESP();
void DrawSpectatorList();
void DrawTriggerbot();
void DrawRecoilControl();
void DrawPrediction();
void DrawVisibility();
void DrawBoneSystem();
void DrawSmoothCurves();
void DrawRecoilPatterns();
void DrawEntityCache();
void DrawProfileManager();
void DrawOffsetManager();
void DrawResolution();
void DrawGameAdapter();
