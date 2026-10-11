#pragma once

#include "../cs2_game.h"

#include <cstdint>

namespace CS2::RuntimePipeline {

void Publish(Runtime& source, uint64_t generation, uint64_t read_fails);
[[nodiscard]] RuntimeSnapshotLease Acquire();

void PublishLiveness(const LivenessSnapshot& snapshot);
[[nodiscard]] LivenessSnapshotLease AcquireLiveness();

void SubmitConfig(const Config& next_config) noexcept;
[[nodiscard]] OmniGhost::Gameplay::SnapshotExchange<Config>::ReadLease AcquireConfig();

void ResetPublishMetrics() noexcept;
[[nodiscard]] float AcquisitionHz() noexcept;
[[nodiscard]] uint64_t SnapshotDrops() noexcept;

// Shutdown publishes from a different thread after all producers have joined.
void ResetProducerThreadForShutdown() noexcept;

} // namespace CS2::RuntimePipeline
