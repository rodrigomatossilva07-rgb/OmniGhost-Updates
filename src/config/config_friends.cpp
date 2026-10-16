#include "config_friends.h"
#include "config_core.h"
#include "../src/games/Fivem/friends/friends.h"

namespace config_manager {

void SerializeFriends(std::ostringstream& o) {
    WBool(o, "friends.ignore_aim", friends::config.ignore_aim);
    WBool(o, "friends.ignore_silent", friends::config.ignore_silent);
    WBool(o, "friends.show_friend_esp", friends::config.show_friend_esp);
    WBool(o, "friends.add_by_prox", friends::config.add_by_prox);
    WFloat(o, "friends.prox_max_distance", friends::config.prox_max_distance);
    WU32(o, "friends.friend_color", friends::config.friend_color);

    {
        std::lock_guard<std::mutex> lock(friends::list_mutex);
        WInt(o, "friends.count", (int)friends::friend_list.size());
        for (size_t i = 0; i < friends::friend_list.size(); ++i) {
            WInt(o, ("friend." + std::to_string(i) + ".id").c_str(), (int)friends::friend_list[i].id);
            WStr(o, ("friend." + std::to_string(i) + ".name").c_str(), friends::friend_list[i].name);
        }
    }
}

bool DeserializeFriends(const std::string& key, const std::string& val) {
    if (key.rfind("friends.", 0) != 0) return false;
    auto bi = [&](const char* k) { return key == k; };
    auto toB = [&]() -> bool { return config_manager::toB(val); };
    auto toF = [&]() -> float { return config_manager::toF(val); };
    auto toI = [&]() -> int { return config_manager::toI(val); };
    auto toU = [&]() -> ImU32 { return config_manager::toU(val); };

    if (bi("friends.ignore_aim")) { friends::config.ignore_aim = toB(); return true; }
    if (bi("friends.ignore_silent")) { friends::config.ignore_silent = toB(); return true; }
    if (bi("friends.show_friend_esp")) { friends::config.show_friend_esp = toB(); return true; }
    if (bi("friends.add_by_prox")) { friends::config.add_by_prox = toB(); return true; }
    if (bi("friends.prox_max_distance")) { friends::config.prox_max_distance = toF(); return true; }
    if (bi("friends.friend_color")) { friends::config.friend_color = toU(); return true; }


    if (key.rfind("friend.", 0) == 0) {
        // CFG-007 Fix: this blanket return used to swallow every friend.N.*
        // key — the id/name restore branches in config_manager were dead code
        // (they run AFTER DeserializeFriends), so friends never came back
        // from a saved config at all.
        const auto sep = key.rfind('.');
        if (sep == std::string::npos || sep + 1 >= key.size())
            return true;
        const std::string leaf = key.substr(sep + 1);
        const auto first = key.find('.');
        const size_t idx = (first == std::string::npos || sep == first)
            ? SIZE_MAX
            : (size_t)strtoul(key.substr(first + 1, sep - first - 1).c_str(), nullptr, 10);
        if (leaf == "id") {
            const uint32_t id = (uint32_t)strtoul(val.c_str(), nullptr, 10);
            friends::AddFriend(std::string("Friend_") + val, id, false);
        } else if (leaf == "name" && idx != SIZE_MAX) {
            // friend.N.name — N matches the insertion order of the ids above.
            std::lock_guard<std::mutex> lock(friends::list_mutex);
            if (idx < friends::friend_list.size() && !val.empty())
                friends::friend_list[idx].name = val;
        }
        return true;
    }

    return false;
}

}