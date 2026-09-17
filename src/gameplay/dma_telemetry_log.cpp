#include "gameplay/dma_telemetry_log.h"
#include "platform/session_log.h"
#include "../../DMALibrary/Memory/Memory.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>
#include <Windows.h>
#include <psapi.h>

namespace OmniGhost::Gameplay::DmaTelemetry {
namespace {

constexpr int kWindow = 256;
constexpr float kSpikeWarnMs = 50.f;
constexpr float kSpikeSevereMs = 200.f;
constexpr float kSpikeCriticalMs = 500.f;

std::atomic_bool g_enabled{false};
Sample g_fivem{};
Sample g_cs2{};

struct Ring {
    std::array<float, kWindow> data{};
    int count = 0;
    int write = 0;
    std::mutex mu;

    void Push(float v) {
        std::lock_guard<std::mutex> lock(mu);
        data[static_cast<size_t>(write % kWindow)] = v;
        write = (write + 1) % kWindow;
        if (count < kWindow) ++count;
    }

    WindowStats Stats() {
        std::lock_guard<std::mutex> lock(mu);
        WindowStats s{};
        if (count <= 0) return s;
        std::vector<float> sorted;
        sorted.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
            sorted.push_back(data[static_cast<size_t>(i)]);
        // Ring may not be 0..count if wrapped — reconstruct properly
        sorted.clear();
        if (count < kWindow) {
            for (int i = 0; i < count; ++i)
                sorted.push_back(data[static_cast<size_t>(i)]);
        } else {
            for (int i = 0; i < kWindow; ++i)
                sorted.push_back(data[static_cast<size_t>((write + i) % kWindow)]);
        }
        std::sort(sorted.begin(), sorted.end());
        const int n = static_cast<int>(sorted.size());
        s.samples = n;
        s.last = sorted.back(); // approximate; real last pushed separately
        double sum = 0.0;
        for (float v : sorted) sum += v;
        s.avg = static_cast<float>(sum / n);
        auto at = [&](float q) -> float {
            const int idx = std::clamp(static_cast<int>(std::ceil(q * n) - 1), 0, n - 1);
            return sorted[static_cast<size_t>(idx)];
        };
        s.p50 = at(0.50f);
        s.p95 = at(0.95f);
        s.p99 = at(0.99f);
        s.max = sorted.back();
        // Enforce ordering
        s.p95 = (std::max)(s.p95, s.p50);
        s.p99 = (std::max)(s.p99, s.p95);
        s.max = (std::max)(s.max, s.p99);
        return s;
    }
};

Ring g_acqF{}, g_acqC{};
Ring g_renderF{}, g_renderC{};
Ring g_dmaExec{};

std::atomic<float> g_lastAcqF{0.f}, g_lastAcqC{0.f};

std::chrono::steady_clock::time_point g_lastSpikeWarn{};
std::chrono::steady_clock::time_point g_lastSpikeSevere{};

uint64_t g_prevReads = 0;
uint64_t g_prevScatter = 0;
std::chrono::steady_clock::time_point g_prevMemSample{};

void Emit(std::string_view tag, std::string_view body) {
    std::string msg;
    msg.reserve(body.size() + 16);
    msg.append("[").append(tag).append("] ").append(body);
    SessionLog::Write(SessionLog::Severity::Info, SessionLog::Subsystem::Adapter, msg);
}

void EmitWarn(std::string_view tag, std::string_view body) {
    std::string msg;
    msg.reserve(body.size() + 16);
    msg.append("[").append(tag).append("] ").append(body);
    SessionLog::Write(SessionLog::Severity::Warning, SessionLog::Subsystem::Adapter, msg);
}

void PublishAcquireStats(Sample& s, Ring& ring, float last) {
    auto st = ring.Stats();
    st.last = last;
    s.acquire_ms.store(st.avg, std::memory_order_relaxed);
    s.acquire_p50_ms.store(st.p50, std::memory_order_relaxed);
    s.acquire_p95_ms.store(st.p95, std::memory_order_relaxed);
    s.acquire_p99_ms.store(st.p99, std::memory_order_relaxed);
    s.acquire_max_ms.store(st.max, std::memory_order_relaxed);
}

const char* IdleName(int r) {
    switch (r) {
    case 1: return "dead";
    case 2: return "not_in_match";
    case 3: return "menu";
    case 4: return "esp_disabled";
    case 5: return "no_local_player";
    default: return "none";
    }
}

void WritePerf(std::string_view tag, Sample& s) {
    char buf[1400]{};
    std::snprintf(buf, sizeof(buf),
        "PERF\n"
        "active=%d\nalive=%d\npid=%u\nidle_reason=%s\n\n"
        "players=%d\n"
        "reads_s=%d\nscatter_batches_s=%d\n"
        "dma_avg_ms=%.2f\ndma_p50_ms=%.2f\ndma_p95_ms=%.2f\ndma_p99_ms=%.2f\ndma_max_ms=%.2f\n"
        "read_fails=%llu\n\n"
        "acquire_avg_ms=%.2f\nacquire_p50_ms=%.2f\nacquire_p95_ms=%.2f\nacquire_p99_ms=%.2f\nacquire_max_ms=%.2f\n"
        "scheduler_wait_ms=%.2f\n\n"
        "snapshot_age_ms=%.1f\nsnapshot_hz=%.1f\nsnapshot_interval_ms=%.1f\nsnapshot_drops=%llu\n\n"
        "camera_age_ms=%.1f\nmotion_age_ms=%.1f\n"
        "bones_players=%d\n"
        "render_avg_ms=%.2f\nrender_p95_ms=%.2f\nrender_max_ms=%.2f\n\n"
        "suppressed_spikes_20=%llu\nsuppressed_spikes_50=%llu\nsuppressed_spikes_100=%llu",
        s.active.load() ? 1 : 0,
        s.process_connected.load() ? 1 : 0,
        s.process_id.load(),
        IdleName(s.idle_reason.load()),
        s.entities.load(),
        s.reads_s.load(),
        s.scatter_s.load(),
        s.vmm_avg_ms.load(),
        s.vmm_p50_ms.load(),
        s.vmm_p95_ms.load(),
        s.vmm_p99_ms.load(),
        s.vmm_max_ms.load(),
        static_cast<unsigned long long>(s.read_fails.load()),
        s.acquire_ms.load(),
        s.acquire_p50_ms.load(),
        s.acquire_p95_ms.load(),
        s.acquire_p99_ms.load(),
        s.acquire_max_ms.load(),
        s.scheduler_wait_ms.load(),
        s.snapshot_age_ms.load(),
        s.snapshot_hz.load(),
        s.snapshot_interval_ms.load(),
        static_cast<unsigned long long>(s.snapshot_drops.load()),
        s.camera_age_ms.load(),
        s.motion_age_ms.load(),
        s.bones_players.load(),
        s.render_ms.load(),
        s.render_p95_ms.load(),
        s.render_max_ms.load(),
        static_cast<unsigned long long>(s.suppressed_spikes_20.load()),
        static_cast<unsigned long long>(s.suppressed_spikes_50.load()),
        static_cast<unsigned long long>(s.suppressed_spikes_100.load()));
    // Resource footprint (leak detection)
    DWORD handleCount = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handleCount);
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    SIZE_T working = 0, priv = 0;
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        working = pmc.WorkingSetSize;
        priv = pmc.PrivateUsage;
    }
    char res[256]{};
    std::snprintf(res, sizeof(res),
        "\nprocess_handles=%lu\nprocess_threads=%lu\nworking_set_mb=%.1f\nprivate_mb=%.1f",
        static_cast<unsigned long>(handleCount),
        static_cast<unsigned long>(0), // filled below if possible
        working / (1024.0 * 1024.0),
        priv / (1024.0 * 1024.0));
    // append resource block
    std::string full = buf;
    full.append(res);
    // thread count via Toolhelp is heavier — skip; handle/memory is enough for leak rate
    Emit(tag, full);
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

void ObserveAcquire(Sample& s, float work_ms, bool activeSample, uint64_t scan_id) noexcept {
    if (work_ms < 0.f) return;
    if (!activeSample) {
        s.active.store(false, std::memory_order_relaxed);
        return;
    }
    s.active.store(true, std::memory_order_relaxed);
    s.idle_reason.store(0, std::memory_order_relaxed);

    const bool isCs2 = (&s == &g_cs2);
    Ring& ring = isCs2 ? g_acqC : g_acqF;
    ring.Push(work_ms);
    if (isCs2) g_lastAcqC.store(work_ms, std::memory_order_relaxed);
    else g_lastAcqF.store(work_ms, std::memory_order_relaxed);
    PublishAcquireStats(s, ring, work_ms);

    // Rate-limited spike reporting (never on idle)
    if (!IsEnabled()) return;
    const auto now = std::chrono::steady_clock::now();
    if (work_ms < 20.f) return;
    if (work_ms < 50.f) {
        s.suppressed_spikes_20.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (work_ms < 100.f) {
        s.suppressed_spikes_50.fetch_add(1, std::memory_order_relaxed);
        if (now - g_lastSpikeWarn < std::chrono::milliseconds(500))
            return;
        g_lastSpikeWarn = now;
    } else if (work_ms < kSpikeSevereMs) {
        s.suppressed_spikes_100.fetch_add(1, std::memory_order_relaxed);
        if (now - g_lastSpikeWarn < std::chrono::milliseconds(500))
            return;
        g_lastSpikeWarn = now;
    } else if (work_ms < kSpikeCriticalMs) {
        if (now - g_lastSpikeSevere < std::chrono::milliseconds(250))
            return;
        g_lastSpikeSevere = now;
    }

    s.acquisition_spikes.fetch_add(1, std::memory_order_relaxed);
    char buf[256]{};
    std::snprintf(buf, sizeof(buf),
                  "SPIKE\nscan_id=%llu\ntotal_ms=%.2f\nplayers=%d\nreads_s=%d\nscatter_s=%d",
                  static_cast<unsigned long long>(scan_id),
                  work_ms, s.entities.load(), s.reads_s.load(), s.scatter_s.load());
    EmitWarn(isCs2 ? "CS2" : "FIVEM", buf);
}

void ObserveRender(Sample& s, float ms) noexcept {
    if (ms < 0.f) return;
    Ring& ring = (&s == &g_cs2) ? g_renderC : g_renderF;
    ring.Push(ms);
    auto st = ring.Stats();
    s.render_ms.store(st.avg, std::memory_order_relaxed);
    s.render_p95_ms.store(st.p95, std::memory_order_relaxed);
    s.render_max_ms.store(st.max, std::memory_order_relaxed);
}

void ObserveSchedulerWait(Sample& s, float wait_ms) noexcept {
    if (wait_ms < 0.f) return;
    const float old = s.scheduler_wait_ms.load(std::memory_order_relaxed);
    s.scheduler_wait_ms.store(old <= 0.f ? wait_ms : old + (wait_ms - old) * 0.15f,
                              std::memory_order_relaxed);
}

void RefreshFromMemory() noexcept {
    const auto snap = mem.GetDiagnosticsSnapshot();
    const auto now = std::chrono::steady_clock::now();
    double dt = 1.0;
    if (g_prevMemSample.time_since_epoch().count() != 0) {
        dt = std::chrono::duration<double>(now - g_prevMemSample).count();
        if (dt < 0.05) dt = 0.05;
    }
    g_prevMemSample = now;

    const uint64_t reads = snap.readRequestCount;
    const uint64_t scat = snap.scatterReadBatchCount;
    const int reads_s = static_cast<int>((reads - g_prevReads) / dt);
    const int scat_s = static_cast<int>((scat - g_prevScatter) / dt);
    g_prevReads = reads;
    g_prevScatter = scat;

    auto fill = [&](Sample& s) {
        s.reads_s.store((std::max)(0, reads_s), std::memory_order_relaxed);
        s.scatter_s.store((std::max)(0, scat_s), std::memory_order_relaxed);
        s.vmm_avg_ms.store(static_cast<float>(snap.vmmLatencyAverageMs), std::memory_order_relaxed);
        s.vmm_p50_ms.store(static_cast<float>(snap.vmmLatencyP50Ms), std::memory_order_relaxed);
        s.vmm_p95_ms.store(static_cast<float>(snap.vmmLatencyP95Ms), std::memory_order_relaxed);
        s.vmm_p99_ms.store(static_cast<float>(snap.vmmLatencyP95Ms), std::memory_order_relaxed);
        s.vmm_max_ms.store(static_cast<float>(snap.maxVmmLatencyMs), std::memory_order_relaxed);
        s.read_fails.store(snap.readFailureCount, std::memory_order_relaxed);
        s.dma_open.store(snap.deviceOpen, std::memory_order_relaxed);
        s.process_id.store(static_cast<std::uint32_t>(snap.processId), std::memory_order_relaxed);
        s.process_connected.store(snap.processInitialized, std::memory_order_relaxed);
    };
    fill(g_cs2);
    fill(g_fivem);
}

void Tick(std::string_view tag) {
    if (!IsEnabled()) return;
    static std::mutex mu;
    static std::chrono::steady_clock::time_point lastF{}, lastC{};
    std::lock_guard<std::mutex> lock(mu);
    RefreshFromMemory();
    const auto now = std::chrono::steady_clock::now();
    Sample* sample = (tag == "FIVEM") ? &g_fivem : &g_cs2;
    auto& last = (tag == "FIVEM") ? lastF : lastC;
    // PERF once/sec only while active gameplay for CS2/FiveM samples
    if (!sample->active.load(std::memory_order_relaxed)) {
        // Still emit a light STATUS at most every 5s when idle so logs show state
        if (last.time_since_epoch().count() != 0 &&
            now - last < std::chrono::seconds(5))
            return;
        last = now;
        char status[192]{};
        std::snprintf(status, sizeof(status),
                      "STATUS process=%s pid=%u dma=%s active=0 idle_reason=%s",
                      sample->process_connected.load() ? "connected" : "waiting",
                      sample->process_id.load(),
                      sample->dma_open.load() ? "open" : "closed",
                      IdleName(sample->idle_reason.load()));
        Emit(tag, status);
        return;
    }
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
    char buf[640]{};
    std::snprintf(buf, sizeof(buf),
        "SESSION_SUMMARY\n"
        "acquire_avg_ms=%.2f\nacquire_p50_ms=%.2f\nacquire_p95_ms=%.2f\nacquire_p99_ms=%.2f\nacquire_max_ms=%.2f\n"
        "dma_avg_ms=%.2f\ndma_p95_ms=%.2f\ndma_max_ms=%.2f\n"
        "render_avg_ms=%.2f\nrender_p95_ms=%.2f\nrender_max_ms=%.2f\n"
        "snapshot_drops=%llu\nread_fails=%llu\nspikes=%llu\n"
        "suppressed_20=%llu\nsuppressed_50=%llu\nsuppressed_100=%llu",
        s.acquire_ms.load(), s.acquire_p50_ms.load(), s.acquire_p95_ms.load(),
        s.acquire_p99_ms.load(), s.acquire_max_ms.load(),
        s.vmm_avg_ms.load(), s.vmm_p95_ms.load(), s.vmm_max_ms.load(),
        s.render_ms.load(), s.render_p95_ms.load(), s.render_max_ms.load(),
        static_cast<unsigned long long>(s.snapshot_drops.load()),
        static_cast<unsigned long long>(s.read_fails.load()),
        static_cast<unsigned long long>(s.acquisition_spikes.load()),
        static_cast<unsigned long long>(s.suppressed_spikes_20.load()),
        static_cast<unsigned long long>(s.suppressed_spikes_50.load()),
        static_cast<unsigned long long>(s.suppressed_spikes_100.load()));
    Emit(tag, buf);
}

void LogSpikeBreakdown(std::string_view tag, const SpikeBreakdown& b) noexcept {
    if (!IsEnabled()) return;
    if (b.total_ms < 50.f) return;
    static std::chrono::steady_clock::time_point s_last{};
    const auto now = std::chrono::steady_clock::now();
    // At most one breakdown per 500ms (severe) / 250ms (critical)
    const auto minGap = b.total_ms >= 200.f
        ? std::chrono::milliseconds(250)
        : std::chrono::milliseconds(500);
    if (s_last.time_since_epoch().count() != 0 && now - s_last < minGap)
        return;
    s_last = now;
    const float accounted = b.webradar_ms + b.entity_ms + b.local_ms + b.core_scatter_ms
        + b.positions_ms + b.bones_ms + b.weapon_ms + b.spectator_ms + b.bomb_ms
        + b.publish_ms + b.cleanup_ms;
    const float unaccounted = (std::max)(0.f, b.total_ms - accounted);
    char buf[900]{};
    std::snprintf(buf, sizeof(buf),
        "SPIKE_BREAKDOWN\n"
        "scan_id=%llu\n"
        "total_ms=%.2f\n"
        "webradar_ms=%.2f\nentity_ms=%.2f\nlocal_ms=%.2f\n"
        "core_scatter_ms=%.2f\npositions_ms=%.2f\nbones_ms=%.2f\n"
        "weapon_ms=%.2f\nspectator_ms=%.2f\nbomb_ms=%.2f\n"
        "publish_ms=%.2f\ncleanup_ms=%.2f\nother_ms=%.2f\nlock_wait_ms=%.2f\n"
        "unaccounted_ms=%.2f\n"
        "players=%d\nbones_players=%d\n"
        "entity_full_probe=%d\nspectator_refresh=%d\nbomb_refresh=%d\ncache_cleanup=%d",
        static_cast<unsigned long long>(b.scan_id),
        b.total_ms, b.webradar_ms, b.entity_ms, b.local_ms,
        b.core_scatter_ms, b.positions_ms, b.bones_ms,
        b.weapon_ms, b.spectator_ms, b.bomb_ms,
        b.publish_ms, b.cleanup_ms, b.other_ms, b.lock_wait_ms, unaccounted,
        b.players, b.bones_players,
        b.entity_full_probe, b.spectator_refresh, b.bomb_refresh, b.cache_cleanup);
    EmitWarn(tag, buf);
}

} // namespace OmniGhost::Gameplay::DmaTelemetry
