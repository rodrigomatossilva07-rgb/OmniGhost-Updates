#pragma once
// Warzone / CoD Steam decrypt + layout (cheatoffsets / Blonde94 build 0x6A6D4AEF).
// Implements live PEB-mixed client_info + client_base + optional bone_base.
#include <cstdint>
#include <string>

namespace Warzone {

struct DecryptState {
    uintptr_t peb = 0;
    uintptr_t client_info = 0;
    uintptr_t client_base = 0;
    uintptr_t bone_base = 0;
    uintptr_t name_array_base = 0;
    int local_index = -1;
    int player_count_hint = 0;
    int client_info_mode = -1; // which PEB mode produced a heap ptr (0 = ~peb correct)
    bool client_info_ok = false;
    bool client_base_ok = false;
    bool bone_ok = false;
    bool name_array_ok = false;
    char detail[192] = "decrypt idle";
};

// Resolve PEB address for current attached process (MemProcFS vaPEB).
uintptr_t ResolvePebAddress();

// Full decrypt pass. Returns true if at least client_info OR name_array is usable.
bool RunDecrypt(uintptr_t module_base, DecryptState& out);

// Read a single player slot from decrypted client_base (verified path only).
// index: 0..154 typically. Fills pos/head/chest/team/health/name/valid.
bool ReadPlayerSlot(const DecryptState& st, uintptr_t module_base, int index,
                    float out_pos[3], float out_head[3], float out_chest[3],
                    int& out_team, float& out_health, char* out_name, size_t name_cap,
                    bool& out_valid, bool& out_alive);

} // namespace Warzone
