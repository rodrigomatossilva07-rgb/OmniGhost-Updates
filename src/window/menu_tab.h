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
    // CS2-specific (same style, different pages)
    TAB_CS2_VISUALS = 100,
    TAB_CS2_AIM,
    TAB_CS2_MISC,
    TAB_CS2_CONFIGS,
    // Rust-specific (blurred-inspired layout)
    TAB_RUST_VISUALS = 200,
    TAB_RUST_AIM,
    TAB_RUST_WORLD,
    TAB_RUST_PLAYERS,
    TAB_RUST_RADAR,
    TAB_RUST_MISC,
    TAB_RUST_DEBUG,
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

    TAB_FORTNITE_VISUALS = 500,
    TAB_FORTNITE_AIM,
    TAB_FORTNITE_STATUS
};

// FiveM pages
void DrawVisuals();
void DrawAim();
void DrawVehicles();
void DrawFriends();
void DrawSaveConfigs();
void DrawFiveMStatus();

class Overlay;
void DrawConfigs(Overlay* self);

// CS2 pages
void DrawCs2Visuals();
void DrawCs2Aim();
void DrawCs2Misc();

// Rust pages
void DrawRustVisuals();
void DrawRustAim();
void DrawRustWorld();
void DrawRustPlayers();
void DrawRustRadar();
void DrawRustMisc();
void DrawRustDebug();

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
void DrawFortniteAim();
void DrawFortniteStatus();
