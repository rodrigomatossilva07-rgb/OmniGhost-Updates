#include "config_aimtype.h"
#include "config_core.h"
#include "../src/games/Fivem/aimbot/aim_type.h"

namespace config_manager {

void SerializeAimType(std::ostringstream& o) {
    WInt(o, "aimtype.active", (int)aim_type::config.active);
    WBool(o, "aimtype.makcu_enabled", aim_type::config.makcu_enabled);
    WStr(o, "aimtype.makcu_com", aim_type::config.makcu_com);
    WInt(o, "aimtype.makcu_baud", aim_type::config.makcu_baud);
    WBool(o, "aimtype.ferrum_enabled", aim_type::config.ferrum_enabled);
    WStr(o, "aimtype.ferrum_com", aim_type::config.ferrum_com);
    WInt(o, "aimtype.ferrum_baud", aim_type::config.ferrum_baud);
    WBool(o, "aimtype.kmbox_net_enabled", aim_type::config.kmbox_net_enabled);
    WStr(o, "aimtype.kmbox_ip", aim_type::config.kmbox_ip);
    WStr(o, "aimtype.kmbox_port", aim_type::config.kmbox_port);
    WStr(o, "aimtype.kmbox_uuid", aim_type::config.kmbox_uuid);
}

bool DeserializeAimType(const std::string& key, const std::string& val) {
    if (key.rfind("aimtype.", 0) != 0) return false;
    auto bi = [&](const char* k) { return key == k; };
    auto toB = [&]() -> bool { return config_manager::toB(val); };
    auto toF = [&]() -> float { return config_manager::toF(val); };
    auto toI = [&]() -> int { return config_manager::toI(val); };

    if (bi("aimtype.active")) { aim_type::config.active = (aim_type::DeviceType)toI(); return true; }
    if (bi("aimtype.makcu_enabled")) { aim_type::config.makcu_enabled = toB(); return true; }
    if (bi("aimtype.makcu_com")) { strncpy_s(aim_type::config.makcu_com, val.c_str(), _TRUNCATE); return true; }
    if (bi("aimtype.makcu_baud")) { aim_type::config.makcu_baud = toI(); return true; }
    if (bi("aimtype.ferrum_enabled")) { aim_type::config.ferrum_enabled = toB(); return true; }
    if (bi("aimtype.ferrum_com")) { strncpy_s(aim_type::config.ferrum_com, val.c_str(), _TRUNCATE); return true; }
    if (bi("aimtype.ferrum_baud")) { aim_type::config.ferrum_baud = toI(); return true; }
    if (bi("aimtype.kmbox_net_enabled")) { aim_type::config.kmbox_net_enabled = toB(); return true; }
    if (bi("aimtype.kmbox_ip")) { strncpy_s(aim_type::config.kmbox_ip, val.c_str(), _TRUNCATE); return true; }
    if (bi("aimtype.kmbox_port")) { strncpy_s(aim_type::config.kmbox_port, val.c_str(), _TRUNCATE); return true; }
    if (bi("aimtype.kmbox_uuid")) { strncpy_s(aim_type::config.kmbox_uuid, val.c_str(), _TRUNCATE); return true; }
    return false;
}

} // namespace config_manager