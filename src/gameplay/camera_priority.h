#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

namespace OmniGhost::Gameplay {





namespace CameraPriority {


struct ICameraState {
    virtual ~ICameraState() = default;
    virtual bool IsPending() const noexcept = 0;
    virtual uint64_t GetDeadlineMs() const noexcept = 0;
};


inline constexpr int CAMERA_INTERVAL_MS = 16;
inline constexpr int MAX_CAMERA_SPINS = 96;
inline constexpr int BACKOFF_START_SPIN = 32;
inline constexpr std::chrono::microseconds INITIAL_BACKOFF_US{10};
inline constexpr std::chrono::microseconds MAX_BACKOFF_US{500};










inline bool GiveCameraPriority(const ICameraState& camera) noexcept {

    if (!camera.IsPending()) {
        const uint64_t deadline = camera.GetDeadlineMs();
        const uint64_t now = ::GetTickCount64();
        if (deadline == 0 || now + 2u < deadline) {
            return true;
        }
    }


    int spin = 0;
    auto baseDelay = INITIAL_BACKOFF_US;

    while (spin < MAX_CAMERA_SPINS) {
        if (!camera.IsPending()) {
            return true;
        }
        std::this_thread::yield();
        ++spin;


        if (spin >= BACKOFF_START_SPIN) {
            std::this_thread::sleep_for(baseDelay);
            baseDelay *= 2;
            if (baseDelay > MAX_BACKOFF_US) {
                baseDelay = MAX_BACKOFF_US;
            }
        }
    }


    return false;
}










inline bool ShouldYieldToCamera(const ICameraState& camera) noexcept {
    if (camera.IsPending()) {
        return true;
    }
    const uint64_t deadline = camera.GetDeadlineMs();
    if (deadline == 0) {
        return false;
    }
    const uint64_t now = ::GetTickCount64();
    return now + 2u >= deadline;
}

}

}