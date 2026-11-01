#include "local_player_resolver.h"

namespace CS2::RCS {
namespace {
bool UserPointer(uintptr_t value) noexcept {
    return value >= 0x10000ull && value < 0x0000800000000000ull;
}
}

LocalPlayerState LocalPlayerResolver::Inspect(const Runtime& snapshot) noexcept {
    LocalPlayerState state{};
    state.controller = snapshot.local_controller;
    state.pawn = snapshot.local_pawn;
    state.controllerValid = UserPointer(state.controller);
    state.pawnValid = UserPointer(state.pawn);
    state.alive = state.pawnValid && snapshot.local_health > 0 && snapshot.local_health <= 200;
    return state;
}

}
