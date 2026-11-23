#include "dma_thread_pool.h"

namespace OmniGhost::Platform {

DmaThreadPool::DmaThreadPool(size_t threadCount) {

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

}
