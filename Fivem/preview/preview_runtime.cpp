#include "preview_runtime.h"

#include "config/app_settings.h"
#include "globals.h"

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace {
    preview_runtime::Status g_status;
    std::string g_preview_clipboard;
}

void DrawCs2Visuals() {}
void DrawCs2Aim() {}
void DrawCs2Misc() {}

namespace CS2 {
    const char* StatusLine() { return "CS2 preview indisponível"; }
}

namespace esp {
    Config config;
}

namespace aimbot {
    Config config;

    void ApplyLegitProfile()
    {
        config = Config{};
    }

    void ApplyRageProfile()
    {
        config = Config{};
    }
}

namespace aim_type {
    Config config;

    void Initialize()
    {
        config = Config{};
    }

    bool Connect(DeviceType type)
    {
        switch (type) {
        case DeviceType::KmboxNet:
            config.kmbox_net_connected = !config.kmbox_net_connected;
            return config.kmbox_net_connected;
        case DeviceType::Ferrum:
            config.ferrum_connected = !config.ferrum_connected;
            return config.ferrum_connected;
        case DeviceType::Makcu:
            config.makcu_connected = !config.makcu_connected;
            return config.makcu_connected;
        default:
            return false;
        }
    }

    void Disconnect(DeviceType type)
    {
        if (type == DeviceType::KmboxNet) config.kmbox_net_connected = false;
        if (type == DeviceType::Ferrum) config.ferrum_connected = false;
        if (type == DeviceType::Makcu) config.makcu_connected = false;
    }

    bool Test(DeviceType type)
    {
        // Never disconnect — only report whether the device is already online.
        switch (type) {
        case DeviceType::KmboxNet: return config.kmbox_net_connected;
        case DeviceType::Ferrum:   return config.ferrum_connected;
        case DeviceType::Makcu:    return config.makcu_connected;
        default: return false;
        }
    }

    bool IsConnected()
    {
        return config.kmbox_net_connected || config.ferrum_connected || config.makcu_connected;
    }

    const char* StatusText()
    {
        return IsConnected() ? "ONLINE" : "OFFLINE";
    }
}

namespace makcu_wrapper {
    bool IsConnected()
    {
        return aim_type::config.makcu_connected;
    }
}

namespace vehicle_esp {
    Config config;
}

namespace friends {
    Config config;
    std::vector<FriendEntry> friend_list;
    std::vector<PlayerEntry> player_list;
    std::mutex list_mutex;

    void Initialize()
    {
        std::lock_guard<std::mutex> lock(list_mutex);
        friend_list.clear();
        player_list = {
            { "Jogador123", 123, 74.0f, 0 },
            { "NightRunner", 208, 41.0f, 0 },
            { "LisbonGhost", 314, 96.0f, 0 },
            { "CyberPilot", 404, 128.0f, 0 }
        };
    }

    bool IsFriend(uint32_t id)
    {
        std::lock_guard<std::mutex> lock(list_mutex);
        return std::any_of(friend_list.begin(), friend_list.end(),
            [id](const FriendEntry& entry) { return entry.id == id; });
    }

    bool IsFriendPed(uintptr_t)
    {
        return false;
    }

    void AddFriend(const std::string& name, uint32_t id, bool prox)
    {
        std::lock_guard<std::mutex> lock(list_mutex);
        const auto found = std::find_if(friend_list.begin(), friend_list.end(),
            [id](const FriendEntry& entry) { return entry.id == id; });
        if (found == friend_list.end())
            friend_list.push_back({ name, id, prox });
    }

    void RemoveFriend(uint32_t id)
    {
        std::lock_guard<std::mutex> lock(list_mutex);
        friend_list.erase(
            std::remove_if(friend_list.begin(), friend_list.end(),
                [id](const FriendEntry& entry) { return entry.id == id; }),
            friend_list.end());
    }

    void AddClosestAsFriend()
    {
        if (!player_list.empty())
            AddFriend(player_list.front().name, player_list.front().id, true);
    }

    void KickClosestFromFriends()
    {
        std::lock_guard<std::mutex> lock(list_mutex);
        if (!friend_list.empty())
            friend_list.erase(friend_list.begin());
    }

    void UpdatePlayerList()
    {
        // Stable local data: the preview never reads entities or game memory.
    }

    bool ExportFriendsClipboard()
    {
        g_preview_clipboard.clear();
        std::lock_guard<std::mutex> lock(list_mutex);
        for (const FriendEntry& entry : friend_list)
            g_preview_clipboard += std::to_string(entry.id) + "," + entry.name + "\n";
        return true;
    }

    bool ImportFriendsClipboard()
    {
        return !g_preview_clipboard.empty();
    }
}

namespace config_manager {
    std::vector<SavedConfigInfo> saved_list;
    char new_config_name[64] = "Pré-visualização";
    std::string last_status = "Pré-visualização: estado apenas em memória";
    std::string active_config_name = "Pré-visualização predefinida";

    void Initialize()
    {
        saved_list = {
            { "Pré-visualização predefinida", "", "Sessão atual" },
            { "Gold Theme", "", "Sessao atual" }
        };
        last_status = "Pré-visualização: estado apenas em memória";
    }

    void RefreshList()
    {
        last_status = "Lista atualizada";
    }

    void MoveConfig(int from, int to)
    {
        if (from < 0 || to < 0 || from >= static_cast<int>(saved_list.size()) ||
            to >= static_cast<int>(saved_list.size()) || from == to)
            return;
        SavedConfigInfo moved = saved_list[static_cast<std::size_t>(from)];
        saved_list.erase(saved_list.begin() + from);
        saved_list.insert(saved_list.begin() + to, std::move(moved));
        last_status = "Ordem atualizada na pré-visualização";
    }

    const char* ActiveConfigName()
    {
        return active_config_name.c_str();
    }

    void ApplyNamedProfile(const char* name)
    {
        active_config_name = name && *name ? name : "Pré-visualização";
        last_status = "Perfil aplicado na pré-visualização: " + active_config_name;
    }

    std::string SerializeAll()
    {
        return "OMNIGHOST_UI_PREVIEW";
    }

    bool DeserializeAll(const std::string& data)
    {
        last_status = data.empty() ? "Dados invalidos" : "Config aplicada";
        return !data.empty();
    }

    bool SaveToFile(const std::string& name)
    {
        const std::string safe_name = name.empty() ? "Pré-visualização" : name;
        const auto found = std::find_if(saved_list.begin(), saved_list.end(),
            [&safe_name](const SavedConfigInfo& item) { return item.name == safe_name; });
        if (found == saved_list.end())
            saved_list.push_back({ safe_name, "", "Sessao atual" });
        last_status = "Config guardada apenas nesta sessao";
        active_config_name = safe_name;
        return true;
    }

    bool LoadFromFile(const std::string& name)
    {
        active_config_name = name;
        last_status = "Config carregada: " + name;
        return true;
    }

    bool DeleteConfigFile(const std::string& name)
    {
        saved_list.erase(
            std::remove_if(saved_list.begin(), saved_list.end(),
                [&name](const SavedConfigInfo& item) { return item.name == name; }),
            saved_list.end());
        last_status = "Config removida da sessao";
        return true;
    }

    bool CopyToClipboard()
    {
        g_preview_clipboard = SerializeAll();
        last_status = "Copiado para o clipboard simulado";
        return true;
    }

    bool PasteFromClipboard()
    {
        return DeserializeAll(g_preview_clipboard);
    }

    void ResetToDefaults()
    {
        esp::config = esp::Config{};
        aimbot::config = aimbot::Config{};
        aim_type::config = aim_type::Config{};
        vehicle_esp::config = vehicle_esp::Config{};
        friends::config = friends::Config{};
        app_settings::config = app_settings::Config{};
        last_status = "Valores locais repostos";
    }

    std::string GetConfigFolder()
    {
        return {};
    }

    void OpenConfigFolder()
    {
        last_status = "Sem pasta: Preview nao grava ficheiros";
    }
}

namespace preview_runtime {

    const Status& GetStatus()
    {
        return g_status;
    }

    void Initialize()
    {
        g_status = Status{};
        esp::config = esp::Config{};
        aimbot::config = aimbot::Config{};
        aim_type::Initialize();
        vehicle_esp::config = vehicle_esp::Config{};
        friends::config = friends::Config{};
        friends::Initialize();
        config_manager::Initialize();
    }

} // namespace preview_runtime
