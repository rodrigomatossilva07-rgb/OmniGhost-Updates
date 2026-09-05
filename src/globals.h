#pragma once

#include <string>

enum class ActiveGame : int {
    FiveM = 0,
    CS2 = 1,
    Rust = 2,
    // Warzone: DMA attach + offsets + ESP/aim (entity decrypt may still be required).
    Warzone = 3,
    Valorant = 4,
    Fortnite = 5,
    Apex = 6
};

extern std::string g_validExecutable;
extern ActiveGame g_activeGame;
