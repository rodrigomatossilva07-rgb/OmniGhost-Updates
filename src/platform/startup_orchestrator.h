#pragma once

#include "result.h"
#include "interfaces.h"
#include "error_codes.h"
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace OmniGhost::Platform {

// ============================================================
// StartupOrchestrator - extracted from main.cpp
// Separates hardware readiness from launcher readiness
// ============================================================

enum class StartupPhase : std::uint8_t {
    Boot,
    CheckingInstance,
    InitializingRuntime,
    Authenticating,
    CheckingHardware,      // Hardware probe in progress
    HardwareReady,         // Hardware probe complete (success or degraded)
    LauncherReady,         // UI ready, hardware may still be degraded
    StartingGame,
    Running,
    Failed
};

struct StartupConfig {
    bool skipHardwareProbe = false;      // For testing
    bool skipAuthentication = false;     // For testing
    bool allowDegradedHardware = true;   // Allow launcher to open with degraded hardware
    std::chrono::milliseconds hardwareProbeTimeout{30000};
    std::chrono::milliseconds authTimeout{0};  // 0 = no timeout
};

struct StartupResult {
    bool success = false;
    ErrorCode errorCode = ErrorCode::None;
    std::string errorDetail;
    bool hardwareDegraded = false;       // Hardware probe failed but launcher can open
    std::string hardwareStatus;          // Human-readable hardware status
};

class StartupOrchestrator {
public:
    using AuthCallback = std::function<Result<void>()>;
    using HardwareProbeCallback = std::function<Result<bool>()>;

    explicit StartupOrchestrator(const StartupConfig& config = {}) : config_(config) {}

    // Inject dependencies for testing
    void SetClock(std::shared_ptr<IClock> clock) { clock_ = std::move(clock); }
    void SetFilesystem(std::shared_ptr<IFilesystem> fs) { fs_ = std::move(fs); }
    void SetAuthCallback(AuthCallback cb) { auth_cb_ = std::move(cb); }
    void SetHardwareProbeCallback(HardwareProbeCallback cb) { hw_probe_cb_ = std::move(cb); }

    // Run the startup sequence
    // Returns when LauncherReady is reached (UI can be shown)
    // Hardware may still be probing in background
    Result<StartupResult> Run();

    // Check current phase
    StartupPhase CurrentPhase() const noexcept { return phase_; }

    // Check if hardware is ready (may be degraded)
    bool IsHardwareReady() const noexcept { return hardware_ready_; }

    // Check if launcher is ready
    bool IsLauncherReady() const noexcept { return launcher_ready_; }

    // Get hardware status for UI
    std::string_view HardwareStatus() const noexcept { return hardware_status_; }

    // Shutdown
    void Shutdown(const std::string& reason = "shutdown") noexcept;

private:
    StartupConfig config_;
    StartupPhase phase_ = StartupPhase::Boot;
    bool hardware_ready_ = false;
    bool launcher_ready_ = false;
    bool running_ = false;
    std::string hardware_status_;

    std::shared_ptr<IClock> clock_ = std::make_shared<SteadyClock>();
    std::shared_ptr<IFilesystem> fs_ = std::make_shared<StdFilesystem>();
    AuthCallback auth_cb_;
    HardwareProbeCallback hw_probe_cb_;

    Result<void> PhaseBoot();
    Result<void> PhaseCheckingInstance();
    Result<void> PhaseInitializingRuntime();
    Result<void> PhaseAuthenticating();
    Result<void> PhaseCheckingHardware();
    Result<void> PhaseHardwareReady();
    Result<void> PhaseLauncherReady();

    Result<void> WaitForEvents(std::chrono::milliseconds timeout);

    void SetPhase(StartupPhase next);
    void LogPhase(StartupPhase phase);
};

} // namespace OmniGhost::Platform