#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <thread>

#include "session_log.h"

namespace OmniGhost::Platform {

struct WatchdogConfig {
    std::chrono::milliseconds warningThreshold{5000};
    std::chrono::milliseconds criticalThreshold{30000};
    bool enableLogging{true};
    bool enableCallbacks{true};
};

class Watchdog final {
public:
    using Callback = std::function<void(std::string_view stage, std::chrono::milliseconds elapsed)>;

    Watchdog() = default;
    ~Watchdog() { Stop(); }

    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;

    void Configure(const WatchdogConfig& config) noexcept {
        config_ = config;
    }

    void Start(std::string_view stage, Callback warningCb = {}, Callback criticalCb = {}) noexcept {
        std::scoped_lock lock(mutex_);
        if (running_) return;

        stage_ = stage;
        warningCallback_ = std::move(warningCb);
        criticalCallback_ = std::move(criticalCb);
        startTime_ = std::chrono::steady_clock::now();
        running_ = true;
        stopped_ = false;

        thread_ = std::thread([this] { WatchLoop(); });
    }

    void UpdateStage(std::string_view stage) noexcept {
        std::scoped_lock lock(mutex_);
        if (!running_) return;
        stage_ = stage;
    }

    void Stop() noexcept {
        {
            std::scoped_lock lock(mutex_);
            if (!running_) return;
            running_ = false;
            stopped_ = true;
        }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

    [[nodiscard]] bool IsRunning() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::chrono::milliseconds Elapsed() const noexcept {
        if (!running_.load(std::memory_order_acquire)) return {};
        const auto now = std::chrono::steady_clock::now();
        const auto start = startTime_.load(std::memory_order_acquire);
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
    }

private:
    void WatchLoop() noexcept {
        while (true) {
            std::unique_lock lock(mutex_);
            if (!running_) break;

            const auto now = std::chrono::steady_clock::now();
            const auto start = startTime_.load(std::memory_order_acquire);
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);

            const auto warningMs = config_.warningThreshold.count();
            const auto criticalMs = config_.criticalThreshold.count();

            if (elapsed.count() >= criticalMs) {
                if (config_.enableLogging) {
                    SessionLog::Write(
                        SessionLog::Severity::Error,
                        SessionLog::Subsystem::Watchdog,
                        "Operation exceeded critical threshold",
                        {SessionLog::Field{"stage", std::string(stage_), false},
                         SessionLog::Field{"elapsed_ms", std::to_string(elapsed.count()), false},
                         SessionLog::Field{"threshold_ms", std::to_string(criticalMs), false}});
                }
                if (config_.enableCallbacks && criticalCallback_) {
                    lock.unlock();
                    criticalCallback_(stage_, elapsed);
                }
                break;
            }

            if (elapsed.count() >= warningMs && !warningLogged_) {
                warningLogged_ = true;
                if (config_.enableLogging) {
                    SessionLog::Write(
                        SessionLog::Severity::Warning,
                        SessionLog::Subsystem::Watchdog,
                        "Operation exceeded warning threshold",
                        {SessionLog::Field{"stage", std::string(stage_), false},
                         SessionLog::Field{"elapsed_ms", std::to_string(elapsed.count()), false},
                         SessionLog::Field{"threshold_ms", std::to_string(warningMs), false}});
                }
                if (config_.enableCallbacks && warningCallback_) {
                    lock.unlock();
                    warningCallback_(stage_, elapsed);
                }
            }

            const auto waitMs = (std::min)(std::chrono::milliseconds(500),
                                           config_.warningThreshold);
            cv_.wait_for(lock, waitMs, [this] { return !running_; });
        }
    }

    WatchdogConfig config_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopped_{false};
    std::atomic<bool> warningLogged_{false};
    std::atomic<std::chrono::steady_clock::time_point> startTime_{};
    std::string stage_;
    Callback warningCallback_;
    Callback criticalCallback_;
};

class ScopedWatchdog final {
public:
    ScopedWatchdog(std::string_view stage, Watchdog& watchdog,
                   std::chrono::milliseconds warningThreshold = std::chrono::milliseconds(5000),
                   std::chrono::milliseconds criticalThreshold = std::chrono::milliseconds(30000))
        : watchdog_(watchdog), stage_(stage) {
        WatchdogConfig config;
        config.warningThreshold = warningThreshold;
        config.criticalThreshold = criticalThreshold;
        watchdog_.Configure(config);
        watchdog_.Start(stage_);
    }

    ~ScopedWatchdog() {
        watchdog_.Stop();
    }

    void UpdateStage(std::string_view stage) noexcept {
        stage_ = stage;
        watchdog_.UpdateStage(stage);
    }

private:
    Watchdog& watchdog_;
    std::string stage_;
};

} // namespace OmniGhost::Platform