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



bool ConsumeLogoutRequest();

}
