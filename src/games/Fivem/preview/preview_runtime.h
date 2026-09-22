#pragma once

#include "ui/ui_models.h"

#include <mutex>
#include <string>
#include <vector>

namespace esp {
    extern Config config;
}

namespace aimbot {
    extern Config config;
    void ApplyLegitProfile();
    void ApplyRageProfile();
}

namespace aim_type {
    extern Config config;

    void Initialize();
    bool Connect(DeviceType type);
    void Disconnect(DeviceType type);
    bool Test(DeviceType type);
    bool IsConnected();
    const char* StatusText();
}

namespace vehicle_esp {
    extern Config config;
}

namespace friends {
    extern Config config;
    extern std::vector<FriendEntry> friend_list;
    extern std::vector<PlayerEntry> player_list;
    extern std::mutex list_mutex;

    void Initialize();
    bool IsFriend(uint32_t id);
    bool IsFriendPed(uintptr_t ped);
    void AddFriend(const std::string& name, uint32_t id, bool prox = false);
    void RemoveFriend(uint32_t id);
    void AddClosestAsFriend();
    void KickClosestFromFriends();
    void UpdatePlayerList();
    bool ExportFriendsClipboard();
    bool ImportFriendsClipboard();
}

namespace config_manager {
    extern std::vector<SavedConfigInfo> saved_list;
    extern char new_config_name[64];
    extern std::string last_status;
    extern std::string active_config_name;

    void Initialize();
    void RefreshList();
    void MoveConfig(int from, int to);
    const char* ActiveConfigName();
    void ApplyNamedProfile(const char* name);
    std::string SerializeAll();
    bool DeserializeAll(const std::string& data);
    bool SaveToFile(const std::string& name);
    bool LoadFromFile(const std::string& name);
    bool DeleteConfigFile(const std::string& name);
    bool CopyToClipboard();
    bool PasteFromClipboard();
    void ResetToDefaults();
    std::string GetConfigFolder();
    void OpenConfigFolder();
}

namespace preview_runtime {

    struct Status {
        float fps = 144.0f;
        int ping_ms = 11;
        int players = 32;
        bool dma_connected = true;
        const char* build = "3.2";
    };

    const Status& GetStatus();
    void Initialize();

} // namespace preview_runtime

// The preview never loads the MAKCU implementation. AimPage uses this
// lightweight declaration so its existing status widget remains interactive.
namespace makcu_wrapper {
    bool IsConnected();
}
