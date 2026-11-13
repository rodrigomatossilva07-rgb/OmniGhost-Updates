#pragma once

#include "../cs2_game.h"

namespace CS2::RCS {

struct LocalPlayerState {
    uintptr_t controller = 0;
    uintptr_t pawn = 0;
    bool controllerValid = false;
    bool pawnValid = false;
    bool alive = false;
};

class LocalPlayerResolver {
public:


    [[nodiscard]] static LocalPlayerState Inspect(const Runtime& snapshot) noexcept;
};

}
