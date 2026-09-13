#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

namespace OmniGhost::Gameplay {

// Drift-free cadence for acquisition workers.  Unlike sleep_for, the next
// deadline is anchored to the preceding deadline and missed ticks are skipped.
class FixedRateScheduler final {
public:
    using Clock = std::chrono::steady_clock;

    void Wait(std::chrono::milliseconds period) {
        const auto now = Clock::now();
        if (!started_ || next_ <= now - period * 2) {
            next_ = now + period;
            started_ = true;
        } else {
            next_ += period;
        }
        if (next_ > now)
            std::this_thread::sleep_until(next_);
        else
            next_ = now;
    }

private:
    Clock::time_point next_{};
    bool started_ = false;
};

struct PipelineTelemetry final {
    std::atomic<float> acquire_ms{0.f};
    std::atomic<float> process_ms{0.f};
    std::atomic<float> render_ms{0.f};
    std::atomic<float> w2s_ms{0.f};
    std::atomic<float> snapshot_interval_ms{0.f};
    std::atomic<float> average_frame_ms{0.f};
    std::atomic<float> peak_frame_ms{0.f};
    std::atomic<float> slow_work_ms{0.f};
    std::atomic<int> entities{0};
    std::atomic<std::uint64_t> frames{0};

    static void Smooth(std::atomic<float>& value, float sample, float alpha = .08f) noexcept {
        const float old = value.load(std::memory_order_relaxed);
        value.store(old <= 0.f ? sample : old + (sample - old) * alpha,
                    std::memory_order_relaxed);
    }

    void RecordFrame(float frame_ms) noexcept {
        Smooth(average_frame_ms, frame_ms);
        float peak = peak_frame_ms.load(std::memory_order_relaxed);
        peak = (std::max)(frame_ms, peak * .985f);
        peak_frame_ms.store(peak, std::memory_order_relaxed);
        frames.fetch_add(1, std::memory_order_relaxed);
    }
};

inline float TimeMs(std::chrono::steady_clock::time_point begin) noexcept {
    return std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - begin).count();
}

} // namespace OmniGhost::Gameplay
