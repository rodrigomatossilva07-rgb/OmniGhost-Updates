#pragma once


enum class MenuTab : int {
    TAB_VISUALS = 0,
    TAB_AIM,
    TAB_VEHICLES,
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
