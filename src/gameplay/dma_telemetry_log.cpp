#include "gameplay/dma_telemetry_log.h"
#include "platform/session_log.h"
#include "platform/resource_tracker.h"
#include "third_party/DMALibrary/Memory/Memory.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <mutex>
#include <string>
#include <Windows.h>
#include <psapi.h>
#include <tlhelp32.h>


namespace {
int CountProcessThreadsCached() {
    static int cached = 0;
    static ULONGLONG last = 0;
    const ULONGLONG now = GetTickCount64();
    if (last != 0 && now - last < 1000)
        return cached;
    last = now;
    int n = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return cached;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    const DWORD pid = GetCurrentProcessId();
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) ++n;
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    cached = n;
    return cached;
}
}
namespace OmniGhost::Gameplay::DmaTelemetry {
namespace {

constexpr int kWindow = 256;
constexpr float kSpikeWarnMs = 50.f;
constexpr float kSpikeSevereMs = 200.f;
constexpr float kSpikeCriticalMs = 500.f;

std::atomic_bool g_enabled{false};
Sample g_fivem{};
Sample g_cs2{};
Sample g_warzone{};
Sample g_fortnite{};

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
        std::array<float, kWindow> sorted{};
        std::copy_n(data.begin(), count, sorted.begin());
        s.last = data[static_cast<size_t>((write + kWindow - 1) % kWindow)];
        std::sort(sorted.begin(), sorted.begin() + count);
        const int n = count;
        s.samples = n;
        double sum = 0.0;
        for (int i = 0; i < n; ++i) sum += sorted[static_cast<size_t>(i)];
        s.avg = static_cast<float>(sum / n);
        auto at = [&](float q) -> float {
            const int idx = std::clamp(static_cast<int>(std::ceil(q * n) - 1), 0, n - 1);
            return sorted[static_cast<size_t>(idx)];
        };
        s.p50 = at(0.50f);
        s.p95 = at(0.95f);
        s.p99 = at(0.99f);
        s.max = sorted[static_cast<size_t>(n - 1)];

        s.p95 = (std::max)(s.p95, s.p50);
        s.p99 = (std::max)(s.p99, s.p95);
        s.max = (std::max)(s.max, s.p99);
        return s;
    }
};

Ring g_acqF{}, g_acqC{}, g_acqW{}, g_acqFortnite{};
Ring g_renderF{}, g_renderC{}, g_renderW{}, g_renderFortnite{};
Ring g_dmaExec{};
Ring g_cameraAgeC{}, g_cameraAgeW{}, g_cameraAgeFortnite{};
Ring g_cameraReadC{}, g_cameraReadF{}, g_cameraReadW{}, g_cameraReadFortnite{};
Ring g_bonesC{};
Ring g_positionAgeC{}, g_bonesAgeC{}, g_cameraPositionDeltaC{}, g_predictionC{};

std::atomic<float> g_lastAcqF{0.f}, g_lastAcqC{0.f}, g_lastAcqW{0.f}, g_lastAcqFortnite{0.f};

std::atomic<uint64_t> g_lastSpikeWarnMs{0};
std::atomic<uint64_t> g_lastSpikeSevereMs{0};

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
    s.acquire.acquire_ms.store(st.avg, std::memory_order_relaxed);
    s.acquire.acquire_p50_ms.store(st.p50, std::memory_order_relaxed);
    s.acquire.acquire_p95_ms.store(st.p95, std::memory_order_relaxed);
    s.acquire.acquire_p99_ms.store(st.p99, std::memory_order_relaxed);
    s.acquire.acquire_max_ms.store(st.max, std::memory_order_relaxed);
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
    char buf[3072]{};
    std::snprintf(buf, sizeof(buf),
        "PERF\n"
        "active=%d\nalive=%d\npid=%u\nidle_reason=%s\n\n"
        "players=%d\n"
        "reads_s=%d\nscatter_batches_s=%d\n"
        "dma_avg_ms=%.2f\ndma_p50_ms=%.2f\ndma_p95_ms=%.2f\ndma_p99_ms=%.2f\ndma_max_ms=%.2f\n"
        "read_fails=%llu\n\n"
        "acquire_avg_ms=%.2f\nacquire_p50_ms=%.2f\nacquire_p95_ms=%.2f\nacquire_p99_ms=%.2f\nacquire_max_ms=%.2f\n"
        "scheduler_wait_ms=%.2f\npressure_window_p95_ms=%.2f\n\n"
        "snapshot_age_ms=%.1f\nworld_age_ms=%.1f\npublish_delay_ms=%.1f\nsnapshot_hz=%.1f\nsnapshot_interval_ms=%.1f\nsnapshot_drops=%llu\nvisibility_ms=%.2f\nvisibility_players_tested=%d\nvisibility_rays=%d\n\n"
		"camera_age_ms=%.1f\ncamera_age_avg_ms=%.1f\ncamera_age_p50_ms=%.1f\n"
		"camera_age_p95_ms=%.1f\ncamera_age_p99_ms=%.1f\ncamera_age_max_ms=%.1f\n"
		"camera_read_p95_ms=%.2f\ncamera_read_p99_ms=%.2f\ncamera_read_max_ms=%.2f\n"
		"camera_busy_skips=%llu\ncamera_busy_retry_success=%llu\ncamera_consecutive_busy_max=%llu\n"
		"camera_cooldown_skips=%llu\ncamera_cooldown_bypasses=%llu\n"
		"pressure_level=%d\ncooldown_remaining_ms=%llu\ndeath_to_hide_ms=%.1f\n"
        "recent_player_reinsertions=%llu\nlast_good_age_ms=%llu\nmotion_age_ms=%.1f\n"
		"position_age_ms=%.1f\nposition_age_avg_ms=%.1f\nposition_age_p95_ms=%.1f\nposition_age_p99_ms=%.1f\nposition_age_max_ms=%.1f\n"
		"bones_age_ms=%.1f\nbones_age_avg_ms=%.1f\nbones_age_p95_ms=%.1f\nbones_age_p99_ms=%.1f\nbones_age_max_ms=%.1f\n"
		"camera_position_delta_ms=%.1f\ncamera_position_delta_p95_ms=%.1f\ncamera_position_delta_p99_ms=%.1f\ncamera_position_delta_max_ms=%.1f\n"
		"prediction_ms=%.1f\nprediction_p95_ms=%.1f\nprediction_p99_ms=%.1f\nprediction_max_ms=%.1f\n"
		"cached_bones_used=%llu\ncached_bones_age_max_ms=%.1f\n"
        "bones_players=%d\nbones_p95_ms=%.2f\nbones_p99_ms=%.2f\nbones_max_ms=%.2f\n"
        "bones_joints_evaluated=%llu\nbones_joints_rejected=%llu\n"
        "render_avg_ms=%.2f\nrender_p95_ms=%.2f\nrender_p99_ms=%.2f\nrender_max_ms=%.2f\n\n"
        "suppressed_spikes_20=%llu\nsuppressed_spikes_50=%llu\nsuppressed_spikes_100=%llu",
        s.process.active.load() ? 1 : 0,
        s.process.process_connected.load() ? 1 : 0,
        s.process.process_id.load(),
        IdleName(s.process.idle_reason.load()),
        s.counts.entities.load(),
        s.counts.reads_s.load(),
        s.counts.scatter_s.load(),
        s.cache_vmm.vmm_avg_ms.load(),
        s.cache_vmm.vmm_p50_ms.load(),
        s.cache_vmm.vmm_p95_ms.load(),
        s.cache_vmm.vmm_p99_ms.load(),
        s.cache_vmm.vmm_max_ms.load(),
        static_cast<unsigned long long>(s.counts.read_fails.load()),
        s.acquire.acquire_ms.load(),
        s.acquire.acquire_p50_ms.load(),
        s.acquire.acquire_p95_ms.load(),
        s.acquire.acquire_p99_ms.load(),
        s.acquire.acquire_max_ms.load(),
        s.cache_vmm.scheduler_wait_ms.load(),
        s.acquire.pressure_window_p95_ms.load(),
        s.snapshot.snapshot_age_ms.load(),
        s.snapshot.world_age_ms.load(),
        s.snapshot.publish_delay_ms.load(),
        s.snapshot.snapshot_hz.load(),
        s.snapshot.snapshot_interval_ms.load(),
        static_cast<unsigned long long>(s.counts.snapshot_drops.load()),
        s.visibility.visibility_ms.load(),
        s.visibility.visibility_players_tested.load(),
        s.visibility.visibility_rays.load(),
        s.camera.camera_age_ms.load(),
		s.camera.camera_age_avg_ms.load(),
		s.camera.camera_age_p50_ms.load(),
		s.camera.camera_age_p95_ms.load(),
		s.camera.camera_age_p99_ms.load(),
		s.camera.camera_age_max_ms.load(),
		s.camera.camera_read_p95_ms.load(),
		s.camera.camera_read_p99_ms.load(),
		s.camera.camera_read_max_ms.load(),
		static_cast<unsigned long long>(s.camera.camera_busy_skips.load()),
		static_cast<unsigned long long>(s.camera.camera_busy_retry_success.load()),
		static_cast<unsigned long long>(s.camera.camera_consecutive_busy_max.load()),
		static_cast<unsigned long long>(s.camera.camera_cooldown_skips.load()),
		static_cast<unsigned long long>(s.camera.camera_cooldown_bypasses.load()),
		s.pressure.pressure_level.load(),
		static_cast<unsigned long long>(s.pressure.cooldown_remaining_ms.load()),
        s.pressure.death_to_hide_ms.load(),
        static_cast<unsigned long long>(s.pressure.recent_player_reinsertions.load()),
        static_cast<unsigned long long>(s.pressure.last_good_age_ms.load()),
        s.cache_vmm.motion_age_ms.load(),
		s.position.position_age_ms.load(), s.position.position_age_avg_ms.load(), s.position.position_age_p95_ms.load(),
		s.position.position_age_p99_ms.load(), s.position.position_age_max_ms.load(),
		s.bones.bones_age_ms.load(), s.bones.bones_age_avg_ms.load(), s.bones.bones_age_p95_ms.load(),
		s.bones.bones_age_p99_ms.load(), s.bones.bones_age_max_ms.load(),
		s.camera_pos_delta.camera_position_delta_ms.load(), s.camera_pos_delta.camera_position_delta_p95_ms.load(),
		s.camera_pos_delta.camera_position_delta_p99_ms.load(), s.camera_pos_delta.camera_position_delta_max_ms.load(),
		s.prediction.prediction_ms.load(), s.prediction.prediction_p95_ms.load(), s.prediction.prediction_p99_ms.load(),
		s.prediction.prediction_max_ms.load(),
		static_cast<unsigned long long>(s.cache_vmm.cached_bones_used.load()), s.cache_vmm.cached_bones_age_max_ms.load(),
        s.counts.bones_players.load(),
        s.counts.bones_p95_ms.load(),
        s.counts.bones_p99_ms.load(),
        s.counts.bones_max_ms.load(),
        static_cast<unsigned long long>(s.bones.joints_evaluated.load()),
        static_cast<unsigned long long>(s.bones.joints_rejected.load()),
        s.render.render_ms.load(),
        s.render.render_p95_ms.load(),
        s.render.render_p99_ms.load(),
        s.render.render_max_ms.load(),
        static_cast<unsigned long long>(s.counts.suppressed_spikes_20.load()),
        static_cast<unsigned long long>(s.counts.suppressed_spikes_50.load()),
        static_cast<unsigned long long>(s.counts.suppressed_spikes_100.load()));

    DWORD handleCount = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handleCount);
    static DWORD previousHandleCount = 0;
    const long handleDelta = previousHandleCount
        ? static_cast<long>(handleCount) - static_cast<long>(previousHandleCount) : 0;
    previousHandleCount = handleCount;
    const DWORD gdiObjects = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD userObjects = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    SIZE_T working = 0, priv = 0;
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        working = pmc.WorkingSetSize;
        priv = pmc.PrivateUsage;
    }
    const auto memSnap = mem.GetDiagnosticsSnapshot();
    char res[512]{};
    std::snprintf(res, sizeof(res),
		"\nprocess_handles=%lu\nprocess_handles_delta=%ld\ngdi_objects=%lu\nuser_objects=%lu"
		"\nprocess_threads=%d\nworking_set_mb=%.1f\nprivate_mb=%.1f"
		"\nscatter_handles_live=%u\nnon_scatter_handles=%lu\nscatter_created=%llu\nscatter_destroyed=%llu",
        static_cast<unsigned long>(handleCount),
		static_cast<long>(handleDelta),
		static_cast<unsigned long>(gdiObjects),
		static_cast<unsigned long>(userObjects),
        CountProcessThreadsCached(),
        working / (1024.0 * 1024.0),
        priv / (1024.0 * 1024.0),
        static_cast<unsigned>(memSnap.scatterHandlesLive),
		static_cast<unsigned long>(handleCount >= memSnap.scatterHandlesLive
			? handleCount - memSnap.scatterHandlesLive : 0),
        static_cast<unsigned long long>(memSnap.scatterHandlesCreated),
        static_cast<unsigned long long>(memSnap.scatterHandlesDestroyed));
    std::string full = buf;
    full.append(res);
    Emit(tag, full);
}

}

void SetEnabled(bool enabled) noexcept {
    g_enabled.store(enabled, std::memory_order_release);
}

bool IsEnabled() noexcept {
    return g_enabled.load(std::memory_order_acquire);
}

Sample& FiveM() { return g_fivem; }
Sample& CS2() { return g_cs2; }
Sample& Warzone() { return g_warzone; }
Sample& Fortnite() { return g_fortnite; }

void ObserveAcquire(Sample& s, float work_ms, bool activeSample, uint64_t scan_id) noexcept {
    if (work_ms < 0.f) return;
    if (!activeSample) {
        s.process.active.store(false, std::memory_order_relaxed);
        return;
    }
    s.process.active.store(true, std::memory_order_relaxed);
    s.process.idle_reason.store(0, std::memory_order_relaxed);

    const bool isCs2 = (&s == &g_cs2);
    const bool isWarzone = (&s == &g_warzone);
    const bool isFortnite = (&s == &g_fortnite);
    Ring& ring = isCs2 ? g_acqC : isWarzone ? g_acqW : isFortnite ? g_acqFortnite : g_acqF;
    ring.Push(work_ms);
    if (isCs2) g_lastAcqC.store(work_ms, std::memory_order_relaxed);
    else if (isWarzone) g_lastAcqW.store(work_ms, std::memory_order_relaxed);
    else if (isFortnite) g_lastAcqFortnite.store(work_ms, std::memory_order_relaxed);
    else g_lastAcqF.store(work_ms, std::memory_order_relaxed);
    PublishAcquireStats(s, ring, work_ms);


    if (!IsEnabled()) return;
    const uint64_t nowMs = GetTickCount64();
    const auto claimSpikeWindow = [&](std::atomic<uint64_t>& last, uint64_t intervalMs) {
        uint64_t previous = last.load(std::memory_order_relaxed);
        for (;;) {
            if (previous && (nowMs < previous || nowMs - previous < intervalMs)) return false;
            if (last.compare_exchange_weak(previous, nowMs, std::memory_order_relaxed)) return true;
        }
    };
    if (work_ms < 20.f) return;
    if (work_ms < 50.f) {
        s.counts.suppressed_spikes_20.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (work_ms < 100.f) {
        s.counts.suppressed_spikes_50.fetch_add(1, std::memory_order_relaxed);
        if (!claimSpikeWindow(g_lastSpikeWarnMs, 500u))
            return;
    } else if (work_ms < kSpikeSevereMs) {
        s.counts.suppressed_spikes_100.fetch_add(1, std::memory_order_relaxed);
        if (!claimSpikeWindow(g_lastSpikeWarnMs, 500u))
            return;
    } else if (work_ms < kSpikeCriticalMs) {
        if (!claimSpikeWindow(g_lastSpikeSevereMs, 250u))
            return;
    }

    s.counts.acquisition_spikes.fetch_add(1, std::memory_order_relaxed);
    char buf[256]{};
    std::snprintf(buf, sizeof(buf),
                  "SPIKE\nscan_id=%llu\ntotal_ms=%.2f\nplayers=%d\nreads_s=%d\nscatter_s=%d",
                  static_cast<unsigned long long>(scan_id),
                  work_ms, s.counts.entities.load(), s.counts.reads_s.load(), s.counts.scatter_s.load());
    EmitWarn(isCs2 ? "CS2" : isWarzone ? "WARZONE" : isFortnite ? "FORTNITE" : "FIVEM", buf);
}

void ObserveRender(Sample& s, float ms) noexcept {
    if (ms < 0.f) return;
    Ring& ring = (&s == &g_cs2) ? g_renderC : (&s == &g_warzone) ? g_renderW
        : (&s == &g_fortnite) ? g_renderFortnite : g_renderF;
    ring.Push(ms);
    static std::atomic<uint64_t> nextCs2StatsMs{0}, nextFivemStatsMs{0}, nextWarzoneStatsMs{0}, nextFortniteStatsMs{0};
    auto& nextStatsMs = (&s == &g_cs2) ? nextCs2StatsMs : (&s == &g_warzone) ? nextWarzoneStatsMs
        : (&s == &g_fortnite) ? nextFortniteStatsMs : nextFivemStatsMs;
    const uint64_t now = GetTickCount64();
    uint64_t next = nextStatsMs.load(std::memory_order_relaxed);
    if (now < next || !nextStatsMs.compare_exchange_strong(
            next, now + 100u, std::memory_order_relaxed))
        return;
    auto st = ring.Stats();
    s.render.render_ms.store(st.avg, std::memory_order_relaxed);
    s.render.render_p95_ms.store(st.p95, std::memory_order_relaxed);
    s.render.render_p99_ms.store(st.p99, std::memory_order_relaxed);
    s.render.render_max_ms.store(st.max, std::memory_order_relaxed);
}

void ObserveCameraAge(Sample& s, float age_ms) noexcept {
	if (age_ms < 0.f || (&s != &g_cs2 && &s != &g_warzone && &s != &g_fortnite)) return;
	Ring& ring = (&s == &g_cs2) ? g_cameraAgeC : (&s == &g_warzone) ? g_cameraAgeW : g_cameraAgeFortnite;
	ring.Push(age_ms);
	const auto st = ring.Stats();
	s.camera.camera_age_ms.store(age_ms, std::memory_order_relaxed);
	s.camera.camera_age_avg_ms.store(st.avg, std::memory_order_relaxed);
	s.camera.camera_age_p50_ms.store(st.p50, std::memory_order_relaxed);
	s.camera.camera_age_p95_ms.store(st.p95, std::memory_order_relaxed);
	s.camera.camera_age_p99_ms.store(st.p99, std::memory_order_relaxed);
	s.camera.camera_age_max_ms.store(st.max, std::memory_order_relaxed);
}

void ObserveCameraRead(Sample& s, float read_ms) noexcept {
	if (read_ms < 0.f) return;
	Ring& ring = (&s == &g_cs2) ? g_cameraReadC : (&s == &g_warzone) ? g_cameraReadW
	    : (&s == &g_fortnite) ? g_cameraReadFortnite : g_cameraReadF;
	ring.Push(read_ms);
	const auto st = ring.Stats();
	s.camera.camera_read_p95_ms.store(st.p95, std::memory_order_relaxed);
	s.camera.camera_read_p99_ms.store(st.p99, std::memory_order_relaxed);
	s.camera.camera_read_max_ms.store(st.max, std::memory_order_relaxed);
}

void ObserveBones(Sample& s, float bones_ms) noexcept {
    if (bones_ms < 0.f || &s != &g_cs2) return;
    g_bonesC.Push(bones_ms);
    const auto st = g_bonesC.Stats();
    s.counts.bones_p95_ms.store(st.p95, std::memory_order_relaxed);
    s.counts.bones_p99_ms.store(st.p99, std::memory_order_relaxed);
    s.counts.bones_max_ms.store(st.max, std::memory_order_relaxed);
}

void ObservePresentationTiming(Sample& s, float positionAgeMs, float bonesAgeMs,
    float cameraPositionDeltaMs, float predictionMs) noexcept {
    if (&s != &g_cs2) return;
    auto sample = [](Ring& ring, float value, std::atomic<float>& last) {
        if (!std::isfinite(value) || value < 0.f) return;
        ring.Push(value);
        last.store(value, std::memory_order_relaxed);
    };
    sample(g_positionAgeC, positionAgeMs, s.position.position_age_ms);
    sample(g_bonesAgeC, bonesAgeMs, s.bones.bones_age_ms);
    sample(g_cameraPositionDeltaC, cameraPositionDeltaMs, s.camera_pos_delta.camera_position_delta_ms);
    sample(g_predictionC, predictionMs, s.prediction.prediction_ms);
    static std::atomic<uint64_t> nextStatsMs{0};
    const uint64_t now = GetTickCount64();
    uint64_t next = nextStatsMs.load(std::memory_order_relaxed);
    if (now < next || !nextStatsMs.compare_exchange_strong(
            next, now + 100u, std::memory_order_relaxed))
        return;
    const auto positions = g_positionAgeC.Stats();
    s.position.position_age_avg_ms.store(positions.avg, std::memory_order_relaxed);
    s.position.position_age_p95_ms.store(positions.p95, std::memory_order_relaxed);
    s.position.position_age_p99_ms.store(positions.p99, std::memory_order_relaxed);
    s.position.position_age_max_ms.store(positions.max, std::memory_order_relaxed);
    const auto bones = g_bonesAgeC.Stats();
    s.bones.bones_age_avg_ms.store(bones.avg, std::memory_order_relaxed);
    s.bones.bones_age_p95_ms.store(bones.p95, std::memory_order_relaxed);
    s.bones.bones_age_p99_ms.store(bones.p99, std::memory_order_relaxed);
    s.bones.bones_age_max_ms.store(bones.max, std::memory_order_relaxed);
    const auto delta = g_cameraPositionDeltaC.Stats();
    s.camera_pos_delta.camera_position_delta_p95_ms.store(delta.p95, std::memory_order_relaxed);
    s.camera_pos_delta.camera_position_delta_p99_ms.store(delta.p99, std::memory_order_relaxed);
    s.camera_pos_delta.camera_position_delta_max_ms.store(delta.max, std::memory_order_relaxed);
    const auto prediction = g_predictionC.Stats();
    s.prediction.prediction_p95_ms.store(prediction.p95, std::memory_order_relaxed);
    s.prediction.prediction_p99_ms.store(prediction.p99, std::memory_order_relaxed);
    s.prediction.prediction_max_ms.store(prediction.max, std::memory_order_relaxed);
}

void ObserveSchedulerWait(Sample& s, float wait_ms) noexcept {
    if (wait_ms < 0.f) return;
    const float old = s.cache_vmm.scheduler_wait_ms.load(std::memory_order_relaxed);
    s.cache_vmm.scheduler_wait_ms.store(old <= 0.f ? wait_ms : old + (wait_ms - old) * 0.15f,
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
    const auto rate = [dt](uint64_t current, uint64_t previous) -> int {
        if (current < previous) return 0;
        const double value = static_cast<double>(current - previous) / dt;
        return static_cast<int>((std::min)(value, static_cast<double>((std::numeric_limits<int>::max)())));
    };
    const int reads_s = rate(reads, g_prevReads);
    const int scat_s = rate(scat, g_prevScatter);
    g_prevReads = reads;
    g_prevScatter = scat;

    auto fill = [&](Sample& s) {
        s.counts.reads_s.store((std::max)(0, reads_s), std::memory_order_relaxed);
        s.counts.scatter_s.store((std::max)(0, scat_s), std::memory_order_relaxed);
        s.cache_vmm.vmm_avg_ms.store(static_cast<float>(snap.vmmLatencyAverageMs), std::memory_order_relaxed);
        s.cache_vmm.vmm_p50_ms.store(static_cast<float>(snap.vmmLatencyP50Ms), std::memory_order_relaxed);
        s.cache_vmm.vmm_p95_ms.store(static_cast<float>(snap.vmmLatencyP95Ms), std::memory_order_relaxed);
        s.cache_vmm.vmm_p99_ms.store(static_cast<float>(snap.vmmLatencyP99Ms), std::memory_order_relaxed);
        s.cache_vmm.vmm_max_ms.store(static_cast<float>(snap.maxVmmLatencyMs), std::memory_order_relaxed);
        s.counts.read_fails.store(snap.readFailureCount, std::memory_order_relaxed);
        s.process.dma_open.store(snap.deviceOpen, std::memory_order_relaxed);
        s.process.process_id.store(static_cast<std::uint32_t>(snap.processId), std::memory_order_relaxed);
        s.process.process_connected.store(snap.processInitialized, std::memory_order_relaxed);
    };
    fill(g_cs2);
    fill(g_fivem);
    fill(g_warzone);
    fill(g_fortnite);
}

static void ObserveProcessResources(Sample& s) noexcept {
    try {
        const auto snap = OmniGhost::Platform::ResourceTracker::Instance().TakeSnapshot();
        static thread_local uint64_t prevHandles = 0;
        const int64_t delta = prevHandles
            ? static_cast<int64_t>(snap.handleCount) - static_cast<int64_t>(prevHandles)
            : 0;
        prevHandles = snap.handleCount;
        s.process.process_handles.store(snap.handleCount, std::memory_order_relaxed);
        s.process.process_handles_delta.store(delta, std::memory_order_relaxed);
        s.process.process_threads.store(snap.threadCount, std::memory_order_relaxed);
        s.process.working_set_mb.store(static_cast<float>(snap.memoryBytes) / (1024.f * 1024.f),
            std::memory_order_relaxed);

        s.process.private_mb.store(static_cast<float>(snap.memoryBytes) / (1024.f * 1024.f),
            std::memory_order_relaxed);
        const auto memSnap = mem.GetDiagnosticsSnapshot();
        s.process.scatter_handles_live.store(memSnap.scatterHandlesLive, std::memory_order_relaxed);
    } catch (...) {
        OutputDebugStringA("[OmniGhost][Telemetry] Failed to refresh DMA diagnostics.\n");
    }
}

void Tick(std::string_view tag) {
    if (!IsEnabled()) return;
    static std::mutex mu;
    static std::chrono::steady_clock::time_point lastF{}, lastC{}, lastW{}, lastFortnite{};
    std::lock_guard<std::mutex> lock(mu);
    RefreshFromMemory();
    const auto now = std::chrono::steady_clock::now();
    Sample* sample = &g_cs2;
    std::chrono::steady_clock::time_point* last = &lastC;
    if (tag == "FIVEM") { sample = &g_fivem; last = &lastF; }
    else if (tag == "WARZONE" || tag == "WZ") { sample = &g_warzone; last = &lastW; }
    else if (tag == "FORTNITE") { sample = &g_fortnite; last = &lastFortnite; }
    ObserveProcessResources(*sample);

    if (!sample->process.active.load(std::memory_order_relaxed)) {

        if (last->time_since_epoch().count() != 0 &&
            now - *last < std::chrono::seconds(5))
            return;
        *last = now;
        DWORD handleCount = 0;
        GetProcessHandleCount(GetCurrentProcess(), &handleCount);
        char status[256]{};
        std::snprintf(status, sizeof(status),
                      "STATUS process=%s pid=%u dma=%s active=0 idle_reason=%s handles=%lu scatter_handles_live=%u",
                      sample->process.process_connected.load() ? "connected" : "waiting",
                      sample->process.process_id.load(),
                      sample->process.dma_open.load() ? "open" : "closed",
                      IdleName(sample->process.idle_reason.load()),
                      static_cast<unsigned long>(handleCount),
                      OmniGhost::MemoryCounters::GetScatterHandlesLive());
        Emit(tag, status);
        return;
    }
    if (last->time_since_epoch().count() != 0 &&
        now - *last < std::chrono::seconds(1))
        return;
    *last = now;
    WritePerf(tag, *sample);
}

void LogConfigChanged(std::string_view tag, std::string_view config_blob) {
    if (!IsEnabled()) return;
    std::string body = "CONFIG_CHANGED\n";
    body.append(config_blob);
    Emit(tag, body);
}

void LogSessionSummary(std::string_view tag) {
    Sample& s = (tag == "FIVEM") ? g_fivem
        : (tag == "WARZONE" || tag == "WZ") ? g_warzone
        : (tag == "FORTNITE") ? g_fortnite : g_cs2;
    char buf[1400]{};
    std::snprintf(buf, sizeof(buf),
        "SESSION_SUMMARY\n"
        "acquire_avg_ms=%.2f\nacquire_p50_ms=%.2f\nacquire_p95_ms=%.2f\nacquire_p99_ms=%.2f\nacquire_max_ms=%.2f\n"
        "dma_avg_ms=%.2f\ndma_p95_ms=%.2f\ndma_p99_ms=%.2f\ndma_max_ms=%.2f\n"
        "render_avg_ms=%.2f\nrender_p95_ms=%.2f\nrender_p99_ms=%.2f\nrender_max_ms=%.2f\n"
		"camera_age_avg_ms=%.1f\ncamera_age_p50_ms=%.1f\ncamera_age_p95_ms=%.1f\ncamera_age_p99_ms=%.1f\ncamera_age_max_ms=%.1f\n"
		"camera_read_p95_ms=%.2f\ncamera_read_p99_ms=%.2f\ncamera_read_max_ms=%.2f\n"
		"camera_busy_skips=%llu\ncamera_busy_retry_success=%llu\ncamera_consecutive_busy_max=%llu\n"
		"camera_cooldown_skips=%llu\ncamera_cooldown_bypasses=%llu\n"
		"bones_p95_ms=%.2f\nbones_p99_ms=%.2f\nbones_max_ms=%.2f\n"
        "death_to_hide_ms=%.1f\nrecent_player_reinsertions=%llu\nlast_good_age_ms=%llu\n"
        "snapshot_drops=%llu\nread_fails=%llu\nspikes=%llu\n"
        "suppressed_20=%llu\nsuppressed_50=%llu\nsuppressed_100=%llu",
        s.acquire.acquire_ms.load(), s.acquire.acquire_p50_ms.load(), s.acquire.acquire_p95_ms.load(),
        s.acquire.acquire_p99_ms.load(), s.acquire.acquire_max_ms.load(),
        s.cache_vmm.vmm_avg_ms.load(), s.cache_vmm.vmm_p95_ms.load(), s.cache_vmm.vmm_p99_ms.load(), s.cache_vmm.vmm_max_ms.load(),
        s.render.render_ms.load(), s.render.render_p95_ms.load(), s.render.render_p99_ms.load(), s.render.render_max_ms.load(),
		s.camera.camera_age_avg_ms.load(), s.camera.camera_age_p50_ms.load(), s.camera.camera_age_p95_ms.load(),
		s.camera.camera_age_p99_ms.load(), s.camera.camera_age_max_ms.load(),
		s.camera.camera_read_p95_ms.load(), s.camera.camera_read_p99_ms.load(), s.camera.camera_read_max_ms.load(),
		static_cast<unsigned long long>(s.camera.camera_busy_skips.load()),
		static_cast<unsigned long long>(s.camera.camera_busy_retry_success.load()),
		static_cast<unsigned long long>(s.camera.camera_consecutive_busy_max.load()),
		static_cast<unsigned long long>(s.camera.camera_cooldown_skips.load()),
		static_cast<unsigned long long>(s.camera.camera_cooldown_bypasses.load()),
        s.counts.bones_p95_ms.load(), s.counts.bones_p99_ms.load(), s.counts.bones_max_ms.load(),
        s.pressure.death_to_hide_ms.load(),
        static_cast<unsigned long long>(s.pressure.recent_player_reinsertions.load()),
        static_cast<unsigned long long>(s.pressure.last_good_age_ms.load()),
        static_cast<unsigned long long>(s.counts.snapshot_drops.load()),
        static_cast<unsigned long long>(s.counts.read_fails.load()),
        static_cast<unsigned long long>(s.counts.acquisition_spikes.load()),
        static_cast<unsigned long long>(s.counts.suppressed_spikes_20.load()),
        static_cast<unsigned long long>(s.counts.suppressed_spikes_50.load()),
        static_cast<unsigned long long>(s.counts.suppressed_spikes_100.load()));
    Emit(tag, buf);
}

void LogSpikeBreakdown(std::string_view tag, const SpikeBreakdown& b) noexcept {
    if (!IsEnabled()) return;
    if (b.total_ms < 50.f) return;
    static std::chrono::steady_clock::time_point s_last{};
    const auto now = std::chrono::steady_clock::now();

    const auto minGap = b.total_ms >= 200.f
        ? std::chrono::milliseconds(250)
        : std::chrono::milliseconds(500);
    if (s_last.time_since_epoch().count() != 0 && now - s_last < minGap)
        return;
    s_last = now;
    const float accounted = b.entity_ms + b.local_ms + b.core_scatter_ms
        + b.positions_ms + b.bones_ms + b.weapon_ms + b.ammo_ms + b.flags_ms
        + b.visibility_ms + b.spectator_ms + b.bomb_ms
        + b.publish_ms + b.cleanup_ms;
    const float unaccounted = (std::max)(0.f, b.total_ms - accounted);
    char buf[900]{};
    std::snprintf(buf, sizeof(buf),
        "SPIKE_BREAKDOWN\n"
        "scan_id=%llu\n"
        "total_ms=%.2f\n"
        "entity_ms=%.2f\nlocal_ms=%.2f\n"
        "core_scatter_ms=%.2f\npositions_ms=%.2f\nbones_ms=%.2f\n"
        "weapon_ms=%.2f\nammo_ms=%.2f\nflags_ms=%.2f\nvisibility_ms=%.2f\nspectator_ms=%.2f\nbomb_ms=%.2f\n"
        "publish_ms=%.2f\ncleanup_ms=%.2f\nother_ms=%.2f\nlock_wait_ms=%.2f\n"
        "unaccounted_ms=%.2f\n"
        "qread_calls=%d\nqread_total_ms=%.2f\nqread_max_ms=%.2f\n"
        "scatter_calls=%d\nscatter_total_ms=%.2f\nscatter_max_ms=%.2f\n"
        "top_tag=%s\ntop_tag_ms=%.2f\ntop2_tag=%s\ntop2_tag_ms=%.2f\ntop3_tag=%s\ntop3_tag_ms=%.2f\n"
        "players=%d\nbones_players=%d\nvisibility_players_tested=%d\nvisibility_rays=%d\n"
        "entity_full_probe=%d\nspectator_refresh=%d\nbomb_refresh=%d\ncache_cleanup=%d",
        static_cast<unsigned long long>(b.scan_id),
        b.total_ms, b.entity_ms, b.local_ms,
        b.core_scatter_ms, b.positions_ms, b.bones_ms,
        b.weapon_ms, b.ammo_ms, b.flags_ms, b.visibility_ms, b.spectator_ms, b.bomb_ms,
        b.publish_ms, b.cleanup_ms, b.other_ms, b.lock_wait_ms, unaccounted,
        b.qread_calls, b.qread_total_ms, b.qread_max_ms,
        b.scatter_calls, b.scatter_total_ms, b.scatter_max_ms,
        b.top_tag ? b.top_tag : "none", b.top_tag_ms,
        b.top2_tag ? b.top2_tag : "none", b.top2_tag_ms,
        b.top3_tag ? b.top3_tag : "none", b.top3_tag_ms,
        b.players, b.bones_players, b.visibility_players_tested, b.visibility_rays,
        b.entity_full_probe, b.spectator_refresh, b.bomb_refresh, b.cache_cleanup);
    EmitWarn(tag, buf);
}

}
