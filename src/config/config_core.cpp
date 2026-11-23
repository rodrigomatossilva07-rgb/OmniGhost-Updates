#include "config_core.h"
#include "config_manager.h"
#include "app_settings.h"
#include "globals.h"

#include <nlohmann/json.hpp>

namespace OmniGhost::Config {





nlohmann::json SerializeAllToJson() {
    nlohmann::json j;


    j["app_settings"] = nlohmann::json::object({
        {"language", static_cast<int>(app_settings::config.language)},
        {"start_behavior", static_cast<int>(app_settings::config.start_behavior)},
        {"remember_last_game", app_settings::config.remember_last_game},
        {"minimize_behavior", static_cast<int>(app_settings::config.minimize_behavior)},
        {"close_behavior", static_cast<int>(app_settings::config.close_behavior)},
        {"auto_save", app_settings::config.auto_save},
        {"menu_bind", app_settings::config.menu_bind},
        {"black_background", app_settings::config.black_background},
        {"black_level", app_settings::config.black_level},
        {"performance_mode", app_settings::config.performance_mode},
        {"auto_performance", app_settings::config.auto_performance},
        {"particles", app_settings::config.particles},
        {"snow_effect", app_settings::config.snow_effect},
        {"matrix_rain", app_settings::config.matrix_rain},
        {"digital_rain_level", static_cast<int>(app_settings::config.digital_rain_level)},
        {"animation_intensity", static_cast<int>(app_settings::config.animation_intensity)},
        {"ui_sounds", app_settings::config.ui_sounds},
        {"ui_sound_volume", app_settings::config.ui_sound_volume},
        {"notification_sounds", app_settings::config.notification_sounds},
        {"startup_sound", app_settings::config.startup_sound},
        {"high_contrast", app_settings::config.high_contrast},
        {"ui_scale", app_settings::config.ui_scale},
        {"monitor_index", app_settings::config.monitor_index},
        {"show_fps", app_settings::config.show_fps},
        {"telemetry_log_enabled", app_settings::config.telemetry_log_enabled},
        {"show_hotkey_overlay", app_settings::config.show_hotkey_overlay},
        {"vsync", app_settings::config.vsync},
        {"overlay_position_mode", app_settings::config.overlay_position_mode},
        {"overlay_scale", app_settings::config.overlay_scale},
        {"overlay_lock_to_monitor", app_settings::config.overlay_lock_to_monitor},
        {"update_channel", static_cast<int>(app_settings::config.update_channel)},
        {"update_auto_check", app_settings::config.update_auto_check},
        {"update_auto_download", app_settings::config.update_auto_download},
        {"update_install_on_exit", app_settings::config.update_install_on_exit},
        {"show_advanced", app_settings::config.show_advanced},
        {"color_primary", app_settings::config.color_primary},
        {"color_secondary", app_settings::config.color_secondary},
        {"color_fps", app_settings::config.color_fps},
        {"color_particles", app_settings::config.color_particles},
        {"theme_mode", app_settings::config.theme_mode},
        {"accent_preset", app_settings::config.accent_preset},
        {"visual_style", app_settings::config.visual_style}
    });


    j["esp"] = SerializeESPSettings();


    j["aimbot"] = SerializeAimSettings();


    j["trigger"] = SerializeTriggerSettings();


    j["radar"] = SerializeRadarSettings();


    j["misc"] = SerializeMiscSettings();


    j["cs2"] = SerializeCS2Settings();
    j["rust"] = SerializeRustSettings();
    j["fivem"] = SerializeFiveMSettings();
    j["fortnite"] = SerializeFortniteSettings();
    j["warzone"] = SerializeWarzoneSettings();
    j["apex"] = SerializeApexSettings();

    return j;
}

bool DeserializeAllFromJson(std::string_view json) {
    try {
        nlohmann::json j = nlohmann::json::parse(json);
        return DeserializeAllFromJson(j);
    } catch (...) {
        return false;
    }
}

bool DeserializeAllFromJson(const nlohmann::json& j) {
    try {

        if (j.contains("app_settings")) {
            const auto& s = j["app_settings"];
            app_settings::config.language = static_cast<app_settings::Language>(s.value("language", 0));
            app_settings::config.start_behavior = static_cast<app_settings::StartBehavior>(s.value("start_behavior", 0));
            app_settings::config.remember_last_game = s.value("remember_last_game", true);
            app_settings::config.minimize_behavior = static_cast<app_settings::MinimizeBehavior>(s.value("minimize_behavior", 0));
            app_settings::config.close_behavior = static_cast<app_settings::CloseBehavior>(s.value("close_behavior", 0));
            app_settings::config.auto_save = s.value("auto_save", true);
            app_settings::config.menu_bind = s.value("menu_bind", 0x2D);
            app_settings::config.black_background = s.value("black_background", true);
            app_settings::config.black_level = s.value("black_level", 100.0f);
            app_settings::config.performance_mode = s.value("performance_mode", false);
            app_settings::config.auto_performance = s.value("auto_performance", true);
            app_settings::config.particles = s.value("particles", true);
            app_settings::config.snow_effect = s.value("snow_effect", false);
            app_settings::config.matrix_rain = s.value("matrix_rain", true);
            app_settings::config.digital_rain_level = static_cast<app_settings::EffectLevel>(s.value("digital_rain_level", 2));
            app_settings::config.animation_intensity = static_cast<app_settings::EffectLevel>(s.value("animation_intensity", 1));
            app_settings::config.ui_sounds = s.value("ui_sounds", true);
            app_settings::config.ui_sound_volume = s.value("ui_sound_volume", 35.0f);
            app_settings::config.notification_sounds = s.value("notification_sounds", true);
            app_settings::config.startup_sound = s.value("startup_sound", true);
            app_settings::config.high_contrast = s.value("high_contrast", false);
            app_settings::config.ui_scale = s.value("ui_scale", 1.0f);
            app_settings::config.monitor_index = s.value("monitor_index", -1);
            app_settings::config.show_fps = s.value("show_fps", true);
            app_settings::config.telemetry_log_enabled = s.value("telemetry_log_enabled", false);
            app_settings::config.show_hotkey_overlay = s.value("show_hotkey_overlay", false);
            app_settings::config.vsync = s.value("vsync", false);
            app_settings::config.overlay_position_mode = s.value("overlay_position_mode", 0);
            app_settings::config.overlay_scale = s.value("overlay_scale", 1.0f);
            app_settings::config.overlay_lock_to_monitor = s.value("overlay_lock_to_monitor", true);
            app_settings::config.update_channel = static_cast<app_settings::UpdateChannel>(s.value("update_channel", 0));
            app_settings::config.update_auto_check = s.value("update_auto_check", true);
            app_settings::config.update_auto_download = s.value("update_auto_download", false);
            app_settings::config.update_install_on_exit = s.value("update_install_on_exit", false);
            app_settings::config.show_advanced = s.value("show_advanced", false);
            app_settings::config.color_primary = s.value("color_primary", IM_COL32(212, 175, 55, 255));
            app_settings::config.color_secondary = s.value("color_secondary", IM_COL32(40, 40, 50, 255));
            app_settings::config.color_fps = s.value("color_fps", IM_COL32(0, 255, 120, 255));
            app_settings::config.color_particles = s.value("color_particles", IM_COL32(212, 175, 55, 80));
            app_settings::config.theme_mode = s.value("theme_mode", 0);
            app_settings::config.accent_preset = s.value("accent_preset", 0);
            app_settings::config.visual_style = s.value("visual_style", 0);
        }


        if (j.contains("esp")) DeserializeESPSettings(j["esp"]);
        if (j.contains("aimbot")) DeserializeAimSettings(j["aimbot"]);
        if (j.contains("trigger")) DeserializeTriggerSettings(j["trigger"]);
        if (j.contains("radar")) DeserializeRadarSettings(j["radar"]);
        if (j.contains("misc")) DeserializeMiscSettings(j["misc"]);


        if (j.contains("cs2")) DeserializeCS2Settings(j["cs2"]);
        if (j.contains("rust")) DeserializeRustSettings(j["rust"]);
        if (j.contains("fivem")) DeserializeFiveMSettings(j["fivem"]);
        if (j.contains("fortnite")) DeserializeFortniteSettings(j["fortnite"]);
        if (j.contains("warzone")) DeserializeWarzoneSettings(j["warzone"]);
        if (j.contains("apex")) DeserializeApexSettings(j["apex"]);

        return true;
    } catch (...) {
        return false;
    }
}





nlohmann::json SerializeESPSettings() {


    return nlohmann::json::object({
        {"enabled", true},
        {"box", true},
        {"box_type", 0},
        {"health", true},
        {"health_type", 0},
        {"name", true},
        {"distance", true},
        {"skeleton", false},
        {"head_dot", false},
        {"weapon", true},
        {"ammo", false},
        {"flags", true},
        {"fov", 90.0f},
        {"max_distance", 300.0f},
        {"visible_only", false},
        {"dormant", true},
        {"dormant_alpha", 0.5f}
    });
}

bool DeserializeESPSettings(const nlohmann::json& j) {

    return true;
}





nlohmann::json SerializeAimSettings() {
    return nlohmann::json::object({
        {"enabled", true},
        {"fov", 10.0f},
        {"smooth", 8.0f},
        {"bone", 0},
        {"rcs", true},
        {"rcs_x", 1.5f},
        {"rcs_y", 1.5f},
        {"multipoint", false},
        {"resolver", false},
        {"autowall", false},
        {"autoscope", true},
        {"silent", false},
        {"p_silent", false},
        {"hitbox", 0},
        {"hit_chance", 80},
        {"min_damage", 1}
    });
}

bool DeserializeAimSettings(const nlohmann::json& j) {
    return true;
}





nlohmann::json SerializeTriggerSettings() {
    return nlohmann::json::object({
        {"enabled", false},
        {"delay", 30},
        {"burst", 1},
        {"breakable", false},
        {"head", true},
        {"chest", true},
        {"stomach", true},
        {"arms", false},
        {"legs", false},
        {"autowall", false},
        {"min_damage", 1},
        {"hit_chance", 80}
    });
}

bool DeserializeTriggerSettings(const nlohmann::json& j) {
    return true;
}





nlohmann::json SerializeRadarSettings() {
    return nlohmann::json::object({
        {"enabled", true},
        {"range", 300.0f},
        {"size", 200.0f},
        {"alpha", 0.8f},
        {"show_names", true},
        {"show_health", true},
        {"show_distance", true},
        {"rotate", true},
        {"crosshair", true},
        {"enemies_only", false}
    });
}

bool DeserializeRadarSettings(const nlohmann::json& j) {
    return true;
}





nlohmann::json SerializeMiscSettings() {
    return nlohmann::json::object({
        {"bhop", false},
        {"autostrafe", false},
        {"edge_jump", false},
        {"fast_duck", false},
        {"slow_walk", false},
        {"thirdperson", false},
        {"thirdperson_dist", 100.0f},
        {"fov_override", 0.0f},
        {"viewmodel_fov", 68.0f},
        {"aspect_ratio", 0.0f},
        {"no_flash", false},
        {"no_smoke", false},
        {"no_recoil", false},
        {"no_spread", false}
    });
}

bool DeserializeMiscSettings(const nlohmann::json& j) {
    return true;
}





nlohmann::json SerializeCS2Settings() {
    return nlohmann::json::object({
        {"esp_enabled", true},
        {"chams_enabled", false},
        {"chams_type", 0},
        {"glow_enabled", false},
        {"glow_style", 0},
        {"backtrack", false},
        {"backtrack_time", 200},
        {"resolver", true},
        {"antiaim", false},
        {"pitch", 0},
        {"yaw", 0},
        {"fake_yaw", 0},
        {"freestanding", false}
    });
}

bool DeserializeCS2Settings(const nlohmann::json& j) {
    return true;
}





nlohmann::json SerializeRustSettings() {
    return nlohmann::json::object({
        {"esp_enabled", true},
        {"player_esp", true},
        {"resource_esp", true},
        {"heli_esp", true},
        {"bradley_esp", true},
        {"crate_esp", true},
        {"corpse_esp", false},
        {"sleeping_esp", false},
        {"trap_esp", true},
        {"vehicle_esp", true},
        {"animal_esp", false},
        {"stash_esp", true},
        {"item_drop_esp", false},
        {"gatherable_esp", true},
        {"npc_esp", true},
        {"aim_enabled", true},
        {"aim_fov", 5.0f},
        {"aim_smooth", 10.0f},
        {"aim_bone", 0},
        {"recoil_control", true},
        {"rcs_x", 1.0f},
        {"rcs_y", 1.0f},
        {"auto_shoot", false},
        {"auto_melee", false},
        {"auto_reload", false},
        {"inf_ammo", false},
        {"no_recoil", false},
        {"no_spread", false},
        {"super_eoka", false},
        {"rapid_fire", false},
        {"admin_mode", false}
    });
}

bool DeserializeRustSettings(const nlohmann::json& j) {
    return true;
}





nlohmann::json SerializeFiveMSettings() {
    return nlohmann::json::object({
        {"esp_enabled", true},
        {"player_box", true},
        {"player_health", true},
        {"player_armor", true},
        {"player_weapon", true},
        {"player_distance", true},
        {"vehicle_esp", true},
        {"vehicle_health", true},
        {"vehicle_driver", true},
        {"object_esp", false},
        {"pickup_esp", false},
        {"aim_enabled", true},
        {"aim_fov", 10.0f},
        {"aim_smooth", 10.0f},
        {"aim_bone", 0},
        {"aim_fov_type", 0},
        {"trigger_enabled", false},
        {"trigger_delay", 50},
        {"silent_aim", false},
        {"aim_assist", false},
        {"rcs", true},
        {"rcs_x", 1.0f},
        {"rcs_y", 1.0f}
    });
}

bool DeserializeFiveMSettings(const nlohmann::json& j) {
    return true;
}





nlohmann::json SerializeFortniteSettings() {
    return nlohmann::json::object({
        {"esp_enabled", true},
        {"player_box", true},
        {"player_skeleton", false},
        {"player_health", true},
        {"player_shield", true},
        {"player_weapon", true},
        {"player_distance", true},
        {"player_name", true},
        {"vehicle_esp", true},
        {"chest_esp", true},
        {"ammo_esp", false},
        {"supply_drop_esp", true},
        {"lama_esp", true},
        {"weak_spot_esp", false},
        {"aim_enabled", true},
        {"aim_fov", 150.0f},
        {"aim_smooth", 5.0f},
        {"aim_bone", 0},
        {"aim_key", 0},
        {"trigger_enabled", true},
        {"trigger_delay", 20},
        {"silent_aim", false},
        {"prediction", true},
        {"bullet_tp", false}
    });
}

bool DeserializeFortniteSettings(const nlohmann::json& j) {
    return true;
}





nlohmann::json SerializeWarzoneSettings() {
    return nlohmann::json::object({
        {"esp_enabled", true},
        {"player_box", true},
        {"player_skeleton", false},
        {"player_health", true},
        {"player_armor", true},
        {"player_weapon", true},
        {"player_distance", true},
        {"player_name", true},
        {"vehicle_esp", true},
        {"loot_esp", true},
        {"aim_enabled", true},
        {"aim_fov", 5.0f},
        {"aim_smooth", 12.0f},
        {"aim_bone", 0},
        {"aim_key", 0},
        {"trigger_enabled", true},
        {"trigger_delay", 30},
        {"silent_aim", false},
        {"prediction", true},
        {"no_recoil", false},
        {"no_spread", false}
    });
}

bool DeserializeWarzoneSettings(const nlohmann::json& j) {
    return true;
}





nlohmann::json SerializeApexSettings() {
    return nlohmann::json::object({
        {"esp_enabled", true},
        {"player_box", true},
        {"player_health", true},
        {"player_shield", true},
        {"player_name", true},
        {"player_distance", true},
        {"player_weapon", true},
        {"item_esp", true},
        {"loot_tick_esp", true},
        {"death_box_esp", true},
        {"vehicle_esp", false},
        {"aim_enabled", true},
        {"aim_fov", 80.0f},
        {"aim_smooth", 15.0f},
        {"aim_bone", 0},
        {"aim_key", 0},
        {"trigger_enabled", true},
        {"trigger_delay", 25},
        {"silent_aim", false},
        {"prediction", true},
        {"rcs", true},
        {"rcs_x", 1.0f},
        {"rcs_y", 1.0f},
        {"no_recoil", false},
        {"no_spread", false}
    });
}

bool DeserializeApexSettings(const nlohmann::json& j) {
    return true;
}





std::string GetCurrentGameName() {


    return "";
}

}