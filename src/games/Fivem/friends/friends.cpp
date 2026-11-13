#include <unordered_set>
#include <Windows.h>
#include "friends.h"
#include "../game/game.h"
#include "../game/offsets.h"
#include "../game/esp_manager.h"
#include "../esp/esp.h"
#include "../playerInfo/PedData.h"
#include "math/math.h"
#include <Memory/Memory.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <string>
#include <chrono>
#include <atomic>

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
    std::atomic<uint64_t> list_version{0};
    static std::atomic<ULONGLONG> s_playerListRequestedMs{0};

    void RequestPlayerList() noexcept {
        s_playerListRequestedMs.store(GetTickCount64(), std::memory_order_release);
    }

    bool PlayerListRequestedRecently() noexcept {
        const ULONGLONG requested = s_playerListRequestedMs.load(std::memory_order_acquire);
        const ULONGLONG now = GetTickCount64();
        return requested != 0 && now >= requested && now - requested <= 500;
    }

    void Initialize() {
        s_playerListRequestedMs.store(0, std::memory_order_release);
        friend_list.clear();
        s_friendIds.clear();
        player_list.clear();
        list_version.fetch_add(1, std::memory_order_release);
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
        uintptr_t pinfo = mem.Read<uintptr_t>(ped + offset::playerInfo);
        if (!pinfo) return false;
        uint32_t netId = mem.Read<uint32_t>(pinfo + offset::playerInfo_netId);
        return IsFriend(netId);
    }

    void AddFriend(const std::string& name, uint32_t id, bool prox) {
        if (id == 0) return;
        std::lock_guard<std::mutex> lock(list_mutex);
        for (const auto& f : friend_list)
            if (f.id == id) return;
        friend_list.push_back({ name, id, prox });
        s_friendIds.insert(id);
        list_version.fetch_add(1, std::memory_order_release);
    }

    void RemoveFriend(uint32_t id) {
        std::lock_guard<std::mutex> lock(list_mutex);
        friend_list.erase(
            std::remove_if(friend_list.begin(), friend_list.end(),
                [id](const FriendEntry& f) { return f.id == id; }),
            friend_list.end());
        RebuildIdSet();
        list_version.fetch_add(1, std::memory_order_release);
    }

    void UpdatePlayerList() {
        using namespace FiveM;


        std::vector<PlayerEntry> next;
        next.reserve(32);

        auto snapshot = FiveM::ESP::GetValidPedsSnapshot();
        if (!FiveM::ESP::FrameCacheValid() || snapshot.validPeds.empty()) {
            std::lock_guard<std::mutex> lock(list_mutex);
            player_list.clear();
            list_version.fetch_add(1, std::memory_order_release);
            return;
        }

        const Vec3 localPos = FiveM::ESP::GetFrameLocalPos();
        if (localPos.IsZero()) {
            std::lock_guard<std::mutex> lock(list_mutex);
            player_list.clear();
            list_version.fetch_add(1, std::memory_order_release);
            return;
        }

        for (size_t i = 0; i < snapshot.validPeds.size(); ++i) {
            uintptr_t ped = snapshot.validPeds[i];
            if (!ped || ped == offset::localplayer) continue;

            uint32_t netId = 0;
            if (!esp::try_get_prepared_network_id(ped, netId)) continue;

            Vec3 pos = (i < FiveM::ESP::positions.size()) ? FiveM::ESP::positions[i] : Vec3{};
            float dist = pos.IsZero() ? 0.f : pos.distance_to(localPos);

            float hp = 0.f;
            const float hpPct = esp::try_get_prepared_health(ped, hp)
                ? (std::max)(0.f, (std::min)(100.f, hp * 0.5f)) : 0.f;

            char nameBuf[64]{};
            if (!esp::try_get_prepared_player_name(ped, nameBuf, sizeof(nameBuf)))
                snprintf(nameBuf, sizeof(nameBuf), "Jogador_%u", netId);

            next.push_back({ nameBuf, netId, dist, hpPct, ped });
        }

        std::sort(next.begin(), next.end(),
            [](const PlayerEntry& a, const PlayerEntry& b) { return a.distance < b.distance; });

        std::lock_guard<std::mutex> lock(list_mutex);
        player_list = std::move(next);
        list_version.fetch_add(1, std::memory_order_release);
    }

    void AddClosestAsFriend() {
        UpdatePlayerList();
        if (player_list.empty()) return;

        const auto& p = player_list.front();
        if (p.distance <= config.prox_max_distance)
            AddFriend(p.name, p.id, true);
    }

    void KickClosestFromFriends() {

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

}
