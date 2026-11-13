#include "../cs2_game.h"

namespace CS2 {

BoneMapping g_boneMapping;

bool BoneMapping::ResolveFromModel(uintptr_t client_base, uintptr_t model_base) {




    if (!client_base || !model_base)
        return false;
    return resolved;
}

}
