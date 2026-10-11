#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

namespace OmniGhost::Gameplay {

// Shared camera priority coordination for multi-threaded DMA acquisition.
// This header provides a single implementation used by both CS2 and FiveM
// to ensure optional DMA lanes yield to the high-frequency camera lane.

namespace CameraPriority {

// Camera state interface - implemented by each game's camera thread
struct ICameraState {
    virtual ~ICameraState() = default;
    virtual bool IsPending() const noexcept = 0;
    virtual uint64_t GetDeadlineMs() const noexcept = 0;
};

// Configuration constants (identical for CS2 and FiveM per architecture docs)
inline constexpr int CAMERA_INTERVAL_MS = 16;
inline constexpr int MAX_CAMERA_SPINS = 96;
inline constexpr int BACKOFF_START_SPIN = 32;
inline constexpr std::chrono::microseconds INITIAL_BACKOFF_US{10};
inline constexpr std::chrono::microseconds MAX_BACKOFF_US{500};

/**
 * @brief Yields to camera thread if a camera read is in progress or deadline is imminent.
 * 
 * This function implements cooperative yielding with exponential backoff.
 * Optional DMA lanes MUST call this before starting any DMA batch.
 * 
 * @param camera Camera state provider (game-specific implementation)
 * @return true if camera is not busy and it's safe to proceed, false if caller should skip this batch
 */
inline bool GiveCameraPriority(const ICameraState& camera) noexcept {
    // Fast path: no pending camera read and deadline is far enough
    if (!camera.IsPending()) {
        const uint64_t deadline = camera.GetDeadlineMs();
        const uint64_t now = ::GetTickCount64();
        if (deadline == 0 || now + 2u < deadline) {
            return true;
        }
    }

    // Cooperative yield with exponential backoff
    int spin = 0;
    auto baseDelay = INITIAL_BACKOFF_US;

    while (spin < MAX_CAMERA_SPINS) {
        if (!camera.IsPending()) {
            return true; // Camera done, safe to proceed
        }
        std::this_thread::yield();
        ++spin;

        // Exponential backoff after initial spins
        if (spin >= BACKOFF_START_SPIN) {
            std::this_thread::sleep_for(baseDelay);
            baseDelay *= 2;
            if (baseDelay > MAX_BACKOFF_US) {
                baseDelay = MAX_BACKOFF_US;
            }
        }
    }

    // Camera still busy after max retries — skip this optional batch
    return false;
}

/**
 * @brief Quick check if optional DMA should yield to camera.
 * 
 * Lighter version without spin loop - just checks if yield is needed.
 * Use for per-ped or per-batch decisions inside optional lane.
 * 
 * @param camera Camera state provider
 * @return true if should yield (camera pending or deadline imminent)
 */
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

} // namespace CameraPriority

} // namespace OmniGhost::Gameplay