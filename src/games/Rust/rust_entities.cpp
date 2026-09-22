#include "rust_entities.h"
#include "rust_config.h"
#include "rust_decrypt.h"
#include "../DMALibrary/Memory/Memory.h"
#include <cmath>
#include <mutex>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cstring>

extern Memory mem;

namespace Rust {
namespace Entities {
namespace {

std::mutex g_mtx;
std::vector<Player> g_players;

template <typename T>
bool Rd(uintptr_t a, T& o) {
    return a && mem.Read(a, &o, sizeof(T));
}

bool ReadVec3(uintptr_t a, float out[3]) {
    struct { float x, y, z; } v{};
    if (!Rd(a, v)) return false;
    out[0] = v.x; out[1] = v.y; out[2] = v.z;
    return true;
}

bool ReadMatrix(uintptr_t a, float m[16]) {
    return mem.Read(a, m, sizeof(float) * 16);
}

// IL2CPP string: +0x10 length, +0x14 wchar chars (Unity)
bool ReadIl2CppString(uintptr_t str, char* dst, size_t dstn) {
    if (!str || !dst || dstn < 2) return false;
    int32_t len = 0;
    if (!Rd(str + 0x10, len) || len <= 0 || len > 64) return false;
    std::vector<wchar_t> buf(static_cast<size_t>(len) + 1, 0);
    if (!mem.Read(str + 0x14, buf.data(), static_cast<size_t>(len) * sizeof(wchar_t)))
        return false;
    size_t o = 0;
    for (int i = 0; i < len && o + 1 < dstn; ++i) {
        wchar_t c = buf[static_cast<size_t>(i)];
        dst[o++] = (c > 0 && c < 128) ? static_cast<char>(c) : '?';
    }
    dst[o] = 0;
    return o > 0;
}

uintptr_t FollowCamera() {
    const uintptr_t ga = runtime.game_assembly;
    if (!ga || !offsets.MainCamera_TypeInfo) return 0;
    uint64_t ti = 0;
    if (!Rd(ga + offsets.MainCamera_TypeInfo, ti) || !ti) return 0;
    uint64_t statics = 0;
    if (!Rd(static_cast<uintptr_t>(ti) + offsets.cameraStatic, statics) || !statics) return 0;
    // wrapper chain: statics → wrapper → parent → object
    uint64_t wrapper = 0;
    if (!Rd(static_cast<uintptr_t>(statics) + offsets.cameraWrapper, wrapper) || !wrapper) {
        // alternate: instance pointer at +0x0/+0x10
        if (!Rd(static_cast<uintptr_t>(statics) + 0x0, wrapper))
            return 0;
    }
    uint64_t parent = wrapper;
    uint64_t tmp = 0;
    if (Rd(static_cast<uintptr_t>(wrapper) + offsets.cameraParent, tmp) && tmp)
        parent = tmp;
    uint64_t cam = parent;
    if (Rd(static_cast<uintptr_t>(parent) + offsets.cameraObject, tmp) && tmp)
        cam = tmp;
    if (Rd(static_cast<uintptr_t>(cam) + 0x10, tmp) && tmp)
        cam = tmp; // unity object classic
    return static_cast<uintptr_t>(cam);
}

bool UpdateCamera() {
    runtime.matrix_ok = false;
    uintptr_t cam = FollowCamera();
    if (!cam) return false;
    if (!ReadMatrix(cam + offsets.viewMatrix, runtime.view_matrix))
        return false;
    ReadVec3(cam + 0x454, runtime.local_pos); // camera position from dumper
    runtime.matrix_ok = true;
    return true;
}

uintptr_t ResolveEntityBuffer(uint32_t& count) {
    count = 0;
    const uintptr_t ga = runtime.game_assembly;
    if (!ga || !offsets.BaseNetworkable_TypeInfo) return 0;

    uint64_t ti = 0;
    if (!Rd(ga + offsets.BaseNetworkable_TypeInfo, ti) || !ti) return 0;
    uint64_t statics = 0;
    if (!Rd(static_cast<uintptr_t>(ti) + offsets.staticFields, statics) || !statics) return 0;

    uint64_t enc_ce = 0;
    if (!Rd(static_cast<uintptr_t>(statics) + offsets.clientEntities, enc_ce) || !enc_ce) {
        // try a few common slots
        for (uintptr_t d : {0x0ull, 0x10ull, 0x18ull, 0x20ull, 0x28ull}) {
            if (Rd(static_cast<uintptr_t>(statics) + d, enc_ce) && enc_ce) break;
        }
    }
    if (!enc_ce) return 0;

    uintptr_t realm = Decrypt::DecryptClientEntities(static_cast<uintptr_t>(enc_ce));
    if (!realm) {
        // non-encrypted: treat as direct pointer
        realm = static_cast<uintptr_t>(enc_ce);
    }

    uint64_t enc_list = 0;
    if (!Rd(realm + offsets.entityList, enc_list) || !enc_list) return 0;
    uintptr_t list = Decrypt::DecryptEntityList(static_cast<uintptr_t>(enc_list));
    if (!list) list = static_cast<uintptr_t>(enc_list);

    uint64_t buf = 0;
    int32_t sz = 0;
    if (!Rd(list + offsets.buffer, buf) || !buf) {
        // ListDictionary vals pattern
        uint64_t vals = 0;
        if (Rd(list + 0x10, vals) && vals) {
            Rd(static_cast<uintptr_t>(vals) + 0x10, buf);
            Rd(static_cast<uintptr_t>(vals) + 0x18, sz);
        }
    } else {
        Rd(list + offsets.bufferSize, sz);
    }
    if (!buf || sz <= 0 || sz > 50000) return 0;
    count = static_cast<uint32_t>(sz);
    // IL2CPP array: first element at +0x20
    return static_cast<uintptr_t>(buf) + 0x20;
}

Player ReadPlayer(uintptr_t ent) {
    Player p{};
    p.address = ent;
    if (!ent) return p;

    uint64_t model = 0;
    if (!Rd(ent + offsets.playerModel, model) || !model) return p;

    if (!ReadVec3(static_cast<uintptr_t>(model) + offsets.modelPosition, p.pos))
        return p;

    // Sanity world coords
    if (std::fabs(p.pos[0]) > 100000.f || std::fabs(p.pos[2]) > 100000.f)
        return p;

    p.valid = true;
    uint32_t flags = 0;
    Rd(ent + offsets.playerFlags, flags);
    p.sleeping = (flags & 16u) != 0;
    p.wounded = (flags & 64u) != 0;

    int32_t life = 0;
    Rd(ent + offsets.lifestate, life);
    if (life != 0) { /* dead */ p.valid = p.valid && false; }

    float hp = 0, mhp = 100.f;
    Rd(ent + offsets.health, hp);
    Rd(ent + offsets.maxHealth, mhp);
    p.health = hp;
    p.max_health = (mhp > 1.f) ? mhp : 100.f;

    uint64_t team = 0;
    Rd(ent + offsets.currentTeam, team);
    p.team_id = static_cast<int>(team);

    uint8_t npc = 0;
    Rd(static_cast<uintptr_t>(model) + offsets.isNpc, npc);
    p.npc = npc != 0;

    uint64_t namePtr = 0;
    if (Rd(ent + offsets.displayName, namePtr) && namePtr)
        ReadIl2CppString(static_cast<uintptr_t>(namePtr), p.name, sizeof(p.name));
    if (!p.name[0])
        std::snprintf(p.name, sizeof(p.name), "player");

    p.head[0] = p.pos[0];
    p.head[1] = p.pos[1] + 1.7f;
    p.head[2] = p.pos[2];
    return p;
}

} // namespace

bool Refresh() {
    std::vector<Player> next;
    next.reserve(64);
    UpdateCamera();

    uint32_t count = 0;
    uintptr_t arr = ResolveEntityBuffer(count);
    runtime.entity_count = static_cast<int>(count);

    if (arr && count) {
        const uint32_t maxN = (std::min)(count, 256u);
        for (uint32_t i = 0; i < maxN; ++i) {
            uint64_t ent = 0;
            if (!Rd(arr + static_cast<uintptr_t>(i) * 8ull, ent) || !ent)
                continue;
            Player p = ReadPlayer(static_cast<uintptr_t>(ent));
            if (!p.valid) continue;
            if (config.esp_ignore_sleepers && p.sleeping) continue;
            if (config.esp_ignore_npc && p.npc) continue;

            const float dx = p.pos[0] - runtime.local_pos[0];
            const float dy = p.pos[1] - runtime.local_pos[1];
            const float dz = p.pos[2] - runtime.local_pos[2];
            p.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (p.distance > config.esp_max_distance) continue;

            // heuristic local: closest to camera origin
            next.push_back(p);
        }
    }

    // Mark local (nearest under 2m to camera)
    float best = 1e9f;
    int best_i = -1;
    for (int i = 0; i < static_cast<int>(next.size()); ++i) {
        if (next[static_cast<size_t>(i)].distance < best) {
            best = next[static_cast<size_t>(i)].distance;
            best_i = i;
        }
    }
    if (best_i >= 0 && best < 2.5f) {
        next[static_cast<size_t>(best_i)].is_local = true;
        runtime.local_player = next[static_cast<size_t>(best_i)].address;
    }

    {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_players.swap(next);
        runtime.player_count = static_cast<int>(g_players.size());
    }
    return runtime.player_count > 0;
}

const std::vector<Player>& Players() {
    return g_players;
}

void Clear() {
    std::lock_guard<std::mutex> lock(g_mtx);
    g_players.clear();
    runtime.player_count = 0;
    runtime.entity_count = 0;
    runtime.local_player = 0;
    runtime.matrix_ok = false;
}

} // namespace Entities
} // namespace Rust
