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

// Draws the launcher. When this returns true, main.cpp immediately calls the
// existing launch path for the selected game.
bool Draw();
GameId Selected();
void SetSelected(GameId id);
const char* SelectedName();
enum class EntryReason { Normal, Authenticated, SessionEnded };
void Reset(EntryReason reason = EntryReason::Normal);

// Account menu integration. Logout is consumed by main.cpp and returns to the
// existing local authentication screen without terminating the process.
bool ConsumeLogoutRequest();

} // namespace Launcher
