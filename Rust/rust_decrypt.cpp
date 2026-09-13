#include "rust_decrypt.h"
#include "../DMALibrary/Memory/Memory.h"
#include <cstdio>
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>

extern Memory mem;

namespace Rust {
namespace Decrypt {

ChainOffsets chain{};
DecryptOps ops{};
static uintptr_t g_ga = 0;

void SetGameAssembly(uintptr_t ga) { g_ga = ga; }

static bool ReadU64(uintptr_t a, uint64_t& o) {
    return mem.Read(a, &o, sizeof(o));
}
static uint32_t Rol32(uint32_t v, uint32_t r) {
    r &= 31u;
    return r ? ((v << r) | (v >> (32u - r))) : v;
}

void ApplyBuiltinDefaults() {
    if (!ops.gchandle_base_rva)
        ops.gchandle_base_rva = 0x10CA7920ull;
    // XOR constants are build-specific; leave 0 until JSON/probe fills them.
}

static bool GrabHex(const std::string& j, const char* key, uintptr_t& out) {
    const std::string pat = std::string("\"") + key + "\"";
    auto pos = j.find(pat);
    if (pos == std::string::npos) return false;
    pos = j.find("0x", pos);
    if (pos == std::string::npos) {
        pos = j.find(':', j.find(pat));
        if (pos == std::string::npos) return false;
        char* end = nullptr;
        unsigned long long v = strtoull(j.c_str() + pos + 1, &end, 0);
        if (end == j.c_str() + pos + 1) return false;
        out = static_cast<uintptr_t>(v);
        return out != 0;
    }
    char* end = nullptr;
    out = static_cast<uintptr_t>(strtoull(j.c_str() + pos, &end, 16));
    return out != 0;
}

static bool GrabU32(const std::string& j, const char* key, uint32_t& out) {
    uintptr_t v = 0;
    if (!GrabHex(j, key, v)) {
        const std::string pat = std::string("\"") + key + "\"";
        auto pos = j.find(pat);
        if (pos == std::string::npos) return false;
        pos = j.find(':', pos);
        if (pos == std::string::npos) return false;
        out = static_cast<uint32_t>(strtoul(j.c_str() + pos + 1, nullptr, 0));
        return true;
    }
    out = static_cast<uint32_t>(v);
    return true;
}

bool LoadDecryptFromJson(const std::string& json_text) {
    GrabHex(json_text, "gchandle_base_rva", ops.gchandle_base_rva);
    GrabHex(json_text, "il2cpphandle", ops.gchandle_base_rva);
    GrabHex(json_text, "client_entities_decrypt_rva", ops.client_entities_decrypt_rva);
    GrabHex(json_text, "entity_list_decrypt_rva", ops.entity_list_decrypt_rva);
    GrabHex(json_text, "client_entities", chain.client_entities);
    GrabHex(json_text, "entity_list", chain.entity_list);
    GrabHex(json_text, "static_fields", chain.static_fields);
    GrabU32(json_text, "ce_xor1", ops.ce_xor1);
    GrabU32(json_text, "ce_add1", ops.ce_add1);
    GrabU32(json_text, "ce_rol", ops.ce_rol);
    GrabU32(json_text, "ce_xor2", ops.ce_xor2);
    GrabU32(json_text, "ce_add2", ops.ce_add2);
    GrabU32(json_text, "el_xor1", ops.el_xor1);
    GrabU32(json_text, "el_add1", ops.el_add1);
    GrabU32(json_text, "el_rol", ops.el_rol);
    GrabU32(json_text, "el_xor2", ops.el_xor2);
    GrabU32(json_text, "el_add2", ops.el_add2);
    ops.ce_valid = (ops.ce_xor1 | ops.ce_add1 | ops.ce_xor2 | ops.ce_add2) != 0;
    ops.el_valid = (ops.el_xor1 | ops.el_add1 | ops.el_xor2 | ops.el_add2) != 0;
    ApplyBuiltinDefaults();
    return ops.gchandle_base_rva != 0;
}

uintptr_t Il2cppGetHandle(int32_t handle) {
    if (!g_ga || !ops.gchandle_base_rva || handle == 0)
        return 0;
    const uint32_t u = static_cast<uint32_t>(handle);
    // Layout A: classic IL2CPP GC handle buckets
    const uint32_t idx = u >> 3;
    const uint32_t type = (u & 7u) - 1u;
    if (type > 3) {
        // Layout B: flat table
        uint64_t table = 0;
        if (!ReadU64(g_ga + ops.gchandle_base_rva, table) || !table) return 0;
        uint64_t slot = 0;
        if (ReadU64(table + static_cast<uint64_t>(u) * 8ull, slot) && slot)
            return static_cast<uintptr_t>(slot);
        if (ReadU64(table + 0x20 + static_cast<uint64_t>(u) * 8ull, slot) && slot)
            return static_cast<uintptr_t>(slot);
        return 0;
    }
    uint64_t base = 0;
    if (!ReadU64(g_ga + ops.gchandle_base_rva + 0x8 * type + 0x0, base)) {
        // alternate: single base + 0x28 stride
        if (!ReadU64(g_ga + ops.gchandle_base_rva + 0x28ull * type, base) || !base)
            return 0;
    }
    if (!base) {
        if (!ReadU64(g_ga + ops.gchandle_base_rva + 0x28ull * type, base) || !base)
            return 0;
    }
    uint64_t slot = 0;
    if (!ReadU64(base + static_cast<uint64_t>(idx) * 8ull, slot))
        return 0;
    return static_cast<uintptr_t>(slot);
}

static uint64_t Transform(uint64_t raw, bool client_entities) {
    uint32_t lo = static_cast<uint32_t>(raw);
    uint32_t hi = static_cast<uint32_t>(raw >> 32);
    if (client_entities && ops.ce_valid) {
        lo ^= ops.ce_xor1;
        lo += ops.ce_add1;
        lo = Rol32(lo, ops.ce_rol);
        lo ^= ops.ce_xor2;
        lo += ops.ce_add2;
    } else if (!client_entities && ops.el_valid) {
        lo ^= ops.el_xor1;
        lo += ops.el_add1;
        lo = Rol32(lo, ops.el_rol);
        lo ^= ops.el_xor2;
        lo += ops.el_add2;
    }
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

static uintptr_t DecryptHandleObject(uintptr_t encrypted_obj, bool client_entities) {
    if (!encrypted_obj) return 0;
    uint64_t raw = 0;
    // Common: handle payload at +0x14/+0x18
    for (uintptr_t delta : {0x18ull, 0x14ull, 0x10ull}) {
        if (!ReadU64(encrypted_obj + delta, raw) || !raw) continue;
        const uint64_t t = Transform(raw, client_entities);
        if (auto p = Il2cppGetHandle(static_cast<int32_t>(t))) return p;
        if (auto p = Il2cppGetHandle(static_cast<int32_t>(t >> 32))) return p;
        // Sometimes transform yields pointer directly
        if ((t >> 40) > 0 && (t >> 40) < 0x8000) return static_cast<uintptr_t>(t);
    }
    // Raw pointer passthrough (non-encrypted builds)
    uint64_t direct = 0;
    if (ReadU64(encrypted_obj + 0x10, direct) && (direct >> 40) > 0)
        return static_cast<uintptr_t>(direct);
    return 0;
}

uintptr_t DecryptClientEntities(uintptr_t encrypted_obj) {
    return DecryptHandleObject(encrypted_obj, true);
}
uintptr_t DecryptEntityList(uintptr_t encrypted_obj) {
    return DecryptHandleObject(encrypted_obj, false);
}

uintptr_t ResolveNetworkableBuffer(uint32_t& out_count) {
    out_count = 0;
    if (!g_ga) return 0;

    // Read BN TypeInfo pointer from JSON-loaded global stored by game layer via ops — use chain from static field.
    // Caller must have set objects; we re-read TypeInfo from a well-known path:
    // Actual BN address is injected via static: we expect game layer to pass through Read of (GA+BN_RVA).
    // Here we only walk from a provided static objects pointer stored in thread-local alternative:
    // Implemented fully in entities using this decrypt for CE/EL steps.
    return 0;
}

uintptr_t ResolveVisiblePlayersBuffer(uintptr_t /*local_player*/, uint32_t& out_count) {
    out_count = 0;
    return 0;
}

std::string ProbeEntityChainStatus() {
    std::ostringstream o;
    o << "ga=0x" << std::hex << g_ga
      << " gchandle_rva=0x" << ops.gchandle_base_rva
      << " ce_rva=0x" << ops.client_entities_decrypt_rva
      << " el_rva=0x" << ops.entity_list_decrypt_rva
      << " ce_valid=" << ops.ce_valid << " el_valid=" << ops.el_valid
      << std::dec;
    return o.str();
}

} // namespace Decrypt
} // namespace Rust
