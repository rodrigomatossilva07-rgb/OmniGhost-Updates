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
    // Inspects the result produced by Core World. The DMA fallback itself lives
    // in cs2_core_acquisition.inl so all consumers share one validated Pawn.
    [[nodiscard]] static LocalPlayerState Inspect(const Runtime& snapshot) noexcept;
};

} // namespace CS2::RCS
