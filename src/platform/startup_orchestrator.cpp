#include "startup_orchestrator.h"
#include "error_codes.h"
#include "runtime_bootstrap.h"
#include "app_paths.h"
#include "session_log.h"
#include "crash_handler.h"
#include "thread_utils.h"
#include "text_encoding.h"
#include "config/app_settings.h"
#include "config/config_manager.h"
#include "window_instance_guard.h"
#include "unique_handle.h"
#include "../../Fivem/friends/friends.h"
#include "../../Fivem/aimbot/aim_type.h"
#include "../../Fivem/aimbot/aimbot.h"
#include "launcher/auth_page.h"
#include <thread>
#include <iostream>

namespace OmniGhost::Platform {

Result<StartupResult> StartupOrchestrator::Run() {
    running_ = true;
    StartupResult result;

    // Phase sequence
    if (auto r = PhaseBoot(); !r.IsOk()) {
        result.errorCode = r.UnwrapErr().message == "Failed" ? ErrorCode::Unknown : ErrorCode::InvalidState;
        result.errorDetail = r.UnwrapErr().message;
        return Err<StartupResult>(r.UnwrapErr().message);
    }
    if (auto r = PhaseCheckingInstance(); !r.IsOk()) return Err<StartupResult>(r.UnwrapErr().message);
    if (auto r = PhaseInitializingRuntime(); !r.IsOk()) return Err<StartupResult>(r.UnwrapErr().message);
    if (auto r = PhaseAuthenticating(); !r.IsOk()) return Err<StartupResult>(r.UnwrapErr().message);

    // Hardware probe runs in background; we don't block launcher on it
    if (auto r = PhaseCheckingHardware(); !r.IsOk()) {
        if (!config_.allowDegradedHardware) {
            result.success = false;
            result.errorCode = ErrorCode::DmaDeviceNotFound;
            result.errorDetail = "Hardware probe failed";
            return Err<StartupResult>("Hardware probe failed");
        }
        result.hardwareDegraded = true;
        hardware_status_ = "Hardware probe failed - degraded mode";
    }

    PhaseHardwareReady();
    PhaseLauncherReady();

    result.success = true;
    result.hardwareDegraded = hardware_ready_ && hardware_status_.find("failed") != std::string::npos;
    result.hardwareStatus = hardware_status_;
    return Ok(std::move(result));
}

void StartupOrchestrator::Shutdown(const std::string& reason) noexcept {
    running_ = false;
    phase_ = StartupPhase::Failed;
    std::cout << "[Startup] Shutdown: " << reason << "\n";
}

void StartupOrchestrator::SetPhase(StartupPhase next) {
    phase_ = next;
    LogPhase(next);
}

void StartupOrchestrator::LogPhase(StartupPhase phase) {
    static constexpr std::string_view names[] = {
        "Boot", "CheckingInstance", "InitializingRuntime", "Authenticating",
        "CheckingHardware", "HardwareReady", "LauncherReady",
        "StartingGame", "Running", "Failed"
    };
    std::cout << "[Startup] Phase: " << names[static_cast<int>(phase)] << "\n";
    OmniGhost::SessionLog::Write(
        SessionLog::Severity::Info, SessionLog::Subsystem::Core,
        std::string("startup phase: ") + std::string(names[static_cast<int>(phase)]));
}

Result<void> StartupOrchestrator::PhaseBoot() {
    SetPhase(StartupPhase::Boot);

    // Runtime bootstrap
    const auto bootstrap = OmniGhost::RuntimeBootstrap::Prepare();
    if (bootstrap == RuntimeBootstrap::Result::Failed) {
        return Err<void>("Runtime bootstrap failed");
    }

    // Crash handler
    OmniGhost::CrashHandler::Install();

    // Session log
    OmniGhost::SessionLog::Initialize();
    OmniGhost::SessionLog::Write(
        SessionLog::Severity::Info, SessionLog::Subsystem::Core,
        "startup entered", {{"session_id", SessionLog::CurrentSessionId()}});

    // Version logging
    std::clog << "[RUNTIME] mode=SINGLE_PROCESS_PORTABLE\n";
    std::clog << "[RUNTIME] build=PRIVATE_DEVELOPMENT_BUILD\n";
    std::clog << "[RUNTIME] static_vmm=YES source_integration\n";
    std::clog << "[RUNTIME] vendor_d3xx=EXTERNAL_FTD3XXWU_ON_FPGA_OPEN\n";

    return Ok();
}

Result<void> StartupOrchestrator::PhaseCheckingInstance() {
    SetPhase(StartupPhase::CheckingInstance);

    // Single instance check
    constexpr wchar_t kSingleInstanceName[] = L"Local\\OmniGhostApp-6A670FC7";
    HANDLE handle = CreateMutexW(nullptr, FALSE, L"Local\\OmniGhostApp-6A670FC7");
    const DWORD error = GetLastError();
    UniqueHandle singleInstance(handle);

    if (!singleInstance) {
        return Err<void>("single-instance mutex creation failed");
    }
    if (error == ERROR_ALREADY_EXISTS) {
        ActivateExistingOmniGhostWindow();
        return Err<void>("another instance already running");
    }

    // Legacy cleanup
    std::wstring legacyError;
    if (!OmniGhost::RuntimeBootstrap::RetireLegacyPrivateInstall(legacyError)) {
        return Err<void>("legacy cleanup failed: " + WideToUtf8(legacyError));
    }
    OmniGhost::Paths::MigrateLegacyUserData();

    // Settings
    app_settings::Initialize();
    config_manager::Initialize();

    return Ok();
}

Result<void> StartupOrchestrator::PhaseInitializingRuntime() {
    SetPhase(StartupPhase::InitializingRuntime);

    // Initialize services that don't need hardware
    friends::Initialize();
    aim_type::Initialize();
    aimbot::Initialize();

    return Ok();
}

Result<void> StartupOrchestrator::PhaseAuthenticating() {
    SetPhase(StartupPhase::Authenticating);

    LauncherAuth::Reset();

    auto deadline = config_.authTimeout.count() > 0
        ? clock_->NowMs() + config_.authTimeout.count()
        : 0;

    bool authenticated = false;
    while (running_ && !authenticated) {
        if (deadline && clock_->NowMs() >= deadline) {
            return Err<void>("Authentication timeout");
        }
        // The actual auth logic would be in a callback
        if (auth_cb_) {
            auto result = auth_cb_();
            if (result.IsOk()) authenticated = true;
            else if (result.IsErr()) return Err<void>(result.UnwrapErr().message);
        }
        // Item 63: Replace polling with event-based wait
        WaitForEvents(std::chrono::milliseconds(16));
    }

    if (!authenticated && !running_) {
        return Err<void>("Application closed during authentication");
    }

    return Ok();
}

Result<void> StartupOrchestrator::PhaseCheckingHardware() {
    SetPhase(StartupPhase::CheckingHardware);

    if (config_.skipHardwareProbe) {
        hardware_ready_ = true;
        hardware_status_ = "Hardware probe skipped";
        return Ok();
    }

    // Start hardware probe asynchronously
    // In real implementation, this would start the HardwareManager
    // and we would poll Poll() until Complete() or timeout

    auto deadline = clock_->NowMs() + config_.hardwareProbeTimeout.count();
    hardware_status_ = "Probing hardware...";

    // For now, run synchronously
    if (hw_probe_cb_) {
        auto result = hw_probe_cb_();
        if (result.IsOk()) {
            hardware_ready_ = result.Unwrap();
            hardware_status_ = hardware_ready_ ? "Hardware ready" : "Hardware not available";
        } else {
            hardware_ready_ = false;
            hardware_status_ = "Hardware probe failed: " + result.UnwrapErr().message;
        }
    } else {
        // Default: probe DMA device
        hardware_ready_ = false;
        hardware_status_ = "No hardware probe callback";
    }

    return Ok();
}

Result<void> StartupOrchestrator::PhaseHardwareReady() {
    SetPhase(StartupPhase::HardwareReady);
    hardware_ready_ = true;
    std::cout << "[Startup] Hardware: " << hardware_status_ << "\n";
    return Ok();
}

Result<void> StartupOrchestrator::PhaseLauncherReady() {
    SetPhase(StartupPhase::LauncherReady);
    launcher_ready_ = true;
    std::cout << "[Startup] Launcher ready" << "\n";
    return Ok();
}

Result<void> StartupOrchestrator::WaitForEvents(std::chrono::milliseconds timeout) {
    if (!clock_) return Err<void>("Clock not initialized");
    const auto deadline = clock_->NowMs() + timeout.count();
    while (running_ && clock_->NowMs() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return Ok();
}

} // namespace OmniGhost::Platform