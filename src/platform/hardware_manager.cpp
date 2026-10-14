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

} // namespace

Manager::~Manager() {
    Shutdown();
}

void Manager::Shutdown() noexcept {
    // Request cooperative cancellation and join the single worker. Joining
    // here is safe: Shutdown runs on the main thread at process exit, not in
    // the per-frame Poll() path.
    {
        std::scoped_lock lock(mutex_);
        if (worker_.joinable())
            worker_.request_stop();
    }
    if (worker_.joinable())
        worker_.join();
    {
        std::scoped_lock lock(mutex_);
        workerState_.store(0, std::memory_order_relaxed);
        workerKind_.store(0, std::memory_order_relaxed);
        state_.stage = ProbeStage::Complete;
    }
}

void Manager::CancelWorkersLocked() noexcept {
    if (worker_.joinable())
        worker_.request_stop();
    if (worker_.joinable())
        worker_.join();
    workerState_.store(0, std::memory_order_relaxed);
    workerKind_.store(0, std::memory_order_relaxed);
}

void Manager::DrainWorkerLocked() noexcept {
    if (workerState_.load(std::memory_order_acquire) != 2)
        return;
    if (worker_.joinable())
        worker_.join(); // already signaled completion: this cannot block
    worker_ = std::jthread{};
    workerState_.store(0, std::memory_order_relaxed);
    workerKind_.store(0, std::memory_order_relaxed);
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
        StartWorkerLocked(1, [] {
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
    // Idempotent: only (re)start when the single worker slot is free. After a
    // MAKCU timeout the discovery thread may still be unwinding; the DMA probe
    // is scheduled by Poll() as soon as the slot drains instead of blocking
    // the UI on a join.
    if (state_.stage == ProbeStage::Complete)
        return;
    if (workerState_.load(std::memory_order_acquire) != 0)
        return;
    state_.stage = ProbeStage::CheckingDma;
    stageStarted_ = std::chrono::steady_clock::now();
    std::clog << "[STARTUP][Hardware] stage=CheckingDma required=YES mode=PASSIVE_PNP timeout_ms="
              << timeouts_.dmaTimeout.count() << "\n";
    StartWorkerLocked(2, [] { return PassiveDmaPnpProbe(); });
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

    // PLAT-004 Fix: consume finished background work through the result
    // channel. Poll() never joins a live thread — a done worker is joined
    // only after it has signaled completion (cannot block the UI).
    if (workerState_.load(std::memory_order_acquire) == 2) {
        const int kind = workerKind_.load(std::memory_order_relaxed);
        const bool result = workerResult_.load(std::memory_order_relaxed);
        if (kind == 1) {            // MAKCU discovery result
            state_.makcuAvailable = result;
            state_.makcuFinished = true;
        } else if (kind == 2) {     // DMA PnP probe result (arrives any time)
            state_.dmaAvailable = result;
            state_.dmaFinished = true;
            state_.stage = ProbeStage::Complete;
            std::clog << "[STARTUP][Hardware] dma="
                      << (state_.dmaAvailable ? "PRESENT_PNP" : "NOT_DETECTED")
                      << " complete=YES note=PnP_is_diagnostic_only\n";
        }
        DrainWorkerLocked();
    }

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
        if (makcu_wrapper::IsConnected()) {
            state_.makcuAvailable = true;
            state_.makcuFinished = true;
            std::clog << "[STARTUP][Hardware] makcu=AVAILABLE\n";
            StartDmaProbeLocked();
        } else if (!state_.makcuFinished && now - stageStarted_ >= timeouts_.makcuTimeout) {
            // Timeout: request cooperative stop but never join from the UI
            // thread; the DMA probe starts as soon as the worker slot drains.
            if (worker_.joinable())
                worker_.request_stop();
            state_.makcuFinished = true;
            state_.makcuAvailable = false;
            std::clog << "[STARTUP][Hardware] makcu=TIMEOUT optional=YES continuing=DMA\n";
        }
        // The MAKCU thread may outlive the stage timeout (serial I/O is not
        // cancellable); promote to the DMA stage once the slot is free.
        if (state_.makcuFinished &&
            workerState_.load(std::memory_order_acquire) == 0 &&
            workerKind_.load(std::memory_order_relaxed) == 0)
            StartDmaProbeLocked();
        return;
    }

    ConsumeLateMakcuResultLocked();
    if (state_.stage == ProbeStage::CheckingDma) {
        // The probe was deferred while MAKCU discovery still occupied the
        // single worker slot; start it now that the slot is free.
        if (workerState_.load(std::memory_order_acquire) == 0 &&
            workerKind_.load(std::memory_order_relaxed) == 0 && !state_.dmaFinished)
            StartDmaProbeLocked();
        if (!state_.dmaFinished && now - stageStarted_ >= timeouts_.dmaTimeout) {
            if (worker_.joinable())
                worker_.request_stop();
            state_.dmaAvailable = false;
            state_.dmaFinished = true;
            state_.stage = ProbeStage::Complete;
            std::clog << "[STARTUP][Hardware] dma=TIMEOUT launcher_continues=YES note=PnP_probe_does_not_guarantee_functional_session\n";
        }
        if (state_.stage == ProbeStage::Complete)
            std::clog << "[STARTUP][Hardware] dma="
                      << (state_.dmaAvailable ? "PRESENT_PNP" : "NOT_DETECTED")
                      << " complete=YES note=PnP_is_diagnostic_only\n";
    }

    // Support devices connected after the launcher opens. Retries are bounded,
    // asynchronous and never block rendering or DMA discovery.
    if (state_.stage == ProbeStage::Complete && !makcu_wrapper::IsConnected()) {
        if (now >= nextMakcuRetry_ &&
            workerState_.load(std::memory_order_acquire) == 0) {
            state_.makcuFinished = false;
            nextMakcuRetry_ = now + 5s;
            StartWorkerLocked(1, [] {
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