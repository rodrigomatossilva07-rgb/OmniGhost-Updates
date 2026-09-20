#pragma once

#include "launcher_data.h"

#include <cstdint>
#include <string>

namespace Launcher::SteamGameUpdateCheck {

enum class State : std::uint8_t { Idle, Checking, Current, OffsetsOutdated, Unavailable };

struct Status {
    State state{State::Idle};
    std::uint64_t latestNewsUnix{};
    std::uint64_t localOffsetsUnix{};
    std::string detail;
};

// Safe to call every frame while the Library is visible. It starts a bounded,
// background Steam query at most once per refresh interval.
void StartForLibrary();

// Returns a value copy. The local offset-file timestamp is re-evaluated here,
// so a manual offset update clears an existing warning without restarting.
Status Get(GameId game);

} // namespace Launcher::SteamGameUpdateCheck
