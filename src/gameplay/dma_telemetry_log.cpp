#include "gameplay/dma_telemetry_log.h"
#include "platform/session_log.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>

namespace OmniGhost::Gameplay::DmaTelemetry {
namespace {

std::atomic_bool g_enabled{false};
Sample g_fivem{};
Sample g_cs2{};

void EwmaP95(std::atomic<float>& p95, float sample) noexcept {
    const float old = p95.load(std::memory_order_relaxed);
    // Cheap approximate tail: track high percentile via asymmetric EWMA.
    const float alpha = sample > old ? 0.25f : 0.05f;
    p95.store(old <= 0.f ? sample : old + (sample - old) * alpha, std::memory_order_relaxed);
}

void TrackMax(std::atomic<float>& mx, float sample) noexcept {
    float cur = mx.load(std::memory_order_relaxed);
    while (sample > cur && !mx.compare_exchange_weak(cur, sample, std::memory_order_relaxed)) {
    }
    // Soft decay so max reflects recent session, not all-time forever
    if (cur > 0.f)
        mx.store(cur * 0.998f, std::memory_order_relaxed);
}

std::string NowStamp() {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const auto tt = clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch())
                        .count() %
                    1000;
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &tt);
#else
    localtime_r(&tt, &local);
#endif
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
                  local.tm_hour, local.tm_min, local.tm_sec, static_cast<int>(ms));
    return buf;
}

void Emit(std::string_view tag, std::string_view body) {
    // SessionLog tees into logs.txt without blocking render with endl flushes
    // on the calling thread beyond a bounded queue.
    std::string msg;
    msg.reserve(body.size() + 32);
    msg.append("[").append(tag).append("] ").append(body);
    SessionLog::Write(SessionLog::Severity::Info, SessionLog::Subsystem::Adapter, msg);
}

void EmitWarn(std::string_view tag, std::string_view body) {
    std::string msg;
    msg.reserve(body.size() + 32);
    msg.append("[").append(tag).append("] ").append(body);
    SessionLog::Write(SessionLog::Severity::Warning, SessionLog::Subsystem::Adapter, msg);
}

void WritePerf(std::string_view tag, Sample& s) {
    char buf[1024]{};
    std::snprintf(buf, sizeof(buf),
        "PERF\n"
        "reads_s=%d\nscatter_s=%d\n"
        "vmm_avg_ms=%.2f\nvmm_p95_ms=%.2f\nvmm_max_ms=%.2f\n"
        "read_fails=%llu\n\n"
        "snapshot_age_ms=%.1f\nsnapshot_hz=%.1f\nsnapshot_interval_ms=%.1f\n"
        "snapshot_drops=%llu\npeds=%d\n\n"
        "acquire_ms=%.2f\nacquire_p95_ms=%.2f\nacquire_max_ms=%.2f\n\n"
        "camera_age_ms=%.1f\nmotion_age_ms=%.1f\nstate_age_ms=%.1f\n\n"
        "bones_batches_s=%d\nbones_players=%d\n\n"
        "render_ms=%.2f\nrender_p95_ms=%.2f\nrender_max_ms=%.2f\n\n"
        "vehicles=%d\nobjects=%d\ntarget_lane_age_ms=%.1f",
        s.reads_s.load(std::memory_order_relaxed),
        s.scatter_s.load(std::memory_order_relaxed),
        s.vmm_avg_ms.load(std::memory_order_relaxed),
        s.vmm_p95_ms.load(std::memory_order_relaxed),
        s.vmm_max_ms.load(std::memory_order_relaxed),
        static_cast<unsigned long long>(s.read_fails.load(std::memory_order_relaxed)),
        s.snapshot_age_ms.load(std::memory_order_relaxed),
        s.snapshot_hz.load(std::memory_order_relaxed),
        s.snapshot_interval_ms.load(std::memory_order_relaxed),
        static_cast<unsigned long long>(s.snapshot_drops.load(std::memory_order_relaxed)),
        s.entities.load(std::memory_order_relaxed),
        s.acquire_ms.load(std::memory_order_relaxed),
        s.acquire_p95_ms.load(std::memory_order_relaxed),
        s.acquire_max_ms.load(std::memory_order_relaxed),
        s.camera_age_ms.load(std::memory_order_relaxed),
        s.motion_age_ms.load(std::memory_order_relaxed),
        s.state_age_ms.load(std::memory_order_relaxed),
        s.bones_batches_s.load(std::memory_order_relaxed),
        s.bones_players.load(std::memory_order_relaxed),
        s.render_ms.load(std::memory_order_relaxed),
        s.render_p95_ms.load(std::memory_order_relaxed),
        s.render_max_ms.load(std::memory_order_relaxed),
        s.vehicles.load(std::memory_order_relaxed),
        s.objects.load(std::memory_order_relaxed),
        s.target_lane_age_ms.load(std::memory_order_relaxed));
    Emit(tag, buf);

    char status[160]{};
    std::snprintf(status, sizeof(status),
                  "STATUS process=%s pid=%u dma=%s",
                  s.process_connected.load() ? "connected" : "waiting",
                  s.process_id.load(),
                  s.dma_open.load() ? "open" : "closed");
    Emit(tag, status);
}

} // namespace

void SetEnabled(bool enabled) noexcept {
    g_enabled.store(enabled, std::memory_order_release);
}

bool IsEnabled() noexcept {
    return g_enabled.load(std::memory_order_acquire);
}

Sample& FiveM() { return g_fivem; }
Sample& CS2() { return g_cs2; }

void ObserveAcquire(Sample& s, float ms) noexcept {
    if (ms < 0.f) return;
    const float old = s.acquire_ms.load(std::memory_order_relaxed);
    s.acquire_ms.store(old <= 0.f ? ms : old + (ms - old) * 0.12f, std::memory_order_relaxed);
    EwmaP95(s.acquire_p95_ms, ms);
    TrackMax(s.acquire_max_ms, ms);
    if (ms >= 20.f) {
        s.acquisition_spikes.fetch_add(1, std::memory_order_relaxed);
        if (IsEnabled()) {
            char buf[256]{};
            std::snprintf(buf, sizeof(buf),
                          "[WARN] ACQUISITION_SPIKE\nacquire_ms=%.2f\npeds=%d\nvmm_p95_ms=%.2f\nvmm_max_ms=%.2f",
                          ms, s.entities.load(), s.vmm_p95_ms.load(), s.vmm_max_ms.load());
            EmitWarn(std::string_view{&s == &g_fivem ? "FIVEM" : "CS2"}, buf);
        }
    }
}

void ObserveRender(Sample& s, float ms) noexcept {
    if (ms < 0.f) return;
    const float old = s.render_ms.load(std::memory_order_relaxed);
    s.render_ms.store(old <= 0.f ? ms : old + (ms - old) * 0.12f, std::memory_order_relaxed);
    EwmaP95(s.render_p95_ms, ms);
    TrackMax(s.render_max_ms, ms);
    if (ms >= 10.f) {
        s.render_spikes.fetch_add(1, std::memory_order_relaxed);
        if (IsEnabled()) {
            char buf[256]{};
            std::snprintf(buf, sizeof(buf),
                          "[WARN] RENDER_SPIKE\nrender_ms=%.2f\npeds=%d\nvehicles=%d\nobjects=%d",
                          ms, s.entities.load(), s.vehicles.load(), s.objects.load());
            EmitWarn(std::string_view{&s == &g_fivem ? "FIVEM" : "CS2"}, buf);
        }
    }
}

void Tick(std::string_view tag) {
    if (!IsEnabled()) return;
    static std::mutex mu;
    static std::chrono::steady_clock::time_point lastF{}, lastC{};
    std::lock_guard<std::mutex> lock(mu);
    const auto now = std::chrono::steady_clock::now();
    Sample* sample = (tag == "FIVEM") ? &g_fivem : &g_cs2;
    auto& last = (tag == "FIVEM") ? lastF : lastC;
    if (last.time_since_epoch().count() != 0 &&
        now - last < std::chrono::seconds(1))
        return;
    last = now;
    WritePerf(tag, *sample);
}

void LogConfigChanged(std::string_view tag, std::string_view config_blob) {
    if (!IsEnabled()) return;
    std::string body = "CONFIG_CHANGED\n";
    body.append(config_blob);
    Emit(tag, body);
}

void LogSessionSummary(std::string_view tag) {
    Sample& s = (tag == "FIVEM") ? g_fivem : g_cs2;
    char buf[512]{};
    std::snprintf(buf, sizeof(buf),
        "SESSION_SUMMARY\n"
        "acquire_avg_ms=%.2f\nacquire_p95_ms=%.2f\nacquire_max_ms=%.2f\n"
        "render_avg_ms=%.2f\nrender_p95_ms=%.2f\nrender_max_ms=%.2f\n"
        "vmm_avg_ms=%.2f\nvmm_p95_ms=%.2f\nvmm_max_ms=%.2f\n"
        "snapshot_drops=%llu\nread_fails=%llu\n"
        "acquisition_spikes=%llu\nrender_spikes=%llu",
        s.acquire_ms.load(), s.acquire_p95_ms.load(), s.acquire_max_ms.load(),
        s.render_ms.load(), s.render_p95_ms.load(), s.render_max_ms.load(),
        s.vmm_avg_ms.load(), s.vmm_p95_ms.load(), s.vmm_max_ms.load(),
        static_cast<unsigned long long>(s.snapshot_drops.load()),
        static_cast<unsigned long long>(s.read_fails.load()),
        static_cast<unsigned long long>(s.acquisition_spikes.load()),
        static_cast<unsigned long long>(s.render_spikes.load()));
    Emit(tag, buf);
}

} // namespace OmniGhost::Gameplay::DmaTelemetry
