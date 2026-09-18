#include <unordered_set>
#include <Windows.h>
#include "friends.h"
#include "../game/game.h"
#include "../game/offsets.h"
#include "../game/esp_manager.h"
#include "../playerInfo/PedData.h"
#include "math/math.h"
#include <Memory/Memory.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <string>
#include <chrono>

namespace friends {

    Config config;
    std::vector<FriendEntry> friend_list;
    static std::unordered_set<uint32_t> s_friendIds;
    static void RebuildIdSet() {
        s_friendIds.clear();
        s_friendIds.reserve(friend_list.size() * 2 + 4);
        for (const auto& f : friend_list) s_friendIds.insert(f.id);
    }
    std::vector<PlayerEntry> player_list;
    std::mutex list_mutex;

    void Initialize() {
        friend_list.clear();
        s_friendIds.clear();
        player_list.clear();
    }

    bool IsFriend(uint32_t id) {
        if (id == 0) return false;
        std::lock_guard<std::mutex> lock(list_mutex);
        return s_friendIds.find(id) != s_friendIds.end();
    }

    bool HasFriends() {
        std::lock_guard<std::mutex> lock(list_mutex);
        return !s_friendIds.empty();
    }

    bool IsFriendPed(uintptr_t ped) {
        if (!ped) return false;
        using namespace FiveM;
        const auto snap = FiveM::ESP::AcquireSnapshot();
        if (!snap) return false;
        for (int i = 0; i < snap->count; ++i) {
            if (snap->entities[i].ped == ped) {
                uint32_t netId = snap->entities[i].network_id;
                return IsFriend(netId);
            }
        }
        return false;
    }

    void AddFriend(const std::string& name, uint32_t id, bool prox) {
        if (id == 0) return;
        std::lock_guard<std::mutex> lock(list_mutex);
        for (const auto& f : friend_list)
            if (f.id == id) return;
        friend_list.push_back({ name, id, prox });
        s_friendIds.insert(id);
    }

    void RemoveFriend(uint32_t id) {
        std::lock_guard<std::mutex> lock(list_mutex);
        friend_list.erase(
            std::remove_if(friend_list.begin(), friend_list.end(),
                [id](const FriendEntry& f) { return f.id == id; }),
            friend_list.end());
        RebuildIdSet();
    }

    void UpdatePlayerList() {
        using namespace FiveM;
        player_list.clear();

        if (!offset::localplayer)
            return;

        const auto snap = FiveM::ESP::AcquireSnapshot();
        if (!snap || snap->count == 0)
            return;

        Vec3 localPos = snap->localPos;
        if (localPos.IsZero()) return;

        for (int i = 0; i < snap->count; ++i) {
            const auto& ef = snap->entities[static_cast<size_t>(i)];
            uintptr_t ped = ef.ped;
            if (!ped || ped == offset::localplayer) continue;
            if (!ef.valid) continue;

            uint32_t netId = ef.network_id;
            if (netId == 0) continue;

            float dist = ef.position.IsZero() ? 0.f : ef.position.distance_to(localPos);

            float hp = ef.health;
            float maxHp = ef.max_health;
            if (maxHp < 1.f) maxHp = 200.f;
            float hpPct = (std::max)(0.f, (std::min)(100.f, (hp / maxHp) * 100.f));

            char nameBuf[64]{};
            // Name should come from name cache (populated by acquisition)
            // For now, use a placeholder
            snprintf(nameBuf, sizeof(nameBuf), "Jogador_%u", netId);

            player_list.push_back({ nameBuf, netId, dist, hpPct, ped });
        }

        std::sort(player_list.begin(), player_list.end(),
            [](const PlayerEntry& a, const PlayerEntry& b) { return a.distance < b.distance; });
    }

    void AddClosestAsFriend() {
        UpdatePlayerList();
        if (player_list.empty()) return;

        const auto& p = player_list.front();
        if (p.distance <= config.prox_max_distance)
            AddFriend(p.name, p.id, true);
    }

    void KickClosestFromFriends() {
        // Remove friend with matching closest player id if in list
        UpdatePlayerList();
        if (player_list.empty()) return;
        RemoveFriend(player_list.front().id);
    }


    bool ExportFriendsClipboard() {
        std::string out;
        {
            std::lock_guard<std::mutex> lock(list_mutex);
            for (const auto& f : friend_list) {
                out += std::to_string(f.id);
                out += ',';
                out += f.name;
                out += '\n';
            }
        }
        if (out.empty()) out = "# empty\n";
        if (!OpenClipboard(nullptr)) return false;
        EmptyClipboard();
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, out.size() + 1);
        if (!h) { CloseClipboard(); return false; }
        void* ptr = GlobalLock(h);
        if (!ptr) { GlobalFree(h); CloseClipboard(); return false; }
        memcpy(ptr, out.c_str(), out.size() + 1);
        GlobalUnlock(h);
        SetClipboardData(CF_TEXT, h);
        CloseClipboard();
        return true;
    }

    bool ImportFriendsClipboard() {
        if (!OpenClipboard(nullptr)) return false;
        HANDLE h = GetClipboardData(CF_TEXT);
        if (!h) { CloseClipboard(); return false; }
        const char* data = (const char*)GlobalLock(h);
        if (!data) { CloseClipboard(); return false; }
        std::string text(data);
        GlobalUnlock(h);
        CloseClipboard();
        size_t pos = 0;
        while (pos < text.size()) {
            size_t nl = text.find('\n', pos);
            if (nl == std::string::npos) nl = text.size();
            std::string line = text.substr(pos, nl - pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            pos = nl + 1;
            if (line.empty() || line[0] == '#') continue;
            size_t comma = line.find(',');
            if (comma == std::string::npos) continue;
            uint32_t id = (uint32_t)strtoul(line.c_str(), nullptr, 10);
            std::string name = line.substr(comma + 1);
            if (id == 0) continue;
            if (!IsFriend(id))
                AddFriend(name.empty() ? ("ID " + std::to_string(id)) : name, id, false);
        }
        return true;
    }

} // namespace friends
