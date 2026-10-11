#include "platform/application_core.h"
#include "platform/app_paths.h"
#include "platform/runtime_bootstrap.h"
#include "platform/crash_handler.h"
#include "platform/thread_utils.h"
#include "platform/unique_handle.h"
#include "platform/window_instance_guard.h"
#include "platform/support_error.h"
#include "platform/dma_integration_test.h"
#include "window/performance_mode.h"
#include "window/widgets.h"
#include "window/hardware_monitor.h"
#include "Launcher/Auth.h"
#include "Updater/Configuration.h"
#include "Updater/ReleaseTrust.h"
#include <chrono>
#include <thread>
#include <iostream>

namespace OmniGhost {

ApplicationCore::~ApplicationCore() {
    ShutdownAll("destructor");
}

int ApplicationCore::Run(int argc, wchar_t** argv) {
    // Integration test mode
    if (argc > 1 && argv && _wcsicmp(argv[1], L"--integration-test") == 0) {
        // Delegate to existing integration test logic
        return RunDmaIntegrationTest(argc, argv);
    }

    if (!InitializeCore(argc, argv)) return 1;
    if (!InitializeOverlay()) return 1;
    if (!AuthenticateUser()) return 0;
    if (!InitializeServices()) return 1;

    if (!RunLauncherLoop()) return 0;

    return 0;
}

bool ApplicationCore::InitializeCore(int argc, wchar_t** argv) {
    // Startup state machine
    startup_ = std::make_unique<Platform::StartupOrchestrator>();
    if (auto result = startup_->Run(); result.IsErr()) {
        MessageBoxW(nullptr, result.UnwrapErr().c_str(), L"OmniGhost", MB_OK | MB_ICONERROR);
        return false;
    }

    // Security verification
    if (!Security::SecurityManager::Instance().VerifyHostIdentity()) {
        MessageBoxW(nullptr, L"SEC-001/002/004: Falha na verificaçăo de origem.", L"OmniGhost", MB_OK | MB_ICONERROR);
        return false;
    }

    const auto bootstrap = Platform::RuntimeBootstrap::Prepare();
    if (bootstrap == Platform::RuntimeBootstrap::Result::Failed) return false;

    if (!Security::SecurityManager::Instance().VerifyStartup()) {
        MessageBoxW(nullptr, L"A instalaçăo năo passou a verificaçăo de segurança.", L"OmniGhost", MB_OK | MB_ICONERROR);
        return false;
    }

    // Single instance
    constexpr wchar_t kSingleInstanceName[] = L"Local\\OmniGhostApp-6A670FC7";
    HANDLE h = CreateMutexW(nullptr, FALSE, kSingleInstanceName);
    DWORD err = GetLastError();
    Platform::UniqueHandle singleInstance(h);
    if (!singleInstance || err == ERROR_ALREADY_EXISTS) {
        Platform::ActivateExistingOmniGhostWindow();
        return false;
    }

    SessionLog::Initialize();
    SessionLog::SetStartReason(argc > 1 && argv && argv[1] ? Platform::WideToUtf8(argv[1]) : "normal");

    // Updater mode check
    if (auto updater_result = Updater::RunUpdaterModeIfRequested(argc, argv))
        return *updater_result;

    return true;
}

bool ApplicationCore::InitializeOverlay() {
    overlay_ = std::make_unique<Overlay>();
    overlay_->SetupOverlay(
#if defined(OMNIGHOST_PRIVATE_STATIC_VMM)
        "OmniGhost — PRIVATE DEVELOPMENT BUILD — NOT FOR DISTRIBUTION"
#else
        "OmniGhostOverlay"
#endif
    );
    if (!overlay_->shouldRun) return false;

    Platform::DisableProcessWindowsGhosting();
    Platform::CrashHandler::Install();
    Platform::CrashHandler::MarkStartupComplete();

    app_settings::Initialize();
    config_manager::Initialize();

    return true;
}

bool ApplicationCore::AuthenticateUser() {
    LauncherAuth::Reset();
    bool authenticated = false;
    while (overlay_->shouldRun && !authenticated) {
        overlay_->StartRender();
        if (!overlay_->shouldRun) break;
        authenticated = LauncherAuth::Draw();
        if (LauncherAuth::ConsumeCloseRequest()) overlay_->RequestClose();
        overlay_->EndRender();
        overlay_->WaitForEvents(std::chrono::milliseconds(1));
    }
    return authenticated && overlay_->shouldRun;
}

bool ApplicationCore::InitializeServices() {
    // Friends & Input
    friends::Initialize();
    aim_type::Initialize();
    aimbot::Initialize();

    // Hardware
    hardware_ = std::make_unique<Platform::Hardware::Manager>();
    hardware_->BeginStartupProbe();

    // Overlay transition
    overlay_->SetCompactMode(false);
    ShowWindow(overlay_->overlay, SW_SHOWNORMAL);
    SetWindowPos(overlay_->overlay, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
    SetForegroundWindow(overlay_->overlay);

    // Update service
    update_service_ = std::make_unique<Updater::UpdateService>();
    Updater::Configuration upd_cfg{};
#if defined(OMNIGHOST_PUBLISH_BUILD)
    upd_cfg.channel = Updater::Channel::Stable;
#else
    upd_cfg.channel = app_settings::config.update_channel == app_settings::UpdateChannel::Beta ? Updater::Channel::Beta : Updater::Channel::Stable;
#endif
    upd_cfg.automaticDownload = app_settings::config.update_auto_download;
    upd_cfg.automaticInstallOnExit = app_settings::config.update_install_on_exit;
    update_service_->Initialize(upd_cfg);

    // Changelog
    changelog_ = std::make_unique<Launcher::ChangelogService>();
    changelog_->Initialize();
    changelog_->RefreshAsync(false);

    // Licensing
    license_ = std::make_unique<Licensing::LicenseService>();
    Licensing::StartSessionRevalidation();

    // Shutdown coordinator
    shutdown_coordinator_ = std::make_unique<Platform::ShutdownCoordinator>();
    shutdown_coordinator_->Register(Platform::ShutdownComponent::Ui, 100, [this] { overlay_->Shutdown(); });
    shutdown_coordinator_->Register(Platform::ShutdownComponent::Hardware, 600, [this] { if (hardware_) hardware_->Shutdown(); });
    shutdown_coordinator_->Register(Platform::ShutdownComponent::Updater, 800, [this] { if (update_service_) update_service_->Shutdown(); });
    shutdown_coordinator_->Register(Platform::ShutdownComponent::Changelog, 700, [this] { if (changelog_) changelog_->Shutdown(); });
    shutdown_coordinator_->Register(Platform::ShutdownComponent::Input, 500, [] { aim_type::Shutdown(); });

    return true;
}

bool ApplicationCore::RunLauncherLoop() {
    while (overlay_->shouldRun) {
        hardware_->Poll();
        const auto hw = hardware_->Snapshot();
        HardwareMonitor::SetDeviceEnabled(HardwareMonitor::DeviceType::DMA, true);
        HardwareMonitor::UpdateDeviceStatus(HardwareMonitor::DeviceType::DMA, hw.dmaFinished && hw.dmaAvailable, hw.dmaFinished ? "PnP detected" : "Checking", "");
        HardwareMonitor::SetDeviceEnabled(HardwareMonitor::DeviceType::Makcu, true);
        const auto mk = makcu_wrapper::GetDiagnostics();
        HardwareMonitor::UpdateDeviceStatus(HardwareMonitor::DeviceType::Makcu, hw.makcuFinished && hw.makcuAvailable, mk.port[0] ? mk.port : (hw.makcuFinished ? "Auto discovery" : "Searching automatically"), mk.deviceInfo[0] ? mk.deviceInfo : (hw.makcuAvailable ? "Connected automatically" : "MAKCU auto-connect"), hw.makcuFinished && !hw.makcuAvailable ? "Not found; retrying automatically" : "");

        overlay_->StartRender();
        if (!overlay_->shouldRun) break;
        if (Launcher::Draw()) selected_game_ = Launcher::Selected();
        bool logout = Launcher::ConsumeLogoutRequest();
        overlay_->EndRender();

        if (update_service_->ShouldExitForUpdate()) overlay_->shouldRun = false;
        if (logout) {
            // Re-auth flow
            overlay_->SetCompactMode(true);
            LauncherAuth::Reset();
            bool reauth = false;
            while (overlay_->shouldRun && !reauth) {
                overlay_->StartRender();
                if (!overlay_->shouldRun) break;
                reauth = LauncherAuth::Draw();
                if (LauncherAuth::ConsumeCloseRequest()) overlay_->RequestClose();
                overlay_->EndRender();
            }
            if (!overlay_->shouldRun) break;
            overlay_->SetCompactMode(false);
            continue;
        }
        if (selected_game_ != Launcher::GameId::None) break;
    }
    return overlay_->shouldRun && selected_game_ != Launcher::GameId::None;
}

bool ApplicationCore::LaunchGame(Launcher::GameId game) {
    const Launcher::GameDefinition* def = Launcher::FindGame(game);
    if (!def || !Security::SecurityManager::Instance().CanUseProtectedFeatures() || !Licensing::HasGameAccess(def->id)) return false;

    auto* adapter = Launcher::FindGameAdapter(game);
    if (!adapter) return false;

    // Offset bootstrap
    ActiveGame ag = (game == Launcher::GameId::CS2) ? ActiveGame::CS2 : (game == Launcher::GameId::Warzone) ? ActiveGame::Warzone : (game == Launcher::GameId::Fortnite) ? ActiveGame::Fortnite : ActiveGame::FiveM;
    if (ag != ActiveGame::Warzone) {
        if (!OffsetAuto::HasCriticalOffsets(ag)) {
            OffsetAuto::EnsureOffsets(ag, false);
        }
    }

    // Adapter launch (async with UI)
    Launcher::AdapterStartResult result = adapter->Start();
    if (!result.succeeded) {
        // Show error UI
        return false;
    }
    return true;
}

bool ApplicationCore::RunGameSession(Launcher::GameId game) {
    // Game-specific session loop
    // This would contain the per-game loop from main.cpp
    return true;
}

void ApplicationCore::ShutdownAll(const std::string& reason) {
    if (shutdown_coordinator_) shutdown_coordinator_->ShutdownAll(reason);
    Licensing::ShutdownSessionRevalidation();
    SessionLog::Shutdown();
}

} // namespace OmniGhost