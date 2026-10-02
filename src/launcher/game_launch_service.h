#pragma once

#include "launcher_data.h"

#include <string>

namespace OmniGhost::GameLaunch {

enum class ProcessProbeState {
    Found,
    NotFound,
    DeviceUnavailable,
    InventoryUnavailable,
};

struct ProcessProbeResult {
    ProcessProbeState state = ProcessProbeState::InventoryUnavailable;
    std::string processName;
};

std::string FindFiveMProcessViaDma();
ProcessProbeResult ProbeProcess(::Launcher::GameId selected);
bool IsProcessPresent(::Launcher::GameId selected);
bool Initialize(::Launcher::GameId selected);

} // namespace OmniGhost::GameLaunch
