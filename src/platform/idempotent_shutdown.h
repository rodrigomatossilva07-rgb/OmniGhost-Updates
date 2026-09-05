#pragma once

#include <functional>
#include <mutex>
#include <vector>
#include <atomic>
#include <string>
#include <string_view>

namespace OmniGhost::Platform {

// Item 56: Idempotent Shutdown Manager
// Guarantees: calling Shutdown() twice doesn't block or double-free resources

struct ShutdownAction {
    int priority;           // Higher = runs first
    std::string name;       // For logging/debugging
    std::function<void()> callback;
    bool required = false;  // If true, failure is logged but doesn't stop other shutdowns
};

class IdempotentShutdownManager {
public:
    using Callback = std::function<void()>;

    IdempotentShutdownManager() = default;
    ~IdempotentShutdownManager() { ShutdownAll("destructor"); }

    // Register a shutdown callback with priority
    // Higher priority = runs earlier
    bool Register(const std::string& name, Callback callback, int priority = 0, bool required = false) {
        std::lock_guard lock(mutex_);
        if (shutdownStarted_) {
            // Late registration - execute immediately if already shutting down
            if (shutdownComplete_) {
                return false;
            }
        }
        registrations_.push_back({name, priority, std::move(callback), false});
        return true;
    }

    // Unregister a previously registered callback
    bool Unregister(const std::string& name) {
        std::lock_guard lock(mutex_);
        auto it = std::find_if(registrations_.begin(), registrations_.end(),
            [&](const ShutdownAction& a) { return a.name == name; });
        if (it != registrations_.end()) {
            registrations_.erase(it);
            return true;
        }
        return false;
    }

    // Execute all shutdown callbacks in priority order (highest first)
    // Idempotent: safe to call multiple times
    bool ShutdownAll(std::string_view reason) noexcept {
        std::vector<ShutdownAction> toExecute;

        {
            std::lock_guard lock(mutex_);
            if (shutdownComplete_) {
                // Already fully shut down - idempotent
                return true;
            }
            if (shutdownStarted_) {
                // Already in progress - wait for completion
                return WaitForCompletion();
            }

            shutdownStarted_ = true;
            shutdownReason_ = reason;

            // Copy and sort by priority (highest first)
            toExecute = std::move(registrations_);
            std::sort(toExecute.begin(), toExecute.end(),
                [](const ShutdownAction& a, const ShutdownAction& b) {
                    return a.priority > b.priority;
                });
        }

        // Execute outside lock to avoid deadlocks
        for (auto& action : toExecute) {
            ExecuteAction(action, reason);
        }

        {
            std::lock_guard lock(mutex_);
            shutdownComplete_ = true;
            shutdownComplete_.notify_all();
        }
        return true;
    }

    // Check if shutdown has been initiated
    [[nodiscard]] bool IsShutdownStarted() const noexcept {
        std::lock_guard lock(mutex_);
        return shutdownStarted_;
    }

    // Check if shutdown has completed
    [[nodiscard]] bool IsShutdownComplete() const noexcept {
        std::lock_guard lock(mutex_);
        return shutdownComplete_;
    }

    // Get current shutdown reason
    [[nodiscard]] std::string_view GetShutdownReason() const noexcept {
        std::lock_guard lock(mutex_);
        return shutdownReason_;
    }

    // Wait for shutdown to complete (with optional timeout)
    bool WaitForCompletion(std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) const {
        std::unique_lock lock(mutex_);
        return shutdownComplete_.wait_for(lock, [this] { return shutdownComplete_; });
    }

    // Get count of registered shutdown actions
    [[nodiscard]] size_t RegisteredCount() const noexcept {
        std::lock_guard lock(mutex_);
        return registrations_.size();
    }

    // Reset for testing (only in debug builds)
#if defined(_DEBUG) || defined(DEBUG)
    void ResetForTesting() {
        std::lock_guard lock(mutex_);
        registrations_.clear();
        shutdownStarted_ = false;
        shutdownComplete_ = false;
        shutdownReason_.clear();
    }
#endif

private:
    void ExecuteAction(const ShutdownAction& action, std::string_view reason) noexcept {
        try {
            SessionLog::Write(
                SessionLog::Severity::Info,
                SessionLog::Subsystem::Core,
                "Shutdown: " + action.name,
                {{"reason", std::string(reason)}, {"priority", std::to_string(action.priority)}}
            );

            action.callback();

            SessionLog::Write(
                SessionLog::Severity::Info,
                SessionLog::Subsystem::Core,
                "Shutdown complete: " + action.name,
                {{"reason", std::string(reason)}}
            );
        } catch (const std::exception& e) {
            const std::string msg = "Shutdown action '" + action.name + "' threw: " + e.what();
            SessionLog::Write(
                SessionLog::Severity::Error,
                SessionLog::Subsystem::Core,
                "Shutdown action failed",
                {{"action", action.name}, {"error", msg}, {"reason", std::string(reason)}}
            );
            // Don't rethrow - continue with other shutdown actions
        } catch (...) {
            SessionLog::Write(
                SessionLog::Severity::Error,
                SessionLog::Subsystem::Core,
                "Shutdown action failed with unknown exception",
                {{"action", action.name}, {"reason", std::string(reason)}}
            );
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable shutdownComplete_;
    std::vector<ShutdownAction> registrations_;
    std::atomic<bool> shutdownStarted_{false};
    std::atomic<bool> shutdownComplete_{false};
    std::string shutdownReason_;
};

} // namespace OmniGhost::Platform