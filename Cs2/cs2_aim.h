#pragma once
#include "cs2_game.h"

namespace CS2_Aim {
void Run(const CS2::Runtime& rt, const CS2::Config& cfg);
int ActiveTargetIndex();
const char* DebugStatus(); // short live status for UI / hotkey overlay
}
