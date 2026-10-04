#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

#include "unified_telemetry.h"

namespace OmniGhost::Gameplay::DmaTelemetry {

// Compatibility layer: forwards to unified telemetry
inline void SetEnabled(bool enabled) noexcept {
    Telemetry::SetEnabled(enabled);
}

inline bool IsEnabled() noexcept {
    return Telemetry::IsEnabled();
}

inline void Tick(std::string_view tag) {
    Telemetry::Tick(tag);
}

inline void LogConfigChanged(std::string_view tag, std::string_view config_blob) {
    Telemetry::LogConfigChanged(tag, config_blob);
}

inline void LogSessionSummary(std::string_view tag) {
    Telemetry::LogSessionSummary(tag);
}

// Fixed-window sample stats (mathematically coherent: max>=p99>=p95>=p50).
struct WindowStats {
    float last = 0.f;
    float avg = 0.f;
    float p50 = 0.f;
    float p95 = 0.f;
    float p99 = 0.f;
    float max = 0.f;
    int samples = 0;
};

// Compatibility Sample struct - forwards to Telemetry::UnifiedSample
struct Sample {
    // Acquisition timing
    std::atomic<float> acquire_ms{0.f};
    std::atomic<float> acquire_p50_ms{0.f};
    std::atomic<float> acquire_p95_ms{0.f};
    std::atomic<float> acquire_p99_ms{0.f};
    std::atomic<float> acquire_max_ms{0.f};
    std::atomic<float> pressure_window_p95_ms{0.f};

    // Render timing
    std::atomic<float> render_ms{0.f};
    std::atomic<float> render_p95_ms{0.f};
    std::atomic<float> render_p99_ms{0.f};
    std::atomic<float> render_max_ms{0.f};

    // Snapshot timing
    std::atomic<float> snapshot_age_ms{0.f};
    std::atomic<float> world_age_ms{0.f};
    std::atomic<float> publish_delay_ms{0.f};

    // Visibility
    std::atomic<float> visibility_ms{0.f};
    std::atomic<int> visibility_players_tested{0};
    std::atomic<int> visibility_rays{0};

    // Snapshot rate
    std::atomic<float> snapshot_interval_ms{0.f};
    std::atomic<float> snapshot_hz{0.f};

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

    // Pressure
    std::atomic<int> pressure_level{0};
    std::atomic<uint64_t> cooldown_remaining_ms{0};

    // Death/hide
    std::atomic<float> death_to_hide_ms{0.f};
    std::atomic<uint64_t> recent_player_reinsertions{0};
    std::atomic<uint64_t> last_good_age_ms{0};

    // Position timing
    std::atomic<float> position_age_ms{0.f};
    std::atomic<float> position_age_avg_ms{0.f};
    std::atomic<float> position_age_p50_ms{0.f};
    std::atomic<float> position_age_p95_ms{0.f};
    std::atomic<float> position_age_p99_ms{0.f};
    std::atomic<float> position_age_max_ms{0.f};

    // Bones timing
    std::atomic<float> bones_age_ms{0.f};
    std::atomic<float> bones_age_avg_ms{0.f};
    std::atomic<float> bones_age_p95_ms{0.f};
    std::atomic<float> bones_age_p99_ms{0.f};
    std::atomic<float> bones_age_max_ms{0.f};

    // Camera-position coherence
    std::atomic<float> camera_position_delta_ms{0.f};
    std::atomic<float> camera_position_delta_p95_ms{0.f};
    std::atomic<float> camera_position_delta_p99_ms{0.f};
    std::atomic<float> camera_position_delta_max_ms{0.f};

    // Prediction
    std::atomic<float> prediction_ms{0.f};
    std::atomic<float> prediction_p95_ms{0.f};
    std::atomic<float> prediction_p99_ms{0.f};
    std::atomic<float> prediction_max_ms{0.f};

    // Cached bones
    std::atomic<uint64_t> cached_bones_used{0};
    std::atomic<float> cached_bones_age_max_ms{0.f};

    // Motion/target
    std::atomic<float> motion_age_ms{0.f};
    std::atomic<float> state_age_ms{0.f};
    std::atomic<float> target_lane_age_ms{0.f};

    // VMM timing
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

    // Bones
    std::atomic<int> bones_players{0};
    std::atomic<int> bones_batches_s{0};
    std::atomic<float> bones_p95_ms{0.f};
    std::atomic<float> bones_p99_ms{0.f};
    std::atomic<float> bones_max_ms{0.f};

    // DMA rates
    std::atomic<int> reads_s{0};
    std::atomic<int> scatter_s{0};
    std::atomic<uint64_t> snapshot_drops{0};
    std::atomic<uint64_t> read_fails{0};

    // Spikes
    std::atomic<uint64_t> acquisition_spikes{0};
    std::atomic<uint64_t> render_spikes{0};
    std::atomic<uint64_t> suppressed_spikes_20{0};
    std::atomic<uint64_t> suppressed_spikes_50{0};
    std::atomic<uint64_t> suppressed_spikes_100{0};

    // Process info
    std::atomic<uint32_t> process_id{0};
    std::atomic_bool process_connected{false};
    std::atomic_bool dma_open{false};
    std::atomic<uint64_t> process_handles{0};
    std::atomic<int64_t> process_handles_delta{0};
    std::atomic<uint32_t> process_threads{0};
    std::atomic<float> working_set_mb{0.f};
    std::atomic<float> private_mb{0.f};
    std::atomic<uint32_t> scatter_handles_live{0};

    // State
    std::atomic_bool active{false};
    std::atomic<int> idle_reason{0};

    // Sync with unified telemetry
    void SyncFromUnified() noexcept {
        auto& u = Telemetry::FiveM(); // Default to FiveM, caller should use correct game
        acquire_ms.store(u.acquire_ms.load(), std::memory_order_relaxed);
        acquire_p50_ms.store(u.acquire_p50_ms.load(), std::memory_order_relaxed);
        acquire_p95_ms.store(u.acquire_p95_ms.load(), std::memory_order_relaxed);
        acquire_p99_ms.store(u.acquire_p99_ms.load(), std::memory_order_relaxed);
        acquire_max_ms.store(u.acquire_max_ms.load(), std::memory_order_relaxed);
        render_ms.store(u.render_ms.load(), std::memory_order_relaxed);
        render_p95_ms.store(u.render_p95_ms.load(), std::memory_order_relaxed);
        render_p99_ms.store(u.render_p99_ms.load(), std::memory_order_relaxed);
        render_max_ms.store(u.render_max_ms.load(), std::memory_order_relaxed);
        snapshot_age_ms.store(u.snapshot_age_ms.load(), std::memory_order_relaxed);
        world_age_ms.store(u.world_age_ms.load(), std::memory_order_relaxed);
        camera_age_ms.store(u.camera_age_ms.load(), std::memory_order_relaxed);
        position_age_ms.store(u.position_age_ms.load(), std::memory_order_relaxed);
        bones_age_ms.store(u.bones_age_ms.load(), std::memory_order_relaxed);
        camera_position_delta_ms.store(u.camera_position_delta_ms.load(), std::memory_order_relaxed);
        prediction_ms.store(u.prediction_ms.load(), std::memory_order_relaxed);
        scheduler_wait_ms.store(u.scheduler_wait_ms.load(), std::memory_order_relaxed);
        entities.store(u.entities.load(), std::memory_order_relaxed);
        active.store(u.active.load(), std::memory_order_relaxed);
        pressure_level.store(u.pressure_level.load(), std::memory_order_relaxed);
    }
};

// Game-specific accessors (forward to Telemetry)
inline Sample& FiveM() {
    static Sample s;
    s.SyncFromUnified(); // Note: this only syncs FiveM
    return s;
}

inline Sample& CS2() {
    static Sample s;
    auto& u = Telemetry::CS2();
    s.acquire_ms.store(u.acquire_ms.load(), std::memory_order_relaxed);
    s.acquire_p50_ms.store(u.acquire_p50_ms.load(), std::memory_order_relaxed);
    s.acquire_p95_ms.store(u.acquire_p95_ms.load(), std::memory_order_relaxed);
    s.acquire_p99_ms.store(u.acquire_p99_ms.load(), std::memory_order_relaxed);
    s.acquire_max_ms.store(u.acquire_max_ms.load(), std::memory_order_relaxed);
    s.render_ms.store(u.render_ms.load(), std::memory_order_relaxed);
    s.render_p95_ms.store(u.render_p95_ms.load(), std::memory_order_relaxed);
    s.render_p99_ms.store(u.render_p99_ms.load(), std::memory_order_relaxed);
    s.render_max_ms.store(u.render_max_ms.load(), std::memory_order_relaxed);
    s.snapshot_age_ms.store(u.snapshot_age_ms.load(), std::memory_order_relaxed);
    s.world_age_ms.store(u.world_age_ms.load(), std::memory_order_relaxed);
    s.camera_age_ms.store(u.camera_age_ms.load(), std::memory_order_relaxed);
    s.position_age_ms.store(u.position_age_ms.load(), std::memory_order_relaxed);
    s.bones_age_ms.store(u.bones_age_ms.load(), std::memory_order_relaxed);
    s.camera_position_delta_ms.store(u.camera_position_delta_ms.load(), std::memory_order_relaxed);
    s.prediction_ms.store(u.prediction_ms.load(), std::memory_order_relaxed);
    s.scheduler_wait_ms.store(u.scheduler_wait_ms.load(), std::memory_order_relaxed);
    s.entities.store(u.entities.load(), std::memory_order_relaxed);
    s.active.store(u.active.load(), std::memory_order_relaxed);
    s.pressure_level.store(u.pressure_level.load(), std::memory_order_relaxed);
    return s;
}

inline Sample& Warzone() {
    static Sample s;
    auto& u = Telemetry::Warzone();
    s.acquire_ms.store(u.acquire_ms.load(), std::memory_order_relaxed);
    s.acquire_p50_ms.store(u.acquire_p50_ms.load(), std::memory_order_relaxed);
    s.acquire_p95_ms.store(u.acquire_p95_ms.load(), std::memory_order_relaxed);
    s.acquire_p99_ms.store(u.acquire_p99_ms.load(), std::memory_order_relaxed);
    s.acquire_max_ms.store(u.acquire_max_ms.load(), std::memory_order_relaxed);
    s.render_ms.store(u.render_ms.load(), std::memory_order_relaxed);
    s.render_p95_ms.store(u.render_p95_ms.load(), std::memory_order_relaxed);
    s.render_p99_ms.store(u.render_p99_ms.load(), std::memory_order_relaxed);
    s.render_max_ms.store(u.render_max_ms.load(), std::memory_order_relaxed);
    s.snapshot_age_ms.store(u.snapshot_age_ms.load(), std::memory_order_relaxed);
    s.world_age_ms.store(u.world_age_ms.load(), std::memory_order_relaxed);
    s.camera_age_ms.store(u.camera_age_ms.load(), std::memory_order_relaxed);
    s.position_age_ms.store(u.position_age_ms.load(), std::memory_order_relaxed);
    s.bones_age_ms.store(u.bones_age_ms.load(), std::memory_order_relaxed);
    s.camera_position_delta_ms.store(u.camera_position_delta_ms.load(), std::memory_order_relaxed);
    s.prediction_ms.store(u.prediction_ms.load(), std::memory_order_relaxed);
    s.scheduler_wait_ms.store(u.scheduler_wait_ms.load(), std::memory_order_relaxed);
    s.entities.store(u.entities.load(), std::memory_order_relaxed);
    s.active.store(u.active.load(), std::memory_order_relaxed);
    s.pressure_level.store(u.pressure_level.load(), std::memory_order_relaxed);
    return s;
}

inline Sample& Fortnite() {
    static Sample s;
    auto& u = Telemetry::Fortnite();
    s.acquire_ms.store(u.acquire_ms.load(), std::memory_order_relaxed);
    s.acquire_p50_ms.store(u.acquire_p50_ms.load(), std::memory_order_relaxed);
    s.acquire_p95_ms.store(u.acquire_p95_ms.load(), std::memory_order_relaxed);
    s.acquire_p99_ms.store(u.acquire_p99_ms.load(), std::memory_order_relaxed);
    s.acquire_max_ms.store(u.acquire_max_ms.load(), std::memory_order_relaxed);
    s.render_ms.store(u.render_ms.load(), std::memory_order_relaxed);
    s.render_p95_ms.store(u.render_p95_ms.load(), std::memory_order_relaxed);
    s.render_p99_ms.store(u.render_p99_ms.load(), std::memory_order_relaxed);
    s.render_max_ms.store(u.render_max_ms.load(), std::memory_order_relaxed);
    s.snapshot_age_ms.store(u.snapshot_age_ms.load(), std::memory_order_relaxed);
    s.world_age_ms.store(u.world_age_ms.load(), std::memory_order_relaxed);
    s.camera_age_ms.store(u.camera_age_ms.load(), std::memory_order_relaxed);
    s.position_age_ms.store(u.position_age_ms.load(), std::memory_order_relaxed);
    s.bones_age_ms.store(u.bones_age_ms.load(), std::memory_order_relaxed);
    s.camera_position_delta_ms.store(u.camera_position_delta_ms.load(), std::memory_order_relaxed);
    s.prediction_ms.store(u.prediction_ms.load(), std::memory_order_relaxed);
    s.scheduler_wait_ms.store(u.scheduler_wait_ms.load(), std::memory_order_relaxed);
    s.entities.store(u.entities.load(), std::memory_order_relaxed);
    s.active.store(u.active.load(), std::memory_order_relaxed);
    s.pressure_level.store(u.pressure_level.load(), std::memory_order_relaxed);
    return s;
}

// Forward to unified telemetry
inline void ObserveAcquire(Sample& s, float work_ms, bool activeSample, uint64_t scan_id = 0) noexcept {
    if (!activeSample) return;
    s.acquire_ms.store(work_ms, std::memory_order_relaxed);
    // Also forward to unified
    int idx = 0;
    if (&s == &CS2()) Telemetry::ObserveAcquire(Telemetry::CS2(), work_ms, activeSample, scan_id);
    else if (&s == &Warzone()) Telemetry::ObserveAcquire(Telemetry::Warzone(), work_ms, activeSample, scan_id);
    else if (&s == &Fortnite()) Telemetry::ObserveAcquire(Telemetry::Fortnite(), work_ms, activeSample, scan_id);
    else Telemetry::ObserveAcquire(Telemetry::FiveM(), work_ms, activeSample, scan_id);
}

inline void ObserveRender(Sample& s, float ms) noexcept {
    s.render_ms.store(ms, std::memory_order_relaxed);
    if (&s == &CS2()) Telemetry::ObserveRender(Telemetry::CS2(), ms);
    else if (&s == &Warzone()) Telemetry::ObserveRender(Telemetry::Warzone(), ms);
    else if (&s == &Fortnite()) Telemetry::ObserveRender(Telemetry::Fortnite(), ms);
    else Telemetry::ObserveRender(Telemetry::FiveM(), ms);
}

inline void ObserveCameraAge(Sample& s, float age_ms) noexcept {
    s.camera_age_ms.store(age_ms, std::memory_order_relaxed);
    if (&s == &CS2()) Telemetry::ObserveCameraAge(Telemetry::CS2(), age_ms);
    else if (&s == &Warzone()) Telemetry::ObserveCameraAge(Telemetry::Warzone(), age_ms);
    else if (&s == &Fortnite()) Telemetry::ObserveCameraAge(Telemetry::Fortnite(), age_ms);
    else Telemetry::ObserveCameraAge(Telemetry::FiveM(), age_ms);
}

inline void ObserveCameraRead(Sample& s, float read_ms) noexcept {
    if (&s == &CS2()) Telemetry::ObserveCameraRead(Telemetry::CS2(), read_ms);
    else if (&s == &Warzone()) Telemetry::ObserveCameraRead(Telemetry::Warzone(), read_ms);
    else if (&s == &Fortnite()) Telemetry::ObserveCameraRead(Telemetry::Fortnite(), read_ms);
    else Telemetry::ObserveCameraRead(Telemetry::FiveM(), read_ms);
}

inline void ObserveBones(Sample& s, float bones_ms) noexcept {
    if (&s == &CS2()) Telemetry::ObserveBones(Telemetry::CS2(), bones_ms);
    else if (&s == &Warzone()) Telemetry::ObserveBones(Telemetry::Warzone(), bones_ms);
    else if (&s == &Fortnite()) Telemetry::ObserveBones(Telemetry::Fortnite(), bones_ms);
    else Telemetry::ObserveBones(Telemetry::FiveM(), bones_ms);
}

inline void ObservePresentationTiming(Sample& s, float position_age_ms, float bones_age_ms,
    float camera_position_delta_ms, float prediction_ms) noexcept {
    if (&s == &CS2()) Telemetry::ObservePresentationTiming(Telemetry::CS2(), position_age_ms, bones_age_ms, camera_position_delta_ms, prediction_ms);
    else if (&s == &Warzone()) Telemetry::ObservePresentationTiming(Telemetry::Warzone(), position_age_ms, bones_age_ms, camera_position_delta_ms, prediction_ms);
    else if (&s == &Fortnite()) Telemetry::ObservePresentationTiming(Telemetry::Fortnite(), position_age_ms, bones_age_ms, camera_position_delta_ms, prediction_ms);
    else Telemetry::ObservePresentationTiming(Telemetry::FiveM(), position_age_ms, bones_age_ms, camera_position_delta_ms, prediction_ms);
}

inline void ObserveSchedulerWait(Sample& s, float wait_ms) noexcept {
    s.scheduler_wait_ms.store(wait_ms, std::memory_order_relaxed);
    if (&s == &CS2()) Telemetry::ObserveSchedulerWait(Telemetry::CS2(), wait_ms);
    else if (&s == &Warzone()) Telemetry::ObserveSchedulerWait(Telemetry::Warzone(), wait_ms);
    else if (&s == &Fortnite()) Telemetry::ObserveSchedulerWait(Telemetry::Fortnite(), wait_ms);
    else Telemetry::ObserveSchedulerWait(Telemetry::FiveM(), wait_ms);
}

inline void RefreshFromMemory() noexcept {
    Telemetry::RefreshFromMemory();
}

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

inline void LogSpikeBreakdown(std::string_view tag, const SpikeBreakdown& b) noexcept {
    Telemetry::LogSpikeBreakdown(tag, b);
}

} // namespace OmniGhost::Gameplay::DmaTelemetry