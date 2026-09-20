#include <Windows.h>
#include <TlHelp32.h>
#include <Shellapi.h>

#include "globals.h"
#include "app_version.h"
#include "window/window.hpp"
#include "window/theme.h"
#include "config/app_settings.h"
#include "config/config_manager.h"
#include "launcher/game_select.h"
#include "launcher/changelog_service.h"
#include "launcher/auth_page.h"
#include "launcher/application_transitions.h"
#include "launcher/game_launch_service.h"
#include "launcher/game_adapter_registry.h"
#include "licensing/license_service.h"
#include "updater/update_service.h"
#include "updater/updater_mode.h"
#include "../DMALibrary/Memory/Memory.h"
#include "../Fivem/game/game.h"
#include "../Fivem/aimbot/aim_type.h"
#include "../Fivem/aimbot/aimbot.h"
#include "../Fivem/friends/friends.h"
#include "../Cs2/cs2_game.h"
#include "../Cs2/aimbot/cs2_aim.h"
#include "../Warzone/warzone_game.h"
#include "../Valorant/valorant_game.h"
#include "../Fortnite/fortnite_game.h"
#include "../Valorant/valorant_esp.h"
#include "../Valorant/valorant_aim.h"
#include "platform/offset_auto.h"
#include "../Fivem/game/offsets.h"
#include "platform/app_paths.h"
#include "platform/runtime_bootstrap.h"
#include "platform/hardware_manager.h"
#include "platform/startup_state.h"
#include "platform/crash_handler.h"
#include "platform/thread_utils.h"
#include "platform/unique_handle.h"
#include "platform/session_log.h"
#include "platform/shutdown_coordinator.h"
#include "platform/text_encoding.h"
#include "platform/window_instance_guard.h"
#include "window/performance_mode.h"
#include "window/widgets.h"
#include "window/hardware_monitor.h"
#include "../ImGui/imgui.h"
#include "makcu/makcu_wrapper.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <future>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

#if defined(OMNIGHOST_PUBLISH_BUILD) && !defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
#error Publish builds must retain a configured entitlement provider.
#endif

namespace {

// Release is a native GUI application. Diagnostic output is mirrored to SessionLog.

// DMA reads the *remote* process list (game PC), not this machine's Task Manager.
// Local Toolhelp32 never sees FiveM when the cheat runs on the second PC.

// A DMA PID lookup is intentionally not treated as an authoritative liveness
// signal.  During a VMM refresh it can be empty even while FiveM is running.
// When OmniGhost and the game share a PC, Toolhelp gives us an independent,
// cheap confirmation before a session is ever returned to the launcher.
bool IsLocalFiveMProcessRunning() noexcept {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            const wchar_t* name = entry.szExeFile;
            const bool buildProcess = wcsstr(name, L"FiveM_b") == name &&
                wcsstr(name, L"_GTAProcess.exe") != nullptr;
            if (_wcsicmp(name, L"GTAProcess.exe") == 0 ||
                _wcsicmp(name, L"FiveM_GTAProcess.exe") == 0 || buildProcess) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

int RunOmniGhost(int argc, wchar_t** argv) {
    OmniGhost::Startup::StateMachine startupState;
    (void)startupState.Transition(OmniGhost::Startup::State::Boot,
                            OmniGhost::Startup::State::CheckingInstance);
    bool updaterInvocation = false;
    for (int i = 1; argv && i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--apply-update") == 0 ||
            _wcsicmp(argv[i], L"--updater") == 0) {
            updaterInvocation = true;
            break;
        }
    }

    // Serialize the complete startup path before runtime preparation, auth,
    // hardware workers or any HWND can be created. This guard is deliberately
    // independent from the normal application/window guards below: even two
    // simultaneous double-clicks cannot both enter bootstrap and later race to
    // create a surface.
    OmniGhost::Platform::UniqueHandle startupOwner;
    if (!updaterInvocation) {
        SetLastError(ERROR_SUCCESS);
        HANDLE handle = CreateMutexW(nullptr, FALSE,
            L"Local\\OmniGhostStartupOwner-6A670FC7");
        const DWORD error = GetLastError();
        startupOwner.reset(handle);
        if (!startupOwner || error == ERROR_ALREADY_EXISTS) {
            OmniGhost::Platform::ActivateExistingOmniGhostWindow();
            return error == ERROR_ALREADY_EXISTS ? 3 : 2;
        }

        std::wstring legacyError;
        if (!OmniGhost::RuntimeBootstrap::RetireLegacyPrivateInstall(legacyError)) {
            MessageBoxW(nullptr, legacyError.c_str(), L"OmniGhost — limpeza da instalação antiga",
                        MB_OK | MB_ICONERROR);
            return 2;
        }
        OmniGhost::Paths::MigrateLegacyUserData();
        // Catch a non-legacy build that owns the product HWND but predates the
        // current mutex names. It is reused rather than creating another window.
        if (OmniGhost::Platform::ActivateExistingOmniGhostWindow())
            return 3;
    }

    const auto bootstrap = OmniGhost::RuntimeBootstrap::Prepare();
    if (bootstrap == OmniGhost::RuntimeBootstrap::Result::Failed)
        return 1;
    (void)startupState.Transition(OmniGhost::Startup::State::CheckingInstance,
                            OmniGhost::Startup::State::InitializingRuntime);

    OmniGhost::Platform::SetCurrentThreadName(L"OmniGhost.Main");
    // OmniGhost owns one native top-level window and keeps its message pump
    // responsive during device discovery. Do not let Windows synthesize a
    // second-looking ghost HWND if a third-party driver stalls momentarily.
    DisableProcessWindowsGhosting();
    OmniGhost::CrashHandler::Install();
    OmniGhost::SessionLog::SetStartReason(
        (argc > 1 && argv && argv[1]) ? OmniGhost::Platform::WideToUtf8(argv[1]) : "normal");
    OmniGhost::SessionLog::Initialize();
    std::clog << "[RUNTIME] mode=SINGLE_PROCESS_PORTABLE\n"
                 "[RUNTIME] self_install=DISABLED\n"
                 "[RUNTIME] private_executable=DISABLED\n"
#if defined(OMNIGHOST_PRIVATE_STATIC_VMM)
                 "[RUNTIME] build=PRIVATE_DEVELOPMENT_BUILD\n"
                 "[RUNTIME] distribution=NOT_FOR_DISTRIBUTION\n"
                 "[RUNTIME] static_vmm=YES source_integration\n"
                 "[RUNTIME] static_leechcore=YES source_integration\n"
#else
                 "[RUNTIME] static_vmm=NO import_library_only\n"
                 "[RUNTIME] static_leechcore=NO import_library_only\n"
#endif
                 "[RUNTIME] vendor_d3xx=EXTERNAL_FTD3XXWU_ON_FPGA_OPEN\n"
#if defined(OMNIGHOST_DISABLE_VMM_INFODB)
                 "[RUNTIME] info_db=DISABLED_BY_BUILD (-disable-infodb)\n"
#else
                 "[RUNTIME] info_db=EXTERNAL_REQUIRED_BY_MEMPROCFS\n"
#endif
#if defined(OMNIGHOST_DISABLE_VMM_SYMBOLS)
                 "[RUNTIME] pdbcrust=DISABLED_BY_BUILD (-disable-symbols)\n"
#else
                 "[RUNTIME] pdbcrust=OPTIONAL_EAT_FALLBACK\n"
#endif
                 "[RUNTIME] cloudflared=EXTERNAL_PROCESS_OPTIONAL\n";
    const auto previousRun = OmniGhost::CrashHandler::PreviousRun();
    std::clog << "[SESSION] previous_run="
              << OmniGhost::CrashHandler::PreviousRunStateName(previousRun)
              << "\n";
    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Core,
        "startup entered",
        {
            {"previous_run", OmniGhost::CrashHandler::PreviousRunStateName(previousRun), false},
            {"session_id", OmniGhost::SessionLog::CurrentSessionId(), false}
        });
    struct SessionShutdownGuard {
        ~SessionShutdownGuard() {
            OmniGhost::SessionLog::Write(
                OmniGhost::SessionLog::Severity::Info,
                OmniGhost::SessionLog::Subsystem::Core,
                "clean shutdown requested");
            OmniGhost::CrashHandler::MarkCleanShutdown();
            OmniGhost::SessionLog::Shutdown();
        }
    } session_shutdown_guard;
    if (const auto updater_result = OmniGhost::Update::RunUpdaterModeIfRequested(argc, argv))
        return *updater_result;

    // Older OmniGhost builds scoped their mutex to the installation directory.
    // If one of those builds is already visible, a new executable from another
    // folder would not see its mutex and both processes could initialize DMA and
    // input hardware. Detect the actual product window before acquiring the new
    // user-wide mutex so mixed-version launches also remain single-instance.
    if (OmniGhost::Platform::ActivateExistingOmniGhostWindow()) {
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Warning,
            OmniGhost::SessionLog::Subsystem::Platform,
            "existing OmniGhost window from another process reused");
        return 3;
    }

    // One interactive OmniGhost instance per Windows user, regardless of where
    // the executable was launched from. The updater mode is handled above this
    // guard, so the in-process updater can still run while the main application
    // is closing. Scoping this mutex to the installation directory allowed a
    // build copy and a packaged/rollback copy to initialize the DMA stack at the
    // same time, producing two startup windows and unstable device ownership.
    constexpr wchar_t kSingleInstanceName[] = L"Local\\OmniGhostApp-6A670FC7";
    HANDLE singleInstanceHandle = CreateMutexW(nullptr, FALSE, kSingleInstanceName);
    const DWORD singleInstanceError = GetLastError();
    OmniGhost::Platform::UniqueHandle singleInstance(singleInstanceHandle);
    if (!singleInstance) {
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Error,
            OmniGhost::SessionLog::Subsystem::Platform,
            "single-instance mutex creation failed",
            {
                {"win32", std::to_string(singleInstanceError), false},
                {"executable", OmniGhost::Platform::WideToUtf8(
                    OmniGhost::Paths::Executable().wstring()), false}
            });
        std::cerr << "[ERRO] Nao foi possivel criar o controlo de instancia unica. win32="
                  << singleInstanceError << "\n";
        return 2;
    }
    if (singleInstanceError == ERROR_ALREADY_EXISTS) {
        const std::wstring installDirectory = OmniGhost::Paths::InstallDirectory().wstring();
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Warning,
            OmniGhost::SessionLog::Subsystem::Platform,
            "second application instance rejected",
            {
                {"install_directory", OmniGhost::Platform::WideToUtf8(installDirectory), false}
            });
        std::cerr << "[ERRO] O OmniGhost ja esta em execucao. A janela existente sera reutilizada.\n";

        // Reuse the existing UI rather than creating another startup surface.
        // The window may still be in compact authentication/hardware mode.
        OmniGhost::Platform::ActivateExistingOmniGhostWindow();
        return 3;
    }
    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Platform,
        "single-instance guard acquired",
        {{"executable", OmniGhost::Platform::WideToUtf8(
            OmniGhost::Paths::Executable().wstring()), false}});

    // Confirm a freshly installed version before entering the visual license/account
    // startup gate. The updater waits for this acknowledgement and otherwise
    // assumes the new binary failed, restores the backup and relaunches the old
    // version. Authentication can legitimately wait for user input, so acknowledge
    // the healthy process before showing that UI.
    OmniGhost::Update::ConfirmUpdaterStartupFromCommandLine(argc, argv);
    OmniGhost::Update::CaptureUpdaterStartupNoticeFromCommandLine(argc, argv);

    // Settings/theme are safe to load before authentication. Accounts may enter
    // without a product license; game services remain gated by entitlements.
    app_settings::Initialize();
    config_manager::Initialize();

    Overlay application;
    // Authentication owns a compact HWND from its very first visible frame.
    // This avoids creating a full-monitor backdrop before the licence form.
    application.SetCompactMode(true);
#if defined(OMNIGHOST_PRIVATE_STATIC_VMM)
    application.SetupOverlay("OmniGhost — PRIVATE DEVELOPMENT BUILD — NOT FOR DISTRIBUTION");
#else
    application.SetupOverlay("OmniGhostOverlay");
#endif
    if (!application.shouldRun)
        return 1;
    OmniGhost::Platform::LogWindowAudit("after-create", application.overlay);

    OmniGhost::Platform::ShutdownCoordinator shutdownCoordinator;
    (void)shutdownCoordinator.Register(
        OmniGhost::Platform::ShutdownComponent::Ui, 100,
        [&application] { application.Shutdown(); });
    // The app is now visibly alive. Waiting for a user to enter a local license or
    // account must not be interpreted by the updater/crash recovery as startup failure.
    OmniGhost::CrashHandler::MarkStartupComplete();
    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Core,
        "startup UI ready");

    LauncherAuth::Reset();
    (void)startupState.Transition(OmniGhost::Startup::State::InitializingRuntime,
                            OmniGhost::Startup::State::Authenticating);
    bool authenticated = false;
while (application.shouldRun && !authenticated) {
        application.StartRender();
        if (!application.shouldRun)
            break;
        authenticated = LauncherAuth::Draw();
        if (LauncherAuth::ConsumeCloseRequest())
            application.RequestClose();
        application.EndRender();

        // Item 63: Event-based wait to replace busy-waiting
        // Using 1ms yield to keep UI responsive without busy-waiting
        application.WaitForEvents(std::chrono::milliseconds(1));
    }
    if (!application.shouldRun || !authenticated) {
        (void)shutdownCoordinator.ShutdownAll("authentication-ended");
        return 0;
    }

    friends::Initialize();
    aim_type::Initialize();
    (void)shutdownCoordinator.Register(
        OmniGhost::Platform::ShutdownComponent::Input, 500,
        [] { aim_type::Shutdown(); });
    aimbot::Initialize();

    // One manager owns background discovery. The launcher becomes visible
    // immediately; MAKCU remains optional and the passive DMA PnP probe starts
    // only after MAKCU finishes or reaches its bounded timeout. No separate
    // hardware surface is created and rendering never calls hardware APIs.
    (void)startupState.Transition(OmniGhost::Startup::State::Authenticating,
                            OmniGhost::Startup::State::CheckingHardware);
    OmniGhost::Hardware::Manager hardwareManager;
    (void)shutdownCoordinator.Register(
        OmniGhost::Platform::ShutdownComponent::Hardware, 600,
        [&hardwareManager] { hardwareManager.Shutdown(); });
    hardwareManager.BeginStartupProbe();
    (void)startupState.Transition(OmniGhost::Startup::State::CheckingHardware,
                            OmniGhost::Startup::State::Ready);
    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Core,
        "hardware discovery started in launcher background");
    std::clog << "[UI][Transition] authentication -> launcher begin; hardware probe remains background-only\n";
    const HWND startupHwnd = application.overlay;
    OmniGhost::Platform::LogWindowAudit("before-launcher-transition", startupHwnd);
    application.SetCompactMode(false);
    if (application.overlay != startupHwnd) {
        std::cerr << "[WINDOW][Invariant] FAIL: startup HWND changed during compact->launcher transition.\n";
        application.RequestClose();
        (void)shutdownCoordinator.ShutdownAll("window-invariant-failed");
        return 4;
    }
    OmniGhost::Platform::LogWindowAudit("after-launcher-transition", application.overlay);
    const OmniGhost::Platform::WindowAudit transitionAudit =
        OmniGhost::Platform::AuditOmniGhostWindows();
    if (transitionAudit.currentProcessWindows != 1 || transitionAudit.foreignProcessWindows != 0) {
        std::cerr << "[WINDOW][Invariant] FAIL: duplicate OmniGhost top-level window detected during launcher transition. "
                  << "current=" << transitionAudit.currentProcessWindows
                  << " foreign=" << transitionAudit.foreignProcessWindows << "\n";
        if (transitionAudit.firstForeign) {
            if (IsIconic(transitionAudit.firstForeign))
                ShowWindow(transitionAudit.firstForeign, SW_RESTORE);
            SetForegroundWindow(transitionAudit.firstForeign);
        }
        application.RequestClose();
        (void)shutdownCoordinator.ShutdownAll("duplicate-window-detected");
        return 4;
    }
    std::clog << "[UI][Transition] SetCompactMode(false) returned; same HWND retained and DXGI resize is deferred to render loop\n";
    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Core,
        "launcher display transition queued safely");

    auto& update_service = OmniGhost::Update::UpdateService::Instance();
    OmniGhost::Update::Configuration update_configuration{};
#if defined(OMNIGHOST_PUBLISH_BUILD)
    update_configuration.channel = OmniGhost::Update::Channel::Stable;
#elif defined(OMNIGHOST_TESTER_BUILD)
    update_configuration.channel = OmniGhost::Update::Channel::Development;
#else
    update_configuration.channel = app_settings::config.update_channel == app_settings::UpdateChannel::Beta
        ? OmniGhost::Update::Channel::Beta
        : OmniGhost::Update::Channel::Stable;
#endif
    update_configuration.automaticDownload = app_settings::config.update_auto_download;
    update_configuration.automaticInstallOnExit = app_settings::config.update_install_on_exit;
    update_service.Initialize(update_configuration);
    (void)shutdownCoordinator.Register(
        OmniGhost::Platform::ShutdownComponent::Updater, 800,
        [&update_service] { update_service.Shutdown(); });
    const auto startupUpdate = update_service.GetSnapshot();
    if (startupUpdate.recentlyUpdated && !startupUpdate.rollbackRestored) {
        const std::string version = startupUpdate.availableVersion.empty()
            ? std::string(OmniGhost::Version)
            : startupUpdate.availableVersion;
        const std::string subtitle = "Versão " + version;
        OmniGhost::UI::RenderTimedTransition(application, "✓ Atualização concluída",
            subtitle.c_str(), app_settings::MotionEnabled() ? 0.48f : 0.05f, true);
        CyberWidgets::Notify(("OmniGhost atualizado para " + version).c_str(),
            CyberWidgets::ToastType::Success);
    }
    if (app_settings::config.update_auto_check)
        update_service.CheckAsync(false);

    auto& changelog_service = Launcher::ChangelogService::Instance();
    changelog_service.Initialize();
    (void)shutdownCoordinator.Register(
        OmniGhost::Platform::ShutdownComponent::Changelog, 700,
        [&changelog_service] { changelog_service.Shutdown(); });
    changelog_service.RefreshAsync(false);

    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Core,
        "startup complete");

    // No DMA session is opened during startup. Selecting a game performs the
    // first device open and then the process-specific bind.

    // Outer loop: return to launcher on attach/offset failure. Re-establish
    // launcher window semantics every time a game session ends (interactive,
    // ESC navigation owned by the launcher, and capture exclusion disabled).
    Launcher::EntryReason nextLauncherEntry = Launcher::EntryReason::Authenticated;
    for (;;) {
    application.RenderMenu = true;
    app_settings::menu_open = true;
    application.SetCaptureExclusion(false);
    Launcher::Reset(nextLauncherEntry);
    nextLauncherEntry = Launcher::EntryReason::Normal;
    Launcher::GameId selected = Launcher::GameId::None;
    bool logout_requested = false;
    while (application.shouldRun && selected == Launcher::GameId::None && !logout_requested) {
        hardwareManager.Poll();
        // The onboarding and status pages are read-only views. Mirror the
        // HardwareManager snapshot into their display model; never let UI code
        // probe a device or open a DMA session by itself.
        const auto hardware = hardwareManager.Snapshot();
        HardwareMonitor::SetDeviceEnabled(HardwareMonitor::DeviceType::DMA, true);
        HardwareMonitor::UpdateDeviceStatus(
            HardwareMonitor::DeviceType::DMA,
            hardware.dmaFinished && hardware.dmaAvailable,
            hardware.dmaFinished ? "PnP detected" : "Checking",
            hardware.dmaFinished ? "Not opened until game launch" : "");
        HardwareMonitor::SetDeviceEnabled(HardwareMonitor::DeviceType::Makcu, true);
        HardwareMonitor::UpdateDeviceStatus(
            HardwareMonitor::DeviceType::Makcu,
            hardware.makcuFinished && hardware.makcuAvailable,
            hardware.makcuFinished ? "Startup probe" : "Checking",
            "Optional input device");
        application.StartRender();
        if (!application.shouldRun)
            break;
        if (Launcher::Draw())
            selected = Launcher::Selected();
        logout_requested = Launcher::ConsumeLogoutRequest();
        application.EndRender();
        if (update_service.ShouldExitForUpdate())
            application.shouldRun = false;
    }

    // Logout keeps the process, updater and device services alive, but requires
    // a fresh local authentication before returning to the Control Center.
    if (application.shouldRun && logout_requested) {
        OmniGhost::UI::RenderTimedTransition(application, "A terminar sessão", "",
            app_settings::MotionEnabled() ? 0.25f : 0.0f);
        application.SetCompactMode(true);
        LauncherAuth::Reset();
        bool reauthenticated = false;
        while (application.shouldRun && !reauthenticated) {
            application.StartRender();
            if (!application.shouldRun)
                break;
            reauthenticated = LauncherAuth::Draw();
            if (LauncherAuth::ConsumeCloseRequest())
                application.RequestClose();
            application.EndRender();
            if (update_service.ShouldExitForUpdate())
                application.shouldRun = false;
        }
        if (!application.shouldRun)
            break;
        application.SetCompactMode(false);
        nextLauncherEntry = Launcher::EntryReason::Authenticated;
        continue;
    }

    if (!application.shouldRun || selected == Launcher::GameId::None) {
        (void)shutdownCoordinator.ShutdownAll(
            update_service.ShouldExitForUpdate() ? "update-restart" : "launcher-exit");
        return 0;
    }

    // Defence in depth: the card state is not the licensing authority. Even if
    // a future UI bug selects a locked card, main refuses to enter offset/DMA
    // process binding without a current entitlement for that product.
    const Launcher::GameDefinition* selectedDefinition = Launcher::FindGame(selected);
    if (!selectedDefinition || !OmniGhost::Licensing::HasGameAccess(selectedDefinition->id)) {
        std::clog << "[LICENSE] Game launch blocked: no entitlement for "
                  << (selectedDefinition ? selectedDefinition->id : "unknown") << ".\n";
        continue;
    }

    // Get adapter for the selected game
    OmniGhost::Launcher::IGameAdapter* adapter =
        OmniGhost::Launcher::FindGameAdapter(selected);
    if (!adapter) {
        std::clog << "[LAUNCHER] No adapter found for game ID: "
                  << static_cast<int>(selected) << std::endl;
        (void)startupState.Transition(OmniGhost::Startup::State::StartingGame,
                                      OmniGhost::Startup::State::Ready);
        continue; // back to launcher
    }
    
    // Map launcher id → ActiveGame for offset bootstrap
    ActiveGame pending_game = ActiveGame::FiveM;
    switch (selected) {
        case Launcher::GameId::CS2: pending_game = ActiveGame::CS2; break;
        case Launcher::GameId::Warzone: pending_game = ActiveGame::Warzone; break;
        case Launcher::GameId::Valorant: pending_game = ActiveGame::Valorant; break;
        case Launcher::GameId::Fortnite: pending_game = ActiveGame::Fortnite; break;
        case Launcher::GameId::FiveM: pending_game = ActiveGame::FiveM; break;
        default: pending_game = ActiveGame::FiveM; break;
    }

    if (!startupState.Transition(OmniGhost::Startup::State::Ready,
                                 OmniGhost::Startup::State::StartingGame)) {
        startupState.Fail();
        std::cerr << "[STARTUP] Invalid Ready -> StartingGame transition; launch refused.\n";
        break;
    }

    // Load the configured offset data before opening the game menu.
    if (pending_game != ActiveGame::Warzone) {
        bool need_refresh = false;
        // Pre-load JSON if present so HasCriticalOffsets works
        if (pending_game == ActiveGame::CS2) {
            CS2::LoadOffsetsFromJson(nullptr);
            if (!OmniGhost::OffsetAuto::HasCriticalOffsets(pending_game))
                need_refresh = true;
        } else if (!OmniGhost::OffsetAuto::HasCriticalOffsets(pending_game)) {
            need_refresh = true;
        }

        if (need_refresh) {
            OmniGhost::OffsetAuto::g_status =
                "Offsets desatualizados ou em falta — a atualizar em fundo...";
            OmniGhost::OffsetAuto::g_busy = true;

            // Reload on a worker so the overlay remains responsive.
            struct WorkerArg {
                ActiveGame game;
                OmniGhost::OffsetAuto::Result result;
            } warg{ pending_game, OmniGhost::OffsetAuto::Result::RefreshFailed };
            std::atomic<bool> workerFinished{false};
            std::jthread worker([&warg, &workerFinished](std::stop_token) {
                warg.result = OmniGhost::OffsetAuto::EnsureOffsets(warg.game, false);
                workerFinished.store(true, std::memory_order_release);
            });

            while (application.shouldRun && !workerFinished.load(std::memory_order_acquire)) {
                application.StartRender();
                // Full-screen status card
                const ImVec2 ds = ImGui::GetIO().DisplaySize;
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(ds);
                ImGui::Begin("##offset_bootstrap", nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(0, 0), ds, IM_COL32(8, 8, 10, 240));
                const char* title = "A carregar offsets";
                ImVec2 ts = ImGui::CalcTextSize(title);
                ImGui::SetCursorPos(ImVec2((ds.x - ts.x) * 0.5f, ds.y * 0.38f));
                ImGui::TextUnformatted(title);
                ImGui::Spacing();
                const std::string& st = OmniGhost::OffsetAuto::g_status;
                ImVec2 ss = ImGui::CalcTextSize(st.c_str());
                ImGui::SetCursorPosX((ds.x - ss.x) * 0.5f);
                ImGui::TextWrapped("%s", st.c_str());
                ImGui::SetCursorPosY(ds.y * 0.55f);
                ImGui::SetCursorPosX(ds.x * 0.5f - 80.f);
                ImGui::TextDisabled("Nao feches o jogo...");
                ImGui::End();
                application.EndRender();

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            // std::jthread owns and joins the worker, preventing the previous
            // stack-use-after-free when a Win32 thread exceeded its timeout.
            if (worker.joinable()) worker.join();
            OmniGhost::OffsetAuto::g_busy = false;

            if (warg.result != OmniGhost::OffsetAuto::Result::Ok &&
                !OmniGhost::OffsetAuto::HasCriticalOffsets(pending_game)) {
                // Non-blocking: log and continue into the menu so the user can still open the game UI.
                std::cout << "[Offsets] Refresh incomplete (non-blocking): "
                          << OmniGhost::OffsetAuto::g_status << std::endl;
            }
        }
    }

    // Preserve process visibility before Attach()/Init changes the active DMA
    // process context. A failed attach must not turn an already observed game
    // into the misleading "game not found" diagnosis.
    const bool game_present_before_attach =
        OmniGhost::GameLaunch::IsProcessPresent(selected);

    // Initialize the game using the adapter
    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Adapter,
        "StartGameAdapter begin",
        {{"game", selectedDefinition ? selectedDefinition->id : "unknown"}});

    OmniGhost::Launcher::AdapterStartResult startResult;
    if (!game_present_before_attach) {
        startResult = { false, { OmniGhost::Launcher::AdapterErrorCode::AttachFailed,
            "Jogo não encontrado. Está aberto?" } };
    } else {
        startResult = OmniGhost::Launcher::StartGameAdapter(selected);
    }
    if (!startResult.succeeded) {
        // Use centralized adapter error model for consistent messaging
        const std::string_view errorMessage =
            OmniGhost::Launcher::AdapterErrorMessage(startResult.error.code);
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Error,
            OmniGhost::SessionLog::Subsystem::Adapter,
            "StartGameAdapter failed",
             {{"code", std::to_string(static_cast<int>(startResult.error.code))},
             {"message", std::string(errorMessage)},
             {"detail", std::string(startResult.error.detail)}});
        std::string detail = startResult.error.detail.empty()
            ? std::string(errorMessage)
            : std::string(startResult.error.detail);

        {
            const DWORD err_until = GetTickCount() + 5500;
            while (application.shouldRun && GetTickCount() < err_until) {
                application.StartRender();
                const ImVec2 ds = ImGui::GetIO().DisplaySize;
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(ds);
                ImGui::Begin("##attach_fail", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(12, 8, 8, 245));
                const float contentWidth = (std::min)(620.f, ds.x * .72f);
                ImGui::SetCursorPosY(ds.y * .32f);
                ImGui::SetCursorPosX((ds.x - contentWidth) * .5f);
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
                ImGui::TextWrapped(
                    "%s\n\n"
                    "Abre o jogo no PC principal e tenta novamente.\n\n"
                    "A voltar ao launcher...",
                    detail.c_str());
                ImGui::PopTextWrapPos();
                ImGui::End();
                application.EndRender();
            }
        }
        
        // Map adapter error to session result for telemetry
        Launcher::SessionResult sessionResult = game_present_before_attach
            ? Launcher::SessionResult::LaunchFailed
            : Launcher::SessionResult::GameNotFound;
        if (game_present_before_attach) switch (startResult.error.code) {
            case OmniGhost::Launcher::AdapterErrorCode::UnsupportedGame:
                sessionResult = Launcher::SessionResult::LaunchFailed;
                break;
            case OmniGhost::Launcher::AdapterErrorCode::AttachFailed:
                sessionResult = Launcher::SessionResult::DeviceMissing;
                break;
            case OmniGhost::Launcher::AdapterErrorCode::ValidationFailed:
            case OmniGhost::Launcher::AdapterErrorCode::InitializationFailed:
                sessionResult = Launcher::SessionResult::OffsetsFailed;
                break;
            case OmniGhost::Launcher::AdapterErrorCode::Cancelled:
                sessionResult = Launcher::SessionResult::LaunchFailed;
                break;
            default:
                sessionResult = Launcher::SessionResult::LaunchFailed;
                break;
        }
        
        Launcher::RecordGameSession(selected, sessionResult, detail);
        (void)startupState.Transition(OmniGhost::Startup::State::StartingGame,
                                      OmniGhost::Startup::State::Ready);
        continue; // back to launcher
    }
    
    // Store adapter for use in the game session loop
    // Note: We'll retrieve it again in the loop for simplicity, or we could store it

    if (!startupState.Transition(OmniGhost::Startup::State::StartingGame,
                                 OmniGhost::Startup::State::Running)) {
        startupState.Fail();
        std::cerr << "[STARTUP] Invalid StartingGame -> Running transition; session refused.\n";
        break;
    }
// Soft-probe is diagnostic only — never block menu entry or force return to launcher.
// User preference: open the game menu even when offsets look outdated.
    if (adapter && (OmniGhost::OffsetAuto::SupportsAutomaticRefresh(pending_game) ||
        pending_game == ActiveGame::Valorant || pending_game == ActiveGame::FiveM ||
        pending_game == ActiveGame::CS2 ||
        pending_game == ActiveGame::Warzone || pending_game == ActiveGame::Fortnite)) {
        const bool probe_ok = OmniGhost::OffsetAuto::SoftProbeLive(pending_game);
        if (probe_ok) {
            OmniGhost::OffsetAuto::MarkLiveValid(pending_game, "soft-probe lobby OK");
        } else {
            std::cout << "[Offsets] SoftProbe FAIL (non-blocking): "
                      << OmniGhost::OffsetAuto::SoftProbeFailReason(pending_game) << std::endl;
        }
    }

    {
        // The Control Center records both the attempted launch time and the final
        // result so cards/Home/Diagnostics show truthful recent-session state.
        Launcher::MarkGameUsed(selected);
        Launcher::RecordGameSession(selected, Launcher::SessionResult::None, {});
    }

    // CS2 keeps a light self-check and reloads only the configured data file.
    if (pending_game == ActiveGame::CS2) {
        if (!OmniGhost::OffsetAuto::ValidateLive(pending_game)) {
            std::cout << "[CS2] Self-test soft-fail — menu opens anyway; use Reload offsets se preciso." << std::endl;
            CS2::LoadOffsetsFromJson(nullptr);
        }
    }

    // Attached OK — fall into game session; on process death re-enter launcher
    // Mesmo comportamento para todos os jogos: menu normal
    app_settings::menu_open = true;
    application.RenderMenu = false;

    const std::uint64_t shutdownGeneration = shutdownCoordinator.BeginSession([&] {
        if (g_activeGame == ActiveGame::CS2) {
            CS2::Shutdown();
            CS2::ready = false;
        } else if (g_activeGame == ActiveGame::Warzone) {
            Warzone::Shutdown();
        } else if (g_activeGame == ActiveGame::Valorant) {
            Valorant::Detach();
        } else if (g_activeGame == ActiveGame::Rust) {
            /* Rust tick handled by adapter */
        } else if (g_activeGame == ActiveGame::Fortnite) {
            Fortnite::Detach();
        } else if (g_activeGame == ActiveGame::FiveM) {
            FiveM::ESP::StopAcquisition();
        }
        mem.InvalidateProcess();
    });


    int process_miss_frames = 0;
    // DMA name lookups can fail transiently while FiveM changes process state or
    // the hardware session is being refreshed.  A single missed lookup must not
    // be interpreted as the game closing.
    ULONGLONG fivem_missing_since = 0;
    ULONGLONG fivem_next_alive_check = 0;
    bool fivem_presence_confirmed = false;
    int offset_probe_failures = 0;
    ULONGLONG next_offset_probe = GetTickCount64() + 5000;
    bool return_to_launcher = false;
    bool return_requested_by_user = false;
    while (application.shouldRun && !return_to_launcher) {
        application.StartRender();
        if (!application.shouldRun)
            break;

// Game session update and process monitoring
    if (adapter) {
        // Update game logic, ESP, and aim through adapter
        adapter->Tick();
        
        // Process alive checking and session management
        bool shouldReturnToLauncher = false;
        std::string terminationReason;
        
        switch (g_activeGame) {
            case ActiveGame::CS2: {
                static uint64_t last_alive_check = 0;
                const auto cs2_snapshot = CS2::AcquireRuntimeSnapshot();
                const uint64_t frames = cs2_snapshot ? cs2_snapshot->frames : 0;
                if (frames - last_alive_check >= 8) {
                    last_alive_check = frames;
                    if (!CS2::IsGameProcessAlive()) {
                        ++process_miss_frames;
                        if (process_miss_frames >= 1) {
                            shouldReturnToLauncher = true;
                            terminationReason = "Processo cs2.exe terminou";
                        }
                    } else {
                        process_miss_frames = 0;
                    }
                }
                break;
            }
            case ActiveGame::Warzone: {
                static uint64_t last_alive_wz = 0;
                if (Warzone::runtime.frames - last_alive_wz >= 90) {
                    last_alive_wz = Warzone::runtime.frames;
                    if (Warzone::runtime.module_base && !Warzone::IsGameProcessAlive()) {
                        shouldReturnToLauncher = true;
                        terminationReason = "Processo Warzone terminou";
                    }
                }
                break;
            }
            case ActiveGame::Valorant: {
                static ULONGLONG last_alive_valorant = 0;
                const ULONGLONG now = GetTickCount64();
                if (now - last_alive_valorant >= 750) {
                    last_alive_valorant = now;
                    DWORD pid = mem.GetPidFromName("VALORANT-Win64-Shipping.exe");
                    if (!pid) pid = mem.GetPidFromName("VALORANT.exe");
                    if (!pid) {
                        ++process_miss_frames;
                        if (process_miss_frames >= 2) {
                            shouldReturnToLauncher = true;
                            terminationReason = "Processo Valorant terminou";
                        }
                    } else {
                        process_miss_frames = 0;
                    }
                }
                break;
            }
            case ActiveGame::Fortnite: {
                static ULONGLONG last_alive_fn = 0;
                const ULONGLONG now_fn = GetTickCount64();
                if (now_fn - last_alive_fn >= 750) {
                    last_alive_fn = now_fn;
                    if (!Fortnite::IsGameProcessAlive()) {
                        ++process_miss_frames;
                        if (process_miss_frames >= 2) {
                            shouldReturnToLauncher = true;
                            terminationReason = "Processo Fortnite terminou";
                        }
                    } else {
                        process_miss_frames = 0;
                    }
                }
                break;
            }
            case ActiveGame::FiveM: {
                // Do not conflate a transient DMA/PID lookup miss with the game
                // exiting.  FiveM can expose GTAProcess under several names.
                const ULONGLONG now = GetTickCount64();
                if (now >= fivem_next_alive_check) {
                    fivem_next_alive_check = now + 1000;
                        DWORD pid = 0;
                        if (!g_validExecutable.empty())
                            pid = mem.GetPidFromName(g_validExecutable);
                        if (!pid)
                            pid = mem.GetPidFromName("GTAProcess.exe");
                        // Also try common FiveM names if specific exe gone
                        if (!pid) {
                            const char* alts[] = {
                                "FiveM_GTAProcess.exe", "FiveM_b3258_GTAProcess.exe",
                                "FiveM_b3407_GTAProcess.exe", "FiveM_b3570_GTAProcess.exe", nullptr
                            };
                            for (int i = 0; alts[i]; ++i) {
                                pid = mem.GetPidFromName(alts[i]);
                                if (pid) break;
                            }
                        }
                        const bool localProcessAlive = IsLocalFiveMProcessRunning();
                        if (pid || localProcessAlive) {
                            fivem_presence_confirmed = true;
                            fivem_missing_since = 0;
                            process_miss_frames = 0;
                        } else if (fivem_presence_confirmed) {
                            if (fivem_missing_since == 0) {
                                fivem_missing_since = now;
                                std::cout << "[FiveM] DMA e processo local não confirmaram FiveM; a confirmar antes de encerrar a sessão." << std::endl;
                            }
                            // Keep the session alive through VMM/FPGA refreshes. A return is
                            // permitted only after a continuous, independently confirmed miss.
                            if (now - fivem_missing_since >= 20000) {
                                shouldReturnToLauncher = true;
                                terminationReason = "Processo FiveM/GTA terminou";
                            }
                        } else {
                            // The adapter attached successfully, but this machine may be the
                            // controller rather than the game PC. Never infer an exit merely
                            // because neither local Toolhelp nor a transient DMA lookup has
                            // observed the remote executable yet.
                            fivem_missing_since = 0;
                        }
                }
                break;
            }
            default:
                break;
        }
        
        if (shouldReturnToLauncher) {
            std::cout << "[" << (g_activeGame == ActiveGame::CS2 ? "CS2" :
                                  g_activeGame == ActiveGame::Warzone ? "Warzone" :
                                  g_activeGame == ActiveGame::Valorant ? "Valorant" :
                                  g_activeGame == ActiveGame::Fortnite ? "Fortnite" :
                                  g_activeGame == ActiveGame::Rust ? "Rust" : "FiveM") 
                      << "] " << terminationReason << " — a voltar ao launcher." << std::endl;
            return_to_launcher = true;
        }
    }

        // A recent API timestamp alone cannot prove compatibility. While a
        // session is active, probe real pointers periodically and quarantine
        // the adapter only after repeated failures (transient reads are ignored).
        const ULONGLONG probe_now = GetTickCount64();
        if (probe_now >= next_offset_probe &&
            g_activeGame == ActiveGame::CS2) {
            next_offset_probe = probe_now + 5000;
            bool can_probe = false;
            if (g_activeGame == ActiveGame::CS2) {
                const auto cs2_snapshot = CS2::AcquireRuntimeSnapshot();
                can_probe = CS2::ready && cs2_snapshot && cs2_snapshot->local_pawn != 0;
            }
            if (can_probe) {
                if (OmniGhost::OffsetAuto::ValidateLive(g_activeGame)) {
                    offset_probe_failures = 0;
                    OmniGhost::OffsetAuto::MarkLiveValid(g_activeGame);
                } else if (++offset_probe_failures >= 3) {
                    // Diagnostic only — keep the menu/session open.
                    const std::string reason = "Os offsets falharam três validações consecutivas no jogo em execução.";
                    std::cerr << "[Offsets] " << reason << " (non-blocking; menu stays open)" << std::endl;
                    offset_probe_failures = 0; // avoid log spam every 5s
                }
            }
        }

        application.Render();
        application.EndRender();
        if (application.ConsumeReturnToLauncherRequest()) {
            std::clog << "[SESSION] user requested game shutdown and launcher return\n";
            return_to_launcher = true;
            return_requested_by_user = true;
        }
        if (update_service.ShouldExitForUpdate())
            application.shouldRun = false;
    }


        // Exactly one owner tears the active adapter down. Late process-loss,
        // menu-exit and application-exit observations cannot double-close it.
        if (application.shouldRun && return_to_launcher) {
            OmniGhost::UI::EndGameSessionWithTransition(application, shutdownCoordinator,
                shutdownGeneration, return_requested_by_user);
        } else {
            (void)shutdownCoordinator.EndSession(shutdownGeneration, "application-exit");
        }

        if (application.shouldRun) {
            (void)startupState.Transition(OmniGhost::Startup::State::Running,
                                         OmniGhost::Startup::State::Ready);
        }

        if (!application.shouldRun) {
            Launcher::RecordGameSession(
                selected, Launcher::SessionResult::Completed,
                "Sessão encerrada pelo utilizador.");
            break; // user quit
        }
        if (return_to_launcher) {
            Launcher::RecordGameSession(
                selected,
                return_requested_by_user
                    ? Launcher::SessionResult::Completed
                    : Launcher::SessionResult::ProcessEnded,
                return_requested_by_user
                    ? "Sessão terminada pelo utilizador."
                    : "O processo do jogo deixou de estar disponível.");
            CyberWidgets::Notify(
                return_requested_by_user
                    ? "Sessão terminada"
                    : "Sessão terminada — o jogo foi fechado",
                CyberWidgets::ToastType::Info);
            nextLauncherEntry = Launcher::EntryReason::SessionEnded;
            std::cout << "[OmniGhost] A regressar ao launcher..." << std::endl;
            continue;
        }
        break;
    } // for (;;) launcher session

    (void)shutdownCoordinator.ShutdownAll(
        update_service.ShouldExitForUpdate() ? "update-restart" : "application-exit");
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // Per-monitor-v2 is also declared in the embedded manifest. Calling the API
    // here keeps DPI behaviour correct for development builds where the manifest
    // may be temporarily absent or overridden by Visual Studio.
    using SetDpiContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        if (auto setDpiContext = reinterpret_cast<SetDpiContextFn>(
                GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
            setDpiContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return RunOmniGhost(0, nullptr);
    const int result = RunOmniGhost(argc, argv);
    LocalFree(argv);
    return result;
}
