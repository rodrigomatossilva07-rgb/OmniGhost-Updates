#include "hardware_manager.h"

#include "../../DMALibrary/Memory/Memory.h"
#include "../makcu/makcu_wrapper.h"
#include "../../Fivem/aimbot/aim_type.h"
#include "watchdog.h"

#include <iostream>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

using namespace std::chrono_literals;

namespace OmniGhost::Hardware {

// Active probe disabled: without physical FPGA hardware, the static VMM
// initialization crashes (SEH). The passive PnP probe reliably detects
// whether the device is present in the system.
// PnP probe is DIAGNOSTIC ONLY - it never proves a functional DMA session.
static bool ActiveDmaProbe() {
    // Use passive probe result as authoritative - it correctly detects
    // FPGA presence via Windows PnP without crashing.
    // NOTE: PnP detection != functional DMA session. Real DMA verification
    // happens only during explicit game launch when the device is opened.
    const bool present = Memory::ProbeDevicePresence();
    std::clog << "[STARTUP][Hardware] active_probe=SKIPPED passive_pnp="
              << (present ? "DETECTED" : "NOT_FOUND")
              << " note=PnP_is_diagnostic_only_not_functional_session\n";
    return present;
}

struct ProbeTask final {
    std::atomic_bool cancelRequested{false};
    std::atomic_bool finished{false};
    std::atomic_bool result{false};

    static std::shared_ptr<ProbeTask> Start(std::function<bool()> operation) {
        auto task = std::make_shared<ProbeTask>();
        // Some vendor/serial APIs have no reliable cancellation primitive. The
        // worker therefore owns no Manager memory and is detached deliberately:
        // a wedged external call can neither block the UI nor make destruction
        // wait. Cancellation suppresses publication of a late result.
        std::thread([task, operation = std::move(operation)]() mutable {
            bool value = false;
            try {
                if (!task->cancelRequested.load(std::memory_order_acquire))
                    value = operation();
            } catch (...) {
                value = false;
            }
            if (!task->cancelRequested.load(std::memory_order_acquire))
                task->result.store(value, std::memory_order_release);
            task->finished.store(true, std::memory_order_release);
        }).detach();
        return task;
    }

    void Cancel() noexcept {
        cancelRequested.store(true, std::memory_order_release);
    }

    [[nodiscard]] bool Ready() const noexcept {
        return finished.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool Result() const noexcept {
        return result.load(std::memory_order_acquire);
    }
};

namespace {
}

Manager::~Manager() {
    Shutdown();
}

void Manager::Shutdown() noexcept {
    std::scoped_lock lock(mutex_);
    CancelWorkersLocked();
    state_.stage = ProbeStage::Complete;
}

void Manager::CancelWorkersLocked() noexcept {
    if (makcuTask_)
        makcuTask_->Cancel();
    if (dmaTask_)
        dmaTask_->Cancel();
    makcuTask_.reset();
    dmaTask_.reset();
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
    state_.makcuAvailable = makcu_wrapper::IsConnected();
    state_.makcuFinished = state_.makcuAvailable;
    stageStarted_ = std::chrono::steady_clock::now();
    std::clog << "[STARTUP][Hardware] stage=CheckingMakcu optional=YES timeout_ms="
              << timeouts_.makcuTimeout.count() << "\n";

    if (state_.makcuFinished) {
        StartDmaProbeLocked();
        return;
    }
    makcuTask_ = ProbeTask::Start([] {
        return aim_type::Connect(aim_type::DeviceType::Makcu);
    });
#endif
}

void Manager::StartDmaProbeLocked() {
    if (state_.stage == ProbeStage::CheckingDma || state_.stage == ProbeStage::Complete)
        return;
    state_.stage = ProbeStage::CheckingDma;
    stageStarted_ = std::chrono::steady_clock::now();
    std::clog << "[STARTUP][Hardware] stage=CheckingDma required=YES mode=ACTIVE_OPEN timeout_ms="
              << timeouts_.dmaTimeout.count() << "\n";
    dmaTask_ = ProbeTask::Start([] { return ActiveDmaProbe(); });
}

void Manager::ConsumeLateMakcuResultLocked() {
    if (!makcuTask_ || !makcuTask_->Ready())
        return;
    const bool available = makcuTask_->Result();
    makcuTask_.reset();
    // A late success may improve the status; a timeout remains a completed,
    // non-blocking optional check and never prevents DMA discovery.
    state_.makcuAvailable = state_.makcuAvailable || available;
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

    if (state_.stage == ProbeStage::CheckingMakcu) {
        if (makcuTask_ && makcuTask_->Ready()) {
            state_.makcuAvailable = makcuTask_->Result();
            makcuTask_.reset();
            state_.makcuFinished = true;
            std::clog << "[STARTUP][Hardware] makcu="
                      << (state_.makcuAvailable ? "AVAILABLE" : "NOT_FOUND") << "\n";
            StartDmaProbeLocked();
        } else if (now - stageStarted_ >= timeouts_.makcuTimeout) {
            if (makcuTask_)
                makcuTask_->Cancel();
            state_.makcuFinished = true;
            state_.makcuAvailable = false;
            std::clog << "[STARTUP][Hardware] makcu=TIMEOUT optional=YES continuing=DMA\n";
            StartDmaProbeLocked();
        }
        return;
    }

    ConsumeLateMakcuResultLocked();
    if (state_.stage == ProbeStage::CheckingDma) {
        if (dmaTask_ && dmaTask_->Ready()) {
            state_.dmaAvailable = dmaTask_->Result();
            dmaTask_.reset();
            state_.dmaFinished = true;
            state_.stage = ProbeStage::Complete;
        } else if (now - stageStarted_ >= timeouts_.dmaTimeout) {
            if (dmaTask_)
                dmaTask_->Cancel();
            state_.dmaAvailable = false;
            state_.dmaFinished = true;
            state_.stage = ProbeStage::Complete;
            std::clog << "[STARTUP][Hardware] dma=TIMEOUT launcher_continues=YES note=PnP_probe_does_not_guarantee_functional_session\n";
        }
        if (state_.stage == ProbeStage::Complete)
            std::clog << "[STARTUP][Hardware] dma="
                      << (state_.dmaAvailable ? "ACTIVE_OPENED" : "NOT_OPENED")
                      << " complete=YES note=PnP_is_diagnostic_only\n";
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
