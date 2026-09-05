#include "performance_manager.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace CyberPerformance {

void PerformanceManager::Tick(float deltaTimeMs) noexcept {
    if (deltaTimeMs <= 0.0f || !std::isfinite(deltaTimeMs)) return;

    // Store frame time
    frameTimes_[frameIndex_] = deltaTimeMs;
    frameIndex_ = (frameIndex_ + 1) % frameTimes_.size();
    if (frameCount_ < frameTimes_.size()) frameCount_++;

    // Initialize timing on first frame
    if (firstFrame_) {
        firstFrame_ = false;
        lastAdjustment_ = std::chrono::steady_clock::now();
    }

    UpdateAverage();

    // Check cooldown
    const auto now = std::chrono::steady_clock::now();
    const float elapsedSec = std::chrono::duration<float>(now - lastAdjustment_).count();

    if (elapsedSec >= config_.adjustmentCooldownSec) {
        AdjustLevels();
        lastAdjustment_ = now;
    }
}

void PerformanceManager::UpdateAverage() noexcept {
    if (frameCount_ == 0) return;

    float sum = 0.0f;
    for (size_t i = 0; i < frameCount_; ++i) {
        sum += frameTimes_[i];
    }
    avgFrameTimeMs_.store(sum / static_cast<float>(frameCount_), std::memory_order_relaxed);

    // Update state based on average
    const float avg = avgFrameTimeMs_.load(std::memory_order_relaxed);
    if (avg >= config_.criticalThresholdMs) {
        state_.store(State::Critical, std::memory_order_relaxed);
    } else if (avg >= config_.warningThresholdMs) {
        state_.store(State::Warning, std::memory_order_relaxed);
    } else {
        state_.store(State::Optimal, std::memory_order_relaxed);
    }
}

void PerformanceManager::AdjustLevels() noexcept {
    if (forcedActive_) return;

    const State currentState = state_.load(std::memory_order_relaxed);
    [[maybe_unused]] const float avg = avgFrameTimeMs_.load(std::memory_order_relaxed);

    if (currentState == State::Critical) {
        // Aggressive reduction
        ReduceLevels();
        ReduceLevels(); // Double reduction for critical
    } else if (currentState == State::Warning) {
        // Moderate reduction
        ReduceLevels();
    } else if (currentState == State::Optimal) {
        // Gradual restore
        RestoreLevels();
    }
}

void PerformanceManager::ReduceLevels() noexcept {
    const float step = config_.reductionStep;

    current_.digitalRainDensity = std::max(0.0f, current_.digitalRainDensity - step);
    current_.particleDensity = std::max(0.0f, current_.particleDensity - step);
    current_.animationIntensity = std::max(0.0f, current_.animationIntensity - step);
    current_.constellationDensity = std::max(0.0f, current_.constellationDensity - step);
    current_.motionScale = std::max(0.3f, current_.motionScale - step * 0.5f); // Don't freeze completely
}

void PerformanceManager::RestoreLevels() noexcept {
    const float step = config_.restoreStep;

    current_.digitalRainDensity = std::min(1.0f, current_.digitalRainDensity + step);
    current_.particleDensity = std::min(1.0f, current_.particleDensity + step);
    current_.animationIntensity = std::min(1.0f, current_.animationIntensity + step);
    current_.constellationDensity = std::min(1.0f, current_.constellationDensity + step);
    current_.motionScale = std::min(1.0f, current_.motionScale + step * 0.5f);
}

void PerformanceManager::Reset() noexcept {
    current_ = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    forcedActive_ = false;
    frameIndex_ = 0;
    frameCount_ = 0;
    avgFrameTimeMs_.store(0.0f, std::memory_order_relaxed);
    state_.store(State::Optimal, std::memory_order_relaxed);
    firstFrame_ = true;
}

} // namespace CyberPerformance