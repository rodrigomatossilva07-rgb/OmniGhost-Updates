#pragma once

#include "result.h"
#include "interfaces.h"
#include <chrono>
#include <functional>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unordered_map>

namespace OmniGhost::Platform {

// ============================================================
// TimeoutManager - Configurable timeouts for SDK operations
// Item 51: Configurable timeouts for SDK operations
// ============================================================

class TimeoutManager {
public:
    struct Config {
        std::chrono::milliseconds dmaDeviceOpen{5000};
        std::chrono::milliseconds dmaDeviceDataPath{10000};
        std::chrono::milliseconds vmmInitialization{15000};
        std::chrono::milliseconds vmmSessionRecovery{30000};
        std::chrono::milliseconds pluginInitialization{10000};
        std::chrono::milliseconds processAttach{10000};
        std::chrono::milliseconds processInfoGeneration{120000};
        std::chrono::milliseconds processInfoZeroStall{15000};
        std::chrono::milliseconds processInfoHeartbeat{5000};
        std::chrono::milliseconds processInfoPoll{500};
        std::chrono::milliseconds processInfoSettle{1500};
        std::chrono::milliseconds procinfoTimeout{100};
        std::chrono::milliseconds procinfoZeroStallMs{15000};
        std::chrono::milliseconds procinfoHeartbeatMs{5000};
        std::chrono::milliseconds procinfoPollMs{500};
        std::chrono::milliseconds procinfoSettleMs{1500};
        std::chrono::milliseconds dmaOperationTimeout{30000};
        std::chrono::milliseconds networkRequest{10000};
        std::chrono::milliseconds fileOperation{5000};
        std::chrono::milliseconds authentication{0};  // 0 = no timeout
    };

    explicit TimeoutManager(const Config& config = {}) : config_(config) {}

    [[nodiscard]] const Config& GetConfig() const noexcept { return config_; }

    // Run an operation with timeout, supports cancellation
    template <typename F>
    [[nodiscard]] Result<void> RunWithTimeout(
        const std::string& operationName,
        std::chrono::milliseconds timeout,
        F&& operation,
        std::shared_ptr<std::atomic<bool>> cancellationToken = nullptr
    ) {
        std::promise<Result<void>> promise;
        auto future = promise.get_future();

        std::thread worker([&]() {
            try {
                auto result = operation();
                promise.set_value(std::move(result));
            } catch (const std::exception& e) {
                promise.set_value(Err<void>(std::string("Exception: ") + e.what()));
            } catch (...) {
                promise.set_value(Err<void>("Unknown exception"));
            }
        });

        std::chrono::milliseconds actualTimeout = timeout.count() > 0 ? timeout : GetDefaultTimeout(operationName);

        std::future_status status = future.wait_for(actualTimeout);
        if (status == std::future_status::timeout) {
            if (cancellationToken) {
                cancellationToken->store(true, std::memory_order_release);
            }
            if (worker_.joinable()) {
                worker_.detach(); // Best effort - thread will exit when it checks cancellation
            }
            LogTimeout(operationName, actualTimeout);
            return Err<void>(ErrorCode::Timeout, "Operation timed out: " + operationName);
        }

        if (worker_.joinable()) {
            worker_.join();
        }

        auto result = future.get();
        if (result.IsErr()) {
            LogError(operationName, result.UnwrapErr().message);
        }
        return result;
    }

    // Synchronous version with default timeout from config
    template <typename F>
    [[nodiscard]] Result<void> RunWithDefaultTimeout(
        const std::string& operationName,
        F&& operation,
        std::shared_ptr<std::atomic<bool>> cancellationToken = nullptr
    ) {
        return RunWithTimeout(operationName, GetDefaultTimeout(operationName), std::forward<F>(operation), cancellationToken);
    }

    [[nodiscard]] std::chrono::milliseconds GetDefaultTimeout(const std::string& operationName) const noexcept {
        static const std::unordered_map<std::string, std::chrono::milliseconds> defaults = {
            {"dma_device_open", config_.dmaDeviceOpen},
            {"dma_data_path", config_.dmaDeviceDataPath},
            {"vmm_init", config_.vmmInitialization},
            {"vmm_recovery", config_.vmmSessionRecovery},
            {"plugin_init", config_.pluginInitialization},
            {"process_attach", config_.processAttach},
            {"procinfo_generation", config_.processInfoGeneration},
            {"procinfo_zero_stall", config_.processInfoZeroStall},
            {"procinfo_heartbeat", config_.processInfoHeartbeat},
            {"procinfo_poll", config_.processInfoPoll},
            {"procinfo_settle", config_.processInfoSettle},
            {"dma_operation", config_.dmaOperationTimeout},
            {"network_request", config_.networkRequest},
            {"file_operation", config_.fileOperation},
            {"authentication", config_.authentication},
        };

        auto it = defaults.find(operationName);
        return it != defaults.end() ? it->second : std::chrono::milliseconds(30000);
    }

    void UpdateConfig(const Config& newConfig) {
        config_ = newConfig;
    }

private:
    Config config_;
    std::thread worker_;

    void LogTimeout(const std::string& operation, std::chrono::milliseconds timeout) {
        SessionLog::Write(
            SessionLog::Severity::Warning,
            SessionLog::Subsystem::Core,
            "Operation timeout",
            {{"operation", operation}, {"timeout_ms", std::to_string(timeout.count())}}
        );
    }

    void LogError(const std::string& operation, const std::string& error) {
        SessionLog::Write(
            SessionLog::Severity::Error,
            SessionLog::Subsystem::Core,
            "Operation failed",
            {{"operation", operation}, {"error", error}}
        );
    }
};

// ============================================================
// CancellationToken - Cooperative cancellation support
// ============================================================

class CancellationToken {
public:
    CancellationToken() = default;
    explicit CancellationToken(std::shared_ptr<std::atomic<bool>> token) : token_(std::move(token)) {}

    [[nodiscard]] bool IsCancelled() const noexcept {
        return token_ && token_->load(std::memory_order_acquire);
    }

    void Cancel() noexcept {
        if (token_) token_->store(true, std::memory_order_release);
    }

    void Reset() noexcept {
        if (token_) token_->store(false, std::memory_order_release);
    }

    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(token_); }

    [[nodiscard]] std::shared_ptr<std::atomic<bool>> GetShared() const noexcept { return token_; }

    static CancellationToken Create() {
        return CancellationToken(std::make_shared<std::atomic<bool>>(false));
    }

    static CancellationToken None() { return CancellationToken(); }

private:
    std::shared_ptr<std::atomic<bool>> token_;
};

// ============================================================
// Watchdog - Detects stuck operations
// Item 53: Internal watchdog for slow operations
// ============================================================

class Watchdog {
public:
    struct WatchInfo {
        std::string operation;
        std::chrono::steady_clock::time_point startTime;
        std::chrono::milliseconds timeout;
        std::thread::id threadId;
        std::function<void(const std::string&)> onTimeout;
    };

    Watchdog() = default;
    ~Watchdog() { Stop(); }

    void Start(const std::string& operation, std::chrono::milliseconds timeout,
               std::function<void(const std::string&)> onTimeout = {}) {
        std::lock_guard lock(mutex_);
        WatchInfo info;
        info.operation = operation;
        info.startTime = std::chrono::steady_clock::now();
        info.timeout = timeout;
        info.threadId = std::this_thread::get_id();
        info.onTimeout = std::move(onTimeout);
        watches_.emplace_back(std::move(info));

        if (!running_) {
            running_ = true;
            watcher_ = std::thread(&Watchdog::WatchLoop, this);
        }
    }

    void Stop() {
        {
            std::lock_guard lock(mutex_);
            running_ = false;
            cv_.notify_all();
        }
        if (watcher_.joinable()) {
            watcher_.join();
        }
    }

    void CheckIn(const std::string& operation) {
        std::lock_guard lock(mutex_);
        for (auto& watch : watches_) {
            if (watch.operation == operation) {
                watch.startTime = std::chrono::steady_clock::now();
                break;
            }
        }
    }

    void Remove(const std::string& operation) {
        std::lock_guard lock(mutex_);
        watches_.erase(
            std::remove_if(watches_.begin(), watches_.end(),
                [&](const WatchInfo& w) { return w.operation == operation; }),
            watches_.end()
        );
    }

private:
    void WatchLoop() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::vector<WatchInfo> timedOut;

            {
                std::lock_guard lock(mutex_);
                auto now = std::chrono::steady_clock::now();
                for (auto& watch : watches_) {
                    if (now - watch.startTime > watch.timeout) {
                        timedOut.push_back(watch);
                    }
                }
            }

            for (const auto& watch : timedOut) {
                SessionLog::Write(
                    SessionLog::Severity::Warning,
                    SessionLog::Subsystem::Core,
                    "Watchdog timeout",
                    {{"operation", watch.operation}, {"timeout_ms", std::to_string(watch.timeout.count())}}
                );
                if (watch.onTimeout) {
                    watch.onTimeout(watch.operation);
                }
                Remove(watch.operation);
            }
        }

        {
            std::unique_lock lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(100), [this] { return !running_; });
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<WatchInfo> watches_;
    std::thread watcher_;
    bool running_ = false;
};

} // namespace OmniGhost::Platform