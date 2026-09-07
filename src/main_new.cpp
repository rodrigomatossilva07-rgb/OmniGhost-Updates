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
#include "../Cs2/cs2_radar.h"
#include "../Cs2/cs2_esp.h"
#include "../Cs2/cs2_aim.h"
#include "../Rust/rust_game.h"
#include "../Rust/rust_esp.h"
#include "../Rust/rust_aim.h"
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
#include "../ImGui/imgui.h"
#include "makcu/makcu_wrapper.h"

#include "platform/service_container.h"
#include "platform/event_bus.h"
#include "platform/session_factory.h"
#include "platform/scoped_session.h"
#include "platform/feature_flags.h"
#include "platform/feature_registration.h"
#include "platform/input_device.h"
#include "platform/makcu_device.h"
#include "platform/kmbox_device.h"
#include "platform/ferrum_device_impl.h"
#include "platform/virtual_device.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <future>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>

#if (defined(OMNIGHOST_TESTER_BUILD) || defined(OMNIGHOST_PUBLISH_BUILD)) && !defined(OMNIGHOST_REQUIRE_LOCAL_LICENSE)
#error Distribution builds must retain product-entitlement enforcement.
#endif

namespace {

int RunOmniGhost(int argc, wchar_t** argv) {
    // Initialize DI container
    auto container = OmniGhost::Platform::CreateProductionContainer();
    
    // Register input devices
    auto inputManager = OmniGhost::Platform::CreateInputManager();
    inputManager->RegisterDevice(std::make_unique<OmniGhost::Platform::MakcuDevice>());
    inputManager->RegisterDevice(std::make_unique<OmniGhost::Platform::KMBoxDevice>());
    inputManager->RegisterDevice(std::make_unique<OmniGhost::Platform::FerrumDeviceImpl>());
    inputManager->RegisterDevice(std::make_unique<OmniGhost::Platform::VirtualInputDevice>());
    
    // Set fallback chain: Makcu -> KMBox -> Ferrum -> Virtual
    inputManager->SetFallbackChain({
        OmniGhost::Platform::InputDeviceType::Makcu,
        OmniGhost::Platform::InputDeviceType::KMBoxNet,
        OmniGhost::Platform::InputDeviceType::Ferrum,
        OmniGhost::Platform::InputDeviceType::Virtual
    });
    
    container.RegisterInstance<OmniGhost::Platform::IInputManager>(std::shared_ptr<OmniGhost::Platform::IInputManager>(std::move(inputManager)));
    
    // Start EventBus async processing
    OmniGhost::Platform::GetEventBus().StartAsyncProcessing();
    
    // Register features
    OmniGhost::Platform::RegisterAllFeatures(OmniGhost::Platform::GetFeatureRegistry());
    
    // Register game sessions
    OmniGhost::Platform::SessionManager sessionManager;
    OmniGhost::Platform::RegisterAllGameSessions(sessionManager, container);
    
    // Startup state machine
    OmniGhost::Startup::StateMachine startupState;
    startupState.Transition(OmniGhost::Startup::State::Boot, OmniGhost::Startup::State::CheckingInstance);
    
    bool updaterInvocation = false;
    for (int i = 1; argv && i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--apply-update") == 0 || _wcsicmp(argv[i], L"--updater") == 0) {
            updaterInvocation = true;
            break;
        }
    }
    
    // Single instance mutex
    OmniGhost::Platform::UniqueHandle startupOwner;
    if (!updaterInvocation) {
        SetLastError(ERROR_SUCCESS);
        HANDLE handle = CreateMutexW(nullptr, FALSE, L"Local\\OmniGhostStartupOwner-6A670FC7");
        const DWORD error = GetLastError();
        startupOwner.reset(handle);
        if (!startupOwner || error == ERROR_ALREADY_EXISTS) {
            OmniGhost::Platform::ActivateExistingOmniGhostWindow();
            return error == ERROR_ALREADY_EXISTS ? 3 : 2;
        }
        
        std::wstring legacyError;
        if (!OmniGhost::RuntimeBootstrap::RetireLegacyPrivateInstall(legacyError)) {
            MessageBoxW(nullptr, legacyError.c_str(), L"OmniGhost — limpeza da instalação antiga", MB_OK | MB_ICONERROR);
            return 2;
        }
        OmniGhost::Paths::MigrateLegacyUserData();
        if (OmniGhost::Platform::ActivateExistingOmniGhostWindow()) return 3;
    }
    
    const auto bootstrap = OmniGhost::RuntimeBootstrap::Prepare();
    if (bootstrap == OmniGhost::RuntimeBootstrap::Result::Failed) return 1;
    
    startupState.Transition(OmniGhost::Startup::State::CheckingInstance, OmniGhost::Startup::State::InitializingRuntime);
    
    OmniGhost::Platform::SetCurrentThreadName(L"OmniGhost.Main");
    DisableProcessWindowsGhosting();
    OmniGhost::CrashHandler::Install();
    OmniGhost::SessionLog::SetStartReason(
        (argc > 1 && argv && argv[1]) ? OmniGhost::Platform::WideToUtf8(argv[1]) : "normal");
    OmniGhost::SessionLog::Initialize();
    
    std::clog << "[RUNTIME] mode=SINGLE_PROCESS_PORTABLE\n"
              << "[RUNTIME] self_install=DISABLED\n"
              << "[RUNTIME] private_executable=DISABLED\n";
    
    const auto previousRun = OmniGhost::CrashHandler::PreviousRun();
    std::clog << "[SESSION] previous_run=" << OmniGhost::CrashHandler::PreviousRunStateName(previousRun) << "\n";
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
            OmniGhost::Platform::GetEventBus().StopAsyncProcessing();
        }
    } session_shutdown_guard;
    
    if (const auto updater_result = OmniGhost::Update::RunUpdaterModeIfRequested(argc, argv))
        return *updater_result;
    
    if (OmniGhost::Platform::ActivateExistingOmniGhostWindow()) {
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Warning,
            OmniGhost::SessionLog::Subsystem::Platform,
            "existing OmniGhost window from another process reused");
        return 3;
    }
    
    constexpr wchar_t kSingleInstanceName[] = L"Local\\OmniGhostApp-6A670FC7";
    HANDLE singleInstanceHandle = CreateMutexW(nullptr, FALSE, kSingleInstanceName);
    const DWORD singleInstanceError = GetLastError();
    OmniGhost::Platform::UniqueHandle singleInstance(singleInstanceHandle);
    if (!singleInstance) {
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Error,
            OmniGhost::SessionLog::Subsystem::Platform,
            "single-instance mutex creation failed",
            {{"win32", std::to_string(singleInstanceError), false}});
        return 2;
    }
    if (singleInstanceError == ERROR_ALREADY_EXISTS) {
        OmniGhost::Platform::ActivateExistingOmniGhostWindow();
        return 3;
    }
    
    OmniGhost::Update::ConfirmUpdaterStartupFromCommandLine(argc, argv);
    OmniGhost::Update::CaptureUpdaterStartupNoticeFromCommandLine(argc, argv);
    
    // Load settings
    app_settings::Initialize();
    config_manager::Initialize();
    
    // Initialize EventBus subscriptions
    auto& eventBus = OmniGhost::Platform::GetEventBus();
    
    // Subscribe to hardware events
    eventBus.Subscribe<OmniGhost::Platform::HardwareConnectedEvent>([](const auto& e) {
        std::clog << "[HARDWARE] Connected: " << e.deviceType << " (" << e.identifier << ")\n";
    });
    
    eventBus.Subscribe<OmniGhost::Platform::HardwareDisconnectedEvent>([](const auto& e) {
        std::clog << "[HARDWARE] Disconnected: " << e.deviceType << " - " << e.reason << "\n";
    });
    
    eventBus.Subscribe<OmniGhost::Platform::OffsetsLoadedEvent>([](const auto& e) {
        std::clog << "[OFFSETS] Loaded for " << e.gameId << " from " << e.source << "\n";
    });
    
    eventBus.Subscribe<OmniGhost::Platform::SessionStartedEvent>([](const auto& e) {
        std::clog << "[SESSION] Started: " << e.gameId << "\n";
    });
    
    eventBus.Subscribe<OmniGhost::Platform::SessionEndedEvent>([](const auto& e) {
        std::clog << "[SESSION] Ended: " << e.gameId << " - " << e.reason << "\n";
    });
    
    eventBus.Subscribe<OmniGhost::Platform::SessionErrorEvent>([](const auto& e) {
        std::cerr << "[SESSION ERROR] " << e.gameId << ": " << e.error << "\n";
    });
    
    Overlay application;
    application.SetCompactMode(true);
#if defined(OMNIGHOST_PRIVATE_STATIC_VMM)
    application.SetupOverlay("OmniGhost — PRIVATE DEVELOPMENT BUILD — NOT FOR DISTRIBUTION");
#else
    application.SetupOverlay("OmniGhostOverlay");
#endif
    if (!application.shouldRun) return 1;
    
    OmniGhost::Platform::ShutdownCoordinator shutdownCoordinator;
    shutdownCoordinator.Register(
        OmniGhost::Platform::ShutdownComponent::Ui, 100,
        [&application] { application.Shutdown(); });
    shutdownCoordinator.Register(
        OmniGhost::Platform::ShutdownComponent::Radar, 900,
        [] { CS2_Radar::Shutdown(); });
    
    OmniGhost::CrashHandler::MarkStartupComplete();
    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Core,
        "startup UI ready");
    
    LauncherAuth::Reset();
    startupState.Transition(OmniGhost::Startup::State::InitializingRuntime, OmniGhost::Startup::State::Authenticating);
    
    bool authenticated = false;
    while (application.shouldRun && !authenticated) {
        application.StartRender();
        if (!application.shouldRun) break;
        authenticated = LauncherAuth::Draw();
        if (LauncherAuth::ConsumeCloseRequest()) application.RequestClose();
        application.EndRender();
        application.WaitForEvents(std::chrono::milliseconds(1));
    }
    
    if (!application.shouldRun || !authenticated) {
        shutdownCoordinator.ShutdownAll("authentication-ended");
        return 0;
    }
    
    friends::Initialize();
    aim_type::Initialize();
    shutdownCoordinator.Register(
        OmniGhost::Platform::ShutdownComponent::Input, 500,
        [] { aim_type::Shutdown(); });
    aimbot::Initialize();
    
    startupState.Transition(OmniGhost::Startup::State::Authenticating, OmniGhost::Startup::State::CheckingHardware);
    
    OmniGhost::Hardware::Manager hardwareManager;
    shutdownCoordinator.Register(
        OmniGhost::Platform::ShutdownComponent::Hardware, 600,
        [&hardwareManager] { hardwareManager.Shutdown(); });
    hardwareManager.BeginStartupProbe();
    
    startupState.Transition(OmniGhost::Startup::State::CheckingHardware, OmniGhost::Startup::State::Ready);
    
    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Core,
        "hardware discovery started in launcher background");
    
    const HWND startupHwnd = application.overlay;
    OmniGhost::Platform::LogWindowAudit("before-launcher-transition", startupHwnd);
    application.SetCompactMode(false);
    if (application.overlay != startupHwnd) {
        std::cerr << "[WINDOW][Invariant] FAIL: startup HWND changed during compact->launcher transition.\n";
        application.RequestClose();
        shutdownCoordinator.ShutdownAll("window-invariant-failed");
        return 4;
    }
    OmniGhost::Platform::LogWindowAudit("after-launcher-transition", application.overlay);
    
    const OmniGhost::Platform::WindowAudit transitionAudit = OmniGhost::Platform::AuditOmniGhostWindows();
    if (transitionAudit.currentProcessWindows != 1 || transitionAudit.foreignProcessWindows != 0) {
        if (transitionAudit.firstForeign) {
            if (IsIconic(transitionAudit.firstForeign)) ShowWindow(transitionAudit.firstForeign, SW_RESTORE);
            SetForegroundWindow(transitionAudit.firstForeign);
        }
        application.RequestClose();
        shutdownCoordinator.ShutdownAll("duplicate-window-detected");
        return 4;
    }
    
    auto& update_service = OmniGhost::Update::UpdateService::Instance();
    OmniGhost::Update::Configuration update_configuration{};
#if defined(OMNIGHOST_PUBLISH_BUILD)
    update_configuration.channel = OmniGhost::Update::Channel::Stable;
#elif defined(OMNIGHOST_TESTER_BUILD)
    update_configuration.channel = OmniGhost::Update::Channel::Development;
#else
    update_configuration.channel = app_settings::config.update_channel == app_settings::UpdateChannel::Beta
        ? OmniGhost::Update::Channel::Beta : OmniGhost::Update::Channel::Stable;
#endif
    update_configuration.automaticDownload = app_settings::config.update_auto_download;
    update_configuration.automaticInstallOnExit = app_settings::config.update_install_on_exit;
    update_service.Initialize(update_configuration);
    shutdownCoordinator.Register(
        OmniGhost::Platform::ShutdownComponent::Updater, 800,
        [&update_service] { update_service.Shutdown(); });
    
    const auto startupUpdate = update_service.GetSnapshot();
    if (startupUpdate.recentlyUpdated && !startupUpdate.rollbackRestored) {
        const std::string version = startupUpdate.availableVersion.empty()
            ? std::string(OmniGhost::Version) : startupUpdate.availableVersion;
        OmniGhost::UI::RenderTimedTransition(application, "✓ Atualização concluída",
            ("Versão " + version).c_str(), app_settings::MotionEnabled() ? 0.48f : 0.05f, true);
    }
    if (app_settings::config.update_auto_check) update_service.CheckAsync(false);
    
    auto& changelog_service = Launcher::ChangelogService::Instance();
    changelog_service.Initialize();
    shutdownCoordinator.Register(
        OmniGhost::Platform::ShutdownComponent::Changelog, 700,
        [&changelog_service] { changelog_service.Shutdown(); });
    changelog_service.RefreshAsync(false);
    
    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Core,
        "startup complete");
    
    // Main launcher loop
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
            application.StartRender();
            if (!application.shouldRun) break;
            if (Launcher::Draw()) selected = Launcher::Selected();
            logout_requested = Launcher::ConsumeLogoutRequest();
            application.EndRender();
            if (update_service.ShouldExitForUpdate()) application.shouldRun = false;
        }
        
        if (application.shouldRun && logout_requested) {
            OmniGhost::UI::RenderTimedTransition(application, "A terminar sessão", "",
                app_settings::MotionEnabled() ? 0.25f : 0.0f);
            application.SetCompactMode(true);
            LauncherAuth::Reset();
            bool reauthenticated = false;
            while (application.shouldRun && !reauthenticated) {
                application.StartRender();
                if (!application.shouldRun) break;
                reauthenticated = LauncherAuth::Draw();
                if (LauncherAuth::ConsumeCloseRequest()) application.RequestClose();
                application.EndRender();
                if (update_service.ShouldExitForUpdate()) application.shouldRun = false;
            }
            if (!application.shouldRun) break;
            application.SetCompactMode(false);
            nextLauncherEntry = Launcher::EntryReason::Authenticated;
            continue;
        }
        
        if (!application.shouldRun || selected == Launcher::GameId::None) {
            shutdownCoordinator.ShutdownAll(update_service.ShouldExitForUpdate() ? "update-restart" : "launcher-exit");
            return 0;
        }
        
        const Launcher::GameDefinition* selectedDefinition = Launcher::FindGame(selected);
        if (!selectedDefinition || !OmniGhost::Licensing::HasGameAccess(selectedDefinition->id)) {
            std::clog << "[LICENSE] Game launch blocked: no entitlement\n";
            continue;
        }
        
        // Map launcher ID to ActiveGame
        ActiveGame pending_game = ActiveGame::FiveM;
        switch (selected) {
            case Launcher::GameId::CS2: pending_game = ActiveGame::CS2; break;
            case Launcher::GameId::Rust: pending_game = ActiveGame::Rust; break;
            case Launcher::GameId::Warzone: pending_game = ActiveGame::Warzone; break;
            case Launcher::GameId::Valorant: pending_game = ActiveGame::Valorant; break;
            case Launcher::GameId::Fortnite: pending_game = ActiveGame::Fortnite; break;
            case Launcher::GameId::FiveM: pending_game = ActiveGame::FiveM; break;
            default: pending_game = ActiveGame::FiveM; break;
        }
        
        if (!startupState.Transition(OmniGhost::Startup::State::Ready, OmniGhost::Startup::State::StartingGame)) {
            startupState.Fail();
            break;
        }
        
        // Load offsets
        if (pending_game != ActiveGame::Warzone) {
            bool need_refresh = false;
            if (pending_game == ActiveGame::Rust) {
                Rust::LoadOffsetsFromJson(nullptr);
                if (!Rust::offsets.loaded) Rust::ApplyEmbeddedDefaults();
            } else if (pending_game == ActiveGame::CS2) {
                CS2::LoadOffsetsFromJson(nullptr);
                if (!OmniGhost::OffsetAuto::HasCriticalOffsets(pending_game)) need_refresh = true;
            } else if (!OmniGhost::OffsetAuto::HasCriticalOffsets(pending_game)) {
                need_refresh = true;
            }
            
            if (need_refresh) {
                OmniGhost::OffsetAuto::g_status = "Offsets desatualizados — a atualizar...";
                OmniGhost::OffsetAuto::g_busy = true;
                
                struct WorkerArg { ActiveGame game; OmniGhost::OffsetAuto::Result result; } warg{ pending_game, OmniGhost::OffsetAuto::Result::RefreshFailed };
                std::atomic<bool> workerFinished{false};
                std::jthread worker([&warg, &workerFinished](std::stop_token) {
                    warg.result = OmniGhost::OffsetAuto::EnsureOffsets(warg.game, false);
                    workerFinished.store(true, std::memory_order_release);
                });
                
                while (application.shouldRun && !workerFinished.load(std::memory_order_acquire)) {
                    application.StartRender();
                    const ImVec2 ds = ImGui::GetIO().DisplaySize;
                    ImGui::SetNextWindowPos(ImVec2(0, 0));
                    ImGui::SetNextWindowSize(ds);
                    ImGui::Begin("##offset_bootstrap", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
                    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(8, 8, 10, 240));
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
                
                if (worker.joinable()) worker.join();
                OmniGhost::OffsetAuto::g_busy = false;
                
                if (warg.result != OmniGhost::OffsetAuto::Result::Ok && !OmniGhost::OffsetAuto::HasCriticalOffsets(pending_game)) {
                    std::cout << "[Offsets] Refresh incomplete: " << OmniGhost::OffsetAuto::g_status << std::endl;
                }
            }
        }
        
        const bool game_present_before_attach = OmniGhost::GameLaunch::IsProcessPresent(selected);
        (void)game_present_before_attach;
        
        // Start session using new architecture
        OmniGhost::Platform::ScopedSession::Config sessionConfig;
        sessionConfig.tickInterval = std::chrono::milliseconds(1);
        sessionConfig.probeInterval = std::chrono::milliseconds(5000);
        sessionConfig.aliveCheckInterval = std::chrono::milliseconds(750);
        sessionConfig.enableSoftProbe = true;
        sessionConfig.onCleanup = [&] {
            std::clog << "[SESSION] Cleanup callback executed\n";
        };
        
        auto sessionResult = sessionManager.StartSession(pending_game, container, sessionConfig);
        if (!sessionResult) {
            // Show error UI
            const std::string error = sessionResult.error();
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
                ImGui::TextWrapped("%s\n\nAbre o jogo e tenta novamente.\n\nA voltar ao launcher...", error.c_str());
                ImGui::PopTextWrapPos();
                ImGui::End();
                application.EndRender();
            }
            
            startupState.Transition(OmniGhost::Startup::State::StartingGame, OmniGhost::Startup::State::Ready);
            continue;
        }
        
        auto scopedSession = std::move(sessionResult.value());
        
        if (!startupState.Transition(OmniGhost::Startup::State::StartingGame, OmniGhost::Startup::State::Running)) {
            startupState.Fail();
            break;
        }
        
        // Soft probe
        if (OmniGhost::OffsetAuto::SupportsAutomaticRefresh(pending_game) || pending_game == ActiveGame::Valorant || pending_game == ActiveGame::FiveM || pending_game == ActiveGame::CS2 || pending_game == ActiveGame::Rust || pending_game == ActiveGame::Warzone || pending_game == ActiveGame::Fortnite) {
            OmniGhost::OffsetAuto::SoftProbeLive(pending_game);
        }
        
        Launcher::MarkGameUsed(selected);
        Launcher::RecordGameSession(selected, Launcher::SessionResult::None, {});
        
        if (pending_game == ActiveGame::CS2) {
            if (!OmniGhost::OffsetAuto::ValidateLive(pending_game)) {
                std::cout << "[CS2] Self-test soft-fail\n";
                CS2::LoadOffsetsFromJson(nullptr);
            }
        }
        
        app_settings::menu_open = true;
        application.RenderMenu = false;
        
        const std::uint64_t shutdownGeneration = shutdownCoordinator.BeginSession([&] {
            if (g_activeGame == ActiveGame::CS2) { CS2::Shutdown(); CS2::ready = false; }
            else if (g_activeGame == ActiveGame::Rust) { Rust::Shutdown(); Rust::ready = false; }
            else if (g_activeGame == ActiveGame::Warzone) { Warzone::Shutdown(); }
            else if (g_activeGame == ActiveGame::Valorant) { Valorant::Detach(); }
            else if (g_activeGame == ActiveGame::Fortnite) { Fortnite::Detach(); }
            mem.InvalidateProcess();
        });
        
        bool return_to_launcher = false;
        bool return_requested_by_user = false;
        
        // Session loop - ScopedSession handles tick/probe in background
        while (application.shouldRun && !return_to_launcher) {
            application.StartRender();
            if (!application.shouldRun) break;
            
            // Check if session is still running
            if (!scopedSession || !scopedSession->IsRunning()) {
                return_to_launcher = true;
                break;
            }
            
            application.Render();
            application.EndRender();
            
            if (application.ConsumeReturnToLauncherRequest()) {
                std::clog << "[SESSION] user requested game shutdown and launcher return\n";
                return_to_launcher = true;
                return_requested_by_user = true;
            }
            
            if (update_service.ShouldExitForUpdate()) application.shouldRun = false;
        }
        
        if (application.shouldRun && return_to_launcher) {
            OmniGhost::UI::EndGameSessionWithTransition(application, shutdownCoordinator, shutdownGeneration, return_requested_by_user);
        } else {
            shutdownCoordinator.EndSession(shutdownGeneration, "application-exit");
        }
        
        if (application.shouldRun) {
            startupState.Transition(OmniGhost::Startup::State::Running, OmniGhost::Startup::State::Ready);
        }
        
        if (!application.shouldRun) {
            Launcher::RecordGameSession(selected, Launcher::SessionResult::Completed, "Sessão encerrada pelo utilizador.");
            break;
        }
        
        if (return_to_launcher) {
            Launcher::RecordGameSession(selected,
                return_requested_by_user ? Launcher::SessionResult::Completed : Launcher::SessionResult::ProcessEnded,
                return_requested_by_user ? "Sessão terminada pelo utilizador." : "O processo do jogo deixou de estar disponível.");
            
            nextLauncherEntry = Launcher::EntryReason::SessionEnded;
            continue;
        }
        break;
    }
    
    shutdownCoordinator.ShutdownAll(update_service.ShouldExitForUpdate() ? "update-restart" : "application-exit");
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    using SetDpiContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        if (auto setDpiContext = reinterpret_cast<SetDpiContextFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
            setDpiContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }
    
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return RunOmniGhost(0, nullptr);
    const int result = RunOmniGhost(argc, argv);
    LocalFree(argv);
    return result;
}