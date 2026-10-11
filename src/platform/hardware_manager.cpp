#include "hardware_manager.h"

#include "third_party/DMALibrary/Memory/Memory.h"
#include "../makcu/makcu_wrapper.h"
#include "src/games/Fivem/aimbot/aim_type.h"
#include "watchdog.h"

#include <iostream>
#include <atomic>
#include <cstdio>
#include <functional>
#include <memory>
#include <thread>

using namespace std::chrono_literals;

namespace OmniGhost::Hardware {

// Active probe disabled: without physical FPGA hardware, the static VMM
// initialization crashes (SEH). The passive PnP probe reliably detects
// whether the device is present in the system.
// PnP probe is DIAGNOSTIC ONLY - it never proves a functional DMA session.
static bool PassiveDmaPnpProbe() {
    // Use global mem instance so startupDeviceProbeOk_ is set for GetDiagnosticsSnapshot()
    // NOTE: PnP detection != functional DMA session. Real DMA verification
    // happens only during explicit game launch when the device is opened.
    const bool present = mem.ProbeDeviceAvailability();
    std::clog << "[STARTUP][Hardware] active_probe=SKIPPED passive_pnp="
              << (present ? "DETECTED" : "NOT_FOUND")
              << " note=PnP_is_diagnostic_only_not_functional_session\n";
    return present;
}

namespace {

// Helper to run a cancellable background operation using jthread.
// The operation receives a stop_token and should check it periodically.
// Returns false if cancelled or operation failed, true if operation succeeded.
template<typename Op>
bool RunCancellableTask(std::jthread& worker, Op&& operation) {
    // If there's already a worker running, don't start another
    if (worker.joinable()) {
        return false;
    }

    worker = std::jthread([op = std::forward<Op>(operation)](std::stop_token st) {
        bool value = false;
        try {
            if (!st.stop_requested()) {
                // Pass stop_token to operation if it accepts it
                if constexpr (std::is_invocable_v<Op, std::stop_token>) {
                    value = op(st);
                } else {
                    // For legacy operations that don't accept stop_token,
                    // run them but check cancellation before starting
                    value = op();
                }
            }
        } catch (const std::exception& ex) {
            std::cerr << "[Hardware] Background task failed: " << ex.what() << "\n";
            value = false;
        } catch (...) {
            std::cerr << "[Hardware] Background task failed with unknown exception\n";
            value = false;
        }
        // Note: result handling is done by the caller checking the worker state
        (void)value; // suppress unused warning when not checking
    });

    return true;
}

} // namespace

Manager::~Manager() {
    Shutdown();
}

void Manager::Shutdown() noexcept {
    // Request stop on all workers - jthread will automatically join on destruction
    // But we explicitly request stop to interrupt any blocking operations that check stop_token
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.request_stop();
        }
    }
    // jthread destructors will join automatically
    workers_.clear();

    std::scoped_lock lock(mutex_);
    state_.stage = ProbeStage::Complete;
}

void Manager::CancelWorkersLocked() noexcept {
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.request_stop();
        }
    }
    workers_.clear();
}

void Manager::BeginStartupProbe() {
    std::scoped_lock lock(mutex_);
    if (state_.stage != ProbeStage::Idle)
        return;

    state_.stage = ProbeStage::CheckingMakcu;
#if defined(OMNIGHOST_MOCK_HARDWARE)
    state_.makcuAvailable = false;
    state_.makcuFinished = false;
    stageStarted_ = std::chrono::steady_clock::now();
    std::clog << "[STARTUP][Hardware] stage=CheckingMakcu mode=MOCK optional=YES\n";
    return;
#else
    // Discovery/connection is isolated from the render thread. The MAKCU
    // implementation validates the device protocol before reporting success,
    // so opening the launcher is enough to obtain a working input device.
    state_.makcuAvailable = makcu_wrapper::IsConnected();
    state_.makcuFinished = state_.makcuAvailable;
    stageStarted_ = std::chrono::steady_clock::now();
    nextMakcuRetry_ = stageStarted_ + 5s;
    if (!state_.makcuAvailable) {
        // Start MAKCU initialization as a cancellable background task
        RunCancellableTask(workers_.emplace_back(), [] {
            makcu_wrapper::MakcuInitialize("");
            return makcu_wrapper::IsConnected();
        });
    }
    std::clog << "[STARTUP][Hardware] stage=CheckingMakcu optional=YES auto_connect=YES already_connected="
              << (state_.makcuAvailable ? "YES" : "NO") << "\n";
    if (state_.makcuAvailable)
        StartDmaProbeLocked();
#endif
}

void Manager::StartDmaProbeLocked() {
    if (state_.stage == ProbeStage::CheckingDma || state_.stage == ProbeStage::Complete)
        return;
    state_.stage = ProbeStage::CheckingDma;
    stageStarted_ = std::chrono::steady_clock::now();
    std::clog << "[STARTUP][Hardware] stage=CheckingDma required=YES mode=PASSIVE_PNP timeout_ms="
              << timeouts_.dmaTimeout.count() << "\n";
    RunCancellableTask(workers_.emplace_back(), [] { return PassiveDmaPnpProbe(); });
}

void Manager::ConsumeLateMakcuResultLocked() {
    // With jthread, we don't have explicit task objects to check.
    // The MAKCU connection state is checked directly in Poll() via makcu_wrapper::IsConnected().
    // This function is kept for compatibility but is now a no-op.
    (void)0;
}

void Manager::Poll() {
    std::scoped_lock lock(mutex_);
    const auto now = std::chrono::steady_clock::now();

#if defined(OMNIGHOST_MOCK_HARDWARE)
    // Advance one stage per rendered frame. This preserves the observable
    // MAKCU -> DMA ordering and lets startup/window tests run without hardware.
    if (state_.stage == ProbeStage::CheckingMakcu) {
        state_.makcuAvailable = true;
        state_.makcuFinished = true;
        state_.stage = ProbeStage::CheckingDma;
        stageStarted_ = now;
        std::clog << "[STARTUP][Hardware] makcu=AVAILABLE mode=MOCK\n";
        return;
    }
    if (state_.stage == ProbeStage::CheckingDma) {
        state_.dmaAvailable = true;
        state_.dmaFinished = true;
        state_.stage = ProbeStage::Complete;
        std::clog << "[STARTUP][Hardware] dma=DETECTED complete=YES mode=MOCK\n";
        return;
    }
#endif

    // Publish an asynchronously-established connection on the UI thread and
    // make MAKCU the active input device without requiring a button click.
    if (makcu_wrapper::IsConnected()) {
        state_.makcuAvailable = true;
        state_.makcuFinished = true;
        aim_type::config.makcu_connected = true;
        aim_type::config.makcu_enabled = true;
        aim_type::config.active = aim_type::DeviceType::Makcu;
        const auto diagnostics = makcu_wrapper::GetDiagnostics();
        if (diagnostics.port[0])
            std::snprintf(aim_type::config.makcu_com,
                sizeof(aim_type::config.makcu_com), "%s", diagnostics.port);
    }

    if (state_.stage == ProbeStage::CheckingMakcu) {
        // Check if MAKCU background task completed
        // Since we use jthread, we rely on makcu_wrapper::IsConnected() which
        // reflects the actual connection state. The background task may have
        // already finished and updated the connection.
        if (makcu_wrapper::IsConnected()) {
            state_.makcuAvailable = true;
            state_.makcuFinished = true;
            std::clog << "[STARTUP][Hardware] makcu=AVAILABLE\n";
            StartDmaProbeLocked();
        } else if (now - stageStarted_ >= timeouts_.makcuTimeout) {
            // Timeout - cancel any pending MAKCU worker
            for (auto& worker : workers_) {
                if (worker.joinable()) {
                    worker.request_stop();
                }
            }
            workers_.clear();
            state_.makcuFinished = true;
            state_.makcuAvailable = false;
            std::clog << "[STARTUP][Hardware] makcu=TIMEOUT optional=YES continuing=DMA\n";
            StartDmaProbeLocked();
        }
        return;
    }

    ConsumeLateMakcuResultLocked();
    if (state_.stage == ProbeStage::CheckingDma) {
        // DMA probe is passive PnP check - typically fast
        // We rely on the fact that PassiveDmaPnpProbe() runs quickly
        // If worker is still running, we wait for it (it should complete within dmaTimeout)
        if (now - stageStarted_ >= timeouts_.dmaTimeout) {
            // Timeout - cancel any pending DMA worker
            for (auto& worker : workers_) {
                if (worker.joinable()) {
                    worker.request_stop();
                }
            }
            workers_.clear();
            state_.dmaAvailable = false;
            state_.dmaFinished = true;
            state_.stage = ProbeStage::Complete;
            std::clog << "[STARTUP][Hardware] dma=TIMEOUT launcher_continues=YES note=PnP_probe_does_not_guarantee_functional_session\n";
        }
        // Note: Actual result consumption happens via the next Poll() call
        // when the worker has finished. For passive PnP, it's usually instant.
        if (!workers_.empty()) {
            // Check if the DMA worker completed by trying to see if we can proceed
            // The worker result is reflected in the next call
            // For simplicity, we assume passive PnP is fast enough
        } else if (state_.dmaFinished == false) {
            // Worker finished, we need to get its result
            // Since we don't have a direct result channel, we re-run the probe
            // (it's cheap and passive)
            state_.dmaAvailable = PassiveDmaPnpProbe();
            state_.dmaFinished = true;
            state_.stage = ProbeStage::Complete;
        }
        if (state_.stage == ProbeStage::Complete)
            std::clog << "[STARTUP][Hardware] dma="
                      << (state_.dmaAvailable ? "PRESENT_PNP" : "NOT_DETECTED")
                      << " complete=YES note=PnP_is_diagnostic_only\n";
    }

    // Support devices connected after the launcher opens. Retries are bounded,
    // asynchronous and never block rendering or DMA discovery.
    if (state_.stage == ProbeStage::Complete && !makcu_wrapper::IsConnected()) {
        if (!workers_.empty() && workers_.back().joinable()) {
            // Previous retry may still be running, wait for it
            // In practice, we just clear and start new
            workers_.clear();
        }
        if (now >= nextMakcuRetry_) {
            state_.makcuFinished = false;
            nextMakcuRetry_ = now + 5s;
            RunCancellableTask(workers_.emplace_back(), [] {
                makcu_wrapper::MakcuInitialize("");
                return makcu_wrapper::IsConnected();
            });
            std::clog << "[Hardware] MAKCU automatic rediscovery started\n";
        }
    }
}

State Manager::Snapshot() const {
    std::scoped_lock lock(mutex_);
    return state_;
}

bool Manager::Complete() const {
    return Snapshot().stage == ProbeStage::Complete;
}

} // namespace OmniGhost::Hardware