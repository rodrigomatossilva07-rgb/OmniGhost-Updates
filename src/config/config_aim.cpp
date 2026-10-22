#include "config_aim.h"
#include "config_core.h"
#include "../src/games/Fivem/aimbot/aimbot.h"
#include "../src/games/Fivem/aimbot/aim_type.h"
#include <algorithm>

namespace config_manager {

void SerializeAim(std::ostringstream& o) {
    auto& a = aimbot::config;
    WBool(o, "aim.aimbot_enabled", a.aimbot_enabled);
    WBool(o, "aim.show_fov", a.show_fov);
    WFloat(o, "aim.fov_size", a.fov_size);
    WInt(o, "aim.dynamic_fov_mode", (int)a.dynamic_fov_mode);
    WFloat(o, "aim.dynamic_fov_min", a.dynamic_fov_min);
    WFloat(o, "aim.dynamic_fov_max", a.dynamic_fov_max);
    WFloat(o, "aim.dynamic_fov_near_distance", a.dynamic_fov_near_distance);
    WFloat(o, "aim.dynamic_fov_far_distance", a.dynamic_fov_far_distance);
    WFloat(o, "aim.dynamic_fov_curve", a.dynamic_fov_curve);
    WFloat(o, "aim.dynamic_fov_transition_speed", a.dynamic_fov_transition_speed);
    WFloat(o, "aim.max_distance", a.max_distance);
    WFloat(o, "aim.smooth_x", a.smooth_x);
    WFloat(o, "aim.smooth_y", a.smooth_y);
    WBool(o, "aim.humanize", a.humanize);
    WBool(o, "aim.threat_priority", a.threat_priority);
    WBool(o, "aim.velocity_prediction", a.velocity_prediction);
    WBool(o, "aim.sticky_aim", a.sticky_aim);
    WFloat(o, "aim.sticky_retention", a.sticky_retention);
    WFloat(o, "aim.sticky_tolerance", a.sticky_tolerance);
    WFloat(o, "aim.switch_delay", a.switch_delay);
    WBool(o, "aim.switch_immediately_if_invalid", a.switch_immediately_if_invalid);
    WBool(o, "aim.respect_weapon_range", a.respect_weapon_range);
    WBool(o, "aim.trigger_respect_weapon_range", a.trigger_respect_weapon_range);
    WInt(o, "aim.weapon_range_category", (int)a.weapon_range_category);
    WFloat(o, "aim.weapon_range_custom", a.weapon_range_custom);
    WFloat(o, "aim.weapon_range_melee", a.weapon_range_melee);
    WFloat(o, "aim.weapon_range_pistol", a.weapon_range_pistol);
    WFloat(o, "aim.weapon_range_smg", a.weapon_range_smg);
    WFloat(o, "aim.weapon_range_shotgun", a.weapon_range_shotgun);
    WFloat(o, "aim.weapon_range_rifle", a.weapon_range_rifle);
    WFloat(o, "aim.weapon_range_sniper", a.weapon_range_sniper);
    WFloat(o, "aim.weapon_range_heavy", a.weapon_range_heavy);
    WBool(o, "aim.closest_to_crosshair", a.closest_to_crosshair);
    WInt(o, "aim.hitbox", (int)a.hitbox);
    WInt(o, "aim.fov_style", (int)a.fov_style);
    WInt(o, "aim.aimbot_bind", a.aimbot_bind);
    WInt(o, "aim.aimbot_bind2", a.aimbot_bind2);
    WBool(o, "aim.visible_check", a.visible_check);
    WBool(o, "aim.trigger_enabled", a.trigger_enabled);
    WBool(o, "aim.trigger_always_on", a.trigger_always_on);
    WInt(o, "aim.trigger_bind", a.trigger_bind);
    WBool(o, "aim.trigger_head_only", a.trigger_head_only);
    WBool(o, "aim.trigger_show_fov", a.trigger_show_fov);
    WFloat(o, "aim.trigger_fov", a.trigger_fov);
    WFloat(o, "aim.trigger_delay", a.trigger_delay);
    WFloat(o, "aim.trigger_random_extra", a.trigger_random_extra);
    WBool(o, "aim.trigger_burst", a.trigger_burst);
    WFloat(o, "aim.trigger_burst_duration", a.trigger_burst_duration);
    WFloat(o, "aim.trigger_burst_interval", a.trigger_burst_interval);
    WFloat(o, "aim.trigger_cooldown", a.trigger_cooldown);
    WBool(o, "aim.lock_beep", a.lock_beep);
    WBool(o, "aim.crosshair_enabled", a.crosshair_enabled);
    WFloat(o, "aim.crosshair_size", a.crosshair_size);
    WFloat(o, "aim.crosshair_gap", a.crosshair_gap);
    WU32(o, "aim.crosshair_color", a.crosshair_color);
    WBool(o, "aim.recoil_pattern_visual", a.recoil_pattern_visual);
    WU32(o, "aim.fov_color", a.fov_color);
}

bool DeserializeAim(const std::string& key, const std::string& val) {
    if (key.rfind("aim.", 0) != 0) return false;
    auto& a = aimbot::config;
    auto bi = [&](const char* k) { return key == k; };
    auto toB = [&]() -> bool { return config_manager::toB(val); };
    auto toF = [&]() -> float { return config_manager::toF(val); };
    auto toI = [&]() -> int { return config_manager::toI(val); };
    auto toU = [&]() -> ImU32 { return config_manager::toU(val); };

    if (bi("aim.aimbot_enabled")) { a.aimbot_enabled = toB(); return true; }
    if (bi("aim.show_fov")) { a.show_fov = toB(); return true; }
    if (bi("aim.fov_size")) { a.fov_size = toF(); return true; }
    if (bi("aim.dynamic_fov_mode")) { a.dynamic_fov_mode = (aimbot::DynamicFovMode)std::clamp(toI(), 0, 1); return true; }
    if (bi("aim.dynamic_fov_min")) { a.dynamic_fov_min = toF(); return true; }
    if (bi("aim.dynamic_fov_max")) { a.dynamic_fov_max = toF(); return true; }
    if (bi("aim.dynamic_fov_near_distance")) { a.dynamic_fov_near_distance = toF(); return true; }
    if (bi("aim.dynamic_fov_far_distance")) { a.dynamic_fov_far_distance = toF(); return true; }
    if (bi("aim.dynamic_fov_curve")) { a.dynamic_fov_curve = toF(); return true; }
    if (bi("aim.dynamic_fov_transition_speed")) { a.dynamic_fov_transition_speed = toF(); return true; }
    if (bi("aim.max_distance")) { a.max_distance = toF(); return true; }
    if (bi("aim.smooth_x")) { a.smooth_x = toF(); return true; }
    if (bi("aim.smooth_y")) { a.smooth_y = toF(); return true; }
    if (bi("aim.humanize")) { a.humanize = toB(); return true; }
    if (bi("aim.threat_priority")) { a.threat_priority = toB(); return true; }
    if (bi("aim.velocity_prediction")) { a.velocity_prediction = toB(); return true; }
    if (bi("aim.sticky_aim")) { a.sticky_aim = toB(); return true; }
    if (bi("aim.sticky_retention")) { a.sticky_retention = toF(); return true; }
    if (bi("aim.sticky_tolerance")) { a.sticky_tolerance = toF(); return true; }
    if (bi("aim.switch_delay")) { a.switch_delay = toF(); return true; }
    if (bi("aim.switch_immediately_if_invalid")) { a.switch_immediately_if_invalid = toB(); return true; }
    if (bi("aim.respect_weapon_range")) { a.respect_weapon_range = toB(); return true; }
    if (bi("aim.trigger_respect_weapon_range")) { a.trigger_respect_weapon_range = toB(); return true; }
    if (bi("aim.weapon_range_category")) { a.weapon_range_category = (aimbot::WeaponRangeCategory)std::clamp(toI(), 0, 7); return true; }
    if (bi("aim.weapon_range_custom")) { a.weapon_range_custom = toF(); return true; }
    if (bi("aim.weapon_range_melee")) { a.weapon_range_melee = toF(); return true; }
    if (bi("aim.weapon_range_pistol")) { a.weapon_range_pistol = toF(); return true; }
    if (bi("aim.weapon_range_smg")) { a.weapon_range_smg = toF(); return true; }
    if (bi("aim.weapon_range_shotgun")) { a.weapon_range_shotgun = toF(); return true; }
    if (bi("aim.weapon_range_rifle")) { a.weapon_range_rifle = toF(); return true; }
    if (bi("aim.weapon_range_sniper")) { a.weapon_range_sniper = toF(); return true; }
    if (bi("aim.weapon_range_heavy")) { a.weapon_range_heavy = toF(); return true; }
    if (bi("aim.closest_to_crosshair")) { a.closest_to_crosshair = toB(); return true; }
    if (bi("aim.hitbox")) {
        int value = toI();
        if (value < 0 || value > static_cast<int>(aimbot::Hitbox::Legs))
            value = static_cast<int>(aimbot::Hitbox::Head);
        a.hitbox = static_cast<aimbot::Hitbox>(value);
        return true;
    }
    if (bi("aim.fov_style")) { a.fov_style = (aimbot::FovStyle)toI(); return true; }
    if (bi("aim.aimbot_bind")) { a.aimbot_bind = toI(); return true; }
    if (bi("aim.aimbot_bind2")) { a.aimbot_bind2 = toI(); return true; }



    if (bi("aim.visible_check")) { a.visible_check = toB(); return true; }
    if (bi("aim.trigger_enabled")) { a.trigger_enabled = toB(); return true; }
    if (bi("aim.trigger_always_on")) { a.trigger_always_on = toB(); return true; }
    if (bi("aim.trigger_bind")) { a.trigger_bind = toI(); return true; }
    if (bi("aim.trigger_head_only")) { a.trigger_head_only = toB(); return true; }
    if (bi("aim.trigger_show_fov")) { a.trigger_show_fov = toB(); return true; }
    if (bi("aim.trigger_fov")) { a.trigger_fov = toF(); return true; }
    if (bi("aim.trigger_delay")) { a.trigger_delay = toF(); return true; }
    if (bi("aim.trigger_random_extra")) { a.trigger_random_extra = toF(); return true; }
    if (bi("aim.trigger_burst")) { a.trigger_burst = toB(); return true; }
    if (bi("aim.trigger_burst_duration")) { a.trigger_burst_duration = toF(); return true; }
    if (bi("aim.trigger_burst_interval")) { a.trigger_burst_interval = toF(); return true; }
    if (bi("aim.trigger_cooldown")) { a.trigger_cooldown = toF(); return true; }
    if (bi("aim.lock_beep")) { a.lock_beep = toB(); return true; }
    if (bi("aim.crosshair_enabled")) { a.crosshair_enabled = toB(); return true; }
    if (bi("aim.crosshair_size")) { a.crosshair_size = toF(); return true; }
    if (bi("aim.crosshair_gap")) { a.crosshair_gap = toF(); return true; }
    if (bi("aim.crosshair_color")) { a.crosshair_color = toU(); return true; }
    if (bi("aim.recoil_pattern_visual")) { a.recoil_pattern_visual = toB(); return true; }
    if (bi("aim.fov_color")) { a.fov_color = toU(); return true; }
    return false;
}

}
