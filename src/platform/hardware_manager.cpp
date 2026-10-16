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





static bool PassiveDmaPnpProbe() {



    const bool present = mem.ProbeDeviceAvailability();
    std::clog << "[STARTUP][Hardware] active_probe=SKIPPED passive_pnp="
              << (present ? "DETECTED" : "NOT_FOUND")
              << " note=PnP_is_diagnostic_only_not_functional_session\n";
    return present;
}

namespace {

}

Manager::~Manager() {
    Shutdown();
}

void Manager::Shutdown() noexcept {



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
        worker_.join();
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



    state_.makcuAvailable = makcu_wrapper::IsConnected();
    state_.makcuFinished = state_.makcuAvailable;
    stageStarted_ = std::chrono::steady_clock::now();
    nextMakcuRetry_ = stageStarted_ + 5s;
    if (!state_.makcuAvailable) {

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



    (void)0;
}

void Manager::Poll() {
    std::scoped_lock lock(mutex_);
    const auto now = std::chrono::steady_clock::now();

#if defined(OMNIGHOST_MOCK_HARDWARE)


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




    if (workerState_.load(std::memory_order_acquire) == 2) {
        const int kind = workerKind_.load(std::memory_order_relaxed);
        const bool result = workerResult_.load(std::memory_order_relaxed);
        if (kind == 1) {
            state_.makcuAvailable = result;
            state_.makcuFinished = true;
        } else if (kind == 2) {
            state_.dmaAvailable = result;
            state_.dmaFinished = true;
            state_.stage = ProbeStage::Complete;
            std::clog << "[STARTUP][Hardware] dma="
                      << (state_.dmaAvailable ? "PRESENT_PNP" : "NOT_DETECTED")
                      << " complete=YES note=PnP_is_diagnostic_only\n";
        }
        DrainWorkerLocked();
    }



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


            if (worker_.joinable())
                worker_.request_stop();
            state_.makcuFinished = true;
            state_.makcuAvailable = false;
            std::clog << "[STARTUP][Hardware] makcu=TIMEOUT optional=YES continuing=DMA\n";
        }


        if (state_.makcuFinished &&
            workerState_.load(std::memory_order_acquire) == 0 &&
            workerKind_.load(std::memory_order_relaxed) == 0)
            StartDmaProbeLocked();
        return;
    }

    ConsumeLateMakcuResultLocked();
    if (state_.stage == ProbeStage::CheckingDma) {


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

}