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



void StartForLibrary();



Status Get(GameId game);

void Shutdown();

}
