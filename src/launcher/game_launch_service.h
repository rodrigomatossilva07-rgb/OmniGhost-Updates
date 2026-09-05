#pragma once

#include "launcher_data.h"

#include <string>

namespace OmniGhost::GameLaunch {

std::string FindFiveMProcessViaDma();
bool IsProcessPresent(::Launcher::GameId selected);
bool Initialize(::Launcher::GameId selected);

} // namespace OmniGhost::GameLaunch
