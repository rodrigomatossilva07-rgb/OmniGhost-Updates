#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <mutex>
#include <unordered_map>

namespace OmniGhost::Platform {

// Item 65: Resource counters for threads, handles, memory
// Track before/after each session in development builds

class ResourceTracker {
public:
    struct Snapshot {
        uint32_t threadCount = 0;
        uint64_t handleCount = 0;
        uint64_t memoryBytes = 0;
        uint64_t gdiObjects = 0;
        uint64_t userObjects = 0;
        std::chrono::steady_clock::time_point timestamp;
    };

    static ResourceTracker& Instance() noexcept {
        static ResourceTracker instance;
        return instance;
    }

    // Take a snapshot of current resource usage
    Snapshot TakeSnapshot() noexcept;

    // Record a baseline for comparison
    void SetBaseline() noexcept {
        baseline_ = TakeSnapshot();
        hasBaseline_ = true;
    }

    // Compare current state to baseline
    struct Delta {
        int32_t threadDelta = 0;
        int64_t handleDelta = 0;
        int64_t memoryDelta = 0;
        int64_t gdiDelta = 0;
        int64_t userDelta = 0;
        bool hasLeak = false;
    };

    [[nodiscard]] std::optional<Delta> GetDelta() const noexcept {
        if (!hasBaseline_) return std::nullopt;
        Snapshot current = TakeSnapshot();
        Delta delta;
        delta.threadDelta = static_cast<int32_t>(current.threadCount) - static_cast<int32_t>(baseline_.threadCount);
        delta.handleDelta = static_cast<int64_t>(current.handleCount) - static_cast<int64_t>(baseline_.handleCount);
        delta.memoryDelta = static_cast<int64_t>(current.memoryBytes) - static_cast<int64_t>(baseline_.memoryBytes);
        delta.gdiDelta = static_cast<int64_t>(current.gdiObjects) - static_cast<int64_t>(baseline_.gdiObjects);
        delta.userDelta = static_cast<int64_t>(current.userObjects) - static_cast<int64_t>(baseline_.userObjects);
        delta.hasLeak = (delta.threadDelta > 0 || delta.handleDelta > 0 ||
                         delta.memoryDelta > 1024 * 1024 || // 1MB threshold
                         delta.gdiDelta > 0 || delta.userDelta > 0);
        return delta;
    }

    // Log current resource usage
    void LogUsage(std::string_view context) noexcept;

    // Reset baseline
    void Reset() noexcept {
        hasBaseline_ = false;
    }

    // Get current snapshot without modifying baseline
    [[nodiscard]] Snapshot GetCurrentSnapshot() const noexcept {
        return TakeSnapshot();
    }

private:
    ResourceTracker() = default;
    Snapshot baseline_;
    bool hasBaseline_ = false;
};

} // namespace OmniGhost::Platform