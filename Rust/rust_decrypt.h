#pragma once
#include <cstdint>
#include <string>

namespace Rust {
namespace Decrypt {

// Chain offsets (stable across many builds; constants inside decrypt change each patch)
struct ChainOffsets {
    uintptr_t static_fields = 0xB8;
    uintptr_t client_entities = 0x28;   // static Encrypted<EntityRealm> — try 0x0/0x10/0x18/0x20/0x28
    uintptr_t entity_list = 0x10;       // Encrypted list inside realm
    uintptr_t buffer_list = 0x10;       // ListDictionary vals / bufferlist
    uintptr_t buffer = 0x10;            // T[] inside BufferList
    uintptr_t count = 0x18;
};

// Per-patch decrypt ops (2× uint32 rounds). Loaded from rust_offsets.json "decrypt" or probed.
struct DecryptOps {
    // client_entities (wrapper) — applied to qword at encrypted+0x18
    uint32_t ce_xor1 = 0;
    uint32_t ce_add1 = 0;
    uint32_t ce_rol = 0;   // left rotate amount (0 = disabled identity-ish)
    uint32_t ce_xor2 = 0;
    uint32_t ce_add2 = 0;
    bool ce_valid = false;

    // entity_list
    uint32_t el_xor1 = 0;
    uint32_t el_add1 = 0;
    uint32_t el_rol = 0;
    uint32_t el_xor2 = 0;
    uint32_t el_add2 = 0;
    bool el_valid = false;

    // GCHandle table RVA (GameAssembly + rva)
    uintptr_t gchandle_base_rva = 0;
};

extern ChainOffsets chain;
extern DecryptOps ops;

// Resolve GCHandle handle → object pointer (needs ops.gchandle_base_rva)
uintptr_t Il2cppGetHandle(int32_t handle);

// Decrypt encrypted wrapper pointer (reads +0x18 from encrypted object, then handle)
uintptr_t DecryptClientEntities(uintptr_t encrypted_obj);
uintptr_t DecryptEntityList(uintptr_t encrypted_obj);

// Full chain:
// GA+BN_TypeInfo → +0xB8 static → +client_entities → DecryptCE → +entity_list → DecryptEL → buffer/count
// Returns buffer pointer, writes count. Returns 0 if chain fails.
uintptr_t ResolveNetworkableBuffer(uint32_t& out_count);

// Prefer players via BasePlayer.visiblePlayerList (no BN decrypt). Always try this first for ESP.
uintptr_t ResolveVisiblePlayersBuffer(uint32_t& out_count);

// Live probe: tries common client_entities offsets + reports which chain links are non-null.
// Does NOT invent XOR constants — logs what is readable so RE can fill ops.
std::string ProbeEntityChainStatus();

bool LoadDecryptFromJson(const std::string& json_text);
void SetGameAssembly(uintptr_t ga);
void ApplyBuiltinDefaults();

} // namespace Decrypt
} // namespace Rust
