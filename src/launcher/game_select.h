#pragma once

#include "launcher_data.h"

namespace Launcher {

enum class CardState : int {
    Ready = 0,
    Hover,
    Launching,
    Running,
    NotInstalled,
    NeedsOffsets,
    DeviceMissing,
    UpdateRequired,
    Beta,
    ComingSoon,
    LaunchFailed,
    GameNotFound,
    LicenseRequired,
    Error
};



bool Draw();
GameId Selected();
void SetSelected(GameId id);
const char* SelectedName();
enum class EntryReason { Normal, Authenticated, SessionEnded };
void Reset(EntryReason reason = EntryReason::Normal);

// LN-004: drain/cancel the Library background jobs (remote process scan and
// offset refresh) that share the DMA data plane with the launch worker. Call
// right before committing a game launch transition.
void DrainBackgroundWorkBeforeLaunch();

// LN-012: wait out a still-running one-shot startup offset check at a
// well-defined shutdown point so the std::async future never blocks inside
// static destruction. Registered by main.cpp with the ShutdownCoordinator.
void DrainStartupOffsetChecks();



bool ConsumeLogoutRequest();

}
