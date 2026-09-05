#pragma once

namespace PerformanceMode {

struct State {
    bool effective = false;
    bool auto_engaged = false;
    float smoothed_fps = 0.0f;
};

// Resets the shared performance controller. Call once during UI startup.
void Reset();

// Combines the manual setting with an automatic low-FPS fallback.
// The automatic mode uses hysteresis and short hold times to avoid flicker.
State Update(
    float frames_per_second,
    float delta_seconds,
    bool manual_enabled,
    bool automatic_enabled);

// Returns the most recently calculated state.
State Current();

} // namespace PerformanceMode
