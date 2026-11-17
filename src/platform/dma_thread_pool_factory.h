#pragma once

#include "idma_thread_pool.h"
#include "dma_thread_pool.h"
#include "platform/expected.h"
#include <memory>

namespace OmniGhost::Platform {

// =============================================================================
// DmaThreadPoolFactory - Factory for creating DmaThreadPool instances
// =============================================================================

class DmaThreadPoolFactory final {
public:
    Expected<std::unique_ptr<IDmaThreadPool>> Create(size_t threadCount = 0) {
        if (threadCount == 0) {
            threadCount = std::thread::hardware_concurrency();
        }
        return Success(std::make_unique<DmaThreadPool>(threadCount));
    }
};

} // namespace OmniGhost::Platform