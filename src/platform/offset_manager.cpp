#include "ioffset_manager.h"
#include "offset_manager_factory.h"
#include "offset_auto.h"
#include "platform/expected.h"
#include "platform/logging.h"

#include <chrono>
#include <mutex>
#include <thread>

namespace OmniGhost::Platform {

// =============================================================================
// OffsetManager - Implementation of IOffsetManager wrapping OffsetAuto
// =============================================================================

class OffsetManager final : public IOffsetManager {
public:
    OffsetManager() = default;
    ~OffsetManager() override = default;

    Expected<void> Initialize() override {
        OGS_LOG_INFO("[OffsetManager] Initializing...");
        
        // Initialize OffsetAuto static state
        OffsetAuto::g_busy = false;
        OffsetAuto::g_status = "Pronto";
        
        OGS_LOG_INFO("[OffsetManager] Initialized");
        return Success();
    }

    Expected<void> Shutdown() override {
        OGS_LOG_INFO("[OffsetManager] Shutting down...");
        return Success();
    }

    Expected<void> LoadOffsets(ActiveGame game) override {
        OGS_LOG_INFO("[OffsetManager] Loading offsets for game: {}", static_cast<int>(game));
        
        const auto result = OffsetAuto::EnsureOffsets(game, false);
        if (result != OffsetAuto::Result::Ok && result != OffsetAuto::Result::Current &&
            result != OffsetAuto::Result::UsingCached) {
            return Failure<void>(ErrorCode::OffsetsMissing, "Failed to load offsets for game");
        }
        
        return Success();
    }

    Expected<void> ReloadOffsets(ActiveGame game) override {
        OGS_LOG_INFO("[OffsetManager] Reloading offsets for game: {}", static_cast<int>(game));
        
        // Reset and reload
        OffsetAuto::MarkChecking(game);
        return LoadOffsets(game);
    }

    Expected<bool> HasCriticalOffsets(ActiveGame game) const override {
        return Success(OffsetAuto::HasCriticalOffsets(game));
    }

    Expected<bool> ValidateLive(ActiveGame game) override {
        return Success(OffsetAuto::ValidateLive(game));
    }

    Expected<bool> SoftProbeLive(ActiveGame game) const override {
        return Success(OffsetAuto::SoftProbeLive(game));
    }

    Expected<std::string> SoftProbeFailReason(ActiveGame game) const override {
        return Success(OffsetAuto::SoftProbeFailReason(game));
    }

    Expected<bool> SupportsAutomaticRefresh(ActiveGame game) const override {
        return Success(OffsetAuto::SupportsAutomaticRefresh(game));
    }

    Expected<OffsetAuto::Result> EnsureOffsets(ActiveGame game, bool forceRefresh) override {
        return Success(OffsetAuto::EnsureOffsets(game, forceRefresh));
    }

    OffsetAuto::CompatibilitySnapshot Compatibility(ActiveGame game) const override {
        return OffsetAuto::Compatibility(game);
    }

    void MarkChecking(ActiveGame game) override {
        OffsetAuto::MarkChecking(game);
    }

    void MarkLiveValid(ActiveGame game, std::string_view detail) override {
        OffsetAuto::MarkLiveValid(game, std::string(detail));
    }

    void MarkOutdated(ActiveGame game, std::string_view detail) override {
        OffsetAuto::MarkOutdated(game, std::string(detail));
    }

    void MarkUnsupported(ActiveGame game, std::string_view detail) override {
        OffsetAuto::MarkUnsupported(game, std::string(detail));
    }

    bool BlocksLaunch(ActiveGame game) const override {
        return OffsetAuto::BlocksLaunch(game);
    }

    Expected<int> RefreshAllSupported(bool force = false) override {
        return Success(OffsetAuto::RefreshAllSupported(force));
    }

    std::string GetStatus() const override {
        return std::string(OffsetAuto::g_status);
    }

    bool IsBusy() const noexcept override {
        return OffsetAuto::g_busy.load(std::memory_order_acquire);
    }
};

// =============================================================================
// Factory implementation
// =============================================================================

Expected<std::unique_ptr<IOffsetManager>> OffsetManagerFactory::Create() {
    return Success(std::make_unique<OffsetManager>());
}

// Registration function
void RegisterOffsetManagerFactory() {
    // This would be called during DI container setup
    // For now, we'll use a simple function to get the factory
}

} // namespace OmniGhost::Platform
