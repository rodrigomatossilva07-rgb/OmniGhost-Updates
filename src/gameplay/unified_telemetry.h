#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>
#include <chrono>

namespace OmniGhost::Gameplay::Telemetry {

// Unified telemetry sample structure consolidating:
// - PipelineTelemetry (frame_pipeline.h)
// - DmaTelemetry::Sample (dma_telemetry_log.h)
struct UnifiedSample {
    // Frame timing
    std::atomic<float> frame_ms{0.f};
    std::atomic<float> frame_avg_ms{0.f};
    std::atomic<float> frame_p95_ms{0.f};
    std::atomic<float> frame_p99_ms{0.f};
    std::atomic<float> frame_max_ms{0.f};

    // Acquisition timing
    std::atomic<float> acquire_ms{0.f};
    std::atomic<float> acquire_avg_ms{0.f};
    std::atomic<float> acquire_p50_ms{0.f};
    std::atomic<float> acquire_p95_ms{0.f};
    std::atomic<float> acquire_p99_ms{0.f};
    std::atomic<float> acquire_max_ms{0.f};

    // Render timing
    std::atomic<float> render_ms{0.f};
    std::atomic<float> render_avg_ms{0.f};
    std::atomic<float> render_p50_ms{0.f};
    std::atomic<float> render_p95_ms{0.f};
    std::atomic<float> render_p99_ms{0.f};
    std::atomic<float> render_max_ms{0.f};

    // Snapshot timing
    std::atomic<float> snapshot_age_ms{0.f};
    std::atomic<float> snapshot_interval_ms{0.f};
    std::atomic<float> snapshot_hz{0.f};
    std::atomic<uint64_t> snapshot_drops{0};

    // Camera timing
    std::atomic<float> camera_age_ms{0.f};
    std::atomic<float> camera_age_avg_ms{0.f};
    std::atomic<float> camera_age_p50_ms{0.f};
    std::atomic<float> camera_age_p95_ms{0.f};
    std::atomic<float> camera_age_p99_ms{0.f};
    std::atomic<float> camera_age_max_ms{0.f};
    std::atomic<float> camera_read_p95_ms{0.f};
    std::atomic<float> camera_read_p99_ms{0.f};
    std::atomic<float> camera_read_max_ms{0.f};
    std::atomic<uint64_t> camera_busy_skips{0};
    std::atomic<uint64_t> camera_busy_retry_success{0};
    std::atomic<uint64_t> camera_consecutive_busy_max{0};
    std::atomic<uint64_t> camera_cooldown_skips{0};
    std::atomic<uint64_t> camera_cooldown_bypasses{0};

    // World timing
    std::atomic<float> world_age_ms{0.f};
    std::atomic<float> position_age_ms{0.f};
    std::atomic<float> position_age_avg_ms{0.f};
    std::atomic<float> position_age_p50_ms{0.f};
    std::atomic<float> position_age_p95_ms{0.f};
    std::atomic<float> position_age_p99_ms{0.f};
    std::atomic<float> position_age_max_ms{0.f};

    // Bones timing
    std::atomic<float> bones_age_ms{0.f};
    std::atomic<float> bones_age_avg_ms{0.f};
    std::atomic<float> bones_age_p50_ms{0.f};
    std::atomic<float> bones_age_p95_ms{0.f};
    std::atomic<float> bones_age_p99_ms{0.f};
    std::atomic<float> bones_age_max_ms{0.f};
    std::atomic<uint64_t> cached_bones_used{0};
    std::atomic<float> cached_bones_age_max_ms{0.f};

    // Camera-position coherence
    std::atomic<float> camera_position_delta_ms{0.f};
    std::atomic<float> camera_position_delta_p95_ms{0.f};
    std::atomic<float> camera_position_delta_p99_ms{0.f};
    std::atomic<float> camera_position_delta_max_ms{0.f};

    // Visibility
    std::atomic<float> visibility_ms{0.f};
    std::atomic<int> visibility_players_tested{0};
    std::atomic<int> visibility_rays{0};

    // Prediction
    std::atomic<float> prediction_ms{0.f};
    std::atomic<float> prediction_p95_ms{0.f};
    std::atomic<float> prediction_p99_ms{0.f};
    std::atomic<float> prediction_max_ms{0.f};

    // DMA/VMM timing
    std::atomic<float> vmm_avg_ms{0.f};
    std::atomic<float> vmm_p50_ms{0.f};
    std::atomic<float> vmm_p95_ms{0.f};
    std::atomic<float> vmm_p99_ms{0.f};
    std::atomic<float> vmm_max_ms{0.f};

    // Scheduler
    std::atomic<float> scheduler_wait_ms{0.f};

    // Entity counts
    std::atomic<int> entities{0};
    std::atomic<int> vehicles{0};
    std::atomic<int> objects{0};

    // Bone stats
    std::atomic<int> bones_players{0};
    std::atomic<int> bones_batches{0};
    std::atomic<float> bones_p95_ms{0.f};
    std::atomic<float> bones_p99_ms{0.f};
    std::atomic<float> bones_max_ms{0.f};

    // Read/Scatter counts
    std::atomic<int> reads_per_sec{0};
    std::atomic<int> scatter_per_sec{0};
    std::atomic<uint64_t> read_fails{0};

    // Spikes
    std::atomic<uint64_t> acquisition_spikes{0};
    std::atomic<uint64_t> render_spikes{0};
    std::atomic<uint64_t> suppressed_spikes_20{0};
    std::atomic<uint64_t> suppressed_spikes_50{0};
    std::atomic<uint64_t> suppressed_spikes_100{0};

    // Process resources
    std::atomic<uint32_t> process_id{0};
    std::atomic<bool> process_connected{false};
    std::atomic<bool> dma_open{false};
    std::atomic<uint64_t> process_handles{0};
    std::atomic<int64_t> process_handles_delta{0};
    std::atomic<uint32_t> process_threads{0};
    std::atomic<float> working_set_mb{0.f};
    std::atomic<float> private_mb{0.f};
    std::atomic<uint32_t> scatter_handles_live{0};

    // State
    std::atomic<bool> active{false};
    std::atomic<int> idle_reason{0}; // 0=none,1=dead,2=not_in_match,3=menu,4=esp_off,5=no_local

    // Pressure
    std::atomic<int> pressure_level{0};
    std::atomic<uint64_t> cooldown_remaining_ms{0};

    // Config change tracking
    std::atomic<uint64_t> config_change_count{0};

    // Death-to-hide latency
    std::atomic<float> death_to_hide_ms{0.f};

    // Reinsertions (P0-2)
    std::atomic<uint64_t> recent_player_reinsertions{0};
    std::atomic<uint64_t> last_good_age_ms{0};

    // Motion/target lanes
    std::atomic<float> motion_age_ms{0.f};
    std::atomic<float> state_age_ms{0.f};
    std::atomic<float> target_lane_age_ms{0.f};

    // Smooth exponential moving average
    static void Smooth(std::atomic<float>& value, float sample, float alpha = 0.08f) noexcept {
        const float old = value.load(std::memory_order_relaxed);
        value.store(old <= 0.f ? sample : old + (sample - old) * alpha,
                    std::memory_order_relaxed);
    }

    void RecordFrame(float frame_ms) noexcept {
        Smooth(frame_avg_ms, frame_ms);
        float peak = frame_max_ms.load(std::memory_order_relaxed);
        peak = (std::max)(frame_ms, peak * .985f);
        frame_max_ms.store(peak, std::memory_order_relaxed);
    }
};

// Game-specific telemetry instances
UnifiedSample& FiveM();
UnifiedSample& CS2();
UnifiedSample& Warzone();
UnifiedSample& Fortnite();

// High-level observation functions
void ObserveAcquire(UnifiedSample& s, float work_ms, bool activeSample, uint64_t scan_id = 0) noexcept;
void ObserveRender(UnifiedSample& s, float ms) noexcept;
void ObserveCameraAge(UnifiedSample& s, float age_ms) noexcept;
void ObserveCameraRead(UnifiedSample& s, float read_ms) noexcept;
void ObserveBones(UnifiedSample& s, float bones_ms) noexcept;
void ObservePresentationTiming(UnifiedSample& s, float position_age_ms, float bones_age_ms,
                               float camera_position_delta_ms, float prediction_ms) noexcept;
void ObserveSchedulerWait(UnifiedSample& s, float wait_ms) noexcept;

// Memory diagnostics integration
void RefreshFromMemory() noexcept;

// Spike breakdown logging
struct SpikeBreakdown {
    uint64_t scan_id = 0;
    float total_ms = 0.f;
    float webradar_ms = 0.f;
    float entity_ms = 0.f;
    float local_ms = 0.f;
    float core_scatter_ms = 0.f;
    float positions_ms = 0.f;
    float bones_ms = 0.f;
    float weapon_ms = 0.f;
    float ammo_ms = 0.f;
    float flags_ms = 0.f;
    float visibility_ms = 0.f;
    float spectator_ms = 0.f;
    float bomb_ms = 0.f;
    float publish_ms = 0.f;
    float cleanup_ms = 0.f;
    float other_ms = 0.f;
    float lock_wait_ms = 0.f;
    int players = 0;
    int bones_players = 0;
    int visibility_players_tested = 0;
    int visibility_rays = 0;
    int entity_full_probe = 0;
    int spectator_refresh = 0;
    int bomb_refresh = 0;
    int cache_cleanup = 0;
    int qread_calls = 0;
    float qread_total_ms = 0.f;
    float qread_max_ms = 0.f;
    int scatter_calls = 0;
    float scatter_total_ms = 0.f;
    float scatter_max_ms = 0.f;
    const char* top_tag = "none";
    float top_tag_ms = 0.f;
    const char* top2_tag = "none";
    float top2_tag_ms = 0.f;
    const char* top3_tag = "none";
    float top3_tag_ms = 0.f;
};
void LogSpikeBreakdown(std::string_view tag, const SpikeBreakdown& b) noexcept;

// Config change logging
void LogConfigChanged(std::string_view tag, std::string_view config_blob);
void LogSessionSummary(std::string_view tag);
void SetEnabled(bool enabled) noexcept;
bool IsEnabled() noexcept;
void Tick(std::string_view tag);

} // namespace OmniGhost::Gameplay::Telemetry