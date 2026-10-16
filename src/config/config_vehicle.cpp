#include "config_vehicle.h"
#include "config_core.h"
#include "../src/games/Fivem/esp/vehicle_esp.h"

namespace config_manager {

void SerializeVehicle(std::ostringstream& o) {
    auto& v = vehicle_esp::config;
    WBool(o, "veh.enabled", v.enabled);
    WBool(o, "veh.box_3d", v.box_3d);
    WBool(o, "veh.snaplines", v.snaplines);
    WBool(o, "veh.distance", v.distance);
    WFloat(o, "veh.max_distance", v.max_distance);
    WBool(o, "veh.rgb_mode", v.rgb_mode);
    WBool(o, "veh.show_speed", v.show_speed);
    WBool(o, "veh.lock_status", v.lock_status);
    WBool(o, "veh.show_occupants", v.show_occupants);
    WBool(o, "veh.ignore_occupied", v.ignore_occupied);
    WU32(o, "veh.color_box_3d", v.color_box_3d);
    WU32(o, "veh.color_snaplines", v.color_snaplines);
    WU32(o, "veh.color_distance", v.color_distance);
    WU32(o, "veh.color_unlocked", v.color_unlocked);
    WU32(o, "veh.color_locked", v.color_locked);
    WU32(o, "veh.color_occupants", v.color_occupants);
    WInt(o, "veh.snapline_pos", v.snapline_pos);
}

bool DeserializeVehicle(const std::string& key, const std::string& val) {
    if (key.rfind("veh.", 0) != 0) return false;
    auto& v = vehicle_esp::config;
    auto bi = [&](const char* k) { return key == k; };
    auto toB = [&]() -> bool { return config_manager::toB(val); };
    auto toF = [&]() -> float { return config_manager::toF(val); };
    auto toI = [&]() -> int { return config_manager::toI(val); };
    auto toU = [&]() -> ImU32 { return config_manager::toU(val); };

    if (bi("veh.enabled")) { v.enabled = toB(); return true; }
    if (bi("veh.box_3d")) { v.box_3d = toB(); return true; }
    if (bi("veh.snaplines")) { v.snaplines = toB(); return true; }
    if (bi("veh.distance")) { v.distance = toB(); return true; }
    if (bi("veh.max_distance")) { v.max_distance = toF(); return true; }
    if (bi("veh.rgb_mode")) { v.rgb_mode = toB(); return true; }
    if (bi("veh.show_speed")) { v.show_speed = toB(); return true; }
    if (bi("veh.lock_status")) { v.lock_status = toB(); return true; }
    if (bi("veh.show_occupants")) { v.show_occupants = toB(); return true; }
    if (bi("veh.ignore_occupied")) { v.ignore_occupied = toB(); return true; }
    if (bi("veh.color_box_3d")) { v.color_box_3d = toU(); return true; }
    if (bi("veh.color_snaplines")) { v.color_snaplines = toU(); return true; }
    if (bi("veh.color_distance")) { v.color_distance = toU(); return true; }
    if (bi("veh.color_unlocked")) { v.color_unlocked = toU(); return true; }
    if (bi("veh.color_locked")) { v.color_locked = toU(); return true; }
    if (bi("veh.color_occupants")) { v.color_occupants = toU(); return true; }
    if (bi("veh.snapline_pos")) { v.snapline_pos = toI(); return true; }
    // FIVEM-005: legacy veh.* keys from removed features are ignored
    // gracefully (self_vehicle, anti_fall_moto, marker, vehicle_name,
    // color_marker, color_name, unlock/lock/fix binds).
    return false;
}

}