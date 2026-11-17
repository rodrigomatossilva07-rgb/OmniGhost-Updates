#pragma once

#include "platform/expected.h"
#include <future>
#include <chrono>
#include <functional>
#include <cstddef>

namespace OmniGhost::Platform {

// =============================================================================
// IDmaThreadPool - Interface for async DMA operations thread pool
// =============================================================================

struct DmaThreadPoolStats {
    size_t tasksSubmitted = 0;
    size_t tasksCompleted = 0;
    size_t tasksFailed = 0;
    std::chrono::steady_clock::time_point startTime;
};

class IDmaThreadPool {
public:
    virtual ~IDmaThreadPool() = default;

    // Submit a generic task
    template <typename F, typename... Args>
    auto Submit(F&& f, Args&&... args) 
        -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>> {
        return DoSubmit(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    }

    // Submit void task
    virtual std::future<void> SubmitVoid(std::function<void()> task) = 0;

    // Submit DMA read operation
    template <typename T>
    std::future<Expected<T>> SubmitDmaRead(std::function<Expected<T>()> readOp) {
        return DoSubmitDmaRead(std::move(readOp));
    }

    // Submit DMA write operation
    virtual std::future<Expected<void>> SubmitDmaWrite(std::function<Expected<void>()> writeOp) = 0;

    // Statistics
    [[nodiscard]] virtual DmaThreadPoolStats GetStats() const noexcept = 0;
    [[nodiscard]] virtual size_t QueueSize() const noexcept = 0;
    [[nodiscard]] virtual size_t ThreadCount() const noexcept = 0;

    // Control
    virtual void WaitForAll() = 0;
    virtual void Shutdown() = 0;
    [[nodiscard]] virtual bool IsStopped() const noexcept = 0;

protected:
    virtual std::future<void> DoSubmit(std::function<void()> task) = 0;
    
    template <typename T>
    virtual std::future<Expected<T>> DoSubmitDmaRead(std::function<Expected<T>()> readOp) = 0;
};

} // namespace OmniGhost::Platform