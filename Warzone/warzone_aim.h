#pragma once
#include "warzone_game.h"

namespace Warzone_Aim {
void Run(const Warzone::Runtime& rt, const Warzone::Config& cfg);
int ActiveTargetIndex();
const char* DebugStatus();
}
