#pragma once
#include "rust_game.h"

namespace Rust_Aim {
void Run(const Rust::Runtime& rt, const Rust::Config& cfg);
int ActiveTargetIndex();
const char* DebugStatus();
}
