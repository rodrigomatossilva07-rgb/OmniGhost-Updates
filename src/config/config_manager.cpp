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
#include "../../src/games/Fivem/esp/esp.h"
#include "../../src/games/Fivem/friends/friends.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <array>

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



    if (g_activeGame == ActiveGame::FiveM) {
        SerializeESP(o);
        SerializeAim(o);
        SerializeVehicle(o);
        SerializeFriends(o);
        SerializeAimType(o);
    }


    switch (g_activeGame) {
    case ActiveGame::CS2:
        SerializeCS2(o);
        break;
    case ActiveGame::Warzone:
        SerializeWarzone(o);
        break;
    case ActiveGame::Fortnite:


        SerializeFortnite(o);
        break;
    default:
        SerializeFiveM(o);
        break;
    }

    o << Hotkeys::SerializeFeatureToggles();
    o << Hotkeys::SerializeActions();

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
    if (schema_version <= 0 || config_version <= 0) {
        last_status = "Versao de config invalida";
        return false;
    }


    if (schema_version > kConfigSchemaVersion) {
        std::cout << "[Config] Warning: Loading config from newer schema v" << schema_version
                  << " (current is v" << kConfigSchemaVersion << "). Unknown fields will be ignored.\n";
    }
    if (!meta_game.empty() && meta_game != ActiveGameMeta()) {
        last_status = std::string("Config invalida para este jogo (meta.game=") + meta_game + ")";
        return false;
    }





    if (schema_version < 3) {
        std::cout << "[Config] Migrating config from schema v" << schema_version << " to v3\n";




        ResetCS2ToDefaults();
        ResetWarzoneToDefaults();
        ResetActiveGameToDefaults();
    }
    if (schema_version < 4) {
        std::cout << "[Config] Migrating config from schema v" << schema_version << " to v4\n";


    }

    schema_version = kConfigSchemaVersion;
    ResetActiveGameToDefaults();
    if (g_activeGame == ActiveGame::FiveM) {

    }
    active_config_id = meta_config_id;
    if (active_config_id.empty()) active_config_id = NewConfigId();

    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (!ParseLine(line, key, val)) continue;

        auto bi = [&](const char* k) { return key == k; };
        auto toB = [&]() { return val == "1" || val == "true"; };

        auto toF = [&]() -> float {
            char* end = nullptr;
            const float result = std::strtof(val.c_str(), &end);
            return (end != val.c_str() && *end == '\0') ? result : 0.0f;
        };
        auto toI = [&]() -> int {
            char* end = nullptr;
            const long result = std::strtol(val.c_str(), &end, 10);
            return (end != val.c_str() && *end == '\0' && result >= INT_MIN && result <= INT_MAX) ? static_cast<int>(result) : 0;
        };
        auto toU = [&]() -> ImU32 {
            char* end = nullptr;
            const unsigned long result = std::strtoul(val.c_str(), &end, 10);
            return (end != val.c_str() && *end == '\0' && result <= UINT32_MAX) ? static_cast<ImU32>(result) : 0u;
        };

        if (bi("meta.schema_version")) {
            schema_version = toI();
            if (schema_version <= 0) {
                last_status = "Config schema invalido: " + std::to_string(schema_version);
                return false;
            }

            if (schema_version > kConfigSchemaVersion) {

            }
            continue;
        }
        if (bi("meta.app_version")) { continue; }
        if (bi("meta.game")) { meta_game = val; continue; }
        if (bi("meta.config_id")) { meta_config_id = val; continue; }
        if (bi("meta.config_version")) { config_version = toI(); continue; }


        if (key.rfind("cs2.", 0) == 0 && g_activeGame != ActiveGame::CS2) {
            std::cout << "[Config] Ignoring foreign game section: " << key << " (active game: " << ActiveGameMeta() << ")\n";
            continue;
        }
        if (key.rfind("warzone.", 0) == 0 && g_activeGame != ActiveGame::Warzone) {
            std::cout << "[Config] Ignoring foreign game section: " << key << " (active game: " << ActiveGameMeta() << ")\n";
            continue;
        }

        if (key.rfind("fortnite.", 0) == 0 && g_activeGame != ActiveGame::Fortnite) {
            std::cout << "[Config] Ignoring foreign game section: " << key << " (active game: " << ActiveGameMeta() << ")\n";
            continue;
        }
        if ((key.rfind("esp.", 0) == 0 || key.rfind("aim.", 0) == 0 || key.rfind("veh.", 0) == 0)
            && g_activeGame != ActiveGame::FiveM) {
            std::cout << "[Config] Ignoring foreign game section: " << key << " (active game: " << ActiveGameMeta() << ")\n";
            continue;
        }


        if (DeserializeESP(key, val)) continue;
        if (DeserializeAim(key, val)) continue;
        if (DeserializeVehicle(key, val)) continue;
        if (DeserializeFriends(key, val)) continue;
        if (DeserializeAimType(key, val)) continue;
        if (DeserializeCS2(key, val)) continue;
        if (DeserializeWarzone(key, val)) continue;
        if (DeserializeFortnite(key, val)) continue;
        if (DeserializeFiveM(key, val)) continue;
        if (Hotkeys::DeserializeFeatureToggle(key, val)) continue;
        if (Hotkeys::DeserializeAction(key, val)) continue;


    }




    if (aimbot::config.aimbot_bind <= 0 || aimbot::config.aimbot_bind == VK_LBUTTON)
        aimbot::config.aimbot_bind = VK_RBUTTON;
    if (aimbot::config.aimbot_bind2 == VK_LBUTTON || aimbot::config.aimbot_bind2 == aimbot::config.aimbot_bind)
        aimbot::config.aimbot_bind2 = 0;

    if (g_activeGame == ActiveGame::FiveM) {
        auto& aim = aimbot::config;
        aim.fov_size = std::clamp(aim.fov_size, 5.f, 500.f);
        aim.dynamic_fov_min = std::clamp(aim.dynamic_fov_min, 5.f, 500.f);
        aim.dynamic_fov_max = std::clamp(aim.dynamic_fov_max, 5.f, 500.f);
        aim.dynamic_fov_near_distance = std::clamp(aim.dynamic_fov_near_distance, 0.f, 500.f);
        aim.dynamic_fov_far_distance = std::clamp(aim.dynamic_fov_far_distance,
            aim.dynamic_fov_near_distance + 1.f, 1000.f);
        aim.dynamic_fov_curve = std::clamp(aim.dynamic_fov_curve, .2f, 4.f);
        aim.dynamic_fov_transition_speed = std::clamp(aim.dynamic_fov_transition_speed, 1.f, 30.f);
        aim.sticky_retention = std::clamp(aim.sticky_retention, 0.f, 3.f);
        aim.sticky_tolerance = std::clamp(aim.sticky_tolerance, 0.f, 150.f);
        aim.switch_delay = std::clamp(aim.switch_delay, 0.f, 2.f);
        aim.trigger_delay = std::clamp(aim.trigger_delay, 0.f, 2.f);
        aim.trigger_burst_duration = std::clamp(aim.trigger_burst_duration, .02f, 2.f);
        aim.trigger_burst_interval = std::clamp(aim.trigger_burst_interval, .05f, 5.f);
        aim.trigger_cooldown = std::clamp(aim.trigger_cooldown, 0.f, 2.f);
        auto clampRange = [](float& value) { value = std::clamp(value, 1.f, 1000.f); };
        clampRange(aim.weapon_range_custom); clampRange(aim.weapon_range_melee);
        clampRange(aim.weapon_range_pistol); clampRange(aim.weapon_range_smg);
        clampRange(aim.weapon_range_shotgun); clampRange(aim.weapon_range_rifle);
        clampRange(aim.weapon_range_sniper); clampRange(aim.weapon_range_heavy);

        auto& visual = esp::config;
        visual.angel_wings = false;
        visual.devil_horns = false;
        visual.floating_crown = false;
        visual.offscreen_arrow_size = std::clamp(visual.offscreen_arrow_size, 6.f, 32.f);
        visual.offscreen_arrow_margin = std::clamp(visual.offscreen_arrow_margin, 12.f, 200.f);
        visual.offscreen_arrow_max_distance = std::clamp(visual.offscreen_arrow_max_distance, 1.f, 1000.f);
        visual.offscreen_arrow_opacity = std::clamp(visual.offscreen_arrow_opacity, .1f, 1.f);
        visual.active_panel_x = std::clamp(visual.active_panel_x, 0.f, .95f);
        visual.active_panel_y = std::clamp(visual.active_panel_y, 0.f, .95f);
        visual.active_panel_scale = std::clamp(visual.active_panel_scale, .65f, 1.6f);
        visual.active_panel_opacity = std::clamp(visual.active_panel_opacity, .15f, 1.f);
        visual.layout_spacing = std::clamp(visual.layout_spacing, 0.f, 18.f);
        visual.layout_text_scale = std::clamp(visual.layout_text_scale, .65f, 1.6f);
    }


    if (g_activeGame == ActiveGame::CS2) {
        auto& cs = CS2::config;
        cs.angel_wings = false;
        cs.devil_horns = false;
        cs.floating_crown = false;
        cs.dropped_weapons = false;
        // Existing profiles created before the CS2 Trainer support silently excluded every bot.
        // Keep bots targetable by default; the explicit menu toggle can still change this in-session.
        cs.aim_ignore_bots = false;
        if (cs.aim_bind <= 0) cs.aim_bind = 0x01;
        cs.offscreen_arrow_size = std::clamp(cs.offscreen_arrow_size, 6.f, 32.f);
        cs.offscreen_arrow_margin = std::clamp(cs.offscreen_arrow_margin, 12.f, 200.f);
        cs.offscreen_arrow_max_distance = std::clamp(cs.offscreen_arrow_max_distance, 1.f, 1000.f);
        cs.offscreen_arrow_opacity = std::clamp(cs.offscreen_arrow_opacity, .1f, 1.f);
        cs.active_features_x = std::clamp(cs.active_features_x, 0.f, .95f);
        cs.active_features_y = std::clamp(cs.active_features_y, 0.f, .95f);
        cs.active_features_scale = std::clamp(cs.active_features_scale, .65f, 1.6f);
        cs.active_features_opacity = std::clamp(cs.active_features_opacity, .15f, 1.f);
        cs.layout_spacing = std::clamp(cs.layout_spacing, 0.f, 18.f);
        cs.layout_text_scale = std::clamp(cs.layout_text_scale, .65f, 1.6f);
        float* freeLayout[] = { &cs.layout_name_x, &cs.layout_name_y, &cs.layout_health_x, &cs.layout_health_y,
            &cs.layout_armor_x, &cs.layout_armor_y, &cs.layout_weapon_x, &cs.layout_weapon_y,
            &cs.layout_distance_x, &cs.layout_distance_y, &cs.layout_ammo_x, &cs.layout_ammo_y };
        for (float* value : freeLayout) *value = std::clamp(*value, -.20f, 1.20f);
        int* anchors[] = { &cs.layout_name_anchor, &cs.layout_health_anchor,
            &cs.layout_armor_anchor, &cs.layout_weapon_anchor,
            &cs.layout_distance_anchor, &cs.layout_ammo_anchor };
        for (int* anchor : anchors) *anchor = std::clamp(*anchor, 0, 3);
        for (int i = 0; i < 5; ++i) {
            cs.aim_fov_curve_distance[i] = std::clamp(cs.aim_fov_curve_distance[i], 0.f, 1000.f);
            cs.aim_fov_curve_value[i] = std::clamp(cs.aim_fov_curve_value[i], 1.f, 500.f);
            if (i > 0)
                cs.aim_fov_curve_distance[i] = std::max(cs.aim_fov_curve_distance[i],
                    cs.aim_fov_curve_distance[i - 1] + .1f);
        }
        std::array<bool, 4> used{};
        for (int& bone : cs.aim_bone_priority) {
            bone = std::clamp(bone, 0, 3);
            if (used[bone]) {
                for (int candidate = 0; candidate < 4; ++candidate)
                    if (!used[candidate]) { bone = candidate; break; }
            }
            used[bone] = true;
        }
    }

    return true;
}


}
