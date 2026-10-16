#include "config_aim.h"
#include "config_core.h"
#include "../src/games/Fivem/aimbot/aimbot.h"
#include "../src/games/Fivem/aimbot/aim_type.h"

namespace config_manager {

void SerializeAim(std::ostringstream& o) {
    auto& a = aimbot::config;
    WBool(o, "aim.aimbot_enabled", a.aimbot_enabled);
    WBool(o, "aim.show_fov", a.show_fov);
    WBool(o, "aim.fov_rgb", a.fov_rgb);
    WFloat(o, "aim.fov_size", a.fov_size);
    WFloat(o, "aim.max_distance", a.max_distance);
    WFloat(o, "aim.smooth_x", a.smooth_x);
    WFloat(o, "aim.smooth_y", a.smooth_y);
    WFloat(o, "aim.reaction_time", a.reaction_time);
    WBool(o, "aim.humanize", a.humanize);
    WBool(o, "aim.threat_priority", a.threat_priority);
    WBool(o, "aim.velocity_prediction", a.velocity_prediction);
    WBool(o, "aim.closest_to_crosshair", a.closest_to_crosshair);
    WFloat(o, "aim.jitter_amount", a.jitter_amount);
    WFloat(o, "aim.acceleration", a.acceleration);
    WFloat(o, "aim.overshoot_chance", a.overshoot_chance);
    WFloat(o, "aim.overshoot_scale", a.overshoot_scale);
    WFloat(o, "aim.min_move_threshold", a.min_move_threshold);
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
    if (bi("aim.fov_rgb")) { a.fov_rgb = toB(); return true; }
    if (bi("aim.fov_size")) { a.fov_size = toF(); return true; }
    if (bi("aim.max_distance")) { a.max_distance = toF(); return true; }
    if (bi("aim.smooth_x")) { a.smooth_x = toF(); return true; }
    if (bi("aim.smooth_y")) { a.smooth_y = toF(); return true; }
    if (bi("aim.reaction_time")) { a.reaction_time = toF(); return true; }
    if (bi("aim.humanize")) { a.humanize = toB(); return true; }
    if (bi("aim.threat_priority")) { a.threat_priority = toB(); return true; }
    if (bi("aim.velocity_prediction")) { a.velocity_prediction = toB(); return true; }
    if (bi("aim.closest_to_crosshair")) { a.closest_to_crosshair = toB(); return true; }
    if (bi("aim.jitter_amount")) { a.jitter_amount = toF(); return true; }
    if (bi("aim.acceleration")) { a.acceleration = toF(); return true; }
    if (bi("aim.overshoot_chance")) { a.overshoot_chance = toF(); return true; }
    if (bi("aim.overshoot_scale")) { a.overshoot_scale = toF(); return true; }
    if (bi("aim.min_move_threshold")) { a.min_move_threshold = toF(); return true; }
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
    // CFG-005 Fix: the bind normalization used to run inside this key chain
    // (executing as a side effect of any unrecognized aim.* key); the same
    // normalization already runs once at the end of DeserializeAll.
    if (bi("aim.visible_check")) { a.visible_check = toB(); return true; }
    if (bi("aim.trigger_enabled")) { a.trigger_enabled = toB(); return true; }
    if (bi("aim.trigger_always_on")) { a.trigger_always_on = toB(); return true; }
    if (bi("aim.trigger_bind")) { a.trigger_bind = toI(); return true; }
    if (bi("aim.trigger_head_only")) { a.trigger_head_only = toB(); return true; }
    if (bi("aim.trigger_show_fov")) { a.trigger_show_fov = toB(); return true; }
    if (bi("aim.trigger_fov")) { a.trigger_fov = toF(); return true; }
    if (bi("aim.trigger_delay")) { a.trigger_delay = toF(); return true; }
    if (bi("aim.trigger_random_extra")) { a.trigger_random_extra = toF(); return true; }
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
