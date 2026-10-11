#include "config_vehicle.h"
#include "config_core.h"
#include "../src/games/Fivem/esp/vehicle_esp.h"

namespace config_manager {

void SerializeVehicle(std::ostringstream& o) {
    auto& v = vehicle_esp::config;
    WBool(o, "veh.enabled", v.enabled);
    WBool(o, "veh.self_vehicle", v.self_vehicle);
    WBool(o, "veh.anti_fall_moto", v.anti_fall_moto);
    WBool(o, "veh.box_3d", v.box_3d);
    WBool(o, "veh.snaplines", v.snaplines);
    WBool(o, "veh.marker", v.marker);
    WBool(o, "veh.distance", v.distance);
    WBool(o, "veh.max_distance", v.max_distance);
    WBool(o, "veh.rgb_mode", v.rgb_mode);
    WBool(o, "veh.show_speed", v.show_speed);
    WBool(o, "veh.lock_status", v.lock_status);
    WBool(o, "veh.show_occupants", v.show_occupants);
    WBool(o, "veh.ignore_occupied", v.ignore_occupied);
    WU32(o, "veh.color_box_3d", v.color_box_3d);
    WU32(o, "veh.color_snaplines", v.color_snaplines);
    WU32(o, "veh.color_marker", v.color_marker);
    WU32(o, "veh.color_distance", v.color_distance);
    WU32(o, "veh.color_unlocked", v.color_unlocked);
    WU32(o, "veh.color_locked", v.color_locked);
    WU32(o, "veh.color_occupants", v.color_occupants);
    WU32(o, "veh.color_name", v.color_name);
    WInt(o, "veh.snapline_pos", v.snapline_pos);
    WInt(o, "veh.unlock_bind", v.unlock_bind);
    WInt(o, "veh.lock_bind", v.lock_bind);
    WInt(o, "veh.fix_total_bind", v.fix_total_bind);
    WInt(o, "veh.fix_motor_bind", v.fix_motor_bind);
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
    if (bi("veh.self_vehicle")) { v.self_vehicle = toB(); return true; }
    if (bi("veh.anti_fall_moto")) { v.anti_fall_moto = toB(); return true; }
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
    if (bi("veh.color_marker")) { v.color_marker = toU(); return true; }
    if (bi("veh.color_distance")) { v.color_distance = toU(); return true; }
    if (bi("veh.color_unlocked")) { v.color_unlocked = toU(); return true; }
    if (bi("veh.color_locked")) { v.color_locked = toU(); return true; }
    if (bi("veh.color_occupants")) { v.color_occupants = toU(); return true; }
    if (bi("veh.color_name")) { v.color_name = toU(); return true; }
    if (bi("veh.snapline_pos")) { v.snapline_pos = toI(); return true; }
    if (bi("veh.unlock_bind")) { v.unlock_bind = toI(); return true; }
    if (bi("veh.lock_bind")) { v.lock_bind = toI(); return true; }
    if (bi("veh.fix_total_bind")) { v.fix_total_bind = toI(); return true; }
    if (bi("veh.fix_motor_bind")) { v.fix_motor_bind = toI(); return true; }
    return false;
}

} // namespace config_manager