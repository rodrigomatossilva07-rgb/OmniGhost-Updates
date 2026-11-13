#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <string_view>
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







struct StatusProxy {
    [[nodiscard]] operator std::string() const;
    StatusProxy& operator=(std::string_view value);
};
inline StatusProxy g_status{};
extern std::atomic_bool g_busy;



Result EnsureOffsets(ActiveGame game, bool force_refresh = false);


bool HasCriticalOffsets(ActiveGame game);


bool ValidateLive(ActiveGame game);


bool SoftProbeLive(ActiveGame game);
const char* SoftProbeFailReason(ActiveGame game);

CompatibilitySnapshot Compatibility(ActiveGame game);
void MarkChecking(ActiveGame game);
void MarkLiveValid(ActiveGame game, const std::string& detail = {});
void MarkOutdated(ActiveGame game, const std::string& detail);
void MarkUnsupported(ActiveGame game, const std::string& detail);
bool BlocksLaunch(ActiveGame game);
bool SupportsAutomaticRefresh(ActiveGame game);


int RefreshAllSupported(bool force = false);

}
