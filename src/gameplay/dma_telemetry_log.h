#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

namespace OmniGhost::Gameplay::DmaTelemetry {

// Periodic PERF + spike lines for [FIVEM] / [CS2] into SessionLog (logs.txt).
// Never blocks the render/acquire path with ofstream flushes.

void SetEnabled(bool enabled) noexcept;
[[nodiscard]] bool IsEnabled() noexcept;

// Tag is "FIVEM" or "CS2".
void Tick(std::string_view tag);

void LogConfigChanged(std::string_view tag, std::string_view config_blob);
void LogSessionSummary(std::string_view tag);

// Sample producers push metrics here (lock-free atomics).
struct Sample {
    std::atomic<float> acquire_ms{0.f};
    std::atomic<float> acquire_p95_ms{0.f};
    std::atomic<float> acquire_max_ms{0.f};
    std::atomic<float> render_ms{0.f};
    std::atomic<float> render_p95_ms{0.f};
    std::atomic<float> render_max_ms{0.f};
    std::atomic<float> snapshot_age_ms{0.f};
    std::atomic<float> snapshot_interval_ms{0.f};
    std::atomic<float> snapshot_hz{0.f};
    std::atomic<float> camera_age_ms{0.f};
    std::atomic<float> motion_age_ms{0.f};
    std::atomic<float> state_age_ms{0.f};
    std::atomic<float> target_lane_age_ms{0.f};
    std::atomic<float> vmm_avg_ms{0.f};
    std::atomic<float> vmm_p95_ms{0.f};
    std::atomic<float> vmm_max_ms{0.f};
    std::atomic<int> entities{0};
    std::atomic<int> vehicles{0};
    std::atomic<int> objects{0};
    std::atomic<int> bones_players{0};
    std::atomic<int> bones_batches_s{0};
    std::atomic<int> reads_s{0};
    std::atomic<int> scatter_s{0};
    std::atomic<std::uint64_t> snapshot_drops{0};
    std::atomic<std::uint64_t> read_fails{0};
    std::atomic<std::uint64_t> acquisition_spikes{0};
    std::atomic<std::uint64_t> render_spikes{0};
    std::atomic<std::uint32_t> process_id{0};
    std::atomic_bool process_connected{false};
    std::atomic_bool dma_open{false};
};

[[nodiscard]] Sample& FiveM();
[[nodiscard]] Sample& CS2();

void ObserveAcquire(Sample& s, float ms) noexcept;
void ObserveRender(Sample& s, float ms) noexcept;

} // namespace OmniGhost::Gameplay::DmaTelemetry
