#include "config_profiles.h"
#include "config_games.h"
#include "config_core.h"
#include "../globals.h"
#include "../../src/games/Fivem/esp/esp.h"
#include "../../src/games/Fivem/aimbot/aimbot.h"
#include "../../src/games/Fivem/esp/vehicle_esp.h"
#include "../../src/games/Fivem/friends/friends.h"
#include "../../src/games/Fortnite/fortnite_game.h"
#include <cstring>

namespace config_manager {

void ApplyMinimalProfileForActiveGame() {
    ResetActiveGameToDefaults();
    switch (g_activeGame) {
    case ActiveGame::CS2:
        CS2::config.esp_enabled = true;
        CS2::config.box = true;
        CS2::config.health_bar = true;
        CS2::config.name = true;
        CS2::config.team_check = true;
        CS2::config.aim_enabled = false;
        CS2::config.trigger_enabled = false;
        break;
    case ActiveGame::Warzone:
        Warzone::config.esp_enabled = true;
        Warzone::config.box = true;
        Warzone::config.health_bar = true;
        Warzone::config.name = true;
        Warzone::config.distance = true;
        Warzone::config.skeleton = false;
        Warzone::config.radar_2d = false;
        Warzone::config.aim_enabled = false;
        Warzone::config.trigger_enabled = false;
        std::strncpy(Warzone::config.scenario_preset, "minimal", sizeof(Warzone::config.scenario_preset) - 1);
        Warzone::config.scenario_preset[sizeof(Warzone::config.scenario_preset) - 1] = '\0';
        break;
    default:
        esp::config.enabled = true;
        esp::config.box_2d = true;
        esp::config.health_bar = true;
        esp::config.player_name = true;
        esp::config.distance = true;
        esp::config.skeleton = false;
        esp::config.snaplines = false;
        aimbot::config.aimbot_enabled = false;
        aimbot::config.trigger_enabled = false;
        break;
    }
}

void ApplyVisualProfileForActiveGame() {
    ApplyMinimalProfileForActiveGame();
    switch (g_activeGame) {
    case ActiveGame::CS2:
        CS2::config.skeleton = true;
        CS2::config.distance = true;
        CS2::config.radar_2d = true;
        CS2::config.radar_2d_size = 180.f;
        break;
    case ActiveGame::Warzone:
        Warzone::config.skeleton = true;
        Warzone::config.radar_2d = true;
        Warzone::config.radar_size = 180.f;
        Warzone::config.esp_auto_hide_names = true;
        std::strncpy(Warzone::config.scenario_preset, "visuals", sizeof(Warzone::config.scenario_preset) - 1);
        Warzone::config.scenario_preset[sizeof(Warzone::config.scenario_preset) - 1] = '\0';
        break;
    default:
        esp::config.skeleton = true;
        esp::config.weapon_name = true;
        esp::config.armor_bar = true;
        esp::config.triangle_radar = true;
        break;
    }
}

void ApplyGameProfile(GameProfile profile) {
    switch (profile) {
    case GameProfile::Default:
        ResetActiveGameToDefaults();
        active_profile_name = "Default";
        break;
    case GameProfile::Minimal:
        ApplyMinimalProfileForActiveGame();
        active_profile_name = "Minimal";
        last_status = "Profile Minimal aplicado";
        break;
    case GameProfile::Visual:
        ApplyVisualProfileForActiveGame();
        active_profile_name = "Visual";
        last_status = "Profile Visual aplicado";
        break;
    case GameProfile::Custom: {
        const std::string name = std::string(ActiveGamePrefix()) + "custom";
        if (LoadFromFile(name)) {
            active_profile_name = "Custom";
            last_status = "Profile Custom carregado";
        } else {
            active_profile_name = "Custom";
            SaveToFile("custom");
            last_status = "Profile Custom criado a partir da configuração atual";
        }
        break;
    }
    }
}

bool SaveCustomGameProfile() {
    const bool ok = SaveToFile("custom");
    if (ok) {
        active_profile_name = "Custom";
        last_status = "Profile Custom atualizado";
    }
    return ok;
}

const char* CurrentGameProfileName() {
    return active_profile_name.c_str();
}

void ApplyNamedProfile(const char* name) {
    if (!name) return;
    std::string n(name);
    for (auto& c : n) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    if (n == "default") ApplyGameProfile(GameProfile::Default);
    else if (n == "minimal" || n == "legit") ApplyGameProfile(GameProfile::Minimal);
    else if (n == "visual" || n == "visuals") ApplyGameProfile(GameProfile::Visual);
    else if (n == "custom") ApplyGameProfile(GameProfile::Custom);
    else if (LoadFromFile(name)) {
        active_profile_name = "Custom";
        last_status = std::string("Loaded profile: ") + name;
    }
}

void SaveNamedProfile(const char* name) {
    if (!name || !*name) return;
    SaveToFile(name);
    active_profile_name = "Custom";
    last_status = std::string("Saved profile: ") + name;
}

void ResetActiveGameToDefaults() {
    switch (g_activeGame) {
    case ActiveGame::CS2:
        CS2::config = CS2::Config{};
        break;
    case ActiveGame::Warzone:
        Warzone::config = Warzone::Config{};
        break;
    case ActiveGame::Fortnite:



        Fortnite::config = Fortnite::Config{};
        break;
    default:
        esp::config = esp::Config{};
        aimbot::config = aimbot::Config{};
        vehicle_esp::config = vehicle_esp::Config{};
        friends::config = friends::Config{};
        break;
    }
    active_profile_name = "Default";
    last_status = "Profile Default aplicado";
}

void ResetToDefaults() {
    ResetActiveGameToDefaults();
}

void ResetSettingsOnly() {
    ResetActiveGameToDefaults();
    active_config_name.clear();
    active_profile_name = "Default";
    last_status = "Definições repostas (autenticação/licença preservadas)";
    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Config,
        "settings reset (auth/license preserved)");
}

}
