#pragma once
#include "platform/startup_orchestrator.h"
#include "window/window.hpp"
#include "launcher/game_select.h"
#include "updater/update_service.h"
#include "launcher/changelog_service.h"
#include "licensing/license_service.h"
#include "platform/hardware_manager.h"
#include "platform/shutdown_coordinator.h"
#include "platform/session_log.h"
#include "security/security_manager.h"
#include "config/app_settings.h"
#include "config/config_manager.h"
#include "window/onboarding.h"
#include "makcu/makcu_wrapper.h"
#include "globals.h"
#include <memory>

namespace OmniGhost {

class ApplicationCore final {
public:
    ApplicationCore() = default;
    ~ApplicationCore();

    // Delete copy/move
    ApplicationCore(const ApplicationCore&) = delete;
    ApplicationCore& operator=(const ApplicationCore&) = delete;

    int Run(int argc, wchar_t** argv);

private:
    // Components
    std::unique_ptr<Overlay> overlay_;
    std::unique_ptr<Platform::StartupOrchestrator> startup_;
    std::unique_ptr<Platform::Hardware::Manager> hardware_;
    std::unique_ptr<Updater::UpdateService> update_service_;
    std::unique_ptr<Launcher::ChangelogService> changelog_;
    std::unique_ptr<Licensing::LicenseService> license_;
    std::unique_ptr<Platform::ShutdownCoordinator> shutdown_coordinator_;

    // State
    bool running_ = false;
    Launcher::GameId selected_game_ = Launcher::GameId::None;

    // Initialization phases
    bool InitializeCore(int argc, wchar_t** argv);
    bool InitializeOverlay();
    bool AuthenticateUser();
    bool InitializeServices();
    bool RunLauncherLoop();
    bool LaunchGame(Launcher::GameId game);
    bool RunGameSession(Launcher::GameId game);
    void ShutdownAll(const std::string& reason);

    // Callbacks
    static void OnReturnToLauncher();
};

} // namespace OmniGhost