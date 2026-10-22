#pragma once
#include "third_party/ImGui/imgui.h"
#include "ui/ui_models.h"
#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include <atomic>

namespace friends {

    extern Config config;
    extern std::vector<FriendEntry> friend_list;
    extern std::vector<PlayerEntry> player_list;
    extern std::mutex list_mutex;


    extern std::atomic<uint64_t> list_version;

    void Initialize();
    bool IsFriend(uint32_t id);
    bool IsFriendPed(uintptr_t ped);
    bool HasFriends();
    void RequestPlayerList() noexcept;
    bool PlayerListRequestedRecently() noexcept;
    void AddFriend(const std::string& name, uint32_t id, bool prox = false);
    void RemoveFriend(uint32_t id);
    void AddClosestAsFriend();
    void KickClosestFromFriends();
    void UpdatePlayerList();
    bool ExportFriendsClipboard();
    bool ImportFriendsClipboard();
}
