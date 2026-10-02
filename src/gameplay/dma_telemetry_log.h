#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

namespace OmniGhost::Gameplay::DmaTelemetry {

void SetEnabled(bool enabled) noexcept;
[[nodiscard]] bool IsEnabled() noexcept;

// Tag is "FIVEM" or "CS2".
void Tick(std::string_view tag);

void LogConfigChanged(std::string_view tag, std::string_view config_blob);
void LogSessionSummary(std::string_view tag);

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

struct Sample {
    std::atomic<float> acquire_ms{0.f};
    std::atomic<float> acquire_p50_ms{0.f};
    std::atomic<float> acquire_p95_ms{0.f};
    std::atomic<float> acquire_p99_ms{0.f};
    std::atomic<float> acquire_max_ms{0.f};
    std::atomic<float> pressure_window_p95_ms{0.f};
    std::atomic<float> render_ms{0.f};
    std::atomic<float> render_p95_ms{0.f};
    std::atomic<float> render_max_ms{0.f};
    std::atomic<float> snapshot_age_ms{0.f};
    std::atomic<float> world_age_ms{0.f};
    std::atomic<float> publish_delay_ms{0.f};
    std::atomic<float> visibility_ms{0.f};
    std::atomic<int> visibility_players_tested{0};
    std::atomic<int> visibility_rays{0};
    std::atomic<float> snapshot_interval_ms{0.f};
    std::atomic<float> snapshot_hz{0.f};
    std::atomic<float> camera_age_ms{0.f};
	std::atomic<float> camera_age_avg_ms{0.f};
	std::atomic<float> camera_age_p50_ms{0.f};
	std::atomic<float> camera_age_p95_ms{0.f};
	std::atomic<float> camera_age_p99_ms{0.f};
	std::atomic<float> camera_age_max_ms{0.f};
	std::atomic<float> camera_read_p95_ms{0.f};
	std::atomic<float> camera_read_p99_ms{0.f};
	std::atomic<float> camera_read_max_ms{0.f};
	std::atomic<std::uint64_t> camera_busy_skips{0};
	std::atomic<std::uint64_t> camera_busy_retry_success{0};
	std::atomic<std::uint64_t> camera_consecutive_busy_max{0};
	std::atomic<std::uint64_t> camera_cooldown_skips{0};
	std::atomic<std::uint64_t> camera_cooldown_bypasses{0};
	std::atomic<int> pressure_level{0};
	std::atomic<std::uint64_t> cooldown_remaining_ms{0};
    std::atomic<float> death_to_hide_ms{0.f};
    std::atomic<std::uint64_t> recent_player_reinsertions{0};
    std::atomic<std::uint64_t> last_good_age_ms{0};
    std::atomic<float> position_age_ms{0.f};
    std::atomic<float> position_age_avg_ms{0.f};
    std::atomic<float> position_age_p95_ms{0.f};
    std::atomic<float> position_age_p99_ms{0.f};
    std::atomic<float> position_age_max_ms{0.f};
    std::atomic<float> bones_age_ms{0.f};
    std::atomic<float> bones_age_avg_ms{0.f};
    std::atomic<float> bones_age_p95_ms{0.f};
    std::atomic<float> bones_age_p99_ms{0.f};
    std::atomic<float> bones_age_max_ms{0.f};
    std::atomic<float> camera_position_delta_ms{0.f};
    std::atomic<float> camera_position_delta_p95_ms{0.f};
    std::atomic<float> camera_position_delta_p99_ms{0.f};
    std::atomic<float> camera_position_delta_max_ms{0.f};
    std::atomic<float> prediction_ms{0.f};
    std::atomic<float> prediction_p95_ms{0.f};
    std::atomic<float> prediction_p99_ms{0.f};
    std::atomic<float> prediction_max_ms{0.f};
    std::atomic<std::uint64_t> cached_bones_used{0};
    std::atomic<float> cached_bones_age_max_ms{0.f};
    std::atomic<float> motion_age_ms{0.f};
    std::atomic<float> state_age_ms{0.f};
    std::atomic<float> target_lane_age_ms{0.f};
    std::atomic<float> vmm_avg_ms{0.f};
    std::atomic<float> vmm_p50_ms{0.f};
    std::atomic<float> vmm_p95_ms{0.f};
    std::atomic<float> vmm_p99_ms{0.f};
    std::atomic<float> vmm_max_ms{0.f};
    std::atomic<float> scheduler_wait_ms{0.f};
    std::atomic<int> entities{0};
    std::atomic<int> vehicles{0};
    std::atomic<int> objects{0};
    std::atomic<int> bones_players{0};
    std::atomic<int> bones_batches_s{0};
    std::atomic<float> bones_p95_ms{0.f};
    std::atomic<float> bones_p99_ms{0.f};
    std::atomic<float> bones_max_ms{0.f};
    std::atomic<int> reads_s{0};
    std::atomic<int> scatter_s{0};
    std::atomic<std::uint64_t> snapshot_drops{0};
    std::atomic<std::uint64_t> read_fails{0};
    std::atomic<std::uint64_t> acquisition_spikes{0};
    std::atomic<std::uint64_t> render_spikes{0};
    std::atomic<std::uint64_t> suppressed_spikes_20{0};
    std::atomic<std::uint64_t> suppressed_spikes_50{0};
    std::atomic<std::uint64_t> suppressed_spikes_100{0};
    std::atomic<std::uint32_t> process_id{0};
    std::atomic_bool process_connected{false};
    std::atomic_bool dma_open{false};
    std::atomic_bool active{false}; // gameplay acquisition samples only when true
    std::atomic<int> idle_reason{0}; // 0=none,1=dead,2=not_in_match,3=menu,4=esp_off,5=no_local
};

[[nodiscard]] Sample& FiveM();
[[nodiscard]] Sample& CS2();

// activeSample=false → idle/dead/menu: never enters percentiles or SPIKE logs.
void ObserveAcquire(Sample& s, float work_ms, bool activeSample, uint64_t scan_id = 0) noexcept;
void ObserveRender(Sample& s, float ms) noexcept;
void ObserveCameraAge(Sample& s, float age_ms) noexcept;
void ObserveCameraRead(Sample& s, float read_ms) noexcept;
void ObserveBones(Sample& s, float bones_ms) noexcept;
// Presentation timing is sampled by the renderer for every player it draws.
// These windows make temporal skew visible instead of relying on one last value.
void ObservePresentationTiming(Sample& s, float position_age_ms, float bones_age_ms,
    float camera_position_delta_ms, float prediction_ms) noexcept;
void ObserveSchedulerWait(Sample& s, float wait_ms) noexcept;

// Pull Memory diagnostics into rates (reads_s / scatter_s / vmm_*).
void RefreshFromMemory() noexcept;

// Detailed acquire phase breakdown (only emitted for real spikes).
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

} // namespace OmniGhost::Gameplay::DmaTelemetry
