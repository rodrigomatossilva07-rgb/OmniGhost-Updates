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
        
        // Initialize compatibility states for all games
        for (int i = 0; i <= static_cast<int>(ActiveGame::Apex); ++i) {
            OffsetAuto::SetState(static_cast<ActiveGame>(i), 
                                OffsetAuto::CompatibilityState::Unknown, 
                                "Não verificado");
        }
        
        OGS_LOG_INFO("[OffsetManager] Initialized");
        return Success();
    }

    Expected<void> Shutdown() override {
        OGS_LOG_INFO("[OffsetManager] Shutting down...");
        return Success();
    }

    Expected<void> LoadOffsets(ActiveGame game) override {
        OGS_LOG_INFO("[OffsetManager] Loading offsets for game: {}", static_cast<int>(game));
        
        // Use existing game-specific load functions
        bool success = false;
        switch (game) {
            case ActiveGame::CS2:
                success = CS2::LoadOffsetsFromJson(nullptr);
                break;
            case ActiveGame::FiveM:
                success = FiveM::LoadOffsetsFromJson(nullptr) > 0;
                break;
            case ActiveGame::Rust:
                // Rust uses LoadRuntimeOffsets
                success = Rust::LoadRuntimeOffsets();
                break;
            case ActiveGame::Fortnite:
                success = Fortnite::LoadOffsetsFromJson(nullptr);
                break;
            default:
                return Failure<void>(ErrorCode::UnsupportedOperation, "Game not supported for offset loading");
        }
        
        if (!success) {
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

    Expected<bool> SupportsAutomaticRefresh(ActiveGame game) const noexcept override {
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
        OffsetAuto::MarkLiveValid(game, detail);
    }

    void MarkOutdated(ActiveGame game, std::string_view detail) override {
        OffsetAuto::MarkOutdated(game, detail);
    }

    void MarkUnsupported(ActiveGame game, std::string_view detail) override {
        OffsetAuto::MarkUnsupported(game, detail);
    }

    bool BlocksLaunch(ActiveGame game) const override {
        return OffsetAuto::BlocksLaunch(game);
    }

    Expected<int> RefreshAllSupported(bool force = false) override {
        return Success(OffsetAuto::RefreshAllSupported(force));
    }

    OffsetAuto::CompatibilitySnapshot Compatibility(ActiveGame game) const override {
        return OffsetAuto::Compatibility(game);
    }

    void MarkChecking(ActiveGame game) override {
        OffsetAuto::MarkChecking(game);
    }

    void MarkLiveValid(ActiveGame game, std::string_view detail) override {
        OffsetAuto::MarkLiveValid(game, detail);
    }

    void MarkOutdated(ActiveGame game, std::string_view detail) override {
        OffsetAuto::MarkOutdated(game, detail);
    }

    void MarkUnsupported(ActiveGame game, std::string_view detail) override {
        OffsetAuto::MarkUnsupported(game, detail);
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

class OffsetManagerFactory final : public IOffsetManagerFactory {
public:
    Expected<std::unique_ptr<IOffsetManager>> Create() override {
        return Success(std::make_unique<OffsetManager>());
    }
};

// Registration function
void RegisterOffsetManagerFactory() {
    // This would be called during DI container setup
    // For now, we'll use a simple function to get the factory
}

} // namespace OmniGhost::Platform