#pragma once

#include "../cs2_game.h"

#include <atomic>
#include <cstdint>

namespace CS2::Camera {

// Owns the camera snapshot exchange and its short presentation timeline.
// Acquisition controls the deadline flags; presentation only acquires/selects
// immutable snapshots and therefore never waits for DMA.
void Publish(const float* matrix, uint64_t sample_ms = 0);
[[nodiscard]] CameraSnapshotLease Acquire();
[[nodiscard]] CameraSnapshot Select(uint64_t world_sample_ms);
void Reset() noexcept;

void SetDeadline(uint64_t deadline_ms) noexcept;
[[nodiscard]] uint64_t Deadline() noexcept;
void SetPending(bool pending) noexcept;
[[nodiscard]] bool Pending() noexcept;

} // namespace CS2::Camera
