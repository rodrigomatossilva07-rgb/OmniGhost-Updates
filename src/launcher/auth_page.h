#pragma once

namespace LauncherAuth {

void Reset();
// Draws the local account gate. Product entitlements are enforced separately
// by the launcher, so an account can sign in without owning a game license.
bool Draw();
bool ConsumeCloseRequest();

} // namespace LauncherAuth
