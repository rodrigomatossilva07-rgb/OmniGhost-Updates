#pragma once
#include <cstdint>
#include <string>

namespace Rust {
namespace Decrypt {

struct ChainOffsets {
    uintptr_t static_fields = 0xB8;
    uintptr_t client_entities = 0x28;
    uintptr_t entity_list = 0x10;
    uintptr_t buffer = 0x10;
    uintptr_t count = 0x18;
};

struct DecryptOps {
    uintptr_t gchandle_base_rva = 0;
    uintptr_t client_entities_decrypt_rva = 0;
    uintptr_t entity_list_decrypt_rva = 0;
    uint32_t ce_xor1 = 0, ce_add1 = 0, ce_rol = 0, ce_xor2 = 0, ce_add2 = 0;
    uint32_t el_xor1 = 0, el_add1 = 0, el_rol = 0, el_xor2 = 0, el_add2 = 0;
    bool ce_valid = false;
    bool el_valid = false;
};

extern ChainOffsets chain;
extern DecryptOps ops;

void SetGameAssembly(uintptr_t ga);
bool LoadDecryptFromJson(const std::string& json_text);
void ApplyBuiltinDefaults();

uintptr_t Il2cppGetHandle(int32_t handle);
uintptr_t DecryptClientEntities(uintptr_t encrypted_obj);
uintptr_t DecryptEntityList(uintptr_t encrypted_obj);

// Full BN chain → entity buffer + count. Returns 0 on failure.
uintptr_t ResolveNetworkableBuffer(uint32_t& out_count);

// Prefer BasePlayer.visiblePlayerList when available (may skip BN decrypt).
uintptr_t ResolveVisiblePlayersBuffer(uintptr_t local_player, uint32_t& out_count);

std::string ProbeEntityChainStatus();

} // namespace Decrypt
} // namespace Rust
