#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include "performance_manager.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <thread>

namespace OmniGhost::Platform {

// ============================================================
// ThreadPool Implementation
// ============================================================

bool ThreadPool::Initialize(const ThreadPoolConfig& config) {
    size_t threadCount = config.threadCount;
    if (threadCount == 0) {
        threadCount = std::thread::hardware_concurrency();
        if (threadCount == 0) threadCount = 4;
        if (threadCount > 1) threadCount -= 1;  // Leave one for main
    }

    stop_ = false;
    threads_.clear();
    threads_.reserve(threadCount);
    for (size_t i = 0; i < threadCount; ++i) {
        threads_.emplace_back(&ThreadPool::WorkerLoop, this);
    }

#if defined(_WIN32)
    if (config.priority != 0) {
        for (auto& t : threads_) {
            // Map simple priority levels if needed; ignore failures.
            (void)t;
            (void)THREAD_PRIORITY_NORMAL;
        }
    }
#else
    (void)config;
#endif

    return true;
}

void ThreadPool::Shutdown() {
    {
        std::lock_guard lock(queueMutex_);
        stop_ = true;
    }
    condition_.notify_all();
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();
}

void ThreadPool::WorkerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock lock(queueMutex_);
            condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
            if (stop_ && tasks_.empty()) return;
            if (tasks_.empty()) continue;
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        try {
            task();
        } catch (...) {
            // Swallow exceptions to not crash the pool
        }
    }
}

// ============================================================
// PerformanceManager Implementation
// ============================================================

bool PerformanceManager::Initialize() {
    // Initialize thread pools
    dmaPool_.Initialize(ThreadPoolConfig{0, THREAD_PRIORITY_HIGHEST, "DMA-High"});
    gamePool_.Initialize(ThreadPoolConfig{0, THREAD_PRIORITY_NORMAL, "Game-Normal"});
    uiPool_.Initialize(ThreadPoolConfig{0, THREAD_PRIORITY_NORMAL, "UI-Normal"});
    loggingPool_.Initialize(ThreadPoolConfig{1, THREAD_PRIORITY_LOWEST, "Logging-Low"});
    
    // Initialize frame history
    frameHistory_.fill(16.667f);
    dmaHistory_.fill(0.0f);
    
    // Set initial quality
    qualitySettings_.ApplyLevel(currentQuality_);
    
    return true;
}

void PerformanceManager::Shutdown() {
    dmaPool_.Shutdown();
    gamePool_.Shutdown();
    uiPool_.Shutdown();
    loggingPool_.Shutdown();
}

void PerformanceManager::BeginFrame() {
    frameStart_ = std::chrono::high_resolution_clock::now();
    lastFrame_ = {};
    lastFrame_.currentQuality = currentQuality_;
}

void PerformanceManager::EndFrame() {
    auto frameEnd = std::chrono::high_resolution_clock::now();
    
    // Calculate frame time
    auto frameDuration = frameEnd - frameStart_;
    lastFrame_.totalFrameMs = std::chrono::duration<float, std::milli>(frameDuration).count();
    
    // Calculate component times
    if (dmaEnd_ > dmaStart_) {
        lastFrame_.dmaReadMs = std::chrono::duration<float, std::milli>(dmaEnd_ - dmaStart_).count();
    }
    if (logicEnd_ > logicStart_) {
        lastFrame_.gameLogicMs = std::chrono::duration<float, std::milli>(logicEnd_ - logicStart_).count();
    }
    if (uiEnd_ > uiStart_) {
        lastFrame_.uiRenderMs = std::chrono::duration<float, std::milli>(uiEnd_ - uiStart_).count();
    }
    lastFrame_.overheadMs = lastFrame_.totalFrameMs - lastFrame_.dmaReadMs - lastFrame_.gameLogicMs - lastFrame_.uiRenderMs;
    if (lastFrame_.overheadMs < 0) lastFrame_.overheadMs = 0;
    
    // Budget check
    float dmaBudgetMs = frameBudget_.targetFrameMs * frameBudget_.dmaReadBudget;
    float logicBudgetMs = frameBudget_.targetFrameMs * frameBudget_.gameLogicBudget;
    float uiBudgetMs = frameBudget_.targetFrameMs * frameBudget_.uiRenderBudget;
    
    float totalBudgetUsage = (lastFrame_.dmaReadMs / dmaBudgetMs) * frameBudget_.dmaReadBudget +
                            (lastFrame_.gameLogicMs / logicBudgetMs) * frameBudget_.gameLogicBudget +
                            (lastFrame_.uiRenderMs / uiBudgetMs) * frameBudget_.uiRenderBudget;
    
    lastFrame_.budgetUsage = totalBudgetUsage;
    lastFrame_.budgetExceeded = totalBudgetUsage > 1.0f;
    
    // Update history
    frameHistory_[historyIndex_] = lastFrame_.totalFrameMs;
    dmaHistory_[historyIndex_] = lastFrame_.dmaReadMs;
    historyIndex_ = (historyIndex_ + 1) % frameHistory_.size();
    
    // Update adaptive quality
    if (adaptiveQualityEnabled_) {
        UpdateAdaptiveQuality();
    }
    
    // Update stats
    UpdateStats();
    
    // Callbacks
    if (lastFrame_.budgetExceeded && budgetExceededCallback_) {
        budgetExceededCallback_(lastFrame_.budgetUsage);
    }
}

void PerformanceManager::MarkDmaReadStart() {
    dmaStart_ = std::chrono::high_resolution_clock::now();
}

void PerformanceManager::MarkDmaReadEnd() {
    dmaEnd_ = std::chrono::high_resolution_clock::now();
}

void PerformanceManager::MarkGameLogicStart() {
    logicStart_ = std::chrono::high_resolution_clock::now();
}

void PerformanceManager::MarkGameLogicEnd() {
    logicEnd_ = std::chrono::high_resolution_clock::now();
}

void PerformanceManager::MarkUiRenderStart() {
    uiStart_ = std::chrono::high_resolution_clock::now();
}

void PerformanceManager::MarkUiRenderEnd() {
    uiEnd_ = std::chrono::high_resolution_clock::now();
}

void PerformanceManager::IncrementDmaReads(uint64_t count) noexcept {
    lastFrame_.dmaReadsCount += count;
}

void PerformanceManager::IncrementEntitiesProcessed(uint64_t count) noexcept {
    lastFrame_.entitiesProcessed += count;
}

void PerformanceManager::IncrementBonesProcessed(uint64_t count) noexcept {
    lastFrame_.bonesProcessed += count;
}

void PerformanceManager::SetQualityLevel(QualityLevel level) noexcept {
    if (level != currentQuality_) {
        QualityLevel old = currentQuality_;
        currentQuality_ = level;
        qualitySettings_.ApplyLevel(level);
        if (qualityChangeCallback_) {
            qualityChangeCallback_(old, level);
        }
    }
}

QualityLevel PerformanceManager::GetQualityLevel() const noexcept {
    return currentQuality_;
}

const QualitySettings& PerformanceManager::GetQualitySettings() const noexcept {
    return qualitySettings_;
}

void PerformanceManager::SetAdaptiveQualityEnabled(bool enabled) noexcept {
    adaptiveQualityEnabled_ = enabled;
}

bool PerformanceManager::IsAdaptiveQualityEnabled() const noexcept {
    return adaptiveQualityEnabled_;
}

void PerformanceManager::UpdateAdaptiveQuality() {
    if (lastFrame_.budgetUsage > frameBudget_.qualityCriticalThreshold) {
        // Aggressive reduction
        if (currentQuality_ < QualityLevel::Potato) {
            SetQualityLevel(static_cast<QualityLevel>(static_cast<uint8_t>(currentQuality_) + 1));
        }
    } else if (lastFrame_.budgetUsage > frameBudget_.qualityHighThreshold) {
        // Moderate reduction
        if (currentQuality_ < QualityLevel::Low) {
            SetQualityLevel(static_cast<QualityLevel>(static_cast<uint8_t>(currentQuality_) + 1));
        }
    } else if (lastFrame_.budgetUsage < frameBudget_.qualityRecoveryThreshold) {
        // Recovery
        if (currentQuality_ > QualityLevel::Ultra) {
            SetQualityLevel(static_cast<QualityLevel>(static_cast<uint8_t>(currentQuality_) - 1));
        }
    }
}

void PerformanceManager::UpdateStats() {
    std::lock_guard lock(statsMutex_);
    
    // Exponential moving average (alpha = 0.05 for ~20 frame window)
    constexpr float alpha = 0.05f;
    stats_.avgFrameMs = stats_.avgFrameMs * (1.0f - alpha) + lastFrame_.totalFrameMs * alpha;
    stats_.avgDmaMs = stats_.avgDmaMs * (1.0f - alpha) + lastFrame_.dmaReadMs * alpha;
    stats_.avgLogicMs = stats_.avgLogicMs * (1.0f - alpha) + lastFrame_.gameLogicMs * alpha;
    stats_.avgUiMs = stats_.avgUiMs * (1.0f - alpha) + lastFrame_.uiRenderMs * alpha;
    stats_.avgFps = 1000.0f / std::max(0.001f, stats_.avgFrameMs);
    
    // Update percentiles from history
    std::array<float, 120> sortedFrames = frameHistory_;
    std::array<float, 120> sortedDma = dmaHistory_;
    std::sort(sortedFrames.begin(), sortedFrames.end());
    std::sort(sortedDma.begin(), sortedDma.end());
    stats_.p99FrameMs = sortedFrames[static_cast<size_t>(119 * 0.99)];
    stats_.p99DmaMs = sortedDma[static_cast<size_t>(119 * 0.99)];
    
    stats_.currentQuality = currentQuality_;
    stats_.totalFrames++;
    if (lastFrame_.budgetExceeded) stats_.budgetExceededFrames++;
    if (lastFrame_.totalFrameMs > frameBudget_.targetFrameMs * 1.5f) stats_.framesDropped++;
}

FrameTiming PerformanceManager::GetLastFrameTiming() const noexcept {
    return lastFrame_;
}

PerformanceStats PerformanceManager::GetStats() const noexcept {
    std::lock_guard lock(statsMutex_);
    return stats_;
}

bool PerformanceManager::IsBudgetExceeded() const noexcept {
    return lastFrame_.budgetExceeded;
}

float PerformanceManager::GetBudgetUsage() const noexcept {
    return lastFrame_.budgetUsage;
}

} // namespace OmniGhost::Platform