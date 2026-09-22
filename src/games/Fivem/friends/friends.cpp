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

        if (!offset::localplayer || FiveM::ESP::validPeds.empty())
            return;

        Vec3 localPos = mem.Read<Vec3>(offset::localplayer + offset::playerPosition);

        for (size_t i = 0; i < FiveM::ESP::validPeds.size(); ++i) {
            uintptr_t ped = FiveM::ESP::validPeds[i];
            if (!ped || ped == offset::localplayer) continue;

            uintptr_t pinfo = mem.Read<uintptr_t>(ped + offset::playerInfo);
            if (!pinfo) continue;

            uint32_t netId = mem.Read<uint32_t>(pinfo + offset::playerInfo_netId);

            Vec3 pos = (i < FiveM::ESP::positions.size()) ? FiveM::ESP::positions[i] : Vec3{};
            float dist = pos.IsZero() ? 0.f : pos.distance_to(localPos);

            float hp = mem.Read<float>(ped + offset::playerHealth);
            float maxHp = mem.Read<float>(ped + 0x284);
            if (maxHp < 1.f) maxHp = 200.f;
            float hpPct = (std::max)(0.f, (std::min)(100.f, (hp / maxHp) * 100.f));

            char nameBuf[64]{};
            // try CPlayerInfo+0xFC name
            char raw[32]{};
            mem.Read(pinfo + offset::playerInfo_name, raw, 31);
            raw[31] = 0;
            bool nameOk = raw[0] && (unsigned char)raw[0] >= 32 && (unsigned char)raw[0] < 127;
            if (!nameOk) {
                uintptr_t sp = mem.Read<uintptr_t>(pinfo + offset::playerInfo_name);
                if (sp > 0x10000) {
                    memset(raw, 0, sizeof(raw));
                    mem.Read(sp, raw, 31);
                    nameOk = raw[0] != 0;
                }
            }
            if (nameOk)
                snprintf(nameBuf, sizeof(nameBuf), "%s", raw);
            else
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
