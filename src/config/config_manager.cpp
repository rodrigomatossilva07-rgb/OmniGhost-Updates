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
#include "../constants.h"
#include "../app_version.h"
#include "../platform/app_paths.h"
#include "../../src/games/Fivem/aimbot/aimbot.h"
#include "../../src/games/Fivem/esp/esp.h"
#include "../../src/games/Fivem/friends/friends.h"
#include "../../src/licensing/license_service.h"
#include "../../src/updater/json.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>

namespace config_manager {

namespace {
constexpr int kConfigSchemaVersion = OmniGhost::Constants::kConfigSchemaVersion;

std::string CurrentAppVersion() { return OmniGhost::Version; }

std::string ActiveGameMeta() {
    switch (g_activeGame) {
    case ActiveGame::CS2: return "cs2";
    case ActiveGame::Rust: return "rust";
    case ActiveGame::Warzone: return "warzone";
    case ActiveGame::Apex: return "apex";
    case ActiveGame::Fortnite: return "fortnite";
    case ActiveGame::FiveM:
    default: return "fivem";
    }
}

std::string NewConfigId() {
    const auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::ostringstream id;
    id << std::hex << static_cast<unsigned long long>(ticks) << '-' << GetCurrentProcessId();
    return id.str();
}

bool ParseLine(const std::string& line, std::string& key, std::string& value) {
    const auto separator = line.find('=');
    if (separator == std::string::npos || separator == 0) return false;
    key = line.substr(0, separator);
    value = line.substr(separator + 1);
    return true;
}
}

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
    case ActiveGame::Rust:
        SerializeCS2(o);
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

std::string GetConfigFolder() {
    return OmniGhost::Paths::Configs().string();
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


        if (key.rfind("cs2.", 0) == 0 && g_activeGame != ActiveGame::CS2 &&
            g_activeGame != ActiveGame::Rust) {
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
        std::array<bool, 7> used{};
        for (int& bone : cs.aim_bone_priority) {
            bone = std::clamp(bone, 0, 6);
            if (used[bone]) {
                for (int candidate = 0; candidate < 7; ++candidate)
                    if (!used[candidate]) { bone = candidate; break; }
            }
            used[bone] = true;
        }
    }

    return true;
}

// =============================================================================
// DEV Config (admin only)
// =============================================================================

bool IsDevConfigAvailable() {
    // Only available in Development builds
    #if defined(OMNIGHOST_DEV_BUILD) || defined(OMNIGHOST_DEV_BUILD)
    return true;
    #else
    return false;
    #endif
}

namespace {
    // Helper to make DEV Config API requests
    OmniGhost::Auth::LauncherApiResponse DevConfigRequest(std::string_view method, std::string_view path, std::string_view body = {}) {
        return OmniGhost::Licensing::WebsiteRequest(method, path, body);
    }

    bool ParseDevConfigResponse(const std::string& body, std::string& outData, std::string& outError) {
        if (body.empty()) {
            outError = "Resposta vazia do servidor";
            return false;
        }
        OmniGhost::Update::Json::Value root;
        OmniGhost::Update::Json::Limits limits{24, 64u * 1024u, 5000};
        const auto parsed = OmniGhost::Update::Json::Parse(body, root, limits);
        const auto* object = parsed.ok ? OmniGhost::Update::Json::AsObject(root) : nullptr;
        if (!object) {
            outError = "Resposta JSON invalida";
            return false;
        }
        if (const auto* okValue = OmniGhost::Update::Json::Field(*object, "ok")) {
            const bool* ok = OmniGhost::Update::Json::AsBool(*okValue);
            if (!ok || !*ok) {
                if (const auto* errValue = OmniGhost::Update::Json::Field(*object, "error")) {
                    const std::string* err = OmniGhost::Update::Json::AsString(*errValue);
                    if (err) outError = *err;
                }
                if (outError.empty()) outError = "Operacao recusada pelo servidor";
                return false;
            }
        }
        if (const auto* dataValue = OmniGhost::Update::Json::Field(*object, "data")) {
            const std::string* data = OmniGhost::Update::Json::AsString(*dataValue);
            if (data) outData = *data;
        }
        return true;
    }

    // Parse detailed DEV config info from server response
    bool ParseDevConfigInfo(const OmniGhost::Update::Json::Object& obj, config_manager::DevConfigInfo& info) {
        auto getString = [&](const char* key, std::string& out) {
            if (const auto* val = OmniGhost::Update::Json::Field(obj, key)) {
                if (const auto* str = OmniGhost::Update::Json::AsString(*val)) out = *str;
            }
        };
        auto getInt = [&](const char* key, int& out) {
            if (const auto* val = OmniGhost::Update::Json::Field(obj, key)) {
                double number = 0.0;
                if (OmniGhost::Update::Json::AsDouble(*val, number)) out = static_cast<int>(number);
            }
        };
        auto getBool = [&](const char* key, bool& out) {
            if (const auto* val = OmniGhost::Update::Json::Field(obj, key)) {
                if (const auto* b = OmniGhost::Update::Json::AsBool(*val)) out = *b;
            }
        };
        auto getArray = [&](const char* key, std::vector<std::string>& out) {
            if (const auto* val = OmniGhost::Update::Json::Field(obj, key)) {
                if (const auto* arr = OmniGhost::Update::Json::AsArray(*val)) {
                    for (const auto& item : *arr) {
                        if (const auto* str = OmniGhost::Update::Json::AsString(item)) out.push_back(*str);
                    }
                }
            }
        };

        getString("name", info.name);
        getString("title", info.title);
        getString("description", info.description);
        getString("author", info.author);
        getString("version", info.version);
        getInt("version_int", info.version_int);
        getString("exe_version", info.exe_version);
        getString("changelog", info.changelog);
        getString("updated_at", info.updated_at);
        getString("download_url", info.download_url);
        getInt("downloads", info.downloads);
        getBool("official", info.official);
        getArray("tags", info.tags);
        getString("min_launcher_version", info.min_launcher_version);

        // Validar versão do EXE compatível
        if (info.exe_version.empty()) {
            info.exe_version = OmniGhost::Version;
        }

        return true;
    }

    // Storage key for notified updates
    const char* kNotifiedUpdatesKey = "dev_config_notified_updates";
    
    std::vector<std::string> GetNotifiedUpdates() {
        std::vector<std::string> notified;
        // Load from app settings / local storage
        // For now, return empty - can be persisted in config file
        return notified;
    }
    
    void AddNotifiedUpdate(const std::string& name) {
        // Save to persistent storage
        // Implementation depends on config system
    }
    
    bool IsUpdateNotified(const std::string& name) {
        auto notified = GetNotifiedUpdates();
        return std::find(notified.begin(), notified.end(), name) != notified.end();
    }
}

// Legacy simple list (backward compatibility)
std::vector<std::string> ListDevConfigs() {
    auto detailed = ListDevConfigsDetailed();
    std::vector<std::string> names;
    for (const auto& info : detailed) names.push_back(info.name);
    return names;
}

// Enhanced detailed list with versioning info
std::vector<DevConfigInfo> ListDevConfigsDetailed() {
    std::vector<DevConfigInfo> configs;
    
    if (!IsDevConfigAvailable()) {
        last_status = "Configs DEV disponiveis apenas em builds de desenvolvimento";
        return configs;
    }
    
    if (!OmniGhost::Licensing::IsAdmin()) {
        last_status = "Acesso negado: Apenas administradores podem listar configs DEV";
        return configs;
    }
    
    auto response = DevConfigRequest("GET", "/api/v1/dev-configs");
    if (response.statusCode < 200 || response.statusCode >= 300 || !response.error.empty()) {
        last_status = "Erro ao listar configs DEV: " + (response.error.empty() ? std::to_string(response.statusCode) : response.error);
        return configs;
    }
    
    OmniGhost::Update::Json::Value root;
    OmniGhost::Update::Json::Limits limits{24, 64u * 1024u, 5000};
    const auto parsed = OmniGhost::Update::Json::Parse(response.body, root, limits);
    const auto* object = parsed.ok ? OmniGhost::Update::Json::AsObject(root) : nullptr;
    if (!object) {
        last_status = "Resposta invalida do servidor";
        return configs;
    }
    
    if (const auto* dataValue = OmniGhost::Update::Json::Field(*object, "data")) {
        const auto* array = OmniGhost::Update::Json::AsArray(*dataValue);
        if (array) {
            for (const auto& item : *array) {
                const auto* itemObj = OmniGhost::Update::Json::AsObject(item);
                if (itemObj) {
                    DevConfigInfo info;
                    if (ParseDevConfigInfo(*itemObj, info)) {
                        // Check if this version is newer than what we have locally
                        // For now, mark all as potentially having updates
                        info.has_update = false; // Will be set by CheckDevConfigUpdates()
                        configs.push_back(std::move(info));
                    }
                }
            }
        }
    }
    
    last_status = "Lista detalhada de configs DEV atualizada (" + std::to_string(configs.size()) + " configs)";
    return configs;
}

// Download specific version of DEV config (or latest if target_version = -1)
bool DownloadDevConfigVersioned(const std::string& name, int target_version) {
    if (!OmniGhost::Licensing::IsAdmin()) {
        last_status = "Acesso negado: Apenas administradores podem baixar configs DEV";
        return false;
    }
    
    if (!IsDevConfigAvailable()) {
        last_status = "Configs DEV disponiveis apenas em builds de desenvolvimento";
        return false;
    }
    
    std::string path = "/api/v1/dev-configs/" + name + "/download";
    if (target_version > 0) {
        path += "?version=" + std::to_string(target_version);
    }
    
    auto response = DevConfigRequest("GET", path);
    
    if (response.statusCode < 200 || response.statusCode >= 300 || !response.error.empty()) {
        last_status = "Erro ao baixar config DEV: " + (response.error.empty() ? std::to_string(response.statusCode) : response.error);
        return false;
    }
    
    std::string configData, error;
    if (!ParseDevConfigResponse(response.body, configData, error)) {
        last_status = error;
        return false;
    }
    
    if (configData.empty()) {
        last_status = "Config DEV vazia ou nao encontrada";
        return false;
    }
    
    // Validar schema version antes de aplicar
    std::istringstream metaIn(configData);
    std::string line;
    int schema_version = 0;
    std::string meta_game;
    while (std::getline(metaIn, line)) {
        std::string key, val;
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            key = line.substr(0, eq);
            val = line.substr(eq + 1);
            if (key == "meta.schema_version") schema_version = atoi(val.c_str());
            else if (key == "meta.game") meta_game = val;
        }
    }
    
    // Validar compatibilidade
    constexpr int kCurrentSchemaVersion = 3; // kConfigSchemaVersion
    if (schema_version > kCurrentSchemaVersion) {
        last_status = "Config DEV requer launcher mais novo (schema v" + std::to_string(schema_version) + " > v" + std::to_string(kCurrentSchemaVersion) + ")";
        return false;
    }
    
    if (!meta_game.empty() && meta_game != "fivem" && meta_game != "cs2" && meta_game != "rust" && meta_game != "warzone" && meta_game != "apex" && meta_game != "fortnite") {
        last_status = "Config DEV para jogo desconhecido: " + meta_game;
        return false;
    }
    
    // Deserialize and apply the config
    if (!DeserializeAll(configData)) {
        last_status = "Falha ao aplicar config DEV: " + GetLastStatus();
        return false;
    }
    
    active_config_name = name;
    last_status = "Config DEV '" + name + (target_version > 0 ? " v" + std::to_string(target_version) : "") + "' baixada e aplicada com sucesso";
    return true;
}

// Upload new version of DEV config with full metadata
bool UploadDevConfigVersioned(const std::string& name, const std::string& title, const std::string& description, const std::string& changelog, const std::vector<std::string>& tags) {
    if (!OmniGhost::Licensing::IsAdmin()) {
        last_status = "Acesso negado: Apenas administradores podem enviar configs DEV";
        return false;
    }
    
    if (!IsDevConfigAvailable()) {
        last_status = "Configs DEV disponiveis apenas em builds de desenvolvimento";
        return false;
    }
    
    // Validar nome
    if (name.empty() || name.size() > 64) {
        last_status = "Nome invalido (1-64 caracteres)";
        return false;
    }
    
    // Validar caracteres permitidos
    for (char c : name) {
        if (!std::isalnum(c) && c != '-' && c != '_') {
            last_status = "Nome deve conter apenas letras, numeros, hifen e underscore";
            return false;
        }
    }
    
    // Serialize current config
    std::string configData = SerializeAll();
    if (configData.empty()) {
        last_status = "Nenhuma config ativa para enviar";
        return false;
    }
    
    // Validar config data size
    if (configData.size() > 1024 * 1024) { // 1MB max
        last_status = "Config muito grande (>1MB)";
        return false;
    }
    
    // Escape JSON string helper
    auto escapeJson = [](const std::string& input) {
        std::ostringstream out;
        for (char c : input) {
            switch (c) {
                case '"': out << "\\\""; break;
                case '\\': out << "\\\\"; break;
                case '\n': out << "\\n"; break;
                case '\r': out << "\\r"; break;
                case '\t': out << "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[7];
                        sprintf(buf, "\\u%04x", static_cast<unsigned char>(c));
                        out << buf;
                    } else {
                        out << c;
                    }
            }
        }
        return out.str();
    };
    
    // Create JSON body with full metadata
    std::ostringstream body;
    body << "{";
    body << "\"name\":\"" << escapeJson(name) << "\",";
    body << "\"title\":\"" << escapeJson(title) << "\",";
    body << "\"description\":\"" << escapeJson(description) << "\",";
    body << "\"changelog\":\"" << escapeJson(changelog) << "\",";
    body << "\"config\":\"" << escapeJson(configData) << "\",";
    body << "\"exe_version\":\"" << OmniGhost::Version << "\",";
    
    // Tags array
    body << "\"tags\":[";
    for (size_t i = 0; i < tags.size(); ++i) {
        if (i > 0) body << ",";
        body << "\"" << escapeJson(tags[i]) << "\"";
    }
    body << "],";
    
    if (!active_config_id.empty()) {
        body << "\"config_id\":\"" << active_config_id << "\",";
    }
    body << "\"author\":\"" << escapeJson(OmniGhost::Licensing::RemoteUsername()) << "\"";
    body << "}";
    
    auto response = DevConfigRequest("POST", "/api/v1/dev-configs", body.str());
    
    if (response.statusCode < 200 || response.statusCode >= 300 || !response.error.empty()) {
        last_status = "Erro ao enviar config DEV: " + (response.error.empty() ? std::to_string(response.statusCode) : response.error);
        return false;
    }
    
    std::string resultData, error;
    if (!ParseDevConfigResponse(response.body, resultData, error)) {
        last_status = error;
        return false;
    }
    
    last_status = "Config DEV '" + name + "' enviada com sucesso (v" + OmniGhost::Version + ")";
    return true;
}

bool DeleteDevConfig(const std::string& name) {
    if (!OmniGhost::Licensing::IsAdmin()) {
        last_status = "Acesso negado: Apenas administradores podem deletar configs DEV";
        return false;
    }
    
    if (!IsDevConfigAvailable()) {
        last_status = "Configs DEV disponiveis apenas em builds de desenvolvimento";
        return false;
    }
    
    std::string path = "/api/v1/dev-configs/" + name;
    auto response = DevConfigRequest("DELETE", path);
    
    if (response.statusCode < 200 || response.statusCode >= 300 || !response.error.empty()) {
        last_status = "Erro ao deletar config DEV: " + (response.error.empty() ? std::to_string(response.statusCode) : response.error);
        return false;
    }
    
    std::string resultData, error;
    if (!ParseDevConfigResponse(response.body, resultData, error)) {
        last_status = error;
        return false;
    }
    
    last_status = "Config DEV '" + name + "' deletada com sucesso";
    return true;
}

// Check for DEV config updates on server
bool CheckDevConfigUpdates() {
    if (!OmniGhost::Licensing::IsAdmin()) {
        last_status = "Acesso negado: Apenas administradores podem verificar updates DEV";
        return false;
    }
    
    if (!IsDevConfigAvailable()) {
        last_status = "Configs DEV disponiveis apenas em builds de desenvolvimento";
        return false;
    }
    
    auto response = DevConfigRequest("GET", "/api/v1/dev-configs/check-updates");
    
    if (response.statusCode < 200 || response.statusCode >= 300 || !response.error.empty()) {
        last_status = "Erro ao verificar updates DEV: " + (response.error.empty() ? std::to_string(response.statusCode) : response.error);
        return false;
    }
    
    OmniGhost::Update::Json::Value root;
    OmniGhost::Update::Json::Limits limits{24, 64u * 1024u, 5000};
    const auto parsed = OmniGhost::Update::Json::Parse(response.body, root, limits);
    const auto* object = parsed.ok ? OmniGhost::Update::Json::AsObject(root) : nullptr;
    if (!object) {
        last_status = "Resposta invalida do servidor";
        return false;
    }
    
    // Server returns list of configs with newer versions
    if (const auto* dataValue = OmniGhost::Update::Json::Field(*object, "data")) {
        const auto* array = OmniGhost::Update::Json::AsArray(*dataValue);
        if (array) {
            int updatesFound = 0;
            for (const auto& item : *array) {
                const auto* itemObj = OmniGhost::Update::Json::AsObject(item);
                if (itemObj) {
                    if (const auto* nameValue = OmniGhost::Update::Json::Field(*itemObj, "name")) {
                        if (const auto* name = OmniGhost::Update::Json::AsString(*nameValue)) {
                            // Mark this config as having an update available
                            // Could store in memory/cache for UI
                            updatesFound++;
                        }
                    }
                }
            }
            last_status = updatesFound > 0 
                ? std::to_string(updatesFound) + " config(s) DEV com atualizacao disponivel"
                : "Nenhuma atualizacao DEV disponivel";
        }
    }
    
    return true;
}

// Get list of DEV configs that have updates available
std::vector<DevConfigInfo> GetPendingDevConfigUpdates() {
    std::vector<DevConfigInfo> updates;
    
    if (!OmniGhost::Licensing::IsAdmin() || !IsDevConfigAvailable()) {
        return updates;
    }
    
    auto response = DevConfigRequest("GET", "/api/v1/dev-configs/pending-updates");
    
    if (response.statusCode < 200 || response.statusCode >= 300 || !response.error.empty()) {
        return updates;
    }
    
    OmniGhost::Update::Json::Value root;
    OmniGhost::Update::Json::Limits limits{24, 64u * 1024u, 5000};
    const auto parsed = OmniGhost::Update::Json::Parse(response.body, root, limits);
    const auto* object = parsed.ok ? OmniGhost::Update::Json::AsObject(root) : nullptr;
    if (!object) return updates;
    
    if (const auto* dataValue = OmniGhost::Update::Json::Field(*object, "data")) {
        const auto* array = OmniGhost::Update::Json::AsArray(*dataValue);
        if (array) {
            for (const auto& item : *array) {
                const auto* itemObj = OmniGhost::Update::Json::AsObject(item);
                if (itemObj) {
                    DevConfigInfo info;
                    if (ParseDevConfigInfo(*itemObj, info)) {
                        info.has_update = true;
                        updates.push_back(std::move(info));
                    }
                }
            }
        }
    }
    
    return updates;
}

// Mark DEV config update notification as seen
void MarkDevConfigUpdateNotified(const std::string& name) {
    // Store in persistent config
    // For now, just track in memory
    AddNotifiedUpdate(name);
}

std::string GetLastStatus() {
    return last_status;
}

}
