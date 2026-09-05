#pragma once
#include <atomic>
#include <string>
#include "../globals.h"

namespace OmniGhost::OffsetAuto {

enum class Result {
    Ok = 0,
    Current,
    UsingCached,
    Busy,
    RefreshFailed,
    StillInvalid,
    NotSupported,
};

enum class CompatibilityState {
    Unknown = 0,
    Checking,
    Ready,
    Outdated,
    Unsupported,
};

struct CompatibilitySnapshot {
    CompatibilityState state{CompatibilityState::Unknown};
    std::string detail;
};

// Human-readable status for overlay (UTF-8).
extern std::string g_status;
extern std::atomic_bool g_busy;

// Refreshes the supported data source and reloads local offsets. This function
// never discovers, launches or depends on an external executable.
Result EnsureOffsets(ActiveGame game, bool force_refresh = false);

// True if critical offsets look usable after load (no full self-test).
bool HasCriticalOffsets(ActiveGame game);

// Full in-game probe (requires process already attached when possible).
bool ValidateLive(ActiveGame game);

// Lobby-safe structural probe after attach. False => offsets outdated, do not open menu.
bool SoftProbeLive(ActiveGame game);
const char* SoftProbeFailReason(ActiveGame game);

CompatibilitySnapshot Compatibility(ActiveGame game);
void MarkChecking(ActiveGame game);
void MarkLiveValid(ActiveGame game, const std::string& detail = {});
void MarkOutdated(ActiveGame game, const std::string& detail);
void MarkUnsupported(ActiveGame game, const std::string& detail);
bool BlocksLaunch(ActiveGame game);
bool SupportsAutomaticRefresh(ActiveGame game);

// Best-effort refresh for every game that supports the cheatoffsets API.
int RefreshAllSupported(bool force = false);

} // namespace OmniGhost::OffsetAuto
