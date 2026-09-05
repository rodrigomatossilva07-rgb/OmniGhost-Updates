#pragma once
#include "../../ImGui/imgui.h"
#include "ui/ui_models.h"
#include <string>
#include <vector>
#include <cstdint>
#include <mutex>

namespace friends {

    extern Config config;
    extern std::vector<FriendEntry> friend_list;
    extern std::vector<PlayerEntry> player_list;
    extern std::mutex list_mutex;

    void Initialize();
    bool IsFriend(uint32_t id);
    bool IsFriendPed(uintptr_t ped);
    bool HasFriends();
    void AddFriend(const std::string& name, uint32_t id, bool prox = false);
    void RemoveFriend(uint32_t id);
    void AddClosestAsFriend();
    void KickClosestFromFriends(); // remove closest friend from list
    void UpdatePlayerList(); // fill from ESP valid peds + net id if available
    bool ExportFriendsClipboard(); // CSV id,name
    bool ImportFriendsClipboard(); // parse CSV
} // namespace friends
