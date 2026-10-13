#include "cs2_runtime.h"

#include "../radar/cs2_radar.h"

#include <Windows.h>

#include <atomic>
#include <utility>

namespace CS2::RuntimePipeline {
namespace {

OmniGhost::Gameplay::SnapshotExchange<Runtime, 4> g_runtime_snapshots;
OmniGhost::Gameplay::SnapshotExchange<LivenessSnapshot> g_liveness_snapshots;
OmniGhost::Gameplay::SnapshotExchange<Config> g_config_snapshots;
std::atomic<uint64_t> g_last_publish_ms{0};
std::atomic<uint64_t> g_snapshot_drops{0};
std::atomic<float> g_acquisition_hz{0.f};

} // namespace

void Publish(Runtime& source, uint64_t generation, uint64_t read_fails) {
    const uint64_t now = GetTickCount64();
    const uint64_t previous = g_last_publish_ms.exchange(now, std::memory_order_relaxed);
    if (previous != 0 && now > previous) {
        g_acquisition_hz.store(
            1000.0f / static_cast<float>(now - previous), std::memory_order_relaxed);
    }

    // BUG-007 Fix: Build snapshot in local temporary to avoid global 'runtime'
    // being in a partially-published state during move assignment.
    // CS2-001 Fix: the hand-written move constructor copies the scalar identity
    // fields (client_base/engine_base/entity list/strides/local state/...) and
    // only moves the per-scan vectors (players/spectators/dropped_weapons).
    // The accumulator must keep the identity fields — the next scan depends on
    // client_base/entity_list_entry to run at all — so Publish must never
    // reinitialize `source`. Vectors are moved out (per-scan clear); scalars
    // stay valid for the next acquisition tick.
    Runtime snapshot = std::move(source);
    snapshot.snapshot_timestamp_ms = now;
    snapshot.session_generation = generation;
    snapshot.read_fails = read_fails;
    snapshot.acquisition_hz = g_acquisition_hz.load(std::memory_order_relaxed);
    snapshot.snapshot_interval_ms = previous && now > previous
        ? static_cast<float>(now - previous) : 0.f;
    snapshot.snapshot_drops = g_snapshot_drops.load(std::memory_order_relaxed);

    auto slot = g_runtime_snapshots.TryBeginWrite();
    if (!slot) {
        g_snapshot_drops.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    *slot.value = std::move(snapshot);
    g_runtime_snapshots.Publish(slot.index);
    WebRadar::SubmitSnapshot(*slot.value);
    // CS2-001 Fix: do NOT reset `source` here. Resetting destroyed the module
    // bases and entity-list cache (client_base=0 permanently → every later
    // scan early-returned), which killed ESP/aim/radar/bones until re-attach.
    // Per-scan counters are zeroed and vectors cleared at scan start in
    // RunFrameWithConfig; identity/scalar fields survive by design.
}

RuntimeSnapshotLease Acquire() {
    return g_runtime_snapshots.Acquire();
}

void PublishLiveness(const LivenessSnapshot& snapshot) {
    auto slot = g_liveness_snapshots.TryBeginWrite();
    if (!slot)
        return;
    *slot.value = snapshot;
    g_liveness_snapshots.Publish(slot.index);
}

LivenessSnapshotLease AcquireLiveness() {
    return g_liveness_snapshots.Acquire();
}

void SubmitConfig(const Config& next_config) noexcept {
    auto slot = g_config_snapshots.TryBeginWrite();
    if (!slot)
        return;
    *slot.value = next_config;
    g_config_snapshots.Publish(slot.index);
}

OmniGhost::Gameplay::SnapshotExchange<Config>::ReadLease AcquireConfig() {
    return g_config_snapshots.Acquire();
}

void ResetPublishMetrics() noexcept {
    g_last_publish_ms.store(0, std::memory_order_relaxed);
    g_snapshot_drops.store(0, std::memory_order_relaxed);
    g_acquisition_hz.store(0.f, std::memory_order_relaxed);
}

float AcquisitionHz() noexcept {
    return g_acquisition_hz.load(std::memory_order_relaxed);
}

uint64_t SnapshotDrops() noexcept {
    return g_snapshot_drops.load(std::memory_order_relaxed);
}

void ResetProducerThreadForShutdown() noexcept {
    g_runtime_snapshots.ResetProducerThread();
}

} // namespace CS2::RuntimePipeline
