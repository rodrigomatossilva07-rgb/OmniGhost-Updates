#pragma once

#include "igame_module.h"
#include "platform/service_container.h"
#include "platform/expected.h"
#include "launcher/launcher_data.h"

#include <memory>
#include <string>
#include <string_view>

namespace OmniGhost::Games {

// =============================================================================
// CS2GameModule - CS2-specific game module implementation
// =============================================================================

class CS2GameModule : public GameModuleBase {
public:
    static constexpr std::string_view StaticGameId() noexcept { return "cs2"; }
    static constexpr Launcher::GameId StaticLauncherGameId() noexcept { return Launcher::GameId::CS2; }

    explicit CS2GameModule(OmniGhost::Platform::ServiceContainer& container);
    ~CS2GameModule() override = default;

    // IGameModule interface
    [[nodiscard]] std::string_view GetGameId() const noexcept override { return StaticGameId(); }
    [[nodiscard]] std::string_view GetGameName() const noexcept override { return "Counter-Strike 2"; }
    [[nodiscard]] Launcher::GameId GetLauncherGameId() const noexcept override { return StaticLauncherGameId(); }

    Expected<void> Initialize(OmniGhost::Platform::ServiceContainer& container) override;
    Expected<void> Shutdown() override;
    Expected<void> Update(float deltaTime) override;

    [[nodiscard]] bool IsGameRunning() const noexcept override;
    [[nodiscard]] bool IsAttached() const noexcept override;
    [[nodiscard]] bool IsInGame() const noexcept override;

    Expected<void> LoadOffsets() override;
    Expected<void> ReloadOffsets() override;
    [[nodiscard]] bool HasValidOffsets() const noexcept override;

    Expected<void> EnableFeatures() override;
    Expected<void> DisableFeatures() override;

    void DrawSettings() override;
    void DrawDebugInfo() override;

    Expected<void> SaveConfig() override;
    Expected<void> LoadConfig() override;

    [[nodiscard]] std::string GetDiagnosticsInfo() const override;

private:
    // CS2-specific state
    bool offsetsLoaded_ = false;
    bool featuresEnabled_ = false;
    bool attached_ = false;
    bool inGame_ = false;

    // References to CS2 runtime (will be injected via DI)
    struct CS2RuntimeRef {
        bool* ready = nullptr;
        bool* offsetsLoaded = nullptr;
    } runtimeRef_;

    // Helpers
    Expected<void> AttachToGame();
    Expected<void> DetachFromGame();
    Expected<void> ProbeGameState();
};

} // namespace OmniGhost::Games
