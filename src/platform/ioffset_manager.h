#pragma once

#include "platform/expected.h"
#include "platform/offset_auto.h"
#include "globals.h"
#include <memory>
#include <string>
#include <string_view>

namespace OmniGhost::Platform {

// =============================================================================
// IOffsetManager - Interface for offset management
// =============================================================================

class IOffsetManager {
public:
    virtual ~IOffsetManager() = default;

    // Initialization
    virtual Expected<void> Initialize() = 0;
    virtual Expected<void> Shutdown() = 0;

    // Offset loading
    virtual Expected<void> LoadOffsets(ActiveGame game) = 0;
    virtual Expected<void> ReloadOffsets(ActiveGame game) = 0;

    // Validation
    virtual Expected<bool> HasCriticalOffsets(ActiveGame game) const = 0;
    virtual Expected<bool> ValidateLive(ActiveGame game) = 0;
    virtual Expected<bool> SoftProbeLive(ActiveGame game) const = 0;
    virtual Expected<std::string> SoftProbeFailReason(ActiveGame game) const = 0;
    virtual Expected<bool> SupportsAutomaticRefresh(ActiveGame game) const = 0;

    // Refresh
    virtual Expected<OffsetAuto::Result> EnsureOffsets(ActiveGame game, bool forceRefresh) = 0;

    // Status
    virtual OffsetAuto::CompatibilitySnapshot Compatibility(ActiveGame game) const = 0;
    virtual void MarkChecking(ActiveGame game) = 0;
    virtual void MarkLiveValid(ActiveGame game, std::string_view detail) = 0;
    virtual void MarkOutdated(ActiveGame game, std::string_view detail) = 0;
    virtual void MarkUnsupported(ActiveGame game, std::string_view detail) = 0;
    virtual bool BlocksLaunch(ActiveGame game) const = 0;

    // Refresh all
    virtual Expected<int> RefreshAllSupported(bool force = false) = 0;

    // Status access
    [[nodiscard]] virtual std::string GetStatus() const = 0;
    [[nodiscard]] virtual bool IsBusy() const noexcept = 0;
};

// =============================================================================
// Factory for DI registration
// =============================================================================

class IOffsetManagerFactory {
public:
    virtual ~IOffsetManagerFactory() = default;
    virtual Expected<std::unique_ptr<IOffsetManager>> Create() = 0;
};

} // namespace OmniGhost::Platform
