#pragma once

#include <chrono>

namespace OmniGhost::Gameplay {

// RAII timing helper for scoped phase measurement
// Usage: ScopedPhase phase(&metrics.phase_ms);
struct ScopedPhase {
    float* target;
    std::chrono::steady_clock::time_point t0;
    explicit ScopedPhase(float* out) : target(out), t0(std::chrono::steady_clock::now()) {}
    ~ScopedPhase() {
        if (target)
            *target = TimeMs(t0);
    }
};

} // namespace OmniGhost::Gameplay