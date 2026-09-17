#pragma once
#include "cs2_game.h"

namespace CS2_ESP {
    void Draw(const CS2::Runtime& rt, const CS2::Config& cfg);
    /** Release in-memory textures/futures and delete disk cache under LocalAppData/OmniGhost/cache/cs2/avatars. */
    void ClearAvatarCache();
}
