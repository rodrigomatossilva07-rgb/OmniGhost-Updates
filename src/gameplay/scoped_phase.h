#pragma once

#include <chrono>
#include "frame_pipeline.h"

namespace OmniGhost::Gameplay {



struct ScopedPhase {
    float* target;
    std::chrono::steady_clock::time_point t0;
    explicit ScopedPhase(float* out) : target(out), t0(std::chrono::steady_clock::now()) {}
    ~ScopedPhase() {
        if (target)
            *target = TimeMs(t0);
    }
};

}
