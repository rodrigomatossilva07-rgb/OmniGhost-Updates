#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <functional>
#include <atomic>
#include <optional>

namespace OmniGhost::Platform {

// Item 64: Event-based waiting instead of polling
// Provides efficient waiting with cancellation support

class EventWaiter {
public:
    EventWaiter() = default;
    ~EventWaiter() { NotifyAll(); }

    // Wait for notification or timeout
    // Returns true if notified, false if timeout
    bool WaitFor(std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
        std::unique_lock lock(mutex_);
        if (signaled_) return true;
        if (timeout == std::chrono::milliseconds::max()) {
            cv_.wait(lock, [this] { return signaled_.load(); });
            return true;
        }
        return cv_.wait_for(lock, timeout, [this] { return signaled_.load(); });
    }

    // Wait with predicate (spurious wakeup safe)
    template <typename Predicate>
    bool WaitFor(std::chrono::milliseconds timeout, std::function<bool()> predicate) {
        std::unique_lock lock(mutex_);
        if (predicate()) return true;
        if (timeout == std::chrono::milliseconds::max()) {
            cv_.wait(lock, [this] { return signaled_.load(); });
            return true;
        }
        return cv_.wait_for(lock, timeout, [this] { return signaled_.load() || predicate(); });
    }

    // Signal one waiter
    void NotifyOne() noexcept {
        {
            std::lock_guard lock(mutex_);
            signaled_.store(true, std::memory_order_release);
        }
        cv_.notify_one();
    }

    // Signal all waiters
    void NotifyAll() noexcept {
        {
            std::lock_guard lock(mutex_);
            signaled_.store(true, std::memory_order_release);
        }
        cv_.notify_all();
    }

    // Reset to unsignaled state
    void Reset() noexcept {
        std::lock_guard lock(mutex_);
        signaled_.store(false, std::memory_order_release);
    }

    // Check if signaled without waiting
    [[nodiscard]] bool IsSignaled() const noexcept {
        return signaled_.load(std::memory_order_acquire);
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> signaled_{false};
};

// ============================================================
// AsyncOperation - Run operation with callback on completion
// ============================================================

template <typename T>
class AsyncOperation {
public:
    using Callback = std::function<void(Result<T>)>;

    template <typename F>
    static void Run(std::function<Result<T>()> operation, std::function<void(Result<T>)> callback) {
        std::thread([operation = std::move(operation), callback = std::move(callback)]() mutable {
            try {
                Result<T> result = operation();
                callback(std::move(result));
            } catch (const std::exception& e) {
                callback(Err<T>(std::string("Exception: ") + e.what()));
            } catch (...) {
                callback(Err<T>("Unknown exception"));
            }
        }).detach();
    }
};

// Specialization for void
template <>
class AsyncOperation<void> {
public:
    using Callback = std::function<void(Result<void>)>;

    static void Run(std::function<Result<void>()> operation, std::function<void(Result<void>)> callback) {
        std::thread([operation = std::move(operation), callback = std::move(callback)]() mutable {
            try {
                Result<void> result = operation();
                callback(std::move(result));
            } catch (const std::exception& e) {
                callback(Err<void>(std::string("Exception: ") + e.what()));
            } catch (...) {
                callback(Err<void>("Unknown exception"));
            }
        }).detach();
    }
};

// ============================================================
// Cancellation-aware event waiter
// ============================================================

class CancellableEventWaiter {
public:
    using CancellationToken = std::shared_ptr<std::atomic<bool>>;

    explicit CancellableEventWaiter(std::shared_ptr<std::atomic<bool>> cancelToken = nullptr)
        : cancelToken_(std::move(cancelToken)) {}

    // Wait with cancellation support
    // Returns: true = signaled, false = timeout, nullopt = cancelled
    std::optional<bool> WaitFor(std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
        std::unique_lock lock(mutex_);

        auto predicate = [this]() -> bool {
            if (cancelToken_ && cancelToken_->load(std::memory_order_acquire)) {
                cancelled_ = true;
                return true;
            }
            return signaled_.load(std::memory_order_acquire);
        };

        if (timeout == std::chrono::milliseconds::max()) {
            cv_.wait(lock, [this] { return signaled_.load() || cancelled_; });
            if (cancelled_) return std::nullopt;
            return true;
        }

        bool result = cv_.wait_for(lock, timeout, [this] { return signaled_.load() || cancelled_; });
        if (cancelled_) return std::nullopt;
        return result;
    }

    void Notify() noexcept {
        {
            std::lock_guard lock(mutex_);
            signaled_.store(true, std::memory_order_release);
        }
        cv_.notify_all();
    }

    void Reset() noexcept {
        std::lock_guard lock(mutex_);
        signaled_.store(false, std::memory_order_release);
    }

    [[nodiscard]] bool IsSignaled() const noexcept {
        return signaled_.load(std::memory_order_acquire);
    }

    void SetCancellationToken(std::shared_ptr<std::atomic<bool>> token) {
        cancelToken_ = std::move(token);
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> signaled_{false};
    bool cancelled_ = false;
    std::shared_ptr<std::atomic<bool>> cancelToken_;
};

// ============================================================
// ScopedWaiter - RAII for waiting with automatic cleanup
// ============================================================

template <typename Waiter>
class ScopedWaiter {
public:
    using WaiterPtr = std::shared_ptr<Waiter>;

    explicit ScopedWaiter(WaiterPtr waiter, std::chrono::milliseconds timeout = std::chrono::milliseconds::max())
        : waiter_(std::move(waiter)), timeout_(timeout) {
        result_ = waiter_->WaitFor(timeout_);
    }

    ~ScopedWaiter() {
        if (waiter_) {
            waiter_->NotifyAll(); // Clean up any waiters
        }
    }

    [[nodiscard]] bool WasSignaled() const noexcept {
        return result_.has_value() && *result_;
    }

    [[nodiscard]] bool WasCancelled() const noexcept {
        return result_.has_value() && !*result_;
    }

    [[nodiscard]] bool TimedOut() const noexcept {
        return !result_.has_value();
    }

    [[nodiscard]] std::optional<bool> GetResult() const noexcept { return result_; }

private:
    std::shared_ptr<void> waiter_; // Type-erased
    std::chrono::milliseconds timeout_;
    std::optional<bool> result_;
};

} // namespace OmniGhost::Platform