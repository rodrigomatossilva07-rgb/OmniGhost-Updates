#pragma once

namespace PerformanceMode {

struct State {
    bool effective = false;
    bool auto_engaged = false;
    float smoothed_fps = 0.0f;
};


void Reset();



State Update(
    float frames_per_second,
    float delta_seconds,
    bool manual_enabled,
    bool automatic_enabled);


State Current();

}
