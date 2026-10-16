#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>

namespace CyberPerformance {



class PerformanceManager final {
public:
    struct Config {

        float targetFrameTimeMs = 16.67f;

        float warningThresholdMs = 20.0f;

        float criticalThresholdMs = 33.33f;

        size_t smoothingFrames = 60;

        float adjustmentCooldownSec = 2.0f;

        float reductionStep = 0.15f;

        float restoreStep = 0.08f;
    };

    struct EffectLevels {
        float digitalRainDensity = 1.0f;
        float particleDensity = 1.0f;
        float animationIntensity = 1.0f;
        float constellationDensity = 1.0f;
        float motionScale = 1.0f;
    };

    PerformanceManager() = default;
    ~PerformanceManager() = default;

    PerformanceManager(const PerformanceManager&) = delete;
    PerformanceManager& operator=(const PerformanceManager&) = delete;


    void Configure(const Config& config) noexcept { config_ = config; }


    void Tick(float deltaTimeMs) noexcept;


    [[nodiscard]] EffectLevels GetEffectLevels() const noexcept {
        return current_;
    }


    [[nodiscard]] float GetAverageFrameTimeMs() const noexcept {
        return avgFrameTimeMs_.load(std::memory_order_relaxed);
    }


    enum class State { Optimal, Warning, Critical };
    [[nodiscard]] State GetState() const noexcept {
        return state_.load(std::memory_order_relaxed);
    }


    void SetForcedLevels(const EffectLevels& levels) noexcept {
        forced_ = levels;
        forcedActive_ = true;
    }

    void ClearForcedLevels() noexcept {
        forcedActive_ = false;
    }


    void Reset() noexcept;


    [[nodiscard]] bool IsAutoScaling() const noexcept {
        return !forcedActive_;
    }

private:
    Config config_;
    EffectLevels current_ = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    EffectLevels forced_ = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    std::atomic<bool> forcedActive_{false};


    std::array<float, 256> frameTimes_{};
    size_t frameIndex_ = 0;
    size_t frameCount_ = 0;
    std::atomic<float> avgFrameTimeMs_{0.0f};
    std::atomic<State> state_{State::Optimal};


    std::chrono::steady_clock::time_point lastAdjustment_;
    bool firstFrame_ = true;

    void UpdateAverage() noexcept;
    void AdjustLevels() noexcept;
    void ReduceLevels() noexcept;
    void RestoreLevels() noexcept;
};


inline PerformanceManager g_performanceManager;

}