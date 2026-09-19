#pragma once

// Shared menu tab enum — used by window.cpp, sidebar.cpp and page modules
enum class MenuTab : int {
    TAB_VISUALS = 0,
    TAB_AIM,
    TAB_VEHICLES,
    TAB_FRIENDS,
    TAB_CONFIGS,
    TAB_SAVECONFIG,
    TAB_STATUS,
    TAB_RADAR,
    // CS2-specific (same style, different pages)
    TAB_CS2_VISUALS = 100,
    TAB_CS2_AIM,
    TAB_CS2_MISC,
    TAB_CS2_RADAR,
    TAB_CS2_CONFIGS,
    // Warzone-specific runtime pages
    TAB_WARZONE_AIM = 300,
    TAB_WARZONE_VISUALS,
    TAB_WARZONE_RADAR,
    TAB_WARZONE_WORLD,
    TAB_WARZONE_PLAYERS,
    TAB_WARZONE_MISC,
    // Valorant-specific runtime pages
    TAB_VALORANT_VISUALS = 400,
    TAB_VALORANT_AIM,
    TAB_VALORANT_STATUS,
    
    // New unified system pages (shared across games)
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

    TAB_RUST_VISUALS = 800,
    TAB_RUST_AIM,
    TAB_RUST_SYSTEM
};

// FiveM pages
void DrawVisuals();
void DrawAim();
void DrawVehicles();
void DrawRadar();
void DrawFriends();
void DrawSaveConfigs();
void DrawFiveMStatus();

class Overlay;
void DrawConfigs(Overlay* self);

// CS2 pages
void DrawCs2Visuals();
void DrawCs2Aim();
void DrawCs2Misc();
void DrawCs2Radar();

// Warzone pages
void DrawWarzoneAim();
void DrawWarzoneVisuals();
void DrawWarzoneRadar();
void DrawWarzoneWorld();
void DrawWarzonePlayers();
void DrawWarzoneMisc();

// Valorant pages
void DrawValorantVisuals();
void DrawValorantAim();
void DrawValorantStatus();
void DrawFortniteVisuals();
void DrawRustVisuals();
void DrawRustAim();
void DrawRustSystem();
void DrawFortniteAim();
void DrawFortniteStatus();

// New unified system pages
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
