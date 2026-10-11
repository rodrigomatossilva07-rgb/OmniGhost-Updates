#include "../cs2_game.h"

namespace CS2 {

BoneMapping g_boneMapping;

bool BoneMapping::ResolveFromModel(uintptr_t client_base, uintptr_t model_base) {
    // Current offset snapshots do not expose the studio-header/name table
    // needed to resolve joint names safely. Keep the validated Source 2
    // mapping loaded from data/cs2_offsets.json instead of reading guessed pointers. The interface remains the
    // single future extension point when those schema fields become available.
    if (!client_base || !model_base)
        return false;
    return resolved;
}

} // namespace CS2
