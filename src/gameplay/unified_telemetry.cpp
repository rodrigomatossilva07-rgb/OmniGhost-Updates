#include "unified_telemetry.h"
#include <chrono>
#include <algorithm>

namespace OmniGhost::Gameplay::Telemetry {

namespace {
    UnifiedSample g_fivem;
    UnifiedSample g_cs2;
    UnifiedSample g_warzone;
    UnifiedSample g_fortnite;

    // Fixed-window percentile calculator (Welford's online algorithm for percentiles)
    struct PercentileWindow {
        static constexpr int kWindowSize = 1000;
        float values[kWindowSize];
        int count = 0;
        int head = 0;

        void Add(float v) {
            values[head] = v;
            head = (head + 1) % kWindowSize;
            if (count < kWindowSize) count++;
        }

        float Percentile(float p) const {
            if (count == 0) return 0.f;
            // Simple sort-based percentile (acceptable for 1000 elements)
            float sorted[kWindowSize];
            for (int i = 0; i < count; ++i) sorted[i] = values[i];
            std::sort(sorted, sorted + count);
            int idx = (int)(p * (count - 1));
            return sorted[idx];
        }

        float Avg() const {
            if (count == 0) return 0.f;
            float sum = 0.f;
            for (int i = 0; i < count; ++i) sum += values[i];
            return sum / count;
        }

        float Max() const {
            if (count == 0) return 0.f;
            float m = values[0];
            for (int i = 1; i < count; ++i) m = std::max(m, values[i]);
            return m;
        }
    };

    PercentileWindow g_acquire_window[4]; // 0=FiveM, 1=CS2, 2=Warzone, 3=Fortnite
    PercentileWindow g_render_window[4];
    PercentileWindow g_camera_window[4];
    PercentileWindow g_world_window[4];
    PercentileWindow g_position_window[4];
    PercentileWindow g_bones_window[4];
    PercentileWindow g_camera_pos_delta_window[4];

    int GameIndex(std::string_view tag) {
        if (tag == "FIVEM") return 0;
        if (tag == "CS2") return 1;
        if (tag == "WARZONE") return 2;
        if (tag == "FORTNITE") return 3;
        return 0;
    }
}

void SetEnabled(bool enabled) noexcept {
    FiveM().active.store(enabled, std::memory_order_relaxed);
    CS2().active.store(enabled, std::memory_order_relaxed);
    Warzone().active.store(enabled, std::memory_order_relaxed);
    Fortnite().active.store(enabled, std::memory_order_relaxed);
}

bool IsEnabled() noexcept {
    return FiveM().active.load(std::memory_order_relaxed) ||
           CS2().active.load(std::memory_order_relaxed) ||
           Warzone().active.load(std::memory_order_relaxed) ||
           Fortnite().active.load(std::memory_order_relaxed);
}

void Tick(std::string_view tag) {
    int idx = GameIndex(tag);
    auto& s = *[] (int i) -> UnifiedSample* {
        static UnifiedSample* arr[4] = { &FiveM(), &CS2(), &Warzone(), &Fortnite() };
        return arr[i];
    }(idx);

    // Update per-second rates from Memory diagnostics
    // (This would be called from the main loop)
}

void LogConfigChanged(std::string_view tag, std::string_view config_blob) {
    int idx = GameIndex(tag);
    auto& s = *[] (int i) -> UnifiedSample* {
        static UnifiedSample* arr[4] = { &FiveM(), &CS2(), &Warzone(), &Fortnite() };
        return arr[i];
    }(idx);
    s.config_change_count.fetch_add(1, std::memory_order_relaxed);
    // Could log the blob to file here
}

void LogSessionSummary(std::string_view tag) {
    // Could write summary to log file
}

void ObserveAcquire(UnifiedSample& s, float work_ms, bool activeSample, uint64_t scan_id) noexcept {
    if (!activeSample) return;

    int idx = 0;
    if (&s == &CS2()) idx = 1;
    else if (&s == &Warzone()) idx = 2;
    else if (&s == &Fortnite()) idx = 3;

    g_acquire_window[idx].Add(work_ms);

    s.acquire_ms.store(work_ms, std::memory_order_relaxed);
    s.acquire_avg_ms.store(g_acquire_window[idx].Avg(), std::memory_order_relaxed);
    s.acquire_p50_ms.store(g_acquire_window[idx].Percentile(0.50f), std::memory_order_relaxed);
    s.acquire_p95_ms.store(g_acquire_window[idx].Percentile(0.95f), std::memory_order_relaxed);
    s.acquire_p99_ms.store(g_acquire_window[idx].Percentile(0.99f), std::memory_order_relaxed);
    s.acquire_max_ms.store(g_acquire_window[idx].Max(), std::memory_order_relaxed);

    s.active.store(true, std::memory_order_relaxed);
}

void ObserveRender(UnifiedSample& s, float ms) noexcept {
    int idx = 0;
    if (&s == &CS2()) idx = 1;
    else if (&s == &Warzone()) idx = 2;
    else if (&s == &Fortnite()) idx = 3;

    g_render_window[idx].Add(ms);

    s.render_ms.store(ms, std::memory_order_relaxed);
    s.render_avg_ms.store(g_render_window[idx].Avg(), std::memory_order_relaxed);
    s.render_p50_ms.store(g_render_window[idx].Percentile(0.50f), std::memory_order_relaxed);
    s.render_p95_ms.store(g_render_window[idx].Percentile(0.95f), std::memory_order_relaxed);
    s.render_p99_ms.store(g_render_window[idx].Percentile(0.99f), std::memory_order_relaxed);
    s.render_max_ms.store(g_render_window[idx].Max(), std::memory_order_relaxed);
}

void ObserveCameraAge(UnifiedSample& s, float age_ms) noexcept {
    int idx = 0;
    if (&s == &CS2()) idx = 1;
    else if (&s == &Warzone()) idx = 2;
    else if (&s == &Fortnite()) idx = 3;

    g_camera_window[idx].Add(age_ms);

    s.camera_age_ms.store(age_ms, std::memory_order_relaxed);
    s.camera_age_avg_ms.store(g_camera_window[idx].Avg(), std::memory_order_relaxed);
    s.camera_age_p50_ms.store(g_camera_window[idx].Percentile(0.50f), std::memory_order_relaxed);
    s.camera_age_p95_ms.store(g_camera_window[idx].Percentile(0.95f), std::memory_order_relaxed);
    s.camera_age_p99_ms.store(g_camera_window[idx].Percentile(0.99f), std::memory_order_relaxed);
    s.camera_age_max_ms.store(g_camera_window[idx].Max(), std::memory_order_relaxed);
}

void ObserveCameraRead(UnifiedSample& s, float read_ms) noexcept {
    int idx = 0;
    if (&s == &CS2()) idx = 1;
    else if (&s == &Warzone()) idx = 2;
    else if (&s == &Fortnite()) idx = 3;

    // Camera reads tracked in acquire window
    s.camera_read_p95_ms.store(g_acquire_window[idx].Percentile(0.95f), std::memory_order_relaxed);
    s.camera_read_p99_ms.store(g_acquire_window[idx].Percentile(0.99f), std::memory_order_relaxed);
    s.camera_read_max_ms.store(g_acquire_window[idx].Max(), std::memory_order_relaxed);
}

void ObserveBones(UnifiedSample& s, float bones_ms) noexcept {
    int idx = 0;
    if (&s == &CS2()) idx = 1;
    else if (&s == &Warzone()) idx = 2;
    else if (&s == &Fortnite()) idx = 3;

    g_bones_window[idx].Add(bones_ms);
}

void ObservePresentationTiming(UnifiedSample& s, float position_age_ms, float bones_age_ms,
                               float camera_position_delta_ms, float prediction_ms) noexcept {
    int idx = 0;
    if (&s == &CS2()) idx = 1;
    else if (&s == &Warzone()) idx = 2;
    else if (&s == &Fortnite()) idx = 3;

    g_position_window[idx].Add(position_age_ms);
    g_camera_pos_delta_window[idx].Add(camera_position_delta_ms);

    s.snapshot_age_ms.store(position_age_ms, std::memory_order_relaxed);
    s.position_age_ms.store(position_age_ms, std::memory_order_relaxed);
    s.position_age_avg_ms.store(g_position_window[idx].Avg(), std::memory_order_relaxed);
    s.position_age_p50_ms.store(g_position_window[idx].Percentile(0.50f), std::memory_order_relaxed);
    s.position_age_p95_ms.store(g_position_window[idx].Percentile(0.95f), std::memory_order_relaxed);
    s.position_age_p99_ms.store(g_position_window[idx].Percentile(0.99f), std::memory_order_relaxed);
    s.position_age_max_ms.store(g_position_window[idx].Max(), std::memory_order_relaxed);

    s.bones_age_ms.store(bones_age_ms, std::memory_order_relaxed);
    s.bones_age_avg_ms.store(g_bones_window[idx].Avg(), std::memory_order_relaxed);
    s.bones_age_p50_ms.store(g_bones_window[idx].Percentile(0.50f), std::memory_order_relaxed);
    s.bones_age_p95_ms.store(g_bones_window[idx].Percentile(0.95f), std::memory_order_relaxed);
    s.bones_age_p99_ms.store(g_bones_window[idx].Percentile(0.99f), std::memory_order_relaxed);
    s.bones_age_max_ms.store(g_bones_window[idx].Max(), std::memory_order_relaxed);

    s.camera_position_delta_ms.store(camera_position_delta_ms, std::memory_order_relaxed);
    s.camera_position_delta_p95_ms.store(g_camera_pos_delta_window[idx].Percentile(0.95f), std::memory_order_relaxed);
    s.camera_position_delta_p99_ms.store(g_camera_pos_delta_window[idx].Percentile(0.99f), std::memory_order_relaxed);
    s.camera_position_delta_max_ms.store(g_camera_pos_delta_window[idx].Max(), std::memory_order_relaxed);

    s.prediction_ms.store(prediction_ms, std::memory_order_relaxed);
    s.prediction_p95_ms.store(g_bones_window[idx].Percentile(0.95f), std::memory_order_relaxed);
    s.prediction_p99_ms.store(g_bones_window[idx].Percentile(0.99f), std::memory_order_relaxed);
    s.prediction_max_ms.store(g_bones_window[idx].Max(), std::memory_order_relaxed);
}

void ObserveSchedulerWait(UnifiedSample& s, float wait_ms) noexcept {
    s.scheduler_wait_ms.store(wait_ms, std::memory_order_relaxed);
}

void RefreshFromMemory() noexcept {
    // Would pull Memory diagnostics into rates
    // For now, stub
}

void LogSpikeBreakdown(std::string_view tag, const SpikeBreakdown& b) noexcept {
    // Would write to log file
}

UnifiedSample& FiveM() { return g_fivem; }
UnifiedSample& CS2() { return g_cs2; }
UnifiedSample& Warzone() { return g_warzone; }
UnifiedSample& Fortnite() { return g_fortnite; }

} // namespace OmniGhost::Gameplay::Telemetry