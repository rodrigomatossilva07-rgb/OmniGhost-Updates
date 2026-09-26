#include "config_manager.h"
#include "config_core.h"
#include "config_games.h"
#include "config_esp.h"
#include "config_aim.h"
#include "config_vehicle.h"
#include "config_friends.h"
#include "config_aimtype.h"
#include "../window/hotkeys.h"
#include "../globals.h"
#include "../../src/games/Fivem/aimbot/aimbot.h"
#include "../../src/games/Fivem/friends/friends.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cstdlib>
#include <sstream>

namespace config_manager {

std::string SerializeAll() {
    std::ostringstream o;
    WInt(o, "meta.schema_version", kConfigSchemaVersion);
    WStr(o, "meta.app_version", CurrentAppVersion());
    WStr(o, "meta.game", ActiveGameMeta());
    if (active_config_id.empty()) active_config_id = NewConfigId();
    WStr(o, "meta.config_id", active_config_id);
    WInt(o, "meta.config_version", kConfigSchemaVersion);
    o << "# OmniGhost Config schema " << kConfigSchemaVersion << "\n";

    // Shared modules belong to FiveM only. Other games have isolated schemas
    // and must never persist stale shared runtime state.
    if (g_activeGame == ActiveGame::FiveM) {
        SerializeESP(o);
        SerializeAim(o);
        SerializeVehicle(o);
        SerializeFriends(o);
        SerializeAimType(o);
    }

    // Game-specific
    switch (g_activeGame) {
    case ActiveGame::CS2:
        SerializeCS2(o);
        break;
    case ActiveGame::Warzone:
        SerializeWarzone(o);
        break;
    default:
        SerializeFiveM(o);
        break;
    }

    o << Hotkeys::SerializeFeatureToggles();

    return o.str();
}

bool DeserializeAll(const std::string& data) {
    if (data.empty()) return false;
    std::istringstream in(data);
    std::string line, key, val;
    std::string meta_game;
    std::string meta_config_id;
    int schema_version = 1;
    int config_version = 1;

    // Read metadata first so a foreign/corrupt config can never mutate runtime state.
    {
        std::istringstream metaIn(data);
        std::string metaLine;
        while (std::getline(metaIn, metaLine)) {
            std::string mk, mv;
            if (!ParseLine(metaLine, mk, mv)) continue;
            if (mk == "meta.schema_version") schema_version = atoi(mv.c_str());
            else if (mk == "meta.config_version") config_version = atoi(mv.c_str());
            else if (mk == "meta.game") meta_game = mv;
            else if (mk == "meta.config_id") meta_config_id = mv;
        }
    }
    if (schema_version <= 0 || schema_version > kConfigSchemaVersion ||
        config_version <= 0 || config_version > kConfigSchemaVersion) {
        last_status = "Versao de config nao suportada";
        return false;
    }
    if (!meta_game.empty() && meta_game != ActiveGameMeta()) {
        last_status = std::string("Config invalida para este jogo (meta.game=") + meta_game + ")";
        return false;
    }

    // Start from the canonical defaults so settings absent from older schemas
    // automatically receive the current default instead of leaking a previous value.
    ResetActiveGameToDefaults();
    if (g_activeGame == ActiveGame::FiveM) {
        aimbot::config.silent_enabled = false;
    }
    active_config_id = meta_config_id;
    if (active_config_id.empty()) active_config_id = NewConfigId();

    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (!ParseLine(line, key, val)) continue;

        auto bi = [&](const char* k) { return key == k; };
        auto toB = [&]() { return val == "1" || val == "true"; };
        auto toF = [&]() { return (float)atof(val.c_str()); };
        auto toI = [&]() { return atoi(val.c_str()); };
        auto toU = [&]() { return (ImU32)strtoul(val.c_str(), nullptr, 10); };

        if (bi("meta.schema_version")) {
            schema_version = atoi(val.c_str());
            if (schema_version <= 0 || schema_version > kConfigSchemaVersion) {
                last_status = "Config schema nao suportado: " + std::to_string(schema_version);
                return false;
            }
            continue;
        }
        if (bi("meta.app_version")) { continue; }
        if (bi("meta.game")) { meta_game = val; continue; }
        if (bi("meta.config_id")) { meta_config_id = val; continue; }
        if (bi("meta.config_version")) { config_version = atoi(val.c_str()); continue; }

        // Skip foreign game sections
        if (key.rfind("cs2.", 0) == 0 && g_activeGame != ActiveGame::CS2) continue;
        if (key.rfind("warzone.", 0) == 0 && g_activeGame != ActiveGame::Warzone) continue;
        if ((key.rfind("esp.", 0) == 0 || key.rfind("aim.", 0) == 0 || key.rfind("veh.", 0) == 0)
            && g_activeGame != ActiveGame::FiveM) continue;

        // Try each module
        if (DeserializeESP(key, val)) continue;
        if (DeserializeAim(key, val)) continue;
        if (DeserializeVehicle(key, val)) continue;
        if (DeserializeFriends(key, val)) continue;
        if (DeserializeAimType(key, val)) continue;
        if (DeserializeCS2(key, val)) continue;
        if (DeserializeWarzone(key, val)) continue;
        if (DeserializeFiveM(key, val)) continue;
        if (Hotkeys::DeserializeFeatureToggle(key, val)) continue;

        // Friend list entries
        if (key.rfind("friend.", 0) == 0 && key.find(".id") != std::string::npos) {
            uint32_t id = (uint32_t)strtoul(val.c_str(), nullptr, 10);
            friends::AddFriend(std::string("Friend_") + val, id, false);
            continue;
        }
        if (key.rfind("friend.", 0) == 0 && key.find(".name") != std::string::npos) {
            continue;
        }
    }

    // Silent aim removed — never leave enabled from old files
    aimbot::config.silent_enabled = false;

    // Keep loaded binds, but FiveM fire must remain independent from aim.
    if (aimbot::config.aimbot_bind <= 0 || aimbot::config.aimbot_bind == VK_LBUTTON)
        aimbot::config.aimbot_bind = VK_RBUTTON;
    if (aimbot::config.aimbot_bind2 == VK_LBUTTON || aimbot::config.aimbot_bind2 == aimbot::config.aimbot_bind)
        aimbot::config.aimbot_bind2 = 0;

    // Default bind for CS2 if missing
    if (g_activeGame == ActiveGame::CS2 && CS2::config.aim_bind <= 0)
        CS2::config.aim_bind = 0x01;

    return true;
}


} // namespace config_manager
