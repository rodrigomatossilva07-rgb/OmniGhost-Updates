#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>

namespace CyberPerformance {

// Dynamic performance scaling based on frame time.
// Automatically reduces visual effects when frame time exceeds target.
class PerformanceManager final {
public:
    struct Config {
        // Target frame time in milliseconds (e.g., 16.67 for 60 FPS, 8.33 for 120 FPS)
        float targetFrameTimeMs = 16.67f;
        // Upper threshold - when exceeded, start reducing effects
        float warningThresholdMs = 20.0f;
        // Critical threshold - aggressive reduction
        float criticalThresholdMs = 33.33f;
        // How many frames to average for smoothing
        size_t smoothingFrames = 60;
        // Minimum time between adjustments (seconds)
        float adjustmentCooldownSec = 2.0f;
        // How much to reduce per step (0.0 - 1.0)
        float reductionStep = 0.15f;
        // How much to restore per step (0.0 - 1.0)
        float restoreStep = 0.08f;
    };

    struct EffectLevels {
        float digitalRainDensity = 1.0f;    // 0.0 = off, 1.0 = full
        float particleDensity = 1.0f;       // 0.0 = off, 1.0 = full
        float animationIntensity = 1.0f;    // 0.0 = off, 1.0 = full
        float constellationDensity = 1.0f;  // 0.0 = off, 1.0 = full
        float motionScale = 1.0f;           // 0.0 = frozen, 1.0 = full speed
    };

    PerformanceManager() = default;
    ~PerformanceManager() = default;

    PerformanceManager(const PerformanceManager&) = delete;
    PerformanceManager& operator=(const PerformanceManager&) = delete;

    // Initialize with configuration
    void Configure(const Config& config) noexcept { config_ = config; }

    // Call once per frame with current delta time
    void Tick(float deltaTimeMs) noexcept;

    // Get current effect levels (0.0 - 1.0)
    [[nodiscard]] EffectLevels GetEffectLevels() const noexcept {
        return current_;
    }

    // Get current average frame time
    [[nodiscard]] float GetAverageFrameTimeMs() const noexcept {
        return avgFrameTimeMs_.load(std::memory_order_relaxed);
    }

    // Get current performance state
    enum class State { Optimal, Warning, Critical };
    [[nodiscard]] State GetState() const noexcept {
        return state_.load(std::memory_order_relaxed);
    }

    // Force a specific level (for testing or user override)
    void SetForcedLevels(const EffectLevels& levels) noexcept {
        forced_ = levels;
        forcedActive_ = true;
    }

    void ClearForcedLevels() noexcept {
        forcedActive_ = false;
    }

    // Reset to defaults
    void Reset() noexcept;

    // Check if currently auto-scaling (not forced)
    [[nodiscard]] bool IsAutoScaling() const noexcept {
        return !forcedActive_;
    }

private:
    Config config_;
    EffectLevels current_ = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    EffectLevels forced_ = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    std::atomic<bool> forcedActive_{false};

    // Frame time tracking
    std::array<float, 256> frameTimes_{};
    size_t frameIndex_ = 0;
    size_t frameCount_ = 0;
    std::atomic<float> avgFrameTimeMs_{0.0f};
    std::atomic<State> state_{State::Optimal};

    // Adjustment tracking
    std::chrono::steady_clock::time_point lastAdjustment_;
    bool firstFrame_ = true;

    void UpdateAverage() noexcept;
    void AdjustLevels() noexcept;
    void ReduceLevels() noexcept;
    void RestoreLevels() noexcept;
};

// Global instance
inline PerformanceManager g_performanceManager;

} // namespace CyberPerformance