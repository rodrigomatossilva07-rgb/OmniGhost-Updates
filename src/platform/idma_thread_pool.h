#pragma once

#include "platform/expected.h"
#include <future>
#include <chrono>
#include <functional>
#include <cstddef>

namespace OmniGhost::Platform {





struct DmaThreadPoolStats {
    size_t tasksSubmitted = 0;
    size_t tasksCompleted = 0;
    size_t tasksFailed = 0;
    std::chrono::steady_clock::time_point startTime;
};

class IDmaThreadPool {
public:
    virtual ~IDmaThreadPool() = default;


    virtual std::future<void> SubmitVoid(std::function<void()> task) = 0;


    virtual std::future<Expected<void>> SubmitDmaWrite(std::function<Expected<void>()> writeOp) = 0;


    [[nodiscard]] virtual DmaThreadPoolStats GetStats() const noexcept = 0;
    [[nodiscard]] virtual size_t QueueSize() const noexcept = 0;
    [[nodiscard]] virtual size_t ThreadCount() const noexcept = 0;


    virtual void WaitForAll() = 0;
    virtual void Shutdown() = 0;
    [[nodiscard]] virtual bool IsStopped() const noexcept = 0;

};

}
