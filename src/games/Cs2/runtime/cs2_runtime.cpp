#include "cs2_runtime.h"

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
std::atomic<float> g_publish_interval_ms{0.f};



std::atomic<uintptr_t> g_published_client_base{0};
std::atomic<bool> g_published_in_match{false};

}

void Publish(Runtime& source, uint64_t generation, uint64_t read_fails) {
    const uint64_t now = CS2::SampleClockMs();
    const uint64_t previous = g_last_publish_ms.exchange(now, std::memory_order_relaxed);
    if (previous != 0 && now > previous) {
        const float interval = static_cast<float>(now - previous);
        const float previousAverage = g_publish_interval_ms.load(std::memory_order_relaxed);


        const float average = previousAverage > 0.f
            ? previousAverage + (interval - previousAverage) * 0.20f
            : interval;
        g_publish_interval_ms.store(average, std::memory_order_relaxed);
        g_acquisition_hz.store(1000.0f / average, std::memory_order_relaxed);
    }










    auto slot = g_runtime_snapshots.TryBeginWrite();
    if (!slot) {
        g_snapshot_drops.fetch_add(1, std::memory_order_relaxed);


        source.players.clear();
        source.spectators.clear();
        source.dropped_weapons.clear();
        return;
    }





    slot.value->SwapVectorsWith(source);
    slot.value->CopyScalarsFrom(source);
    slot.value->snapshot_timestamp_ms = now;
    slot.value->session_generation = generation;
    slot.value->read_fails = read_fails;
    slot.value->acquisition_hz = g_acquisition_hz.load(std::memory_order_relaxed);
    slot.value->snapshot_interval_ms = g_publish_interval_ms.load(std::memory_order_relaxed);
    slot.value->snapshot_drops = g_snapshot_drops.load(std::memory_order_relaxed);

    g_runtime_snapshots.Publish(slot.index);
    g_published_client_base.store(slot.value->client_base, std::memory_order_release);
    g_published_in_match.store(slot.value->in_match, std::memory_order_release);





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
    g_publish_interval_ms.store(0.f, std::memory_order_relaxed);
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

uintptr_t PublishedClientBase() noexcept {
    return g_published_client_base.load(std::memory_order_acquire);
}

bool PublishedInMatch() noexcept {
    return g_published_in_match.load(std::memory_order_acquire);
}

}
