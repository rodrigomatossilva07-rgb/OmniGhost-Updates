#include "dma_thread_pool.h"

namespace OmniGhost::Platform {

DmaThreadPool::DmaThreadPool(size_t threadCount) {
    // std::thread::hardware_concurrency() is allowed to return zero.
    if (threadCount == 0) {
        threadCount = 1;
    }

    stats_.startTime = std::chrono::steady_clock::now();
    workers_.reserve(threadCount);
    for (size_t index = 0; index < threadCount; ++index) {
        workers_.emplace_back([this] { WorkerThread(); });
    }
}

DmaThreadPool::~DmaThreadPool() {
    Shutdown();
}

} // namespace OmniGhost::Platform
