#include "performance_mode.h"

#include <algorithm>
#include <cmath>

namespace PerformanceMode {
namespace {

constexpr float kEngageFps = 40.0f;
constexpr float kReleaseFps = 55.0f;
constexpr float kEngageDelaySeconds = 0.35f;
constexpr float kReleaseDelaySeconds = 1.25f;
constexpr float kSmoothingRate = 7.5f;

State g_state{};
float g_low_fps_time = 0.0f;
float g_recovered_time = 0.0f;
bool g_has_sample = false;

float SanitizeDelta(float value) {
    if (!std::isfinite(value) || value < 0.0f)
        return 0.0f;
    return (std::min)(value, 0.10f);
}

float SanitizeFps(float value) {
    if (!std::isfinite(value) || value < 0.0f)
        return 0.0f;
    return (std::min)(value, 1000.0f);
}

} // namespace

void Reset() {
    g_state = State{};
    g_low_fps_time = 0.0f;
    g_recovered_time = 0.0f;
    g_has_sample = false;
}

State Update(
    float frames_per_second,
    float delta_seconds,
    bool manual_enabled,
    bool automatic_enabled) {
    const float fps = SanitizeFps(frames_per_second);
    const float delta = SanitizeDelta(delta_seconds);

    if (!g_has_sample) {
        g_state.smoothed_fps = fps;
        g_has_sample = fps > 0.0f;
    } else if (fps > 0.0f) {
        const float blend = 1.0f - std::exp(-kSmoothingRate * delta);
        g_state.smoothed_fps += (fps - g_state.smoothed_fps) * blend;
    }

    if (!automatic_enabled) {
        g_state.auto_engaged = false;
        g_low_fps_time = 0.0f;
        g_recovered_time = 0.0f;
    } else if (g_state.smoothed_fps > 0.0f) {
        if (!g_state.auto_engaged) {
            if (g_state.smoothed_fps < kEngageFps) {
                g_low_fps_time += delta;
                if (g_low_fps_time >= kEngageDelaySeconds) {
                    g_state.auto_engaged = true;
                    g_low_fps_time = 0.0f;
                    g_recovered_time = 0.0f;
                }
            } else {
                g_low_fps_time = 0.0f;
            }
        } else {
            if (g_state.smoothed_fps > kReleaseFps) {
                g_recovered_time += delta;
                if (g_recovered_time >= kReleaseDelaySeconds) {
                    g_state.auto_engaged = false;
                    g_recovered_time = 0.0f;
                    g_low_fps_time = 0.0f;
                }
            } else {
                g_recovered_time = 0.0f;
            }
        }
    }

    g_state.effective = manual_enabled || g_state.auto_engaged;
    return g_state;
}

State Current() {
    return g_state;
}

} // namespace PerformanceMode
