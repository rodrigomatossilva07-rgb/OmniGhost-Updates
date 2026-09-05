#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>

namespace OmniGhost::Hardware {

struct ProbeTask;

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
    std::chrono::milliseconds makcuTimeout{2500};
    std::chrono::milliseconds dmaTimeout{2000};
};

// Sole owner of startup hardware discovery. Rendering code reads State only and
// never talks to serial, SetupAPI, LeechCore or VMMDLL directly.
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

    mutable std::mutex mutex_;
    State state_{};
    std::shared_ptr<ProbeTask> makcuTask_;
    std::shared_ptr<ProbeTask> dmaTask_;
    std::chrono::steady_clock::time_point stageStarted_{};
    TimeoutConfig timeouts_{};
};

} // namespace OmniGhost::Hardware
