#pragma once

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace OmniGhost::Hardware {

enum class ProbeStage {
    Idle,
    CheckingMakcu,
    CheckingDma,
    Complete
};

struct State {
    ProbeStage stage{ProbeStage::Idle};
    bool makcuFinished{false};
    bool makcuAvailable{false};
    bool dmaFinished{false};
    bool dmaAvailable{false};
};

struct TimeoutConfig {
    std::chrono::milliseconds makcuTimeout{6500};
    std::chrono::milliseconds dmaTimeout{2000};
};



class Manager final {
public:
    Manager() = default;
    ~Manager();
    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;

    void SetTimeouts(const TimeoutConfig& config) noexcept { timeouts_ = config; }
    [[nodiscard]] const TimeoutConfig& GetTimeouts() const noexcept { return timeouts_; }

    void BeginStartupProbe();
    void Shutdown() noexcept;
    void Poll();
    [[nodiscard]] State Snapshot() const;
    [[nodiscard]] bool Complete() const;

private:
    void StartDmaProbeLocked();
    void ConsumeLateMakcuResultLocked();
    void CancelWorkersLocked() noexcept;







    template<typename Op>
    void StartWorkerLocked(int kind, Op&& operation);
    void DrainWorkerLocked() noexcept;

    mutable std::mutex mutex_;
    State state_{};
    std::jthread worker_;
    std::atomic<int> workerState_{0};
    std::atomic<int> workerKind_{0};
    std::atomic<bool> workerResult_{false};
    std::chrono::steady_clock::time_point stageStarted_{};
    std::chrono::steady_clock::time_point nextMakcuRetry_{};
    TimeoutConfig timeouts_{};
};


template<typename Op>
void Manager::StartWorkerLocked(int kind, Op&& operation) {


    if (workerState_.load(std::memory_order_acquire) == 1)
        return;
    workerKind_.store(kind, std::memory_order_relaxed);
    workerResult_.store(false, std::memory_order_relaxed);
    workerState_.store(1, std::memory_order_release);
    worker_ = std::jthread([this, op = std::forward<Op>(operation)](std::stop_token st) {
        bool value = false;
        try {
            if (!st.stop_requested()) {
                if constexpr (std::is_invocable_v<Op, std::stop_token>)
                    value = op(st);
                else
                    value = op();
            }
        } catch (const std::exception& ex) {
            std::cerr << "[Hardware] Background task failed: " << ex.what() << "\n";
        } catch (...) {
            std::cerr << "[Hardware] Background task failed with unknown exception\n";
        }
        workerResult_.store(value, std::memory_order_release);
        workerState_.store(2, std::memory_order_release);
    });
}

}
