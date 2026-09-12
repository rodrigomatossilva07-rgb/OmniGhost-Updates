#include "cs2_config.h"
#include "../src/platform/app_paths.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace CS2 {
// ── Config save/load (simple key=value in %LocalAppData%) ────────────────
static std::string ConfigPath(const char* name) {
    return (OmniGhost::Paths::Cs2Configs() / (std::string(name) + ".cfg")).string();
}

bool SaveConfig(const char* name) {
    fs::create_directories(OmniGhost::Paths::Cs2Configs());
    std::ofstream f(ConfigPath(name));
    if (!f) return false;
    auto w = [&](const char* k, auto v) { f << k << '=' << v << '\n'; };
    auto wc = [&](const char* key, const float value[4]) {
        for (int i = 0; i < 4; ++i)
            f << key << '.' << i << '=' << value[i] << '\n';
    };
    w("esp_enabled", config.esp_enabled ? 1 : 0);
    w("box", config.box ? 1 : 0);
    w("skeleton", config.skeleton ? 1 : 0);
    w("health_bar", config.health_bar ? 1 : 0);
    w("armor_bar", config.armor_bar ? 1 : 0);
    w("name", config.name ? 1 : 0);
    w("distance", config.distance ? 1 : 0);
    w("team_check", config.team_check ? 1 : 0);
    w("show_bots", config.show_bots ? 1 : 0);
    w("head_dot", config.head_dot ? 1 : 0);
    w("trails", config.trails ? 1 : 0);
    w("head_halo", config.head_halo ? 1 : 0);
    w("look_direction", config.look_direction ? 1 : 0);
    w("trail_duration", config.trail_duration);
    w("trail_thickness", config.trail_thickness);
    w("look_direction_length", config.look_direction_length);
    w("snaplines", config.snaplines ? 1 : 0);
    w("weapon_icons", config.weapon_icons ? 1 : 0);
    w("bomb_timer", config.bomb_timer ? 1 : 0);
    w("spectator_list", config.spectator_list ? 1 : 0);
    w("smoke_flash", config.smoke_flash ? 1 : 0);
    w("scope_check", config.scope_check ? 1 : 0);
    w("recoil_visual", config.recoil_visual ? 1 : 0);
    w("radar_2d", config.radar_2d ? 1 : 0);
    w("skeleton_lod", config.skeleton_lod ? 1 : 0);
    w("skeleton_lod_distance", config.skeleton_lod_distance);
    w("max_distance", config.max_distance);
    w("max_entities", config.max_entities);
    w("auto_entity_cap", config.auto_entity_cap ? 1 : 0);
    w("offscreen_arrows", config.offscreen_arrows ? 1 : 0);
    w("highlight_aim_target", config.highlight_aim_target ? 1 : 0);
    w("distance_feet", config.distance_feet ? 1 : 0);
    w("hotkey_overlay", config.hotkey_overlay ? 1 : 0);
    w("panic_key_enabled", config.panic_key_enabled ? 1 : 0);
    w("panic_key", config.panic_key);
    w("bone_draw_arms", config.bone_draw_arms ? 1 : 0);
    w("bone_draw_legs", config.bone_draw_legs ? 1 : 0);
    w("performance_mode", config.performance_mode ? 1 : 0);
    w("aim_enabled", config.aim_enabled ? 1 : 0);
    w("aim_draw_fov", config.aim_draw_fov ? 1 : 0);
    w("aim_dynamic_fov", config.aim_dynamic_fov ? 1 : 0);
    w("aim_fov", config.aim_fov);
    w("aim_fov_min", config.aim_fov_min);
    w("aim_smooth", config.aim_smooth);
    w("aim_max_dist", config.aim_max_dist);
    w("aim_bone", config.aim_bone);
    w("aim_bind", config.aim_bind);
    w("aim_bind2", config.aim_bind2);
    w("aim_bind3", config.aim_bind3);
    w("aim_deadzone", config.aim_deadzone);
    w("sticky_ms", config.sticky_ms);
    w("aim_prediction", config.aim_prediction ? 1 : 0);
    w("prediction_strength", config.prediction_strength);
    w("hit_chance_ui", config.hit_chance_ui ? 1 : 0);
    w("aim_switch_cooldown_ms", config.aim_switch_cooldown_ms);
    w("rcs_enabled", config.rcs_enabled ? 1 : 0);
    w("rcs_strength", config.rcs_strength);
    w("rcs_sensitivity", config.rcs_sensitivity);
    w("rcs_recoil_scale", config.rcs_recoil_scale);
    w("rcs_recovery_ms", config.rcs_recovery_ms);
    w("rcs_pattern_fallback", config.rcs_pattern_fallback ? 1 : 0);
    w("rcs_with_aimbot", config.rcs_with_aimbot ? 1 : 0);
    w("trigger_enabled", config.trigger_enabled ? 1 : 0);
    w("trigger_bind", config.trigger_bind);
    w("trigger_delay_ms", config.trigger_delay_ms);
    w("trigger_team_check", config.trigger_team_check ? 1 : 0);
    w("trigger_scoped_only", config.trigger_scoped_only ? 1 : 0);
    w("trigger_head_only", config.trigger_head_only ? 1 : 0);
    w("trigger_use_ident", config.trigger_use_ident ? 1 : 0);
    w("profile", config.profile);
    w("auto_dump_on_mismatch", config.auto_dump_on_mismatch ? 1 : 0);
    wc("col_enemy", config.col_enemy);
    wc("col_team", config.col_team);
    wc("col_skeleton", config.col_skeleton);
    wc("col_box", config.col_box);
    wc("col_name", config.col_name);
    wc("col_weapon", config.col_weapon);
    wc("col_target", config.col_target);
    wc("col_joints", config.col_joints);
    wc("col_health", config.col_health);
    wc("col_armor", config.col_armor);
    wc("col_snaplines", config.col_snaplines);
    wc("col_trail", config.col_trail);
    wc("col_halo", config.col_halo);
    wc("col_look", config.col_look);
    return true;
}

bool LoadConfig(const char* name) {
    std::ifstream f(ConfigPath(name));
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq);
        const std::string v = line.substr(eq + 1);
        auto bi = [&](const char* key, bool& dst) {
            if (k == key) dst = (std::atoi(v.c_str()) != 0);
        };
        auto fi = [&](const char* key, float& dst) {
            if (k == key) dst = (float)std::atof(v.c_str());
        };
        auto ii = [&](const char* key, int& dst) {
            if (k == key) dst = std::atoi(v.c_str());
        };
        auto ci = [&](const char* key, float dst[4]) {
            for (int i = 0; i < 4; ++i) {
                if (k == std::string(key) + "." + std::to_string(i))
                    dst[i] = (float)std::atof(v.c_str());
            }
        };
        bi("esp_enabled", config.esp_enabled);
        bi("box", config.box);
        bi("skeleton", config.skeleton);
        bi("health_bar", config.health_bar);
        bi("armor_bar", config.armor_bar);
        bi("name", config.name);
        bi("distance", config.distance);
        bi("team_check", config.team_check);
        bi("show_bots", config.show_bots);
        bi("head_dot", config.head_dot);
        bi("trails", config.trails);
        bi("head_halo", config.head_halo);
        bi("look_direction", config.look_direction);
        fi("trail_duration", config.trail_duration);
        fi("trail_thickness", config.trail_thickness);
        fi("look_direction_length", config.look_direction_length);
        bi("snaplines", config.snaplines);
        bi("weapon_icons", config.weapon_icons);
        bi("bomb_timer", config.bomb_timer);
        bi("spectator_list", config.spectator_list);
        bi("smoke_flash", config.smoke_flash);
        bi("scope_check", config.scope_check);
        bi("recoil_visual", config.recoil_visual);
        bi("radar_2d", config.radar_2d);
        bi("skeleton_lod", config.skeleton_lod);
        fi("skeleton_lod_distance", config.skeleton_lod_distance);
        fi("max_distance", config.max_distance);
        ii("max_entities", config.max_entities);
        bi("auto_entity_cap", config.auto_entity_cap);
        bi("offscreen_arrows", config.offscreen_arrows);
        bi("highlight_aim_target", config.highlight_aim_target);
        bi("distance_feet", config.distance_feet);
        bi("hotkey_overlay", config.hotkey_overlay);
        bi("panic_key_enabled", config.panic_key_enabled);
        ii("panic_key", config.panic_key);
        bi("bone_draw_arms", config.bone_draw_arms);
        bi("bone_draw_legs", config.bone_draw_legs);
        bi("performance_mode", config.performance_mode);
        bi("aim_enabled", config.aim_enabled);
        bi("aim_draw_fov", config.aim_draw_fov);
        bi("aim_dynamic_fov", config.aim_dynamic_fov);
        fi("aim_fov", config.aim_fov);
        fi("aim_fov_min", config.aim_fov_min);
        fi("aim_smooth", config.aim_smooth);
        fi("aim_max_dist", config.aim_max_dist);
        ii("aim_bone", config.aim_bone);
        ii("aim_bind", config.aim_bind);
        ii("aim_bind2", config.aim_bind2);
        ii("aim_bind3", config.aim_bind3);
        fi("aim_deadzone", config.aim_deadzone);
        fi("sticky_ms", config.sticky_ms);
        bi("aim_prediction", config.aim_prediction);
        fi("prediction_strength", config.prediction_strength);
        bi("hit_chance_ui", config.hit_chance_ui);
        fi("aim_switch_cooldown_ms", config.aim_switch_cooldown_ms);
        bi("rcs_enabled", config.rcs_enabled);
        fi("rcs_strength", config.rcs_strength);
        fi("rcs_sensitivity", config.rcs_sensitivity);
        fi("rcs_recoil_scale", config.rcs_recoil_scale);
        ii("rcs_recovery_ms", config.rcs_recovery_ms);
        bi("rcs_pattern_fallback", config.rcs_pattern_fallback);
        bi("rcs_with_aimbot", config.rcs_with_aimbot);
        bi("trigger_enabled", config.trigger_enabled);
        ii("trigger_bind", config.trigger_bind);
        ii("trigger_delay_ms", config.trigger_delay_ms);
        bi("trigger_team_check", config.trigger_team_check);
        bi("trigger_scoped_only", config.trigger_scoped_only);
        bi("trigger_head_only", config.trigger_head_only);
        bi("trigger_use_ident", config.trigger_use_ident);
        ii("profile", config.profile);
        bi("auto_dump_on_mismatch", config.auto_dump_on_mismatch);
        ci("col_enemy", config.col_enemy);
        ci("col_team", config.col_team);
        ci("col_skeleton", config.col_skeleton);
        ci("col_box", config.col_box);
        ci("col_name", config.col_name);
        ci("col_weapon", config.col_weapon);
        ci("col_target", config.col_target);
        ci("col_joints", config.col_joints);
        ci("col_health", config.col_health);
        ci("col_armor", config.col_armor);
        ci("col_snaplines", config.col_snaplines);
        ci("col_trail", config.col_trail);
        ci("col_halo", config.col_halo);
        ci("col_look", config.col_look);
    }
    return true;
}


} // namespace CS2
