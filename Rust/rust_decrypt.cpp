#include "rust_decrypt.h"
#include "rust_game.h"
#include "../DMALibrary/Memory/Memory.h"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <vector>

namespace Rust {
namespace Decrypt {

ChainOffsets chain{};
DecryptOps ops{};
static uintptr_t s_ga = 0;

void SetGameAssembly(uintptr_t ga) { s_ga = ga; ApplyBuiltinDefaults(); }

static bool IsUser(uintptr_t p) {
    return p >= 0x10000ULL && p < 0x00007FFFFFFFFFFFULL;
}

template<typename T>
static bool ReadT(uintptr_t addr, T& out) {
    return mem.Read(addr, &out, sizeof(T));
}

// ── Defaults: cheatoffsets.com / user dump Aug 2026 (il2cpp 0x11A88F30) ──
void ApplyBuiltinDefaults() {
    if (!ops.gchandle_base_rva)
        ops.gchandle_base_rva = 0x11A88F30ull;
    // client_entities / visiblePlayerList:
    //   ecx ^= 0xC341ED7D; rol 30; ecx += 0x578817E0  (x2 dwords)
    if (!ops.ce_valid) {
        ops.ce_xor1 = 0xC341ED7Du;
        ops.ce_add1 = 0x578817E0u;
        ops.ce_rol  = 30;
        ops.ce_xor2 = 0;
        ops.ce_add2 = 0;
        ops.ce_valid = true;
    }
    // entity_list:
    //   ecx -= 0x55362D5A; ecx ^= 0x931EEE05; rol 25; ecx += 0x2AA8649C
    if (!ops.el_valid) {
        ops.el_xor1 = 0x931EEE05u;
        ops.el_add1 = 0xAAC9D2A6u; // -0x55362D5A as uint32 (applied as sub in transform)
        ops.el_rol  = 25;
        ops.el_xor2 = 0;
        ops.el_add2 = 0x2AA8649Cu;
        ops.el_valid = true;
    }
    if (!chain.client_entities) chain.client_entities = 0x8; // wrapper_class_ptr
    if (!chain.entity_list) chain.entity_list = 0x10;       // entities
    if (!chain.buffer_list) chain.buffer_list = 0x10;
    if (!chain.buffer) chain.buffer = 0x10;
    if (!chain.count) chain.count = 0x18;
    if (!offsets.BaseNetworkable_TypeInfo)
        offsets.BaseNetworkable_TypeInfo = 0x115B1A70ull;
    if (!offsets.MainCamera_TypeInfo)
        offsets.MainCamera_TypeInfo = 0x115BB318ull;
}

// Specialized: current client_entities / visiblePlayerList (user dump)
static uint64_t TransformClientEntities(uint64_t rax) {
    uint32_t* rdx = reinterpret_cast<uint32_t*>(&rax);
    for (uint32_t r8d = 2; r8d; --r8d) {
        uint32_t ecx = *rdx;
        ecx = ecx ^ 0xC341ED7Du;
        uint32_t eax = ecx;
        ecx = (ecx << 0x1E) | (eax >> 0x2); // ROL 30
        ecx = ecx + 0x578817E0u;
        *rdx++ = ecx;
    }
    return rax;
}

// Specialized: current entity_list transform
static uint64_t TransformEntityList(uint64_t rax) {
    uint32_t* rdx = reinterpret_cast<uint32_t*>(&rax);
    for (uint32_t r8d = 2; r8d; --r8d) {
        uint32_t ecx = *rdx;
        ecx = ecx - 0x55362D5Au;
        ecx = ecx ^ 0x931EEE05u;
        uint32_t eax = ecx;
        ecx = (ecx << 0x19) | (eax >> 0x7); // ROL 25
        ecx = ecx + 0x2AA8649Cu;
        *rdx++ = ecx;
    }
    return rax;
}

// Alternate older formula (Apr 2026) — try if primary fails
static uint64_t TransformClientEntitiesAlt(uint64_t rax) {
    uint32_t* rdx = reinterpret_cast<uint32_t*>(&rax);
    for (uint32_t r8d = 2; r8d; --r8d) {
        uint32_t eax = *rdx;
        eax = eax ^ 0x01E7771Eu;
        uint32_t ecx = eax;
        eax = eax << 6;
        ecx = ecx >> 0x1A;
        ecx = ecx | eax;
        ecx = ecx - 0x1118D90Cu;
        *rdx++ = ecx;
    }
    return rax;
}

static uint64_t TransformEntityListAlt(uint64_t rax) {
    uint32_t* rdx = reinterpret_cast<uint32_t*>(&rax);
    for (uint32_t r8d = 2; r8d; --r8d) {
        uint32_t eax = *rdx;
        eax = eax ^ 0xF651832Au;
        eax = eax + 0x87D72C46u;
        uint32_t ecx = eax;
        eax = eax << 4;
        ecx = ecx >> 0x1C;
        ecx = ecx | eax;
        *rdx++ = ecx;
    }
    return rax;
}

uintptr_t Il2cppGetHandle(int32_t handle) {
    if (!s_ga) return 0;
    ApplyBuiltinDefaults();
    if (!ops.gchandle_base_rva || handle == 0)
        return 0;

    // Two common layouts used by Rust external cheats
    auto try_table = [&](uintptr_t table_rva) -> uintptr_t {
        // Layout A: array of 4 buckets at base + 0x28 * bucket
        const uint32_t u = static_cast<uint32_t>(handle);
        const uint64_t idx = u >> 3;
        const uint64_t bucket = (u & 7u) - 1u;
        if (bucket <= 3) {
            const uintptr_t baseAddr = s_ga + table_rva + 0x28ull * bucket;
            uint32_t limit = 0;
            if (ReadT(baseAddr + 0x10, limit) && idx < limit) {
                uintptr_t bitmap = 0;
                if (ReadT(baseAddr, bitmap) && IsUser(bitmap)) {
                    uintptr_t slot = 0;
                    if (ReadT(bitmap + idx * 8ull, slot)) {
                        if (slot & 1ull) slot &= ~1ull;
                        if (IsUser(slot)) return slot;
                    }
                }
            }
        }
        // Layout B: single table pointer at GA+rva → objects[handle]
        uintptr_t table = 0;
        if (ReadT(s_ga + table_rva, table) && IsUser(table)) {
            uintptr_t slot = 0;
            if (ReadT(table + static_cast<uint64_t>(static_cast<uint32_t>(handle)) * 8ull, slot)) {
                if (slot & 1ull) slot &= ~1ull;
                if (IsUser(slot)) return slot;
            }
            // Some builds: handle is already an index into table+0x20
            if (ReadT(table + 0x20 + static_cast<uint64_t>(static_cast<uint32_t>(handle)) * 8ull, slot)) {
                if (slot & 1ull) slot &= ~1ull;
                if (IsUser(slot)) return slot;
            }
        }
        return 0;
    };

    uintptr_t p = try_table(ops.gchandle_base_rva);
    if (p) return p;
    // fallback known RVAs (newest first)
    for (uintptr_t alt : {0x11A88F30ull, 0x10C71E20ull, 0xE7DF910ull, 0xD38CDD0ull}) {
        if (alt == ops.gchandle_base_rva) continue;
        p = try_table(alt);
        if (p) {
            ops.gchandle_base_rva = alt;
            return p;
        }
    }
    return 0;
}

static uintptr_t DecryptWith(uint64_t raw, bool client_entities, int variant) {
    uint64_t transformed = raw;
    if (client_entities) {
        transformed = (variant == 0) ? TransformClientEntities(raw) : TransformClientEntitiesAlt(raw);
    } else {
        transformed = (variant == 0) ? TransformEntityList(raw) : TransformEntityListAlt(raw);
    }
    const int32_t handle = static_cast<int32_t>(transformed);
    uintptr_t obj = Il2cppGetHandle(handle);
    if (IsUser(obj)) return obj;
    // Sometimes decrypt yields a direct pointer
    if (IsUser(transformed)) return static_cast<uintptr_t>(transformed);
    // Try upper 32 bits as handle
    obj = Il2cppGetHandle(static_cast<int32_t>(transformed >> 32));
    return IsUser(obj) ? obj : 0;
}

static uintptr_t DecryptHandleObject(uintptr_t encrypted_obj, bool client_entities) {
    if (!IsUser(encrypted_obj)) return 0;
    ApplyBuiltinDefaults();
    uint64_t raw = 0;
    if (!ReadT(encrypted_obj + 0x18, raw) || !raw) {
        // try +0x10
        if (!ReadT(encrypted_obj + 0x10, raw) || !raw)
            return 0;
    }
    for (int v = 0; v < 2; ++v) {
        uintptr_t r = DecryptWith(raw, client_entities, v);
        if (IsUser(r)) return r;
    }
    return 0;
}

uintptr_t DecryptClientEntities(uintptr_t encrypted_obj) {
    return DecryptHandleObject(encrypted_obj, true);
}

uintptr_t DecryptEntityList(uintptr_t encrypted_obj) {
    return DecryptHandleObject(encrypted_obj, false);
}

uintptr_t ResolveVisiblePlayersBuffer(uint32_t& out_count) {
    out_count = 0;
    if (!s_ga) return 0;

    // Candidate TypeInfo RVAs (newest build first)
    const uintptr_t ti_try[] = {
        offsets.BasePlayer_TypeInfo,
        offsets.BaseNetworkable_TypeInfo,
        0x115B1A70ull,
        0x10720218ull,
        0x1074E028ull,
        0x1074dca8ull
    };
    const size_t ti_n = sizeof(ti_try) / sizeof(ti_try[0]);

    const uintptr_t sf_try[] = { 0xB8ull, 0x90ull, 0xC0ull, 0xA0ull, 0x80ull };
    const size_t sf_n = sizeof(sf_try) / sizeof(sf_try[0]);

    const uintptr_t try_list[] = {
        offsets.visiblePlayerList ? offsets.visiblePlayerList : 0x20ull,
        0x20, 0x18, 0x28, 0x30, 0x10, 0x08, 0x38, 0x40, 0x48, 0x50, 0x00
    };
    const size_t list_n = sizeof(try_list) / sizeof(try_list[0]);

    const uintptr_t bl_offs[] = { 0x28ull, 0x18ull, 0x10ull, 0x20ull, 0x30ull };
    const size_t bl_n = sizeof(bl_offs) / sizeof(bl_offs[0]);
    const uintptr_t size_offs[] = { 0x18ull, 0x10ull, 0x20ull, 0x14ull };
    const size_t size_n = sizeof(size_offs) / sizeof(size_offs[0]);
    const uintptr_t buf_offs[] = { 0x10ull, 0x18ull, 0x08ull, 0x20ull };
    const size_t buf_n = sizeof(buf_offs) / sizeof(buf_offs[0]);

    for (size_t ti_i = 0; ti_i < ti_n; ++ti_i) {
        const uintptr_t ti = ti_try[ti_i];
        if (!ti) continue;
        uintptr_t typeInfo = 0;
        if (!ReadT(s_ga + ti, typeInfo) || !IsUser(typeInfo))
            continue;
        for (size_t sf_i = 0; sf_i < sf_n; ++sf_i) {
            const uintptr_t sfOff = sf_try[sf_i];
            uintptr_t staticFields = 0;
            if (!ReadT(typeInfo + sfOff, staticFields) || !IsUser(staticFields))
                continue;
            for (size_t li = 0; li < list_n; ++li) {
                const uintptr_t listOff = try_list[li];
                uintptr_t listDict = 0;
                if (!ReadT(staticFields + listOff, listDict) || !IsUser(listDict))
                    continue;
                uintptr_t dec = DecryptClientEntities(listDict);
                if (IsUser(dec)) listDict = dec;

                for (size_t bi = 0; bi < bl_n; ++bi) {
                    const uintptr_t blOff = bl_offs[bi];
                    uintptr_t vals = 0;
                    if (!ReadT(listDict + blOff, vals) || !IsUser(vals))
                        continue;
                    for (size_t si = 0; si < size_n; ++si) {
                        const uintptr_t sizeOff = size_offs[si];
                        uint32_t size = 0;
                        if (!ReadT(vals + sizeOff, size) || size == 0 || size > 512)
                            continue;
                        for (size_t bui = 0; bui < buf_n; ++bui) {
                            const uintptr_t bufOff = buf_offs[bui];
                            uintptr_t buffer = 0;
                            if (!ReadT(vals + bufOff, buffer) || !IsUser(buffer))
                                continue;
                            uintptr_t first = 0;
                            ReadT(buffer + 0x20, first);
                            if (!IsUser(first)) ReadT(buffer, first);
                            if (!IsUser(first)) continue;
                            offsets.visiblePlayerList = listOff;
                            offsets.bufferList = blOff;
                            offsets.buffer = bufOff;
                            offsets.bufferSize = sizeOff;
                            offsets.staticFields = sfOff;
                            if (ti == 0x10720218ull || ti == offsets.BasePlayer_TypeInfo)
                                offsets.BasePlayer_TypeInfo = ti;
                            out_count = size;
                            return buffer;
                        }
                    }
                }
            }
        }
    }
    return 0;
}

uintptr_t ResolveNetworkableBuffer(uint32_t& out_count) {
    out_count = 0;
    if (!s_ga) return 0;
    ApplyBuiltinDefaults();

    if (!offsets.BaseNetworkable_TypeInfo)
        offsets.BaseNetworkable_TypeInfo = 0x115B1A70ull;

    uintptr_t klass = 0;
    if (!ReadT(s_ga + offsets.BaseNetworkable_TypeInfo, klass) || !IsUser(klass))
        return 0;

    uintptr_t staticFields = 0;
    for (uintptr_t sfo : {0xB8ull, 0x90ull, 0xC0ull}) {
        if (ReadT(klass + sfo, staticFields) && IsUser(staticFields)) {
            chain.static_fields = sfo;
            break;
        }
        staticFields = 0;
    }
    if (!IsUser(staticFields))
        return 0;

    // cheatoffsets: wrapper_class_ptr 0x8, entities 0x20
    const uintptr_t try_off[] = {
        chain.client_entities, 0x8, 0x20, 0x28, 0x18, 0x10, 0x0, 0x30
    };
    uintptr_t realm = 0;
    for (uintptr_t off : try_off) {
        uintptr_t enc = 0;
        if (!ReadT(staticFields + off, enc))
            continue;
        if (IsUser(enc)) {
            realm = DecryptClientEntities(enc);
            if (!IsUser(realm)) realm = enc; // already plain
        } else if (enc) {
            // might be an encrypted object pointer that still reads
            realm = DecryptClientEntities(enc);
        }
        if (IsUser(realm)) {
            chain.client_entities = off;
            break;
        }
        realm = 0;
    }
    if (!IsUser(realm))
        return 0;

    // entity list at realm+0x10 (or 0x20)
    uintptr_t list = 0;
    for (uintptr_t elOff : {chain.entity_list, 0x10ull, 0x20ull, 0x18ull, 0x28ull}) {
        uintptr_t encList = 0;
        if (!ReadT(realm + elOff, encList))
            continue;
        list = DecryptEntityList(encList);
        if (!IsUser(list) && IsUser(encList))
            list = encList;
        if (IsUser(list)) {
            chain.entity_list = elOff;
            break;
        }
        list = 0;
    }
    if (!IsUser(list))
        return 0;

    // bufferlist
    for (uintptr_t blOff : {chain.buffer_list, 0x10ull, 0x18ull, 0x20ull, 0x28ull}) {
        uintptr_t bufferList = 0;
        if (!ReadT(list + blOff, bufferList) || !IsUser(bufferList))
            continue;
        for (uintptr_t cntOff : {chain.count, 0x18ull, 0x10ull, 0x14ull}) {
            uint32_t count = 0;
            if (!ReadT(bufferList + cntOff, count) || count == 0 || count > 16384)
                continue;
            for (uintptr_t bufOff : {chain.buffer, 0x10ull, 0x18ull, 0x08ull, 0x20ull}) {
                uintptr_t buffer = 0;
                if (!ReadT(bufferList + bufOff, buffer) || !IsUser(buffer))
                    continue;
                uintptr_t first = 0;
                ReadT(buffer + 0x20, first);
                if (!IsUser(first)) ReadT(buffer, first);
                if (!IsUser(first)) continue;
                chain.buffer_list = blOff;
                chain.count = cntOff;
                chain.buffer = bufOff;
                out_count = count;
                return buffer;
            }
        }
    }
    return 0;
}

std::string ProbeEntityChainStatus() {
    ApplyBuiltinDefaults();
    std::ostringstream ss;
    ss << std::hex;
    uint32_t vis = 0, bn = 0;
    uintptr_t vb = ResolveVisiblePlayersBuffer(vis);
    uintptr_t nb = ResolveNetworkableBuffer(bn);
    ss << "visiblePlayers buf=0x" << vb << " count=" << std::dec << vis
       << " | BN buf=0x" << std::hex << nb << " count=" << std::dec << bn
       << " | BN_TI=0x" << std::hex << offsets.BaseNetworkable_TypeInfo
       << " gchandle=0x" << ops.gchandle_base_rva
       << " ce=" << (ops.ce_valid ? "Y" : "N")
       << " el=" << (ops.el_valid ? "Y" : "N");
    return ss.str();
}

static bool ParseHexU32(const std::string& s, uint32_t& out) {
    try {
        out = static_cast<uint32_t>(std::stoul(s, nullptr, 0));
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[Rust] ParseHexU32 failed for '" << s << "': " << ex.what() << "\n";
        return false;
    } catch (...) {
        std::cerr << "[Rust] ParseHexU32 unknown exception for '" << s << "'\n";
        return false;
    }
}

static bool ParseHexU64(const std::string& s, uintptr_t& out) {
    try {
        out = static_cast<uintptr_t>(std::stoull(s, nullptr, 0));
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[Rust] ParseHexU64 failed for '" << s << "': " << ex.what() << "\n";
        return false;
    } catch (...) {
        std::cerr << "[Rust] ParseHexU64 unknown exception for '" << s << "'\n";
        return false;
    }
}

static bool GrabStr(const std::string& json, const char* key, std::string& out) {
    const std::string k1 = std::string("\"") + key + "\"";
    size_t p = json.find(k1);
    if (p == std::string::npos) return false;
    p = json.find(':', p);
    if (p == std::string::npos) return false;
    ++p;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) ++p;
    if (p >= json.size()) return false;
    if (json[p] == '"') {
        size_t e = json.find('"', p + 1);
        if (e == std::string::npos) return false;
        out = json.substr(p + 1, e - p - 1);
        return true;
    }
    size_t e = p;
    while (e < json.size() && (isalnum((unsigned char)json[e]) || json[e] == 'x' || json[e] == 'X')) ++e;
    out = json.substr(p, e - p);
    return !out.empty();
}

bool LoadDecryptFromJson(const std::string& json_text) {
    std::string v;
    if (GrabStr(json_text, "client_entities", v)) ParseHexU64(v, chain.client_entities);
    if (GrabStr(json_text, "entity_list", v)) ParseHexU64(v, chain.entity_list);
    if (GrabStr(json_text, "gchandle_base_rva", v)) ParseHexU64(v, ops.gchandle_base_rva);
    if (GrabStr(json_text, "BaseNetworkable_TypeInfo", v)) {
        uintptr_t bn = 0;
        if (ParseHexU64(v, bn) && bn) offsets.BaseNetworkable_TypeInfo = bn;
    }

    auto grab_u32 = [&](const char* k, uint32_t& dst) {
        std::string s;
        if (GrabStr(json_text, k, s)) ParseHexU32(s, dst);
    };
    grab_u32("ce_xor1", ops.ce_xor1);
    grab_u32("ce_add1", ops.ce_add1);
    grab_u32("ce_rol", ops.ce_rol);
    grab_u32("ce_xor2", ops.ce_xor2);
    grab_u32("ce_add2", ops.ce_add2);
    grab_u32("el_xor1", ops.el_xor1);
    grab_u32("el_add1", ops.el_add1);
    grab_u32("el_rol", ops.el_rol);
    grab_u32("el_xor2", ops.el_xor2);
    grab_u32("el_add2", ops.el_add2);

    if ((ops.ce_xor1 | ops.ce_add1 | ops.ce_xor2 | ops.ce_add2) != 0)
        ops.ce_valid = true;
    if ((ops.el_xor1 | ops.el_add1 | ops.el_xor2 | ops.el_add2) != 0)
        ops.el_valid = true;

    ApplyBuiltinDefaults();
    return true;
}

} // namespace Decrypt
} // namespace Rust
