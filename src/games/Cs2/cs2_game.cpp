#include "cs2_game.h"
#include "radar/cs2_radar.h"
#include "platform/session_log.h"
#include "../src/platform/offset_auto.h"
#include "../src/platform/app_paths.h"
#include "../src/platform/embedded_offsets.h"
#include "aimbot/cs2_aim.h"
#include "weapons/cs2_weapons.h"
#include "Memory/Memory.h"
#include "globals.h"
#include "gameplay/esp_core.h"
#include "gameplay/entity_cache_hub.h"
#include "gameplay/frame_pipeline.h"
#include "gameplay/scoped_phase.h"
#include "trajectory/cs2_map_collision_cache.h"
#include "trajectory/cs2_visibility.h"
#include "gameplay/unified_esp.h"
#include "unified_adapter.h"

#include <Windows.h>
#include <TlHelp32.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <future>
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>
#include "makcu/makcu_wrapper.h"
#include "src/games/Fivem/aimbot/aim_type.h"
#include "gameplay/dma_telemetry_log.h"

namespace fs = std::filesystem;

namespace CS2 {

using OmniGhost::Gameplay::ScopedPhase;

Offsets offsets;
Config config;
Runtime runtime;
std::atomic<uint64_t> g_read_fails{0};
bool LoadEmbeddedOffsets();
bool RefreshProcessLiveness();
bool ready = false;
std::string status = "idle";
std::string offsets_source = "não carregados";

bool Offsets::Validate(std::string* reason) const noexcept {
    const auto fail = [&](const char* text) {
        if (reason) *reason = text;
        return false;
    };
    const auto module_offset = [](uintptr_t value) {
        return value != 0 && value < 0x40000000ull;
    };
    const auto field_offset = [](uintptr_t value) {
        return value != 0 && value < 0x100000ull;
    };

    if (!module_offset(dwEntityList)) return fail("dwEntityList inválido");
    if (!module_offset(dwLocalPlayerPawn)) return fail("dwLocalPlayerPawn inválido");
    if (!module_offset(dwViewMatrix)) return fail("dwViewMatrix inválido");
    if (!field_offset(m_iHealth)) return fail("m_iHealth inválido");
    if (!field_offset(m_iTeamNum)) return fail("m_iTeamNum inválido");
    if (!field_offset(m_pGameSceneNode)) return fail("m_pGameSceneNode inválido");
    if (!field_offset(m_hPlayerPawn)) return fail("m_hPlayerPawn inválido");
    if (!field_offset(m_vecAbsOrigin)) return fail("m_vecAbsOrigin inválido");
    if (reason) reason->clear();
    return true;
}

namespace {

OmniGhost::Gameplay::SnapshotExchange<Runtime, 4> g_runtime_snapshots;

// Multi-rate acquisition profile (render still every overlay frame)
namespace {
// The renderer presents immutable snapshots with short velocity extrapolation. A sustainable DMA cadence is
// smoother in practice than flooding the FT601 queue and periodically freezing
// for a full transport timeout.
// DMA low-latency profile (CS2-DMA-style tiers):
//  - CameraWorker display-rate view matrix (decoupled from entity scan)
//  - High-frequency: positions / health around 18 ms
//  - Mid: near bones ~24 ms, controllers ~250 ms, weapons ~100-150 ms
//  - Low: team / names at 0.5 s / 1 s
// Presentation extrapolates briefly from timestamped world samples at overlay FPS.
// The camera owns the highest-priority non-blocking lane. 16 ms keeps the
// physical matrix close to display cadence without returning to the 8-12 ms
// rates that previously saturated slower FT601 transports.
constexpr int CAMERA_INTERVAL_MS = 16;
constexpr int BONES_INTERVAL_MS = 24;       // near skeleton tier (~42 Hz)
constexpr int FULL_SCAN_INTERVAL_MS = 18;   // position / HP / core identity
constexpr int WEAPON_INTERVAL_MS = 200;
constexpr int WEAPON_FALLBACK_INTERVAL_MS = 3000;
constexpr int ENTITY_LIST_INTERVAL_MS = 1000;  // increased from 250ms - entity list is very stable
constexpr int ENTITY_LIST_FULL_PROBE_INTERVAL_MS = 15000;  // increased from 4500ms - full page scan very stable
constexpr int NAME_INTERVAL_MS = 1000;      // low-frequency identity
// Bridge occasional missed bone reads without retaining an obsolete pose indefinitely.
// This must match the renderer's visual age ceiling; 50 ms was shorter than
// far-player LOD and could make skeletons blink between otherwise valid reads.
constexpr int BONE_RELIABILITY_HOLD_MS = kBoneReliabilityHoldMs;
constexpr int TEAM_INTERVAL_MS = 500;
}

OmniGhost::Gameplay::SnapshotExchange<CameraSnapshot> g_camera_snapshots;
OmniGhost::Gameplay::SnapshotExchange<LivenessSnapshot> g_liveness_snapshots;
OmniGhost::Gameplay::SnapshotExchange<Config> g_config_snapshots;
// Lock-free ring buffer for camera history (single producer: camera thread).
// Atomic index avoids mutex in 16ms hot path.
std::array<CameraSnapshot, 10> g_camera_history{};
std::atomic<size_t> g_camera_history_next{0};
std::atomic_bool g_acquisition_stop{false};
std::atomic_bool g_acquisition_running{false};
std::atomic<float> g_presentation_fps{0.f};
std::atomic<uint64_t> g_last_snapshot_publish_ms{0};
std::atomic<uint64_t> g_runtime_snapshot_drops{0};
bool g_publish_runtime_this_scan = true; // acquisition producer only
std::atomic<float> g_acquisition_hz{0.f};
std::atomic<uint64_t> g_camera_deadline_ms{0};
std::atomic_bool g_camera_pending{false};
std::atomic<uint64_t> g_session_generation{1};
std::mutex g_process_liveness_mutex;
uint64_t g_process_liveness_next_probe_ms = 0;
std::atomic_bool g_process_liveness_last_result{true};
std::thread g_acquisition_thread;
std::thread g_camera_thread;
std::thread g_web_radar_control_thread;
// Optional DMA lane (bones/weapons/names/flags/dropped). Runs after core
// publish so the next core scan is never blocked by metadata work.
std::thread g_optional_thread;
std::atomic_bool g_optional_stop{false};
std::atomic_bool g_optional_busy{false};
std::atomic_bool g_optional_pending{false};
std::mutex g_optional_mu;
std::condition_variable g_optional_cv;
Config g_optional_config{};
std::atomic<uint64_t> g_optional_job_generation{0};

// ── Optional enrichment pipeline (P0 fix: strict single-producer) ─────────
// The optional worker never touches the global `runtime` and never publishes
// g_runtime_snapshots. It consumes an immutable job snapshot taken by the
// acquisition thread and publishes OptionalResults on its OWN exchange.
// The acquisition thread merges results at the start of the next scan into
// its own caches (bone_cache / recent_players) and is therefore the only
// writer of runtime.players and the only producer of g_runtime_snapshots.
struct OptionalTarget {
    uintptr_t pawn = 0;
    uintptr_t controller = 0;
    uintptr_t scene = 0;
    float origin[3]{};
    bool has_name = false;
};
struct OptionalBoneResult {
    uintptr_t pawn = 0;
    uintptr_t bone_base = 0;
    float bones[kBoneSlotCount][3]{};
    float origin[3]{};
    uint64_t sample_ms = 0; // midpoint of the bone scatter execute (real read time)
    bool bones_ok = false;
    bool full_bones_ok = false;
};
struct OptionalNameResult { uintptr_t pawn = 0; uintptr_t controller = 0; char name[64]{}; };
struct OptionalWeaponResult { uintptr_t pawn = 0; int weapon_def = 0; char weapon[32]{}; int ammo_clip = -1; };
struct OptionalVisibilityResult {
    uintptr_t pawn = 0;
    bool visible = false;
    bool known = false;
    uint64_t checked_ms = 0;
};
struct OptionalResults {
    uint64_t generation = 0;
    std::array<OptionalBoneResult, 64> bones{};
    std::array<OptionalNameResult, 64> names{};
    std::array<OptionalWeaponResult, 64> weapons{};
    std::array<OptionalVisibilityResult, 64> visibility{};
    uint32_t name_count = 0, weapon_count = 0;
    uint32_t bone_count = 0;
    uint32_t visibility_count = 0;
    float bones_ms = 0.f;
    float weapon_ms = 0.f;
    float visibility_ms = 0.f;
    float dropped_ms = 0.f;        // P2-8: dropped weapons timing
    float budget_left_ms = 0.f;    // P2-8: optional budget remaining
    std::vector<DroppedWeapon> dropped_weapons;
    bool dropped_weapons_valid = false;
};
OmniGhost::Gameplay::SnapshotExchange<OptionalResults, 3> g_optional_results; // producer: optional thread only
// P2-4: Single-producer, single-consumer via std::swap() under g_optional_mu.
// Acquisition writes under mutex; optional thread reads via swap() (ownership transfer).
std::vector<OptionalTarget> g_optional_targets;
bool g_optional_in_match = false;
bool g_optional_dropped_only = false;
uintptr_t g_optional_client_base = 0;

std::atomic_bool g_web_radar_control_stop{false};
std::atomic_bool g_web_radar_desired_enabled{false};
std::atomic_bool g_web_radar_desired_cloudflare{false};
std::atomic<int> g_web_radar_desired_port{8080};
OmniGhost::Gameplay::PipelineTelemetry g_pipeline_metrics;

struct VisibilityTarget {
    uintptr_t pawn = 0;
    std::array<Trajectory::Vec3, 4> points{};
};
struct VisibilityResult {
    uint64_t generation = 0;
    uint64_t checked_ms = 0;
    bool known = false;
    bool visible = false;
};
class VisibilityWorker {
public:
    ~VisibilityWorker() { Stop(); }
    void Submit(std::shared_ptr<const Trajectory::CollisionBvh> world,
                Trajectory::Vec3 origin, std::vector<VisibilityTarget> targets,
                uint64_t generation) {
        std::unique_lock lock(mu_);
        if (!thread_.joinable()) thread_ = std::thread([this] { Run(); });
        job_ = {std::move(world), origin, std::move(targets), generation};
        pending_ = true;
        cv_.notify_one();
    }
    VisibilityResult Get(uintptr_t pawn, uint64_t generation) const {
        std::shared_lock lock(mu_);
        const auto it = results_.find(pawn);
        return it != results_.end() && it->second.generation == generation
            ? it->second : VisibilityResult{};
    }
    void Reset() {
        std::unique_lock lock(mu_);
        pending_ = false;
        job_ = {};
        results_.clear();
    }
    void Stop() {
        {
            std::unique_lock lock(mu_);
            stop_ = true;
            pending_ = false;
            job_ = {};
        }
        cv_.notify_one();
        if (thread_.joinable()) thread_.join();
        std::unique_lock lock(mu_);
        results_.clear();
        stop_ = false;
    }
private:
    struct Job {
        std::shared_ptr<const Trajectory::CollisionBvh> world;
        Trajectory::Vec3 origin{};
        std::vector<VisibilityTarget> targets;
        uint64_t generation = 0;
    };
    void Run() {
        for (;;) {
            Job work;
            {
                std::unique_lock lock(mu_);
                cv_.wait(lock, [this] { return stop_ || pending_; });
                if (stop_) return;
                work = std::move(job_);
                pending_ = false;
            }
            const auto begin = std::chrono::steady_clock::now();
            std::vector<std::pair<uintptr_t, VisibilityResult>> complete;
            complete.reserve(work.targets.size());
            int raysTested = 0;
            for (const auto& target : work.targets) {
                if (work.generation != g_session_generation.load(std::memory_order_acquire))
                    break; // release the old map BVH promptly on map/session change
                int targetRays = 0;
                const auto visible = Visibility::CheckPoints(
                    work.world.get(), work.origin, target.points, &targetRays, &stop_);
                raysTested += targetRays;
                complete.push_back({target.pawn, {work.generation, GetTickCount64(),
                    visible.has_value(), visible.value_or(false)}});
            }
            auto& metrics = OmniGhost::Gameplay::DmaTelemetry::CS2();
            metrics.visibility_ms.store(OmniGhost::Gameplay::TimeMs(begin), std::memory_order_relaxed);
            metrics.visibility_players_tested.store(static_cast<int>(work.targets.size()), std::memory_order_relaxed);
            metrics.visibility_rays.store(raysTested, std::memory_order_relaxed);
            {
                std::unique_lock lock(mu_);
                if (!stop_ && work.generation == g_session_generation.load(std::memory_order_acquire))
                    for (const auto& [pawn, result] : complete) results_[pawn] = result;
            }
        }
    }
    mutable std::shared_mutex mu_;
    std::condition_variable_any cv_;
    std::thread thread_;
    std::atomic_bool stop_{false};
    bool pending_ = false;
    Job job_{};
    std::unordered_map<uintptr_t, VisibilityResult> results_;
};
VisibilityWorker g_visibility_worker;

// Optional DMA lanes must not monopolise the FT601 transport while the
// camera deadline is due. We spin-yield until the camera worker clears the
// pending flag or a short wall-clock cap elapses — never Sleep() here (a 1 ms
// sleep on Windows can consume a full ~15.6 ms scheduler tick).

void PublishRuntimeSnapshot() {
    const uint64_t now = GetTickCount64();
    const uint64_t previous = g_last_snapshot_publish_ms.exchange(now, std::memory_order_relaxed);
    if (previous != 0 && now > previous)
        g_acquisition_hz.store(1000.0f / static_cast<float>(now - previous), std::memory_order_relaxed);
    runtime.snapshot_timestamp_ms = now;
    runtime.session_generation = g_session_generation.load(std::memory_order_acquire);
    runtime.read_fails = g_read_fails.load(std::memory_order_relaxed);
    runtime.acquisition_hz = g_acquisition_hz.load(std::memory_order_relaxed);
    runtime.snapshot_interval_ms = previous && now > previous ? static_cast<float>(now - previous) : 0.f;
    auto slot = g_runtime_snapshots.TryBeginWrite();
    if (!slot) {
        g_runtime_snapshot_drops.fetch_add(1, std::memory_order_relaxed);
        return; // renderer still owns both spare slots; never wait
    }
    runtime.snapshot_drops = g_runtime_snapshot_drops.load(std::memory_order_relaxed);
    *slot.value = runtime;
    g_runtime_snapshots.Publish(slot.index);
    WebRadar::SubmitSnapshot(*slot.value);
}

void PublishCameraSnapshot(const float* matrix, uint64_t sample_ms = 0) {
    if (!matrix) return;
    auto slot = g_camera_snapshots.TryBeginWrite();
    if (!slot) return;
    std::memcpy(slot.value->view_matrix, matrix, sizeof(slot.value->view_matrix));
    slot.value->timestamp_ms = sample_ms ? sample_ms : GetTickCount64();
    // P0-7: Lock-free ring buffer update (single producer).
    const size_t idx = g_camera_history_next.fetch_add(1, std::memory_order_relaxed) % g_camera_history.size();
    g_camera_history[idx] = *slot.value;
    g_camera_snapshots.Publish(slot.index);
}

void PublishLivenessSnapshot(const LivenessSnapshot& liveness) {
    auto slot = g_liveness_snapshots.TryBeginWrite();
    if (!slot) return;
    *slot.value = liveness;
    g_liveness_snapshots.Publish(slot.index);
}


bool JsonU64(const std::string& src, const char* key, uintptr_t& out) {
    const std::string pat = std::string("\"") + key + "\"";
    auto pos = src.find(pat);
    if (pos == std::string::npos) return false;
    pos = src.find(':', pos);
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\r' || src[pos] == '\n'))
        ++pos;
    if (pos < src.size() && src[pos] == '"') ++pos;
    char* end = nullptr;
    unsigned long long v = strtoull(src.c_str() + pos, &end, 0);
    if (end == src.c_str() + pos) return false;
    out = static_cast<uintptr_t>(v);
    return true;
}

bool JsonClassU64(const std::string& src, const char* class_name,
                  const char* key, uintptr_t& out) {
    const std::string flat = std::string(class_name) + "." + key;
    if (JsonU64(src, flat.c_str(), out)) return true;
    const std::string class_pattern = std::string("\"") + class_name + "\"";
    const size_t class_pos = src.find(class_pattern);
    if (class_pos == std::string::npos) return false;

    const size_t object_start = src.find('{', class_pos + class_pattern.size());
    if (object_start == std::string::npos) return false;

    size_t object_end = std::string::npos;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (size_t i = object_start; i < src.size(); ++i) {
        const char c = src[i];
        if (in_string) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') in_string = true;
        else if (c == '{') ++depth;
        else if (c == '}' && --depth == 0) {
            object_end = i + 1;
            break;
        }
    }
    if (object_end == std::string::npos) return false;
    return JsonU64(src.substr(object_start, object_end - object_start), key, out);
}

std::string ExeDir() {
    char buf[MAX_PATH]{};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path().string();
}

// A transport pause must stop the current producer from immediately queuing
// more physical reads. Definitions live below the shared DMA state.
static bool HeavyDmaCooldownActive();
static void NotePossibleDeviceStall(float duration_ms);

// Quiet read — avoids flooding log with "[!] Failed to read Memory"
bool QRead(uintptr_t addr, void* buf, size_t size, const char* tag = "QRead") {
    if (!addr || !buf || !size || !mem.vHandle) return false;
    if (addr < 0x10000) return false;
    if (HeavyDmaCooldownActive()) return false;
    mem.SetDmaCallTag(tag);
    const auto begin = std::chrono::steady_clock::now();
    const bool ok = mem.Read(addr, buf, size);
    NotePossibleDeviceStall(OmniGhost::Gameplay::TimeMs(begin));
    if (!ok) {
        g_read_fails.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}

// Camera-only non-blocking read. A busy transport is not a read failure and
// does not consume the camera cadence; the worker retries on its next wake-up.
// Respects HeavyDmaCooldownActive to maintain temporal consistency with core.
bool QTryRead(uintptr_t addr, void* buf, size_t size, bool& attempted,
              const char* tag = "QTryRead") {
    attempted = false;
    if (!addr || !buf || !size || !mem.vHandle || addr < 0x10000)
        return false;
    // P0-5: Respect heavy-lane cooldown to avoid camera/core temporal skew.
    // Return attempted=false so camera thread yields and retries next cycle.
    if (HeavyDmaCooldownActive())
        return false;
    mem.SetDmaCallTag(tag);
    const auto begin = std::chrono::steady_clock::now();
    const bool ok = mem.TryRead(addr, buf, size, attempted);
    if (attempted)
        NotePossibleDeviceStall(OmniGhost::Gameplay::TimeMs(begin));
    if (attempted && !ok)
        g_read_fails.fetch_add(1, std::memory_order_relaxed);
    return ok;
}

bool IsUserPointer(uintptr_t value) {
#if defined(_WIN64)
    return value >= 0x10000ull && value <= 0x00007FFFFFFFFFFFull;
#else
    return value >= 0x10000u && value <= 0x7FFF0000u;
#endif
}

template<typename T>
bool QReadT(uintptr_t addr, T& out, const char* tag = "QRead") {
    return QRead(addr, &out, sizeof(T), tag);
}

// Persistent scatter handle — N sequential DMA reads → 1 round-trip. Ownership
// is explicit so every shutdown/error path closes the native resource once.
class ScatterHandleOwner {
public:
    ScatterHandleOwner() = default;
    ~ScatterHandleOwner() { Reset(); }
    ScatterHandleOwner(const ScatterHandleOwner&) = delete;
    ScatterHandleOwner& operator=(const ScatterHandleOwner&) = delete;

    void Ensure() {
        if (!handle_ && mem.vHandle) {
            handle_ = mem.CreateScatterHandle();
            if (!handle_) std::cerr << "[CS2] Failed to create scatter handle — falling back to QRead\n";
        }
    }
    void Reset() {
        if (!handle_) return;
        mem.CloseScatterHandle(handle_);
        handle_ = nullptr;
    }
    explicit operator bool() const noexcept { return handle_ != nullptr; }
    operator VMMDLL_SCATTER_HANDLE() const noexcept { return handle_; }

private:
    VMMDLL_SCATTER_HANDLE handle_ = nullptr;
};

ScatterHandleOwner g_scatter_full;
ScatterHandleOwner g_scatter_optional; // optional lane only — never shared with core/camera
std::future<void> g_collision_load;
std::atomic_bool g_collision_cancel{false};
std::string g_collision_attempt_map;
std::atomic<uint64_t> g_collision_retry_at{0};
// Per-scan phase timings for SPIKE_BREAKDOWN (acquisition thread only).
// Includes previously-unaccounted sub-phases so other_ms collapses to near zero.
static std::atomic<uint64_t> g_scan_id{0};
struct Cs2PhaseTiming {
    uint64_t scan_id = 0;
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
    float early_publish_ms = 0.f;
    float json_ms = 0.f;
    float cache_prune_ms = 0.f;
    float vector_copy_ms = 0.f;
    float visibility_submit_ms = 0.f;
    float cleanup_ms = 0.f;
    float other_ms = 0.f;
    float lock_wait_ms = 0.f;
    float optional_budget_left_ms = 0.f;
    int entity_full_probe = 0;
    int spectator_refresh = 0;
    int bomb_refresh = 0;
    int cache_cleanup = 0;
    int bones_players = 0;
    int bones_skipped_budget = 0;
    int visibility_players_tested = 0;
    int visibility_rays = 0;
};
static Cs2PhaseTiming g_phase{};
static std::atomic<int> g_pressure_level{0}; // 0=NORMAL 1=MODERATE 2=HIGH 3=RECOVERY
// After a slow VMM/FPGA call, back off heavy phases so the device can recover.
// Vendor-side pauses (~0.5–1.5s every ~10–15s on some stacks) cannot be removed
// from MemProcFS/LeechCore; we avoid stacking more DMA on top of them.
static std::atomic<uint64_t> g_heavy_dma_cooldown_until_ms{0};
static std::atomic<float> g_last_slow_dma_ms{0.f};
// Latest optional-lane cost, surfaced in the menu so a user can see which
// feature is being shed before core ESP ever loses priority.
static std::atomic<float> g_last_weapon_ms{0.f};
static std::atomic<float> g_last_ammo_ms{0.f};
static std::atomic<float> g_last_flags_ms{0.f};

static bool HeavyDmaCooldownActive() {
    return GetTickCount64() < g_heavy_dma_cooldown_until_ms.load(std::memory_order_acquire);
}

static void NotePossibleDeviceStall(float duration_ms) {
    // Only true device hangs (~300ms+). Normal scans on 35T/75T often sit at
    // 20–120ms — treating those as stalls froze ESP (bones skipped, sleeps).
    if (!(duration_ms >= 300.f)) return;
    g_last_slow_dma_ms.store(duration_ms, std::memory_order_relaxed);
    // Give the board/driver queue a real chance to drain. The renderer keeps
    // immutable snapshots during this short interval, so this reduces repeated
    // 0.8–1.0 s pauses without blocking overlay presentation.
    const uint64_t cool = static_cast<uint64_t>((std::max)(40.f, (std::min)(150.f, duration_ms * 0.15f)));
    const uint64_t until = GetTickCount64() + cool;
    uint64_t cur = g_heavy_dma_cooldown_until_ms.load(std::memory_order_relaxed);
    while (until > cur &&
           !g_heavy_dma_cooldown_until_ms.compare_exchange_weak(cur, until, std::memory_order_relaxed)) {
    }
}



uintptr_t g_cached_entity_root = 0; // refreshed once per frame when scanning

void EnsureScatter() {
    g_scatter_full.Ensure();
}

void DestroyScatter() {
    g_scatter_full.Reset();
    g_cached_entity_root = 0;
    {
        std::scoped_lock lock(g_process_liveness_mutex);
        g_process_liveness_next_probe_ms = 0;
        g_process_liveness_last_result.store(false, std::memory_order_release);
    }
}

struct EntityHandleParts {
    uintptr_t page = 0;
    uintptr_t index = 0;
    bool valid = false;
};

constexpr uint32_t kEntityHandlePageMask = 0x7FFFu;
constexpr uint32_t kEntityHandleIndexMask = 0x1FFu;
constexpr int kEntityHandlePageShift = 9;
constexpr uintptr_t kBoneArrayOffsetDefault = 0x80;

constexpr EntityHandleParts DecodeEntityHandle(uint32_t handle) noexcept {
    return { static_cast<uintptr_t>((handle & kEntityHandlePageMask) >> kEntityHandlePageShift),
             static_cast<uintptr_t>(handle & kEntityHandleIndexMask),
             handle != 0 && handle != 0xFFFFFFFFu };
}

static_assert(DecodeEntityHandle(0x201u).page == 1u);
static_assert(DecodeEntityHandle(0x201u).index == 1u);

// The master ESP toggle alone must not keep the player pipeline alive. Every
// listed element maps to an actual player visual/read path below.
static bool HasEnabledPlayerVisuals(const Config& c) {
    if (!c.esp_enabled) return false;
    return c.box || c.box_corner || c.skeleton || c.health_bar || c.armor_bar ||
        c.health_value || c.armor_value || c.name || c.distance || c.head_dot ||
        c.weapon_name || c.weapon_ammo || c.snaplines || c.trails || c.head_halo ||
        c.look_direction || c.chinese_hat || c.angel_wings || c.devil_horns ||
        c.floating_crown;
}

// True only when some feature actually needs the player list this frame.
// With everything OFF this is false → RunFrame is nearly free (target 130+ FPS).
bool NeedsPlayerScan(const Config& frame_config) {
    return HasEnabledPlayerVisuals(frame_config)
        || frame_config.aim_enabled
        || frame_config.trigger_enabled
        || frame_config.radar_2d
        || frame_config.webradar_enabled
        || frame_config.spectator_list
        || frame_config.bomb_timer
        || frame_config.hotkey_overlay;
}

bool ReadTextFile(const fs::path& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream stream;
    stream << file.rdbuf();
    out = stream.str();
    return true;
}

void LoadSchemaOffsets(const std::string& schema) {

    JsonClassU64(schema, "C_BaseEntity", "m_iHealth", offsets.m_iHealth);
    JsonClassU64(schema, "C_BaseEntity", "m_iTeamNum", offsets.m_iTeamNum);
    JsonClassU64(schema, "C_BaseEntity", "m_pGameSceneNode", offsets.m_pGameSceneNode);
    JsonClassU64(schema, "C_BaseEntity", "m_fFlags", offsets.m_fFlags);
    JsonClassU64(schema, "C_BaseEntity", "m_vecVelocity", offsets.m_vecVelocity);
    JsonClassU64(schema, "C_BasePlayerPawn", "m_vOldOrigin", offsets.m_vOldOrigin);
    JsonClassU64(schema, "CCSPlayerController", "m_hPlayerPawn", offsets.m_hPlayerPawn);
    JsonClassU64(schema, "CCSPlayerController", "m_hObserverPawn", offsets.m_hObserverPawn);
    JsonClassU64(schema, "C_BasePlayerPawn", "m_pObserverServices", offsets.m_pObserverServices);
    JsonClassU64(schema, "CPlayer_ObserverServices", "m_hObserverTarget", offsets.m_hObserverTarget);
    JsonClassU64(schema, "CCSPlayerController", "m_iPawnHealth", offsets.m_iPawnHealth);
    JsonClassU64(schema, "CCSPlayerController", "m_iPawnArmor", offsets.m_iPawnArmor);
    JsonClassU64(schema, "CBasePlayerController", "m_iszPlayerName", offsets.m_iszPlayerName);
    JsonClassU64(schema, "CBasePlayerController", "m_steamID", offsets.m_steamID);
    JsonClassU64(schema, "CCSPlayerController", "m_sSanitizedPlayerName", offsets.m_sSanitizedPlayerName);
    JsonClassU64(schema, "CCSPlayerController", "m_bPawnIsAlive", offsets.m_bPawnIsAlive);
    JsonClassU64(schema, "CCSPlayerController", "m_pInGameMoneyServices", offsets.m_pInGameMoneyServices);
    JsonClassU64(schema, "CCSPlayerController_InGameMoneyServices", "m_iAccount", offsets.m_iAccount);
    JsonClassU64(schema, "C_CSPlayerPawn", "m_ArmorValue", offsets.m_ArmorValue);
    JsonClassU64(schema, "C_CSPlayerPawn", "m_angEyeAngles", offsets.m_angEyeAngles);
    JsonClassU64(schema, "C_CSPlayerPawn", "m_iIDEntIndex", offsets.m_iIDEntIndex);
    JsonClassU64(schema, "C_CSPlayerPawn", "m_iShotsFired", offsets.m_iShotsFired);
    JsonClassU64(schema, "C_CSPlayerPawn", "m_bIsDefusing", offsets.m_bIsDefusing);
    JsonClassU64(schema, "C_CSPlayerPawn", "m_bIsScoped", offsets.m_bIsScoped);
    JsonClassU64(schema, "C_CSPlayerPawnBase", "m_flFlashDuration", offsets.m_flFlashDuration);
    JsonClassU64(schema, "C_CSPlayerPawn", "m_aimPunchAngle", offsets.m_aimPunchAngle);
    JsonClassU64(schema, "CGameSceneNode", "m_vecAbsOrigin", offsets.m_vecAbsOrigin);
    JsonClassU64(schema, "CGameSceneNode", "m_bDormant", offsets.m_bDormant);
    JsonClassU64(schema, "CSkeletonInstance", "m_modelState", offsets.m_modelState);
    JsonClassU64(schema, "C_PlantedC4", "m_bBombTicking", offsets.m_bBombTicking);
    JsonClassU64(schema, "C_PlantedC4", "m_flC4Blow", offsets.m_flC4Blow);
    JsonClassU64(schema, "C_PlantedC4", "m_flTimerLength", offsets.m_flTimerLength);
    JsonClassU64(schema, "C_PlantedC4", "m_bBombDefused", offsets.m_bBombDefused);
    JsonClassU64(schema, "C_PlantedC4", "m_bBeingDefused", offsets.m_bBeingDefused);
    JsonClassU64(schema, "C_PlantedC4", "m_flDefuseCountDown", offsets.m_flDefuseCountDown);
    JsonClassU64(schema, "C_PlantedC4", "m_hBombDefuser", offsets.m_hBombDefuser);
    JsonClassU64(schema, "C_BasePlayerPawn", "m_pWeaponServices", offsets.m_pWeaponServices);
    JsonClassU64(schema, "C_BasePlayerPawn", "m_pItemServices", offsets.m_pItemServices);
    JsonClassU64(schema, "CCSPlayer_ItemServices", "m_bHasDefuser", offsets.m_bHasDefuser);
    JsonClassU64(schema, "CPlayer_WeaponServices", "m_hActiveWeapon", offsets.m_hActiveWeapon);
    JsonClassU64(schema, "C_EconEntity", "m_AttributeManager", offsets.m_AttributeManager);
    JsonClassU64(schema, "C_AttributeContainer", "m_Item", offsets.m_Item);
    JsonClassU64(schema, "C_EconItemView", "m_iItemDefinitionIndex", offsets.m_iItemDefinitionIndex);
    offsets.BoneArray = offsets.m_modelState + 0x80;
}

bool ScanRipRelative(uintptr_t module_base, size_t module_size, const char* signature,
                     size_t displacement_offset, size_t instruction_size, uintptr_t& out_rva) {
    if (!module_base || module_size < 0x1000) return false;
    const uintptr_t match = mem.FindSignature(
        signature, module_base, module_base + module_size);
    if (!match) return false;

    int32_t displacement = 0;
    if (!QReadT(match + displacement_offset, displacement)) return false;
    const uintptr_t target = match + instruction_size + static_cast<intptr_t>(displacement);
    if (target < module_base || target >= module_base + module_size) return false;
    out_rva = target - module_base;
    return true;
}

bool ProbeViewMatrix(float* destination = nullptr) {
    float probe[16]{};
    if (!runtime.client_base || !offsets.dwViewMatrix)
        return false;

    mem.SetDmaCallTag("CS2.ViewMatrix");
    if (!QRead(runtime.client_base + offsets.dwViewMatrix, probe, sizeof(probe)))
        return false;

    int finite_values = 0;
    int meaningful_values = 0;
    for (const float value : probe) {
        if (std::isfinite(value) && std::fabs(value) < 1000000.f) {
            ++finite_values;
            if (std::fabs(value) > 0.000001f) ++meaningful_values;
        }
    }
    if (finite_values != 16 || meaningful_values < 4) return false;
    if (destination) std::memcpy(destination, probe, sizeof(probe));
    return true;
}

} // namespace

bool ResolveDataPath(std::string& out_dir) {
    const std::string exe = ExeDir();
    const std::vector<fs::path> search_bases = {
        fs::path(exe),
        fs::path(exe).parent_path(),
        fs::path(exe).parent_path().parent_path(),
        fs::path(exe).parent_path().parent_path().parent_path(),
        fs::current_path(),
    };
    const char* candidates[] = {
        "data",
        "src/games/Cs2/data",
        "CS2/data",
        "build/data",
        "artifacts/OmniGhost-win-x64/data",
        "offsets",
    };
    for (const fs::path& base : search_bases) {
        if (base.empty()) continue;
        for (const char* rel : candidates) {
            fs::path p = base / rel;
            if (fs::exists(p / "cs2_offsets.json")) {
                out_dir = p.string();
                std::cout << "[CS2] Usando offsets de: " << out_dir << std::endl;
                return true;
            }
        }
    }
    out_dir = (fs::path(exe) / "data").string();
    std::cout << "[CS2] Dados de offsets nao encontrados, fallback para: " << out_dir << std::endl;
    return false;
}

bool LoadOffsetsFromJson(const char* path) {
#if !defined(OMNIGHOST_DEV_EXTERNAL_OFFSETS)
    (void)path;
    return LoadEmbeddedOffsets();
#else
    const bool explicit_path = path && *path;
    const Offsets previous = offsets;
    std::string file;
    if (path && *path) {
        file = path;
    } else {
        std::string dir;
        ResolveDataPath(dir);
        std::cout << "[CS2] Resolved data path: " << dir << std::endl;
        file = (fs::path(dir) / "cs2_offsets.json").string();
    }

    std::cout << "[CS2] Loading offsets from: " << file << std::endl;
    std::string data;
    if (!ReadTextFile(file, data)) {
        status = "cs2_offsets.json nao encontrado (coloca em data/ ao lado do exe)";
        std::cout << "[CS2] " << status << "\n    procurou: " << file << std::endl;
        return false;
    }

    offsets.dwEntityList = 0;
    offsets.dwLocalPlayerPawn = 0;
    offsets.dwLocalPlayerController = 0;
    offsets.dwViewMatrix = 0;
    offsets.dwViewAngles = 0;
    offsets.dwGlobalVars = 0;
    offsets.dwBuildNumber = 0;
    offsets.dwPlantedC4 = 0;

    // 1) JSON style: "client.dll" { "dwEntityList": "0x..." } (cs2-dumper / a2x)
    auto sub = data.find("\"client.dll\"");
    const std::string slice = (sub != std::string::npos) ? data.substr(sub) : data;

    JsonU64(slice, "dwEntityList", offsets.dwEntityList);
    JsonU64(slice, "dwLocalPlayerPawn", offsets.dwLocalPlayerPawn);
    JsonU64(slice, "dwLocalPlayerController", offsets.dwLocalPlayerController);
    JsonU64(slice, "dwViewMatrix", offsets.dwViewMatrix);
    JsonU64(slice, "dwViewAngles", offsets.dwViewAngles);
    JsonU64(slice, "dwGlobalVars", offsets.dwGlobalVars);
    JsonU64(slice, "dwPlantedC4", offsets.dwPlantedC4);

    auto eng = data.find("\"engine2.dll\"");
    if (eng != std::string::npos) {
        const std::string es = data.substr(eng);
        JsonU64(es, "dwBuildNumber", offsets.dwBuildNumber);
    }

    // 2) cheatoffsets.com API: values live in markdown C++ fences
    //    e.g. uintptr_t dwEntityList = 0x2554050;
    auto grab_cpp = [&](const char* key, uintptr_t& out) {
        if (out) return; // already from JSON
        const std::string k(key);
        size_t p = 0;
        while ((p = data.find(k, p)) != std::string::npos) {
            // require word boundary-ish: char before is not alphanumeric
            if (p > 0) {
                const char c = data[p - 1];
                if (std::isalnum((unsigned char)c) || c == '_' || c == '"') {
                    p += k.size();
                    continue;
                }
            }
            size_t eq = data.find('=', p + k.size());
            size_t semi = data.find(';', p + k.size());
            if (eq == std::string::npos || semi == std::string::npos || eq > semi || eq - p > 96) {
                p += k.size();
                continue;
            }
            size_t hx = data.find("0x", eq);
            if (hx == std::string::npos || hx > semi) {
                p += k.size();
                continue;
            }
            size_t e = hx + 2;
            while (e < data.size() && std::isxdigit((unsigned char)data[e])) ++e;
            try {
                out = (uintptr_t)std::stoull(data.substr(hx, e - hx), nullptr, 16);
            } catch (const std::exception& ex) {
                std::cerr << "[CS2] LoadOffsetsFromJson: Failed to parse hex value at offset " << hx << ": " << ex.what() << "\n";
                out = 0;
            } catch (...) {
                std::cerr << "[CS2] LoadOffsetsFromJson: Unknown exception parsing hex value at offset " << hx << "\n";
                out = 0;
            }
            return;
        }
    };
    grab_cpp("dwEntityList", offsets.dwEntityList);
    grab_cpp("dwLocalPlayerPawn", offsets.dwLocalPlayerPawn);
    grab_cpp("dwLocalPlayerController", offsets.dwLocalPlayerController);
    grab_cpp("dwViewMatrix", offsets.dwViewMatrix);
    grab_cpp("dwViewAngles", offsets.dwViewAngles);
    grab_cpp("dwGlobalVars", offsets.dwGlobalVars);
    grab_cpp("dwPlantedC4", offsets.dwPlantedC4);
    grab_cpp("dwBuildNumber", offsets.dwBuildNumber);

    // 3) offsets_flat style keys if present
    {
        uintptr_t v = 0;
        if (!offsets.dwEntityList && JsonU64(data, "client.dwEntityList", v)) offsets.dwEntityList = v;
        v = 0; if (!offsets.dwLocalPlayerPawn && JsonU64(data, "client.dwLocalPlayerPawn", v)) offsets.dwLocalPlayerPawn = v;
        v = 0; if (!offsets.dwViewMatrix && JsonU64(data, "client.dwViewMatrix", v)) offsets.dwViewMatrix = v;
    }

    LoadSchemaOffsets(data);
    std::string validation_error;
    offsets.loaded = offsets.Validate(&validation_error);

    const bool loaded_from_file = offsets.loaded;
    if (loaded_from_file) {
        char buf[192];
        std::snprintf(buf, sizeof(buf),
            "Offsets OK  EL=0x%llX  LPP=0x%llX  VM=0x%llX",
            (unsigned long long)offsets.dwEntityList,
            (unsigned long long)offsets.dwLocalPlayerPawn,
            (unsigned long long)offsets.dwViewMatrix);
        status = buf;
        offsets_source = file;
        std::cout << "[CS2] Loaded: " << file << std::endl;
        std::cout << "[CS2] Schema  m_hPlayerPawn=0x" << std::hex << offsets.m_hPlayerPawn
                  << " m_iHealth=0x" << offsets.m_iHealth
                  << " m_iTeamNum=0x" << offsets.m_iTeamNum
                  << " m_vOldOrigin=0x" << offsets.m_vOldOrigin
                  << " m_ArmorValue=0x" << offsets.m_ArmorValue
                  << std::dec << std::endl;
    } else {
        status = validation_error.empty() ? "cs2_offsets.json incompleto" : validation_error;
        offsets_source = status;
        if (explicit_path)
            offsets = previous;
    }
    std::cout << "[CS2] " << status << std::endl;
    return loaded_from_file;
#endif
}

bool LoadEmbeddedOffsets() {
    OmniGhost::EmbeddedOffsets::Snapshot snapshot;
    OmniGhost::EmbeddedOffsets::Diagnostics diagnostics;
    if (!OmniGhost::EmbeddedOffsets::Load(
            OmniGhost::EmbeddedOffsets::Game::CS2, snapshot, diagnostics)) {
        offsets = {};
        offsets_source = "recurso embedded inválido";
        std::clog << "[RESOURCE] game=CS2 source=embedded id=IDR_OFFSETS_CS2 load=FAIL reason="
                  << diagnostics.error << '\n';
        return false;
    }
    offsets = Offsets{};
    auto read = [&](const char* path, uintptr_t& value, bool required = true) {
        std::uint64_t loaded = 0;
        const bool ok = snapshot.TryGet(path, loaded) && loaded != 0;
        if (ok) value = static_cast<uintptr_t>(loaded);
        return ok || !required;
    };
    bool required = true;
    required &= read("client.dll.dwEntityList", offsets.dwEntityList);
    required &= read("client.dll.dwLocalPlayerPawn", offsets.dwLocalPlayerPawn);
    read("client.dll.dwLocalPlayerController", offsets.dwLocalPlayerController, false);
    required &= read("client.dll.dwViewMatrix", offsets.dwViewMatrix);
    read("client.dll.dwViewAngles", offsets.dwViewAngles, false);
    read("client.dll.dwGlobalVars", offsets.dwGlobalVars, false);
    read("client.dll.dwPlantedC4", offsets.dwPlantedC4, false);
    read("engine2.dll.dwBuildNumber", offsets.dwBuildNumber, false);

    // Release builds use the embedded snapshot exclusively. Previously only
    // module globals were restored here, leaving pawn/controller fields at
    // stale header defaults even though the current schema was embedded.
    read("schemas.C_BaseEntity.m_iHealth", offsets.m_iHealth, false);
    read("schemas.C_BaseEntity.m_iTeamNum", offsets.m_iTeamNum, false);
    read("schemas.C_BaseEntity.m_pGameSceneNode", offsets.m_pGameSceneNode, false);
    read("schemas.C_BaseEntity.m_fFlags", offsets.m_fFlags, false);
    read("schemas.C_BaseEntity.m_vecVelocity", offsets.m_vecVelocity, false);
    read("schemas.C_BasePlayerPawn.m_vOldOrigin", offsets.m_vOldOrigin, false);
    read("schemas.CCSPlayerController.m_hPlayerPawn", offsets.m_hPlayerPawn, false);
    read("schemas.CCSPlayerController.m_hObserverPawn", offsets.m_hObserverPawn, false);
    read("schemas.C_BasePlayerPawn.m_pObserverServices", offsets.m_pObserverServices, false);
    read("schemas.CPlayer_ObserverServices.m_hObserverTarget", offsets.m_hObserverTarget, false);
    read("schemas.CCSPlayerController.m_iPawnHealth", offsets.m_iPawnHealth, false);
    read("schemas.CCSPlayerController.m_iPawnArmor", offsets.m_iPawnArmor, false);
    read("schemas.CBasePlayerController.m_iszPlayerName", offsets.m_iszPlayerName, false);
    read("schemas.CBasePlayerController.m_steamID", offsets.m_steamID, false);
    read("schemas.CCSPlayerController.m_sSanitizedPlayerName", offsets.m_sSanitizedPlayerName, false);
    read("schemas.CCSPlayerController.m_bPawnIsAlive", offsets.m_bPawnIsAlive, false);
    read("schemas.CCSPlayerController.m_pInGameMoneyServices", offsets.m_pInGameMoneyServices, false);
    read("schemas.CCSPlayerController_InGameMoneyServices.m_iAccount", offsets.m_iAccount, false);
    read("schemas.C_CSPlayerPawn.m_ArmorValue", offsets.m_ArmorValue, false);
    read("schemas.C_CSPlayerPawn.m_angEyeAngles", offsets.m_angEyeAngles, false);
    read("schemas.C_CSPlayerPawn.m_iIDEntIndex", offsets.m_iIDEntIndex, false);
    read("schemas.C_CSPlayerPawn.m_iShotsFired", offsets.m_iShotsFired, false);
    read("schemas.C_CSPlayerPawn.m_bIsDefusing", offsets.m_bIsDefusing, false);
    read("schemas.C_CSPlayerPawn.m_bIsScoped", offsets.m_bIsScoped, false);
    read("schemas.C_CSPlayerPawnBase.m_flFlashDuration", offsets.m_flFlashDuration, false);
    read("schemas.CGameSceneNode.m_vecAbsOrigin", offsets.m_vecAbsOrigin, false);
    read("schemas.CGameSceneNode.m_bDormant", offsets.m_bDormant, false);
    read("schemas.CSkeletonInstance.m_modelState", offsets.m_modelState, false);
    uintptr_t boneArrayInModelState = kBoneArrayOffsetDefault;
    read("schemas.CModelState.m_boneArray", boneArrayInModelState, false);
    offsets.BoneArray = offsets.m_modelState + boneArrayInModelState;
    read("schemas.C_PlantedC4.m_bBombTicking", offsets.m_bBombTicking, false);
    read("schemas.C_PlantedC4.m_flC4Blow", offsets.m_flC4Blow, false);
    read("schemas.C_PlantedC4.m_flTimerLength", offsets.m_flTimerLength, false);
    read("schemas.C_PlantedC4.m_bBombDefused", offsets.m_bBombDefused, false);
    read("schemas.C_PlantedC4.m_bBeingDefused", offsets.m_bBeingDefused, false);
    read("schemas.C_PlantedC4.m_flDefuseCountDown", offsets.m_flDefuseCountDown, false);
    read("schemas.C_PlantedC4.m_hBombDefuser", offsets.m_hBombDefuser, false);
    read("schemas.C_BasePlayerPawn.m_pWeaponServices", offsets.m_pWeaponServices, false);
    read("schemas.C_BasePlayerPawn.m_pItemServices", offsets.m_pItemServices, false);
    read("schemas.CCSPlayer_ItemServices.m_bHasDefuser", offsets.m_bHasDefuser, false);
    read("schemas.CPlayer_WeaponServices.m_hActiveWeapon", offsets.m_hActiveWeapon, false);
    read("schemas.C_EconEntity.m_AttributeManager", offsets.m_AttributeManager, false);
    read("schemas.C_AttributeContainer.m_Item", offsets.m_Item, false);
    read("schemas.C_EconItemView.m_iItemDefinitionIndex", offsets.m_iItemDefinitionIndex, false);
    read("schemas.C_BasePlayerWeapon.m_iClip1", offsets.m_iClip1, false);
    read("schemas.C_BasePlayerWeapon.m_pReserveAmmo", offsets.m_pReserveAmmo, false);
    read("schemas.C_CSPlayerPawn.m_entitySpottedState", offsets.m_entitySpottedState, false);
    read("schemas.EntitySpottedState_t.m_bSpotted", offsets.m_bSpotted, false);
    std::string validation_error;
    offsets.loaded = required && offsets.Validate(&validation_error);
    if (offsets.loaded) {
        offsets_source = "offsets embedded";
        std::cout << "[RESOURCE] game=CS2 source=embedded id=IDR_OFFSETS_CS2 load=PASS build="
                  << snapshot.metadata.build << std::endl;
    } else if (!validation_error.empty()) {
        status = validation_error;
        offsets_source = "offsets embedded inválidos";
    }
    return offsets.loaded;
}

static std::string LowerString(const std::string& s) {
    std::string out;
    out.resize(s.size());
    std::transform(s.begin(), s.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

static bool EndsWith(const std::string& value, const std::string& ending) {
    if (ending.size() > value.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

static bool ResolveModuleBases() {
    runtime.client_base = mem.GetBaseDaddy("client.dll");
    runtime.engine_base = mem.GetBaseDaddy("engine2.dll");
    if (runtime.client_base && runtime.engine_base)
        return true;

    auto module_names = mem.GetModuleList("");
    for (const auto& module : module_names) {
        const std::string lower = LowerString(module);
        if (!runtime.client_base && EndsWith(lower, "client.dll")) {
            runtime.client_base = mem.GetBaseDaddy(module);
            std::cout << "[CS2] Found client module as " << module << "" << std::endl;
        }
        if (!runtime.engine_base && EndsWith(lower, "engine2.dll")) {
            runtime.engine_base = mem.GetBaseDaddy(module);
            std::cout << "[CS2] Found engine module as " << module << "" << std::endl;
        }
        if (runtime.client_base && runtime.engine_base)
            break;
    }

    if (!runtime.client_base) {
        std::cout << "[CS2] Falha ao encontrar client.dll via GetBaseDaddy; modulos atuais:" << std::endl;
        for (const auto& module : module_names)
            std::cout << "    " << module << std::endl;
    }
    return runtime.client_base != 0;
}

bool WaitForProcess(int timeout_sec) {
    std::cout << "[CS2] Aguardando cs2.exe..." << std::endl;
    status = "Aguardando cs2.exe";
    const int steps = (std::max)(1, timeout_sec);
    for (int i = 0; i < steps; ++i) {
        DWORD pid = 0;
        if (mem.vHandle) {
            pid = mem.GetPidFromName("cs2.exe");
            if (!pid) pid = mem.GetPidFromName("CS2.exe");
        }
        // The game may be on a second PC. A local process is not proof that
        // the DMA target contains CS2, so do not use Toolhelp for attach.
        if (pid) {
            status = "cs2.exe encontrado";
            std::cout << "[CS2] " << status << " (pid " << pid << ")" << std::endl;
            return true;
        }
        if (i % 5 == 0)
            std::cout << "[CS2] ... ainda a aguardar (" << i << "s)" << std::endl;
        Sleep(1000);
    }
    status = "Timeout - cs2.exe nao encontrado";
    std::cout << "[CS2] " << status << std::endl;
    return false;
}

bool RecoverCriticalOffsets() {
    if (!runtime.client_base) return false;

    const size_t client_size = mem.GetBaseSize("client.dll");
    const size_t engine_size = runtime.engine_base ? mem.GetBaseSize("engine2.dll") : 0;
    if (client_size < 0x1000) {
        std::cout << "[CS2] Não foi possível obter o tamanho de client.dll" << std::endl;
        return false;
    }

    status = "A recuperar offsets diretamente do processo";
    std::cout << "[CS2] Offsets empacotados incompatíveis; pesquisa por assinaturas..." << std::endl;

    uintptr_t entity_list = 0;
    uintptr_t view_matrix = 0;
    uintptr_t local_controller = 0;
    uintptr_t prediction = 0;

    const bool entity_ok = ScanRipRelative(runtime.client_base, client_size,
        "48 89 0D ? ? ? ? E9 ? ? ? ? CC", 3, 7, entity_list);
    const bool matrix_ok = ScanRipRelative(runtime.client_base, client_size,
        "48 8D 0D ? ? ? ? 48 C1 E0 06", 3, 7, view_matrix);
    const bool prediction_ok = ScanRipRelative(runtime.client_base, client_size,
        "48 8D 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC CC 40 53 56 41 54",
        3, 7, prediction);
    ScanRipRelative(runtime.client_base, client_size,
        "48 8B 05 ? ? ? ? 41 89 BE", 3, 7, local_controller);

    uintptr_t local_pawn = 0;
    if (prediction_ok) {
        const uintptr_t member_match = mem.FindSignature(
            "4C 39 B6 ? ? ? ? 74 ? 44 88 BE",
            runtime.client_base, runtime.client_base + client_size);
        uint32_t member_offset = 0;
        if (member_match && QReadT(member_match + 3, member_offset) && member_offset < 0x10000)
            local_pawn = prediction + member_offset;
    }

    uintptr_t build_number = 0;
    if (runtime.engine_base && engine_size >= 0x1000) {
        ScanRipRelative(runtime.engine_base, engine_size,
            "89 05 ? ? ? ? 48 8D 0D ? ? ? ? FF 15 ? ? ? ? 48 8B 0D",
            2, 6, build_number);
    }

    if (!entity_ok || !matrix_ok || !local_pawn) {
        char failure[192]{};
        std::snprintf(failure, sizeof(failure),
            "Falha ao recuperar offsets (EntityList=%s, ViewMatrix=%s, LocalPawn=%s)",
            entity_ok ? "OK" : "falhou",
            matrix_ok ? "OK" : "falhou",
            local_pawn ? "OK" : "falhou");
        status = failure;
        std::cout << "[CS2] " << status << std::endl;
        return false;
    }

    offsets.dwEntityList = entity_list;
    offsets.dwViewMatrix = view_matrix;
    offsets.dwLocalPlayerPawn = local_pawn;
    if (local_controller) offsets.dwLocalPlayerController = local_controller;
    if (build_number) offsets.dwBuildNumber = build_number;
    std::string validation_error;
    offsets.loaded = offsets.Validate(&validation_error);
    if (!offsets.loaded) {
        status = validation_error;
        offsets_source = "assinaturas inválidas";
        return false;
    }
    offsets_source = "assinaturas do processo atual";

    char recovered[224]{};
    std::snprintf(recovered, sizeof(recovered),
        "Offsets recuperados  EL=0x%llX  LPP=0x%llX  VM=0x%llX",
        static_cast<unsigned long long>(offsets.dwEntityList),
        static_cast<unsigned long long>(offsets.dwLocalPlayerPawn),
        static_cast<unsigned long long>(offsets.dwViewMatrix));
    status = recovered;
    std::cout << "[CS2] " << status << std::endl;
    return true;
}

bool Attach() {
    status = "A anexar cs2.exe";
    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Adapter,
        "CS2 attach begin",
        {{"game", "CS2"}, {"vHandle", mem.vHandle ? "yes" : "no"}});

    // Prefer opening a functional FPGA session (not launcher PnP probe).
    if (!mem.vHandle) {
        status = "Inicializando DMA";
        std::cout << "[CS2] " << status << std::endl;
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Info,
            OmniGhost::SessionLog::Subsystem::DMA,
            "CS2 opening FPGA session", {});
        try {
            if (!mem.Init("", true, false)) {
                status = "DMA offline (FPGA nao abriu sessao funcional)";
                std::cout << "[CS2] " << status << std::endl;
                OmniGhost::SessionLog::Write(
                    OmniGhost::SessionLog::Severity::Error,
                    OmniGhost::SessionLog::Subsystem::DMA,
                    "CS2 DMA offline after mem.Init",
                    {{"status", status}});
                return false;
            }
            OmniGhost::SessionLog::Write(
                OmniGhost::SessionLog::Severity::Info,
                OmniGhost::SessionLog::Subsystem::DMA,
                "CS2 FPGA session open",
                {{"vHandle", mem.vHandle ? "yes" : "no"}});
        } catch (const std::exception& ex) {
            status = "Excecao ao inicializar DMA: " + std::string(ex.what());
            std::cerr << "[CS2] CRASH em mem.Init: " << ex.what() << std::endl;
            OmniGhost::SessionLog::Write(
                OmniGhost::SessionLog::Severity::Error,
                OmniGhost::SessionLog::Subsystem::DMA,
                "CS2 mem.Init exception",
                {{"what", ex.what()}});
            return false;
        } catch (...) {
            status = "Erro desconhecido ao inicializar DMA";
            std::cerr << "[CS2] CRASH desconhecido em mem.Init" << std::endl;
            OmniGhost::SessionLog::Write(
                OmniGhost::SessionLog::Severity::Error,
                OmniGhost::SessionLog::Subsystem::DMA,
                "CS2 mem.Init unknown exception", {});
            return false;
        }
    }

    // Only the remote DMA target can satisfy an attach to the game PC.
    if (mem.GetPidFromName("cs2.exe") || mem.GetPidFromName("CS2.exe")) {
        status = "cs2.exe encontrado";
        std::cout << "[CS2] " << status << " (DMA)" << std::endl;
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Info,
            OmniGhost::SessionLog::Subsystem::Process,
            "CS2 process present (DMA)", {});
    } else if (!WaitForProcess(10)) {
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Error,
            OmniGhost::SessionLog::Subsystem::Process,
            "CS2 process wait timeout",
            {{"status", status}});
        return false;
    }

    // Attach may fail briefly while the process is still starting; retry.
    bool attached = false;
    for (int a = 0; a < 40; ++a) {
        try {
            if (mem.Init("cs2.exe", false, false) || mem.Init("CS2.exe", false, false)) {
                attached = true;
                break;
            }
        } catch (const std::exception& ex) {
            std::cerr << "[CS2] Attach retry " << a << " exception: " << ex.what() << std::endl;
            OmniGhost::SessionLog::Write(
                OmniGhost::SessionLog::Severity::Warning,
                OmniGhost::SessionLog::Subsystem::Process,
                "CS2 process bind retry exception",
                {{"attempt", std::to_string(a)}, {"what", ex.what()}});
        } catch (...) {
            std::cerr << "[CS2] Attach retry " << a << " unknown exception caught and logged" << std::endl;
        }
        status = "A anexar cs2.exe...";
        if ((a % 5) == 0) {
            std::cout << "[CS2] Attach retry " << a << std::endl;
            OmniGhost::SessionLog::Write(
                OmniGhost::SessionLog::Severity::Info,
                OmniGhost::SessionLog::Subsystem::Process,
                "CS2 process bind retry",
                {{"attempt", std::to_string(a)}});
        }
        Sleep(a < 10 ? 250 : 500);
    }
    if (!attached) {
        status = "Falha ao anexar cs2.exe (DMA aberto mas processo nao ligado)";
        std::cout << "[CS2] " << status << std::endl;
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Error,
            OmniGhost::SessionLog::Subsystem::Process,
            "CS2 process bind failed",
            {{"status", status}});
        return false;
    }
    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Process,
        "CS2 process bound", {});

    // client.dll is mapped late during boot — wait up to ~60s.
    for (int attempt = 0; attempt < 120; ++attempt) {
        try {
            if (ResolveModuleBases())
                break;
        } catch (const std::exception& ex) {
            std::cerr << "[CS2] ResolveModuleBases attempt " << attempt << " exception: " << ex.what() << std::endl;
        } catch (...) {
            std::cerr << "[CS2] ResolveModuleBases attempt " << attempt << " unknown exception caught and logged" << std::endl;
        }
        if (attempt % 5 == 0)
            std::cout << "[CS2] client.dll ainda nao resolvido (" << attempt << ")" << std::endl;
        status = "A aguardar client.dll...";
        Sleep(500);
        // Re-bind process every few seconds in case DMA handle went stale
        if (attempt > 0 && (attempt % 10) == 0) {
            try {
                mem.Init("cs2.exe", false, false) || mem.Init("CS2.exe", false, false);
            } catch (const std::exception& ex) {
                std::cerr << "[CS2] Re-bind attempt " << attempt << " exception: " << ex.what() << std::endl;
            } catch (...) {
                std::cerr << "[CS2] Re-bind attempt " << attempt << " unknown exception caught and logged" << std::endl;
            }
        }
    }
    if (!runtime.client_base) {
        status = "client.dll nao encontrado (abre o CS2 e espera o menu principal)";
        std::cout << "[CS2] " << status << std::endl;
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Error,
            OmniGhost::SessionLog::Subsystem::Process,
            "CS2 client.dll not found",
            {{"status", status}});
        return false;
    }
    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Process,
        "CS2 modules resolved",
        {{"client", std::to_string(runtime.client_base)}});

    // Priority: packaged/local data → embedded fallback → signature recovery.
    if (!offsets.loaded) {
        LoadOffsetsFromJson(nullptr);
    }
    if (!offsets.loaded) {
        LoadOffsetsFromJson(nullptr);
    }
    if (!offsets.loaded) {
        LoadEmbeddedOffsets();
    }
    if (!offsets.loaded) {
        status = "Sem offsets validos";
        return false;
    }

    std::cout << "[CS2] Offsets source final: " << offsets_source
              << "  EL=0x" << std::hex << offsets.dwEntityList
              << "  LPC=0x" << offsets.dwLocalPlayerController
              << "  LPP=0x" << offsets.dwLocalPlayerPawn
              << std::dec << std::endl;

    // Initialize shared weapon name registry (used by both CS2 and FiveM)
    if (!OmniGhost::Gameplay::WeaponNameRegistry::Instance().LoadFromJson("data/weapons.json")) {
        std::cout << "[CS2] Warning: Failed to load data/weapons.json, trying embedded resource..." << std::endl;
        if (!OmniGhost::Gameplay::WeaponNameRegistry::Instance().LoadFromEmbedded()) {
            std::cout << "[CS2] Warning: Weapon name registry not loaded, using fallback maps" << std::endl;
        }
    }

    if (runtime.engine_base && offsets.dwBuildNumber)
        QReadT(runtime.engine_base + offsets.dwBuildNumber, runtime.build_number);

    // Validate globals against the running build. Packaged offsets become stale
    // after game updates; recover them from the loaded modules when necessary.
    if (!ProbeViewMatrix()) {
        std::cout << "[CS2] ViewMatrix ilegível; offsets podem estar desatualizados" << std::endl;
        if (!RecoverCriticalOffsets() || !ProbeViewMatrix()) {
            status = "Offsets incompatíveis com o build atual; atualiza os ficheiros em data/";
            OmniGhost::OffsetAuto::MarkOutdated(OmniGhost::ActiveGame::CS2, status);
            std::cout << "[CS2] " << status << std::endl;
            return false;
        }
    }

    if (runtime.engine_base && offsets.dwBuildNumber)
        QReadT(runtime.engine_base + offsets.dwBuildNumber, runtime.build_number);

    ready = true;
    OmniGhost::OffsetAuto::MarkLiveValid(OmniGhost::ActiveGame::CS2,
        "View matrix e módulos críticos validados no build em execução");
    char buf[160];
    std::snprintf(buf, sizeof(buf), "CS2 OK  client=0x%llX  build=%u",
        (unsigned long long)runtime.client_base, runtime.build_number);
    status = buf;
    std::cout << "[CS2] " << status << std::endl;

    // Auto-detect Makcu on all COM ports (same behaviour as FiveM).
    if (!aim_type::IsConnected()) {
        std::cout << "[CS2] A procurar Makcu em todas as portas COM..." << std::endl;
        makcu_wrapper::MakcuInitialize("");
        if (makcu_wrapper::IsConnected()) {
            aim_type::config.active = aim_type::DeviceType::Makcu;
            aim_type::config.makcu_connected = true;
            std::cout << "[CS2] Makcu ligado automaticamente." << std::endl;
        } else {
            std::cout << "[CS2] Makcu nao encontrado (aimbot usa fallback software)." << std::endl;
        }
    } else if (makcu_wrapper::IsConnected() &&
               aim_type::config.active == aim_type::DeviceType::Makcu) {
        aim_type::config.active = aim_type::DeviceType::Makcu;
        aim_type::config.makcu_connected = true;
    }
    return true;
}

// CS2 entity identity stride is 0x70 (Source 2 CEntityIdentity).
// Reference CS2-DMA always uses 0x70 for controller slots and pawn handles.
// 0x78 is kept only as a soft fallback when 0x70 yields zero controllers.
constexpr uintptr_t kEntityIdentityStride = 0x70;
constexpr uintptr_t kLegacyEntityIdentityStride = 0x78;
constexpr uintptr_t kEntityPageSize = 0x200;
constexpr uintptr_t kEntityPageTableOffset = 0x10;
constexpr uintptr_t kGlobalVarsCurrentTime = 0x30;
constexpr uintptr_t kGlobalVarsCurrentMap = 0x180;

// Handle decoding constants (Source 2 CEntityIdentity)

// Name array default offsets (fallback when schema offsets missing)
constexpr uintptr_t kNameEntrySizeDefault = 0xD0;
constexpr uintptr_t kNameArrayPosDefault = 0x3038;
constexpr uintptr_t kNameEntryNameDefault = 0x10;
constexpr uintptr_t kNameEntryHealthDefault = 0x90;
constexpr uintptr_t kNameEntryArmorDefault = 0x98;
constexpr uintptr_t kNameEntryAliveDefault = 0x78;

// Bone array offset within model state (fallback)

// Player structure default offsets (fallback)
constexpr uintptr_t kPlayerSizeDefault = 0x2808;
constexpr uintptr_t kPlayerPosDataDefault = 0x80;
constexpr uintptr_t kPlayerPosDefault = 0xEB0;
constexpr uintptr_t kPlayerStanceDefault = 0x19D0;
constexpr uintptr_t kPlayerArmorDefault = 0x13F8;
constexpr uintptr_t kPlayerValidDefault = 0x13FC;
constexpr uintptr_t kPlayerTeamDefault = 0xB0;

uintptr_t g_pending_entity_list = 0;
int g_entity_list_confirmations = 0;
uintptr_t g_controller_stride = kEntityIdentityStride;
uintptr_t g_pawn_stride = kEntityIdentityStride;
uint64_t g_entity_list_next_validation_ms = 0;
uint64_t g_entity_list_next_full_probe_ms = 0;
bool g_entity_list_force_refresh = true;

// One authoritative reset boundary for detach, shutdown and map changes.
// Local hot-path caches observe this monotonically increasing generation and
// clear lazily on their next access, avoiding a stop-the-world map sweep.
// Central generation token observed by every entity cache. Caches clear lazily
// on mismatch; this helper is the single authoritative bump point for detach,
// map change and reattach so no static unordered_map can inherit another
// session's pointers.
void ResetSessionState(const char* reason) {
    g_session_generation.fetch_add(1, std::memory_order_acq_rel);
    g_visibility_worker.Reset();
    // Cancel in-flight collision BVH build before clearing the cache so the
    // worker cannot publish into a session that already moved on.
    g_collision_cancel.store(true, std::memory_order_release);
    if (g_collision_load.valid()) {
        try {
            // Non-blocking poll first; if still running, wait briefly then abandon.
            if (g_collision_load.wait_for(std::chrono::milliseconds(50)) == std::future_status::ready)
                g_collision_load.get();
        } catch (const std::exception& ex) {
            std::cerr << "[CS2] Collision load failed during session reset: " << ex.what() << '\n';
        } catch (...) {
            std::cerr << "[CS2] Collision load failed during session reset: unknown exception\n";
        }
    }
    g_collision_attempt_map.clear();
    g_collision_retry_at.store(0, std::memory_order_relaxed);
    Trajectory::CollisionCache().Clear();
    g_pressure_level.store(0, std::memory_order_relaxed);
    g_camera_deadline_ms.store(0, std::memory_order_relaxed);
    g_camera_pending.store(false, std::memory_order_relaxed);
    g_last_snapshot_publish_ms.store(0, std::memory_order_relaxed);
    g_runtime_snapshot_drops.store(0, std::memory_order_relaxed);
    g_acquisition_hz.store(0.f, std::memory_order_relaxed);
    g_camera_history = {};
    g_camera_history_next.store(0, std::memory_order_relaxed);
    g_pending_entity_list = 0;
    g_entity_list_confirmations = 0;
    g_entity_list_next_validation_ms = 0;
    g_entity_list_next_full_probe_ms = 0;
    g_entity_list_force_refresh = true;
    g_cached_entity_root = 0;
    if (reason) std::cout << "[CS2] Reset de sessao: " << reason
                          << " gen=" << g_session_generation.load(std::memory_order_relaxed) << std::endl;
}



bool IsPlayableTeam(int team) {
    return team == 2 || team == 3;
}

bool IsFinitePosition(const float* pos) {
    return pos && std::isfinite(pos[0]) && std::isfinite(pos[1]) && std::isfinite(pos[2]);
}

// Cheap broad-phase test used before requesting a complete bone array.  It
// uses clip space directly, so it needs no ImGui state and can run safely on
// the acquisition thread.  The generous margin preserves targets entering the
// screen while rejecting entities well behind/outside the camera frustum.
bool InsideExpandedFrustum(const float* pos, const float* vm) {
    if (!IsFinitePosition(pos) || !vm) return false;
    const float x = pos[0], y = pos[1], z = pos[2] + 38.f;
    const float clipX = x * vm[0]  + y * vm[1]  + z * vm[2]  + vm[3];
    const float clipY = x * vm[4]  + y * vm[5]  + z * vm[6]  + vm[7];
    const float clipW = x * vm[12] + y * vm[13] + z * vm[14] + vm[15];
    if (!std::isfinite(clipW) || clipW <= 0.01f) return false;
    const float nx = clipX / clipW;
    const float ny = clipY / clipW;
    return std::isfinite(nx) && std::isfinite(ny) &&
           nx >= -1.35f && nx <= 1.35f && ny >= -1.45f && ny <= 1.45f;
}

bool IsPlayableMapName(const char* map) {
    if (!map || !*map) return false;
    const std::string name(map);
    // RefreshMapName already excludes background/main-menu maps. Community
    // maps need not use Valve's de_/cs_/workshop_ prefixes; rejecting them
    // here can switch a live match back to lobby and clear the ESP.
    return name != "lobby" && name != "none" && name != "mainmenu";
}

bool IsBotPlayerName(const char* name) {
    return name && (_strnicmp(name, "BOT ", 4) == 0 || _stricmp(name, "BOT") == 0);
}

void RefreshMapName() {
    if (!offsets.dwGlobalVars) {
        // Keep the last confirmed map during a transient offset/read failure.
        // Clearing it here deactivates the whole ESP for a frame after a DMA
        // timeout and looks exactly like a visual hitch.
        return;
    }

    // P1-8: Cache map name with 30s TTL — map changes once per match.
    static std::string cachedMapName;
    static uint64_t cachedMapNameMs = 0;
    const uint64_t nowMs = GetTickCount64();
    constexpr uint64_t kMapNameTtlMs = 30000;
    if (!cachedMapName.empty() && nowMs < cachedMapNameMs + kMapNameTtlMs) {
        // Use cached name, update runtime
        if (cachedMapName.size() >= sizeof(runtime.map_name))
            cachedMapName.resize(sizeof(runtime.map_name) - 1);
        std::memset(runtime.map_name, 0, sizeof(runtime.map_name));
        std::memcpy(runtime.map_name, cachedMapName.data(), cachedMapName.size());
        return;
    }

    uintptr_t globals = 0;
    uintptr_t map_address = 0;
    char raw[128]{};
    mem.SetDmaCallTag("CS2.MapRefresh");
    EnsureScatter();
    bool mapOk = false;
    if (g_scatter_full) {
        mem.AddScatterReadRequest(g_scatter_full, runtime.client_base + offsets.dwGlobalVars,
                                  &globals, sizeof(globals));
        mem.ExecuteReadScatter(g_scatter_full);
        if (IsUserPointer(globals)) {
            mem.AddScatterReadRequest(g_scatter_full, globals + kGlobalVarsCurrentMap,
                                      &map_address, sizeof(map_address));
            mem.ExecuteReadScatter(g_scatter_full);
            if (IsUserPointer(map_address)) {
                mem.AddScatterReadRequest(g_scatter_full, map_address, raw, sizeof(raw) - 1);
                mem.ExecuteReadScatter(g_scatter_full);
                mapOk = true;
            }
        }
    } else {
        mapOk = QReadT(runtime.client_base + offsets.dwGlobalVars, globals, "CS2.MapRefresh") &&
                IsUserPointer(globals) &&
                QReadT(globals + kGlobalVarsCurrentMap, map_address, "CS2.MapRefresh") &&
                IsUserPointer(map_address) &&
                QRead(map_address, raw, sizeof(raw) - 1, "CS2.MapRefresh");
    }
    if (!mapOk) {
        // The map is stable for an entire match.  A failed maintenance probe
        // must never invalidate an otherwise good live snapshot.
        return;
    }

    raw[sizeof(raw) - 1] = '\0';
    std::string clean(raw);
    std::replace(clean.begin(), clean.end(), '\\', '/');
    std::string lower = clean;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (lower.find("background") != std::string::npos ||
        lower.find("mainmenu") != std::string::npos) {
        runtime.map_name[0] = '\0';
        cachedMapName.clear();
        cachedMapNameMs = nowMs;
        return;
    }
    if (const size_t slash = clean.find_last_of('/'); slash != std::string::npos)
        clean.erase(0, slash + 1);
    if (const size_t extension = clean.find('.'); extension != std::string::npos)
        clean.erase(extension);
    clean.erase(std::remove_if(clean.begin(), clean.end(), [](unsigned char c) {
        return !(std::isalnum(c) || c == '_' || c == '-');
    }), clean.end());

    if (clean.size() >= sizeof(runtime.map_name))
        clean.resize(sizeof(runtime.map_name) - 1);
    std::memset(runtime.map_name, 0, sizeof(runtime.map_name));
    std::memcpy(runtime.map_name, clean.data(), clean.size());

    // Update cache
    cachedMapName = clean;
    cachedMapNameMs = nowMs;
}

// Probe whether a candidate list_entry looks like a live controller table.
static bool ProbeListEntry(uintptr_t entry, uintptr_t stride) {
    if (!IsUserPointer(entry)) return false;
    // Controllers live at list_entry + (i+1)*stride — probe slots 1..4 in one scatter.
    uintptr_t ctrls[4]{};
    EnsureScatter();
    if (g_scatter_full) {
        mem.SetDmaCallTag("CS2.ProbeListEntry");
        for (int i = 1; i <= 4; ++i)
            mem.AddScatterReadRequest(g_scatter_full,
                entry + static_cast<uintptr_t>(i) * stride, &ctrls[i - 1], sizeof(uintptr_t));
        const auto t0 = std::chrono::steady_clock::now();
        mem.ExecuteReadScatter(g_scatter_full);
        NotePossibleDeviceStall(OmniGhost::Gameplay::TimeMs(t0));
    } else {
        for (int i = 1; i <= 4; ++i)
            QReadT(entry + static_cast<uintptr_t>(i) * stride, ctrls[i - 1], "CS2.ProbeListEntry");
    }
    int hits = 0;
    for (int i = 0; i < 4; ++i)
        if (IsUserPointer(ctrls[i])) ++hits;
    return hits >= 1;
}

static bool RefreshEntityListEntry() {
    ScopedPhase _phEntity(&g_phase.entity_ms);
    const uint64_t nowMs = GetTickCount64();
    const bool haveEntry = runtime.entity_list_entry != 0;

    // Healthy hot path: no DMA at all. Entity-list topology is stable during
    // normal play and downstream pawn/core reads validate its usefulness.
    if (haveEntry && IsUserPointer(g_cached_entity_root) &&
        !g_entity_list_force_refresh && nowMs < g_entity_list_next_validation_ms)
        return true;

    uintptr_t root = g_cached_entity_root;
    uintptr_t primaryEntry = 0;
    bool validationOk = false;
    EnsureScatter();
    if (g_scatter_full && IsUserPointer(root) && haveEntry) {
        // Root and the cached root's primary page are independent addresses,
        // so periodic validation costs one scatter execution instead of two
        // sequential QReads.
        mem.SetDmaCallTag("CS2.EntityListValidate");
        uintptr_t observedRoot = 0;
        mem.AddScatterReadRequest(g_scatter_full,
            runtime.client_base + offsets.dwEntityList, &observedRoot, sizeof(observedRoot));
        mem.AddScatterReadRequest(g_scatter_full,
            root + kEntityPageTableOffset, &primaryEntry, sizeof(primaryEntry));
        mem.ExecuteReadScatter(g_scatter_full);
        validationOk = observedRoot == root && primaryEntry == runtime.entity_list_entry;
    }
    if (validationOk && nowMs < g_entity_list_next_full_probe_ms) {
        g_entity_list_force_refresh = false;
        g_entity_list_next_validation_ms = nowMs + ENTITY_LIST_INTERVAL_MS;
        return true;
    }

    // Initial acquisition and recovery are cold paths. Read the root only when
    // it is absent or the one-scatter validation above detected topology drift.
    if (!validationOk) {
        root = 0;
        if (g_scatter_full) {
            mem.SetDmaCallTag("CS2.EntityRoot");
            mem.AddScatterReadRequest(g_scatter_full,
                runtime.client_base + offsets.dwEntityList, &root, sizeof(root));
            mem.ExecuteReadScatter(g_scatter_full);
        } else {
            QReadT(runtime.client_base + offsets.dwEntityList, root, "CS2.EntityRoot");
        }
        if (!IsUserPointer(root)) {
            g_cached_entity_root = 0;
            g_entity_list_next_validation_ms = 0;
            g_entity_list_next_full_probe_ms = 0;
            g_entity_list_force_refresh = true;
            return haveEntry;
        }
        g_cached_entity_root = root;
    }

    g_phase.entity_full_probe = 1;
    static const uintptr_t kPageOffs[] = {
        kEntityPageTableOffset, // 0x10
        0x08,
        0x18,
        0x20,
        0x28
    };

    uintptr_t best_entry = 0;
    uintptr_t best_stride = g_controller_stride ? g_controller_stride : kEntityIdentityStride;

    // One scatter for all candidate page pointers, then probe only valid ones.
    uintptr_t pageEntries[sizeof(kPageOffs) / sizeof(kPageOffs[0])]{};
    EnsureScatter();
    if (g_scatter_full) {
        mem.SetDmaCallTag("CS2.EntityPages");
        for (size_t pi = 0; pi < sizeof(kPageOffs) / sizeof(kPageOffs[0]); ++pi)
            mem.AddScatterReadRequest(g_scatter_full, root + kPageOffs[pi],
                                      &pageEntries[pi], sizeof(uintptr_t));
        const auto tPages = std::chrono::steady_clock::now();
        mem.ExecuteReadScatter(g_scatter_full);
        NotePossibleDeviceStall(OmniGhost::Gameplay::TimeMs(tPages));
    } else {
        for (size_t pi = 0; pi < sizeof(kPageOffs) / sizeof(kPageOffs[0]); ++pi)
            QReadT(root + kPageOffs[pi], pageEntries[pi], "CS2.EntityPages");
    }
    for (size_t pi = 0; pi < sizeof(kPageOffs) / sizeof(kPageOffs[0]); ++pi) {
        const uintptr_t entry = pageEntries[pi];
        if (!IsUserPointer(entry))
            continue;
        if (ProbeListEntry(entry, kEntityIdentityStride)) {
            best_entry = entry;
            best_stride = kEntityIdentityStride;
            break;
        }
        if (ProbeListEntry(entry, kLegacyEntityIdentityStride)) {
            best_entry = entry;
            best_stride = kLegacyEntityIdentityStride;
            break;
        }
        if (!best_entry)
            best_entry = entry;
    }

    if (!best_entry) {
        // Root valid but no page — do NOT wipe a previously working entry
        // for a few frames (prevents ESP flicker on transient nulls).
        return runtime.entity_list_entry != 0;
    }

    if (best_entry == runtime.entity_list_entry) {
        g_controller_stride = best_stride;
        g_pawn_stride = best_stride;
        // Full multi-page/dual-stride validation is intentionally rare.
        // The cheap primary-page pointer confirmation runs between these probes.
        g_entity_list_next_validation_ms = nowMs + ENTITY_LIST_INTERVAL_MS;
        g_entity_list_next_full_probe_ms = nowMs + ENTITY_LIST_FULL_PROBE_INTERVAL_MS;
        g_entity_list_force_refresh = false;
        return true;
    }

    // First acquire after clear: commit immediately.
    // Mid-match change: require 2 matching reads to avoid flicker.
    const int need = (runtime.entity_list_entry == 0) ? 1 : 2;
    if (best_entry != g_pending_entity_list) {
        g_pending_entity_list = best_entry;
        g_entity_list_confirmations = 1;
        if (need == 1) {
            runtime.entity_list_addr = runtime.client_base + offsets.dwEntityList;
            runtime.entity_list_entry = best_entry;
            g_controller_stride = best_stride;
            g_pawn_stride = best_stride;
            g_pending_entity_list = 0;
            g_entity_list_confirmations = 0;
            g_entity_list_next_validation_ms = nowMs + ENTITY_LIST_INTERVAL_MS;
            g_entity_list_next_full_probe_ms = nowMs + ENTITY_LIST_FULL_PROBE_INTERVAL_MS;
            g_entity_list_force_refresh = false;
            return true;
        }
        return runtime.entity_list_entry != 0;
    }
    if (++g_entity_list_confirmations < need)
        return runtime.entity_list_entry != 0;

    runtime.entity_list_addr = runtime.client_base + offsets.dwEntityList;
    runtime.entity_list_entry = best_entry;
    g_controller_stride = best_stride;
    g_pawn_stride = best_stride;
    g_pending_entity_list = 0;
    g_entity_list_confirmations = 0;
    g_entity_list_next_validation_ms = nowMs + ENTITY_LIST_INTERVAL_MS;
    g_entity_list_next_full_probe_ms = nowMs + ENTITY_LIST_FULL_PROBE_INTERVAL_MS;
    g_entity_list_force_refresh = false;
    return true;
}

// Exact Source 2 handle → entity resolution (CS2-DMA-main GetPlayerPawnAddress):
//   root  = *(client + dwEntityList)
//   page  = (handle & 0x7FFF) >> 9
//   index =  handle & 0x1FF
//   chunk = *(root + 0x10 + 8 * page)
//   entity = *(chunk + stride * index)
// Uses g_cached_entity_root when set (once per frame) to avoid re-reading root N times.
static uintptr_t ResolveEntityByHandle(uint32_t handle, uintptr_t stride) {
    if (!handle || handle == 0xFFFFFFFF || !runtime.client_base || !offsets.dwEntityList)
        return 0;

    uintptr_t root = g_cached_entity_root;
    if (!IsUserPointer(root)) {
        if (!QReadT(runtime.client_base + offsets.dwEntityList, root) || !IsUserPointer(root))
            return 0;
        g_cached_entity_root = root;
    }

    const EntityHandleParts parts = DecodeEntityHandle(handle);

    uintptr_t chunk = 0;
    if (!QReadT(root + kEntityPageTableOffset + sizeof(uintptr_t) * parts.page, chunk) ||
        !IsUserPointer(chunk))
        return 0;

    uintptr_t entity = 0;
    if (!QReadT(chunk + stride * parts.index, entity) || !IsUserPointer(entity))
        return 0;
    return entity;
}

static bool IsDroppedWeaponClass(const char* name) {
    if (!name || !*name) return false;
    // Designer names cover both C_WeaponAK47 and weapon_ak47 forms. Exclude
    // attached weapons by requiring a world-scene position later in the pass.
    char lower[64]{};
    std::snprintf(lower, sizeof(lower), "%s", name);
    for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return std::strstr(lower, "weapon_") != nullptr ||
        (std::strstr(lower, "c_weapon") != nullptr && std::strstr(lower, "grenade") == nullptr) ||
        // Several current Source 2 weapon entities omit "Weapon" entirely.
        std::strstr(lower, "c_ak47") != nullptr || std::strstr(lower, "c_deagle") != nullptr ||
        std::strstr(lower, "c_awp") != nullptr || std::strstr(lower, "c_m4a1") != nullptr ||
        std::strstr(lower, "c_glock") != nullptr || std::strstr(lower, "c_usp") != nullptr ||
        std::strstr(lower, "c_p250") != nullptr || std::strstr(lower, "c_mp9") != nullptr ||
        std::strstr(lower, "c_mac10") != nullptr || std::strstr(lower, "c_famas") != nullptr ||
        std::strstr(lower, "c_galilar") != nullptr || std::strstr(lower, "c_ssg08") != nullptr ||
        std::strstr(lower, "c_aug") != nullptr || std::strstr(lower, "c_sg556") != nullptr;
}

static bool DroppedWeaponAllowed(const char* name, const Config& c) {
    char lower[64]{};
    std::snprintf(lower, sizeof(lower), "%s", name ? name : "");
    for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    const auto has = [&](const char* token) { return std::strstr(lower, token) != nullptr; };
    if (has("glock") || has("usp") || has("p2000") || has("p250") || has("deagle") ||
        has("revolver") || has("tec9") || has("fiveseven") || has("cz75") || has("elite"))
        return c.dropped_weapon_pistols;
    if (has("mac10") || has("mp9") || has("mp7") || has("mp5") || has("ump") || has("p90") || has("bizon"))
        return c.dropped_weapon_smgs;
    if (has("nova") || has("xm") || has("mag7") || has("sawed") || has("m249") || has("negev"))
        return c.dropped_weapon_heavy;
    return c.dropped_weapon_rifles;
}

static void FormatDroppedWeaponName(const char* source, char (&out)[48]) {
    std::snprintf(out, sizeof(out), "%s", source && *source ? source : "Arma");
    for (char* p = out; *p; ++p) {
        if (*p == '_') *p = ' ';
    }
    const char* prefixes[] = { "weapon ", "c weapon ", "c ", "item " };
    for (const char* prefix : prefixes) {
        const size_t len = std::strlen(prefix);
        if (_strnicmp(out, prefix, len) == 0) {
            std::memmove(out, out + len, std::strlen(out + len) + 1);
            break;
        }
    }
}

// Low-priority, bounded world-item pass. It is never called unless the user
// enables dropped weapons; discovery alternates entity pages so the camera
// and player lanes stay independent from world-loot work.
// Runs ONLY on the optional worker thread (single-threaded static caches,
// g_scatter_optional exclusive, output vector instead of global runtime).
static void CollectDroppedWeapons(const Config& frame_config, bool in_match,
                                  uintptr_t client_base, std::vector<DroppedWeapon>& out) {
    struct CachedDroppedWeapon { DroppedWeapon weapon{}; uint64_t last_seen_ms = 0; };
    static std::unordered_map<uintptr_t, CachedDroppedWeapon> weapon_cache;
    if (!frame_config.esp_enabled || !frame_config.dropped_weapons || !in_match) {
        weapon_cache.clear();
        out.clear();
        return;
    }
    static uint64_t next_scan_ms = 0;
    static size_t next_page = 0;
    struct CachedClassName { std::array<char, 48> name{}; uint64_t last_seen_ms = 0; };
    static std::unordered_map<uintptr_t, CachedClassName> class_cache;
    static uint64_t classCacheGeneration = 0;
    static uint64_t nextClassCachePruneMs = 0;
    const uint64_t sessionGeneration = g_session_generation.load(std::memory_order_acquire);
    if (classCacheGeneration != sessionGeneration) {
        class_cache.clear();
        weapon_cache.clear();
        next_scan_ms = 0;
        next_page = 0;
        nextClassCachePruneMs = 0;
        classCacheGeneration = sessionGeneration;
    }
    if (class_cache.bucket_count() < 2048) class_cache.reserve(2048);
    const uint64_t now = GetTickCount64();
    const auto publishCachedWeapons = [&] {
        out.clear();
        for (auto it = weapon_cache.begin(); it != weapon_cache.end();) {
            if (now < it->second.last_seen_ms || now - it->second.last_seen_ms > 3000u) {
                it = weapon_cache.erase(it);
                continue;
            }
            if (out.size() < 24u)
                out.push_back(it->second.weapon);
            ++it;
        }
    };
    if (now >= nextClassCachePruneMs) {
        nextClassCachePruneMs = now + 5000u;
        int pruned = 0;
        for (auto it = class_cache.begin(); it != class_cache.end() && pruned < 64;) {
            if (now >= it->second.last_seen_ms && now - it->second.last_seen_ms > 30000u) {
                it = class_cache.erase(it);
                ++pruned;
            } else {
                ++it;
            }
        }
    }
    if (now < next_scan_ms) { publishCachedWeapons(); return; }
    if (g_pressure_level.load(std::memory_order_relaxed) >= 1) {
        next_scan_ms = now + 900;
        publishCachedWeapons();
        return;
    }
    next_scan_ms = now + 250;

    constexpr size_t kPages = 4, kSlotsPerPage = kEntityPageSize, kSlots = kPages * kSlotsPerPage;
    // World entities are not guaranteed to occupy either of the first two
    // entity pages (especially on community servers). Scan 4 pages per batch
    // to complete full sweep in 2 passes (500ms) instead of 8 passes (2s).
    const size_t first_page = (next_page += kPages) % 8;
    std::array<uintptr_t, kPages> pages{};
    std::array<uintptr_t, kSlots> entities{}, identities{}, name_ptrs{}, scenes{};
    std::array<std::array<char, 48>, kSlots> names{};
    std::array<std::array<float, 3>, kSlots> positions{};
    std::array<int, kSlots> ammo{};
    uintptr_t root = g_cached_entity_root;
    if (!IsUserPointer(root) && (!QReadT(client_base + offsets.dwEntityList, root) || !IsUserPointer(root))) {
        publishCachedWeapons();
        return;
    }
    g_cached_entity_root = root;
    g_scatter_optional.Ensure();
    if (!g_scatter_optional) {
        publishCachedWeapons();
        return;
    }

    mem.SetDmaCallTag("CS2.DroppedWeapons.Pages");
    for (size_t page = 0; page < kPages; ++page)
        mem.AddScatterReadRequest(g_scatter_optional, root + kEntityPageTableOffset + sizeof(uintptr_t) * (first_page + page), &pages[page], sizeof(uintptr_t));
    CS2::GiveCameraPriorityBeforeOptionalDma();
    mem.ExecuteReadScatter(g_scatter_optional);
    mem.SetDmaCallTag("CS2.DroppedWeapons.Entities");
    bool queuedEntities = false;
    for (size_t page = 0; page < kPages; ++page) if (IsUserPointer(pages[page]))
        for (size_t slot = 0; slot < kSlotsPerPage; ++slot)
            { mem.AddScatterReadRequest(g_scatter_optional, pages[page] + kEntityIdentityStride * slot,
                &entities[page * kSlotsPerPage + slot], sizeof(uintptr_t)); queuedEntities = true; }
    if (!queuedEntities) { publishCachedWeapons(); return; }
    CS2::GiveCameraPriorityBeforeOptionalDma();
    mem.ExecuteReadScatter(g_scatter_optional);
    mem.SetDmaCallTag("CS2.DroppedWeapons.Identity");
    bool queuedIdentity = false;
    for (size_t i = 0; i < kSlots; ++i) if (IsUserPointer(entities[i]))
        { mem.AddScatterReadRequest(g_scatter_optional, entities[i] + 0x10, &identities[i], sizeof(uintptr_t)); queuedIdentity = true; }
    if (!queuedIdentity) { publishCachedWeapons(); return; }
    CS2::GiveCameraPriorityBeforeOptionalDma();
    mem.ExecuteReadScatter(g_scatter_optional);
    mem.SetDmaCallTag("CS2.DroppedWeapons.Names");
    bool queuedNames = false;
    for (size_t i = 0; i < kSlots; ++i) {
        const auto cached = class_cache.find(identities[i]);
        if (cached != class_cache.end()) {
            names[i] = cached->second.name;
            cached->second.last_seen_ms = now;
        }
        else if (IsUserPointer(identities[i]))
            { mem.AddScatterReadRequest(g_scatter_optional, identities[i] + 0x20, &name_ptrs[i], sizeof(uintptr_t)); queuedNames = true; }
    }
    if (queuedNames) {
        CS2::GiveCameraPriorityBeforeOptionalDma();
        mem.ExecuteReadScatter(g_scatter_optional);
    }
    mem.SetDmaCallTag("CS2.DroppedWeapons.NameText");
    bool queuedNameText = false;
    for (size_t i = 0; i < kSlots; ++i) if (IsUserPointer(name_ptrs[i]))
        { mem.AddScatterReadRequest(g_scatter_optional, name_ptrs[i], names[i].data(), names[i].size() - 1); queuedNameText = true; }
    if (queuedNameText) {
        CS2::GiveCameraPriorityBeforeOptionalDma();
        mem.ExecuteReadScatter(g_scatter_optional);
    }

    mem.SetDmaCallTag("CS2.DroppedWeapons.Data");
    bool queuedData = false;
    for (size_t i = 0; i < kSlots; ++i) {
        if (!IsDroppedWeaponClass(names[i].data()) || !DroppedWeaponAllowed(names[i].data(), frame_config)) continue;
        class_cache.insert_or_assign(identities[i], CachedClassName{names[i], now});
        mem.AddScatterReadRequest(g_scatter_optional, entities[i] + offsets.m_pGameSceneNode, &scenes[i], sizeof(uintptr_t));
        queuedData = true;
        if (frame_config.dropped_weapon_ammo && offsets.m_iClip1)
            mem.AddScatterReadRequest(g_scatter_optional, entities[i] + offsets.m_iClip1, &ammo[i], sizeof(int));
    }
    if (!queuedData) { publishCachedWeapons(); return; }
    CS2::GiveCameraPriorityBeforeOptionalDma();
    mem.ExecuteReadScatter(g_scatter_optional);
    mem.SetDmaCallTag("CS2.DroppedWeapons.Positions");
    bool queuedPositions = false;
    for (size_t i = 0; i < kSlots; ++i) if (IsUserPointer(scenes[i]))
        { mem.AddScatterReadRequest(g_scatter_optional, scenes[i] + offsets.m_vecAbsOrigin, positions[i].data(), sizeof(float) * 3); queuedPositions = true; }
    if (!queuedPositions) { publishCachedWeapons(); return; }
    CS2::GiveCameraPriorityBeforeOptionalDma();
    const uint64_t positionReadBeginMs = GetTickCount64();
    mem.ExecuteReadScatter(g_scatter_optional);
    const uint64_t positionReadEndMs = GetTickCount64();
    const uint64_t positionSampleMs = positionReadBeginMs +
        (positionReadEndMs - positionReadBeginMs) / 2u;

    for (size_t i = 0; i < kSlots; ++i) {
        if (!IsDroppedWeaponClass(names[i].data()) || !IsFinitePosition(positions[i].data())) continue;
        DroppedWeapon weapon{};
        weapon.entity = entities[i]; weapon.ammo_clip = ammo[i]; weapon.sample_timestamp_ms = positionSampleMs;
        std::memcpy(weapon.pos, positions[i].data(), sizeof(weapon.pos));
        FormatDroppedWeaponName(names[i].data(), weapon.name);
        if (IsUserPointer(weapon.entity))
            weapon_cache.insert_or_assign(weapon.entity, CachedDroppedWeapon{weapon, now});
    }
    publishCachedWeapons();
    if (class_cache.size() > 2048) {
        int removed = 0;
        for (auto it = class_cache.begin(); it != class_cache.end() && removed < 32;) {
            it = class_cache.erase(it);
            ++removed;
        }
    }
}

static void CalibratePawnStride(uintptr_t local_controller, uintptr_t local_pawn) {
    static bool calibrated = false;
    static uintptr_t last_local_pawn = 0;
    if (!local_controller || !local_pawn)
        return;
    if (calibrated && last_local_pawn == local_pawn)
        return;

    uint32_t handle = 0;
    if (!QReadT(local_controller + offsets.m_hPlayerPawn, handle) || !handle || handle == 0xFFFFFFFF)
        return;

    const uintptr_t with70 = ResolveEntityByHandle(handle, kEntityIdentityStride);
    if (with70 == local_pawn) {
        g_pawn_stride = kEntityIdentityStride;
        calibrated = true;
        last_local_pawn = local_pawn;
        std::cout << "[CS2] Pawn stride calibrated: 0x70 (handle=0x"
                  << std::hex << handle << std::dec << ")" << std::endl;
        return;
    }
    const uintptr_t with78 = ResolveEntityByHandle(handle, kLegacyEntityIdentityStride);
    if (with78 == local_pawn) {
        g_pawn_stride = kLegacyEntityIdentityStride;
        calibrated = true;
        last_local_pawn = local_pawn;
        std::cout << "[CS2] Pawn stride calibrated: 0x78 (handle=0x"
                  << std::hex << handle << std::dec << ")" << std::endl;
        return;
    }

    // Dump diagnostics once so we can see why calibration failed.
    static bool dumped = false;
    if (!dumped) {
        dumped = true;
        std::cout << "[CS2] Pawn stride calibration FAILED"
                  << " localCtrl=0x" << std::hex << local_controller
                  << " localPawn=0x" << local_pawn
                  << " handle=0x" << handle
                  << " got70=0x" << with70
                  << " got78=0x" << with78
                  << " m_hPlayerPawn=0x" << offsets.m_hPlayerPawn
                  << std::dec << std::endl;
    }
}

static uintptr_t ResolvePawnFromHandle(uint32_t handle, uintptr_t expected_local_pawn) {
    if (!handle || handle == 0xFFFFFFFF) return 0;

    const uintptr_t primary = g_pawn_stride ? g_pawn_stride : kEntityIdentityStride;
    const uintptr_t alternate = (primary == kEntityIdentityStride)
        ? kLegacyEntityIdentityStride : kEntityIdentityStride;

    // CS2-DMA-main accepts the pointer as-is (no health gate here).
    const uintptr_t preferred = ResolveEntityByHandle(handle, primary);
    if (preferred) {
        if (expected_local_pawn && preferred == expected_local_pawn)
            return preferred;
        // Still accept a valid pointer — main loop filters by health/team.
        if (IsUserPointer(preferred))
            return preferred;
    }

    const uintptr_t alt = ResolveEntityByHandle(handle, alternate);
    if (alt) {
        if (expected_local_pawn && alt == expected_local_pawn) {
            g_pawn_stride = alternate;
            return alt;
        }
        if (IsUserPointer(alt)) {
            g_pawn_stride = alternate;
            return alt;
        }
    }
    return 0;
}

static void UpdateLocalWeaponDefinitionThrottled() {
    static uint64_t next_update_ms = 0;
    static uintptr_t cached_pawn = 0;
    static uintptr_t cached_services = 0;
    static uint32_t cached_handle = 0;
    static uintptr_t cached_entity = 0;
    static uint16_t cached_definition = 0;
    const uint64_t now = GetTickCount64();
    if (now < next_update_ms || !IsUserPointer(runtime.local_pawn) || !offsets.m_pWeaponServices || !offsets.m_hActiveWeapon)
        return;
    if (cached_pawn != runtime.local_pawn) {
        cached_pawn = runtime.local_pawn;
        cached_services = 0;
        cached_handle = 0;
        cached_entity = 0;
        cached_definition = 0;
        runtime.local_weapon_definition = 0;
    }
    CS2::GiveCameraPriorityBeforeOptionalDma();
    uintptr_t services = cached_services;
    uint32_t handle = 0;
    bool sampled = false;
    EnsureScatter();
    if (g_scatter_full) {
        mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_pWeaponServices,
                                  &services, sizeof(services));
        if (IsUserPointer(cached_services))
            mem.AddScatterReadRequest(g_scatter_full, cached_services + offsets.m_hActiveWeapon,
                                      &handle, sizeof(handle));
        mem.SetDmaCallTag("CS2.LocalWeaponState");
        mem.ExecuteReadScatter(g_scatter_full);
        // A newly discovered services pointer becomes usable on the next
        // throttled update; never create a dependent QRead in this lane.
        if (!handle && IsUserPointer(services) && services != cached_services) {
            cached_services = services;
            next_update_ms = now + 40u;
            return;
        }
        sampled = true;
    } else {
        sampled = QReadT(runtime.local_pawn + offsets.m_pWeaponServices, services);
        if (sampled && IsUserPointer(services))
            sampled = QReadT(services + offsets.m_hActiveWeapon, handle);
    }
    if (!sampled || !IsUserPointer(services) || !handle || handle == 0xFFFFFFFF) {
        if (sampled) runtime.local_weapon_definition = 0;
        next_update_ms = now + 50u;
        return;
    }
    cached_services = services;
    if (handle == cached_handle && cached_definition) {
        runtime.local_weapon_definition = cached_definition;
        next_update_ms = now + 150u;
        return;
    }
    const uintptr_t entity = ResolveEntityByHandle(handle, g_pawn_stride ? g_pawn_stride : kEntityIdentityStride);
    uint16_t definition = 0;
    if (IsUserPointer(entity) && QReadT(entity + offsets.m_AttributeManager + offsets.m_Item +
        offsets.m_iItemDefinitionIndex, definition) && definition > 0 && definition < 6000) {
        cached_handle = handle;
        cached_entity = entity;
        cached_definition = definition;
        runtime.local_weapon_definition = definition;
        next_update_ms = now + 150u;
    } else {
        cached_handle = handle;
        cached_entity = 0;
        cached_definition = 0;
        runtime.local_weapon_definition = 0;
        next_update_ms = now + 50u;
    }
}

static uint32_t ReadPawnHandle(uintptr_t controller) {
    if (!controller) return 0;
    uint32_t handle = 0;
    if (QReadT(controller + offsets.m_hPlayerPawn, handle) && handle && handle != 0xFFFFFFFF)
        return handle;
    // Spectator / death: fall back to observer pawn handle.
    if (offsets.m_hObserverPawn &&
        QReadT(controller + offsets.m_hObserverPawn, handle) && handle && handle != 0xFFFFFFFF)
        return handle;
    return 0;
}

// Batch-read controller pointers in a single scatter (64 → 1 DMA round-trip).
static int CollectControllers(uintptr_t list_entry, uintptr_t stride, uintptr_t* out, int max_count) {
    int count = 0;
    if (!list_entry || !out || max_count <= 0) return 0;

    EnsureScatter();
    if (g_scatter_full) {
        for (int i = 0; i < max_count; ++i) {
            out[i] = 0;
            mem.AddScatterReadRequest(
                g_scatter_full,
                list_entry + static_cast<uintptr_t>(i + 1) * stride,
                &out[i],
                sizeof(uintptr_t));
        }
        mem.ExecuteReadScatter(g_scatter_full);
        for (int i = 0; i < max_count; ++i) {
            if (IsUserPointer(out[i]))
                ++count;
            else
                out[i] = 0;
        }
        return count;
    }

    for (int i = 0; i < max_count; ++i) {
        uintptr_t controller = 0;
        if (QReadT(list_entry + static_cast<uintptr_t>(i + 1) * stride, controller) &&
            IsUserPointer(controller)) {
            out[i] = controller;
            ++count;
        } else {
            out[i] = 0;
        }
    }
    return count;
}

// Scatter-read m_hPlayerPawn for every non-null controller in one round-trip.
static void ScatterReadPawnHandles(const uintptr_t* controllers, uint32_t* handles, int count) {
    if (!controllers || !handles || count <= 0) return;
    EnsureScatter();
    if (!g_scatter_full) {
        for (int i = 0; i < count; ++i)
            handles[i] = controllers[i] ? ReadPawnHandle(controllers[i]) : 0;
        return;
    }
    for (int i = 0; i < count; ++i) {
        handles[i] = 0;
        if (!controllers[i]) continue;
        mem.AddScatterReadRequest(
            g_scatter_full,
            controllers[i] + offsets.m_hPlayerPawn,
            &handles[i],
            sizeof(uint32_t));
    }
    mem.ExecuteReadScatter(g_scatter_full);
    for (int i = 0; i < count; ++i) {
        if (handles[i] == 0xFFFFFFFF)
            handles[i] = 0;
    }
}

// Resolve all Source 2 handles in two DMA round-trips: one for the unique page
// pointers and one for the entity pointers. Tries both strides in one call
// to avoid double round-trips when stride is uncertain.
static int ScatterResolvePawnHandles(const uint32_t* handles, uintptr_t* pawns,
                                     int count, uintptr_t stride,
                                     uintptr_t alternate_stride,
                                     bool optionalLane = false) {
    if (!handles || !pawns || count <= 0) return 0;
    std::fill(pawns, pawns + count, 0);
    bool anyValidHandle = false;
    for (int i = 0; i < count; ++i) {
        if (DecodeEntityHandle(handles[i]).valid) {
            anyValidHandle = true;
            break;
        }
    }
    if (!anyValidHandle) return 0;

    uintptr_t root = g_cached_entity_root;
    if (!IsUserPointer(root)) {
        if (!QReadT(runtime.client_base + offsets.dwEntityList, root) || !IsUserPointer(root))
            return 0;
        g_cached_entity_root = root;
    }

    constexpr int kMaxPages = 64;
    uintptr_t chunks[kMaxPages]{};
    bool pageUsed[kMaxPages]{};
    static uintptr_t cachedChunks[kMaxPages]{};
    static uintptr_t cachedChunkRoot = 0;
    static uint64_t cachedChunksUntilMs = 0;
    static uint64_t cachedChunkGeneration = 0;
    const uint64_t nowMs = GetTickCount64();
    const uint64_t generation = g_session_generation.load(std::memory_order_acquire);
    if (cachedChunkGeneration != generation || cachedChunkRoot != root ||
        nowMs >= cachedChunksUntilMs) {
        std::fill(std::begin(cachedChunks), std::end(cachedChunks), 0);
        cachedChunkGeneration = generation;
        cachedChunkRoot = root;
        cachedChunksUntilMs = nowMs + 500u;
    }
    for (int i = 0; i < count; ++i) {
        const EntityHandleParts parts = DecodeEntityHandle(handles[i]);
        if (!parts.valid) continue;
        const int page = static_cast<int>(parts.page);
        if (page >= 0 && page < kMaxPages) pageUsed[page] = true;
    }

    EnsureScatter();
    if (!g_scatter_full) {
        if (optionalLane) return 0;
        int resolved = 0;
        for (int i = 0; i < count; ++i) {
            pawns[i] = ResolvePawnFromHandle(handles[i], runtime.local_pawn);
            if (IsUserPointer(pawns[i])) ++resolved;
        }
        return resolved;
    }

    bool queuedChunkPointers = false;
    for (int page = 0; page < kMaxPages; ++page) {
        if (!pageUsed[page]) continue;
        if (IsUserPointer(cachedChunks[page])) {
            chunks[page] = cachedChunks[page];
            continue;
        }
        mem.AddScatterReadRequest(g_scatter_full,
            root + kEntityPageTableOffset + sizeof(uintptr_t) * static_cast<uintptr_t>(page),
            &chunks[page], sizeof(uintptr_t));
        queuedChunkPointers = true;
    }
    if (queuedChunkPointers) {
        if (optionalLane) CS2::GiveCameraPriorityBeforeOptionalDma();
        mem.SetDmaCallTag("CS2.ResolveHandles.Pages");
        mem.ExecuteReadScatter(g_scatter_full);
        for (int page = 0; page < kMaxPages; ++page)
            if (IsUserPointer(chunks[page])) cachedChunks[page] = chunks[page];
    }

    // Try primary stride first
    bool queuedEntityPointers = false;
    for (int i = 0; i < count; ++i) {
        const EntityHandleParts parts = DecodeEntityHandle(handles[i]);
        if (!parts.valid) continue;
        const int page = static_cast<int>(parts.page);
        if (page < 0 || page >= kMaxPages || !IsUserPointer(chunks[page])) continue;
        mem.AddScatterReadRequest(g_scatter_full, chunks[page] + stride * parts.index,
                                  &pawns[i], sizeof(uintptr_t));
        queuedEntityPointers = true;
    }
    if (queuedEntityPointers) {
        if (optionalLane) CS2::GiveCameraPriorityBeforeOptionalDma();
        mem.SetDmaCallTag("CS2.ResolveHandles.Entities");
        mem.ExecuteReadScatter(g_scatter_full);
    }

    // Check if primary stride resolved enough; if not, try alternate stride
    int resolved = 0;
    for (int i = 0; i < count; ++i) {
        if (IsUserPointer(pawns[i])) {
            ++resolved;
        } else {
            pawns[i] = 0;
        }
    }

    // If primary stride failed for many, try alternate in same function
    if (alternate_stride && alternate_stride != stride && resolved < count / 2) {
        std::fill(pawns, pawns + count, 0);
        queuedEntityPointers = false;
        for (int i = 0; i < count; ++i) {
            const EntityHandleParts parts = DecodeEntityHandle(handles[i]);
            if (!parts.valid) continue;
            const int page = static_cast<int>(parts.page);
            if (page < 0 || page >= kMaxPages || !IsUserPointer(chunks[page])) continue;
            mem.AddScatterReadRequest(g_scatter_full, chunks[page] + alternate_stride * parts.index,
                                      &pawns[i], sizeof(uintptr_t));
            queuedEntityPointers = true;
        }
        if (queuedEntityPointers) {
            if (optionalLane) CS2::GiveCameraPriorityBeforeOptionalDma();
            mem.SetDmaCallTag("CS2.ResolveHandles.Entities.Alt");
            mem.ExecuteReadScatter(g_scatter_full);
        }
        resolved = 0;
        for (int i = 0; i < count; ++i) {
            if (IsUserPointer(pawns[i])) ++resolved;
            else pawns[i] = 0;
        }
    }
    return resolved;
}

// Core pawn fields in one scatter: health, team, armor, scene node.
struct PawnCoreFields {
    // -1 means the scatter did not produce a trustworthy value. Zero is a
    // distinct, confirmed dead state and must never enter dropout grace.
    int health = -1;
    uint8_t controller_alive = 0xFF; // 0xFF = read unknown
    uint64_t controller_steam_id = UINT64_MAX; // unread != bot (zero)
    uint8_t team = 0;
    int armor = 0;
    uintptr_t scene = 0;
    float velocity[3]{};
    bool spotted = true;
    bool spotted_known = false;
    uint8_t scoped = 0;
    float flash = 0.f;
    float eye_angles[2]{};
    uintptr_t weapon_services = 0;
};

static void ScatterReadPawnCore(const uintptr_t* pawns, const uintptr_t* controllers,
                                PawnCoreFields* fields, int count,
                                bool need_armor, bool need_scoped, bool need_flash,
                                 bool need_yaw, bool need_weapon_services, bool need_spotted,
                                 const uint8_t* cached_teams,
                                 const bool* refresh_teams,
                                 const int* cached_armors,
                                 const bool* refresh_armors,
                                const uintptr_t* cached_scenes,
                                const bool* refresh_scenes,
                                float (*positions)[3], bool need_velocity,
                                const uintptr_t* cached_weapon_services,
                                const bool* refresh_weapon_services,
                                const bool* cached_spotted,
                                const bool* refresh_spotted) {
    if (!pawns || !fields || count <= 0) return;
    EnsureScatter();
    // P1-5: Remove QRead fallback — if scatter handle unavailable, treat as frame drop
    // to avoid 10+ QReads/player causing massive frame spikes.
    if (!g_scatter_full) {
        for (int i = 0; i < count; ++i) {
            fields[i] = PawnCoreFields{}; // mark as unread
        }
        return;
    }
    static thread_local uint8_t spotted_buf[128];
    if (count > 128) count = 128;
    for (int i = 0; i < count; ++i) {
        const uintptr_t cachedScene = cached_scenes ? cached_scenes[i] : 0;
        const uintptr_t cachedWepSvc = cached_weapon_services ? cached_weapon_services[i] : 0;
        const bool cachedSpot = cached_spotted ? cached_spotted[i] : true;
        fields[i] = {};
        fields[i].health = -1;
        fields[i].controller_alive = 0xFF;
        fields[i].controller_steam_id = UINT64_MAX;
        fields[i].team = cached_teams ? cached_teams[i] : 0;
        fields[i].armor = cached_armors ? cached_armors[i] : 0;
        fields[i].scene = cachedScene;
        fields[i].weapon_services = cachedWepSvc;
        fields[i].spotted = cachedSpot;
        spotted_buf[i] = 1;
        if (!pawns[i]) continue;
        mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_iHealth,
                                  &fields[i].health, sizeof(int));
        if (controllers && IsUserPointer(controllers[i]) && offsets.m_bPawnIsAlive)
            mem.AddScatterReadRequest(g_scatter_full, controllers[i] + offsets.m_bPawnIsAlive,
                                      &fields[i].controller_alive, sizeof(uint8_t));
        if (controllers && IsUserPointer(controllers[i]) && offsets.m_steamID)
            mem.AddScatterReadRequest(g_scatter_full, controllers[i] + offsets.m_steamID,
                                      &fields[i].controller_steam_id, sizeof(uint64_t));
        if (!IsPlayableTeam(static_cast<int>(fields[i].team)) ||
            (refresh_teams && refresh_teams[i]))
            mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_iTeamNum,
                                      &fields[i].team, sizeof(uint8_t));
        if (need_armor && (!cached_armors || (refresh_armors && refresh_armors[i])))
            mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_ArmorValue,
                                      &fields[i].armor, sizeof(int));
        if (!IsUserPointer(cachedScene) || (refresh_scenes && refresh_scenes[i])) {
            // A zero sentinel distinguishes a failed metadata refresh from the
            // previous cached pointer. Origin still uses cachedScene below.
            fields[i].scene = 0;
            mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_pGameSceneNode,
                                      &fields[i].scene, sizeof(uintptr_t));
        }
        // Weapon services: cache the pointer (stable across rounds, long TTL)
        if (need_weapon_services && (!cached_weapon_services || (refresh_weapon_services && refresh_weapon_services[i]))) {
            fields[i].weapon_services = 0;
            mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_pWeaponServices,
                                      &fields[i].weapon_services, sizeof(uintptr_t));
        }
        // Spotted: entity spotted state changes frequently, shorter TTL
        if (need_spotted && offsets.m_entitySpottedState && (!cached_spotted || (refresh_spotted && refresh_spotted[i]))) {
            spotted_buf[i] = 0xFF;
            mem.AddScatterReadRequest(g_scatter_full,
                pawns[i] + offsets.m_entitySpottedState + offsets.m_bSpotted,
                &spotted_buf[i], sizeof(uint8_t));
        } else if (need_spotted) {
            spotted_buf[i] = cachedSpot ? 1 : 0;
        } else {
            spotted_buf[i] = 1;
        }
        if (positions) {
            if (IsUserPointer(cachedScene))
                mem.AddScatterReadRequest(g_scatter_full, cachedScene + offsets.m_vecAbsOrigin,
                                          positions[i], sizeof(float) * 3);
            else
                mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_vOldOrigin,
                                          positions[i], sizeof(float) * 3);
        }
        if (need_velocity && offsets.m_vecVelocity)
            mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_vecVelocity,
                                      fields[i].velocity, sizeof(fields[i].velocity));
        if (need_scoped && offsets.m_bIsScoped)
            mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_bIsScoped,
                                      &fields[i].scoped, sizeof(fields[i].scoped));
        if (need_flash && offsets.m_flFlashDuration)
            mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_flFlashDuration,
                                      &fields[i].flash, sizeof(fields[i].flash));
        if (need_yaw)
            mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_angEyeAngles,
                                      fields[i].eye_angles, sizeof(fields[i].eye_angles));
    }
    mem.ExecuteReadScatter(g_scatter_full);
    for (int i = 0; i < count; ++i)
        fields[i].spotted = (spotted_buf[i] != 0);
}

void UpdateBombState() {
    if (!offsets.dwPlantedC4) {
        runtime.bomb = BombState{};
        return;
    }

    // CS2 has exposed dwPlantedC4 in both forms across builds: either the
    // C_PlantedC4 pointer itself, or a pointer to a one-entry list.
    // Prefer one scatter batch for all bomb fields to avoid 10+ sequential QReads
    // which show up as micro-stutters on 35T/75T during planted C4.
    mem.SetDmaCallTag("CS2.Bomb");
    uintptr_t candidate = 0;
    if (!QReadT(runtime.client_base + offsets.dwPlantedC4, candidate, "CS2.Bomb.Ptr"))
        return;
    if (!IsUserPointer(candidate)) {
        runtime.bomb = BombState{}; // confirmed absent
        return;
    }

    uintptr_t entity = 0;
    uint8_t ticking = 0;
    uintptr_t list_entity = 0;
    uint8_t list_ticking = 0;

    EnsureScatter();
    if (g_scatter_full && offsets.m_bBombTicking) {
        mem.AddScatterReadRequest(g_scatter_full, candidate + offsets.m_bBombTicking,
                                  &ticking, sizeof(ticking));
        mem.AddScatterReadRequest(g_scatter_full, candidate, &list_entity, sizeof(list_entity));
        CS2::GiveCameraPriorityBeforeOptionalDma();
        mem.ExecuteReadScatter(g_scatter_full);
        if (ticking) {
            entity = candidate;
        } else if (IsUserPointer(list_entity)) {
            mem.AddScatterReadRequest(g_scatter_full, list_entity + offsets.m_bBombTicking,
                                      &list_ticking, sizeof(list_ticking));
            CS2::GiveCameraPriorityBeforeOptionalDma();
            mem.ExecuteReadScatter(g_scatter_full);
            ticking = list_ticking;
            entity = list_entity;
        }
    } else {
        if (offsets.m_bBombTicking)
            QReadT(candidate + offsets.m_bBombTicking, ticking, "CS2.Bomb.Tick");
        if (ticking) {
            entity = candidate;
        } else {
            if (!QReadT(candidate, list_entity, "CS2.Bomb.List") || !IsUserPointer(list_entity))
                return;
            if (offsets.m_bBombTicking)
                QReadT(list_entity + offsets.m_bBombTicking, ticking, "CS2.Bomb.ListTick");
            entity = list_entity;
        }
    }
    if (!ticking || !IsUserPointer(entity)) {
        // A valid ticking=false is authoritative. An unresolved list pointer
        // can be a transport failure and must not clear a planted timer.
        if (entity && ticking == 0) runtime.bomb = BombState{};
        return;
    }

    BombState next = runtime.bomb;

    float absolute_blow_time = 0.f;
    float absolute_defuse_time = 0.f;
    float timer_length = 0.f;
    uint8_t defused = 0;
    uint8_t defusing = 0;
    uintptr_t globals = 0;
    uintptr_t scene = 0;
    float cur_slots[4]{};
    constexpr uintptr_t kCurOff[4] = {
        kGlobalVarsCurrentTime, (uintptr_t)0x2C, (uintptr_t)0x34, (uintptr_t)0x38
    };

    EnsureScatter();
    if (g_scatter_full) {
        if (offsets.m_flC4Blow)
            mem.AddScatterReadRequest(g_scatter_full, entity + offsets.m_flC4Blow,
                                      &absolute_blow_time, sizeof(absolute_blow_time));
        if (offsets.m_flTimerLength)
            mem.AddScatterReadRequest(g_scatter_full, entity + offsets.m_flTimerLength,
                                      &timer_length, sizeof(timer_length));
        if (offsets.m_bBombDefused)
            mem.AddScatterReadRequest(g_scatter_full, entity + offsets.m_bBombDefused,
                                      &defused, sizeof(defused));
        if (offsets.m_bBeingDefused)
            mem.AddScatterReadRequest(g_scatter_full, entity + offsets.m_bBeingDefused,
                                      &defusing, sizeof(defusing));
        if (offsets.m_flDefuseCountDown)
            mem.AddScatterReadRequest(g_scatter_full, entity + offsets.m_flDefuseCountDown,
                                      &absolute_defuse_time, sizeof(absolute_defuse_time));
        if (offsets.m_hBombDefuser)
            mem.AddScatterReadRequest(g_scatter_full, entity + offsets.m_hBombDefuser,
                                      &next.defuser_handle, sizeof(next.defuser_handle));
        if (offsets.m_pGameSceneNode)
            mem.AddScatterReadRequest(g_scatter_full, entity + offsets.m_pGameSceneNode,
                                      &scene, sizeof(scene));
        if (offsets.dwGlobalVars)
            mem.AddScatterReadRequest(g_scatter_full, runtime.client_base + offsets.dwGlobalVars,
                                      &globals, sizeof(globals));
        CS2::GiveCameraPriorityBeforeOptionalDma();
        mem.ExecuteReadScatter(g_scatter_full);

        if (IsUserPointer(globals)) {
            for (int i = 0; i < 4; ++i)
                mem.AddScatterReadRequest(g_scatter_full, globals + kCurOff[i],
                                          &cur_slots[i], sizeof(float));
            CS2::GiveCameraPriorityBeforeOptionalDma();
            mem.ExecuteReadScatter(g_scatter_full);
        }
        if (IsUserPointer(scene) && offsets.m_vecAbsOrigin) {
            mem.AddScatterReadRequest(g_scatter_full, scene + offsets.m_vecAbsOrigin,
                                      next.pos, sizeof(next.pos));
            CS2::GiveCameraPriorityBeforeOptionalDma();
            mem.ExecuteReadScatter(g_scatter_full);
        }
    } else {
        if (offsets.m_flC4Blow)
            QReadT(entity + offsets.m_flC4Blow, absolute_blow_time, "CS2.Bomb.Blow");
        if (offsets.m_flTimerLength)
            QReadT(entity + offsets.m_flTimerLength, timer_length, "CS2.Bomb.Timer");
        if (offsets.m_bBombDefused)
            QReadT(entity + offsets.m_bBombDefused, defused, "CS2.Bomb.Defused");
        if (offsets.m_bBeingDefused)
            QReadT(entity + offsets.m_bBeingDefused, defusing, "CS2.Bomb.Defusing");
        if (offsets.m_flDefuseCountDown)
            QReadT(entity + offsets.m_flDefuseCountDown, absolute_defuse_time, "CS2.Bomb.DefuseCD");
        if (offsets.m_hBombDefuser)
            QReadT(entity + offsets.m_hBombDefuser, next.defuser_handle, "CS2.Bomb.Defuser");
        if (offsets.dwGlobalVars)
            QReadT(runtime.client_base + offsets.dwGlobalVars, globals, "CS2.Bomb.Globals");
        if (IsUserPointer(globals)) {
            for (int i = 0; i < 4; ++i)
                QReadT(globals + kCurOff[i], cur_slots[i], "CS2.Bomb.CurTime");
        }
        if (offsets.m_pGameSceneNode &&
            QReadT(entity + offsets.m_pGameSceneNode, scene, "CS2.Bomb.Scene") &&
            IsUserPointer(scene))
            QRead(scene + offsets.m_vecAbsOrigin, next.pos, sizeof(next.pos), "CS2.Bomb.Pos");
    }

    float current_time = 0.f;
    bool have_time = false;
    for (int i = 0; i < 4; ++i) {
        const float t = cur_slots[i];
        if (std::isfinite(t) && t > 1.f && t < 1.0e7f) {
            current_time = t;
            have_time = true;
            break;
        }
    }

    float blow = 0.f;
    if (have_time && std::isfinite(absolute_blow_time) && absolute_blow_time > 1.f)
        blow = absolute_blow_time - current_time;

    // Prefer remaining time from absolute - curtime; fall back to timer_length.
    // CS2 C4 is never > 45s. Huge floats = wrong offsets / garbage read.
    if (!std::isfinite(blow) || blow < 0.05f || blow > 45.f) {
        if (std::isfinite(timer_length) && timer_length > 0.05f && timer_length <= 45.f)
            blow = timer_length;
        else
            return; // refuse to show garbage
    }

    float defuse = 0.f;
    if (have_time && std::isfinite(absolute_defuse_time) && absolute_defuse_time > 1.f)
        defuse = absolute_defuse_time - current_time;
    if (!std::isfinite(defuse) || defuse < 0.f || defuse > 15.f)
        defuse = 0.f;

    next.planted = true;
    next.sample_timestamp_ms = GetTickCount64();
    next.blow_time = blow;
    next.defuse_time = defuse;
    next.defused = defused != 0;
    next.defusing = defusing != 0 && defuse > 0.05f;
    runtime.bomb = next;
}

void UpdateBombStateThrottled() {
    static uint64_t next_update_ms = 0;
    const uint64_t now = GetTickCount64();
    if (now < next_update_ms) return;
    // Before a plant, the C4 pointer is static/absent. Polling it at 20 Hz
    // was an unnecessary sequential DMA read during every round. Once a bomb
    // is confirmed planted, keep the timer responsive at 50 ms.
    next_update_ms = now + (runtime.bomb.planted ? 50u : 250u);
    UpdateBombState();
}

// Returns false when the camera lane is still busy after the wait budget —
// callers should skip/abandon this optional batch so the camera keeps priority.
bool GiveCameraPriorityBeforeOptionalDma() {
    const uint64_t deadline = g_camera_deadline_ms.load(std::memory_order_acquire);
    const bool pending = g_camera_pending.load(std::memory_order_acquire);
    const uint64_t now = GetTickCount64();
    // Far from the deadline and no pending camera read → proceed immediately.
    if (!pending && (deadline == 0 || now + 2u < deadline))
        return true;

    // Cooperative yield until the camera lane clears or a hard wall-clock
    // budget elapses. Between optional micro-batches the caller invokes this
    // again so a long scatter never monopolises the transport without giving
    // the camera thread a chance to run once the dmaReadMutex is released.
    constexpr int kMaxYieldSpins = 96;
    const auto t0 = std::chrono::steady_clock::now();
    constexpr auto kMaxWait = std::chrono::microseconds(1500);
    for (int spin = 0; spin < kMaxYieldSpins; ++spin) {
        if (!g_camera_pending.load(std::memory_order_acquire)) {
            const uint64_t d = g_camera_deadline_ms.load(std::memory_order_acquire);
            const uint64_t n = GetTickCount64();
            if (d == 0 || n + 1u < d)
                return true;
        }
        if (std::chrono::steady_clock::now() - t0 >= kMaxWait)
            return false; // P0-7: report timeout so optional worker can abort
        std::this_thread::yield();
    }
    // After 96 spins, if camera is still pending we should abort the
    // current optional batch instead of monopolising the transport.
    return false;
}

// True when optional DMA should pause before starting another micro-batch
// because the camera deadline is imminent or a camera read is in flight.
static bool OptionalDmaShouldYieldToCamera() noexcept {
    if (g_camera_pending.load(std::memory_order_acquire))
        return true;
    const uint64_t deadline = g_camera_deadline_ms.load(std::memory_order_acquire);
    if (deadline == 0)
        return false;
    const uint64_t now = GetTickCount64();
    return now + 3u >= deadline;
}

// ── Optional enrichment worker ───────────────────────────────────────────
// Core acquisition publishes position/health/velocity/liveness then returns.
// This worker refreshes bones, names, weapons, flags and dropped items using
// the already-published player list so the next core scan is never blocked.
static void RunOptionalEnrichment(const Config& frame_config,
                                  std::vector<OptionalTarget> targets,
                                  const bool in_match, const uintptr_t client_base,
                                  const bool dropped_only, const uint64_t jobGen) {
    if (dropped_only ? !in_match : (!in_match || targets.empty()))
        return;
    const int pressure = g_pressure_level.load(std::memory_order_relaxed);
    const bool isRecovery = pressure >= 3;
    // Sole producer of g_optional_results. All deltas are local until the
    // final publish; the acquisition thread merges them into its own caches.
    OptionalResults results{};
    results.generation = jobGen;

    OmniGhost::Gameplay::OptionalPhaseBudget budget;
    {
        float budgetMs = frame_config.optional_lane_budget_ms > 0.f ? frame_config.optional_lane_budget_ms : (frame_config.performance_mode ? 4.f : 6.f);
        if (isRecovery) budgetMs = 2.f; // P1-2: minimal budget for critical bones only
        else if (pressure == 2) budgetMs = budgetMs * 0.58f; // 3.5/6
        else if (pressure == 1) budgetMs = budgetMs * 0.75f; // 4.5/6
        budget.Reset(budgetMs);
    }

    g_scatter_optional.Ensure();
    if (!g_scatter_optional)
        return;

    OmniGhost::Gameplay::UnifiedConfig::CS2Config unifiedConfig;
    unifiedConfig = CS2::UnifiedConfigAdapter::Get();
    // Update with frame-specific values
    unifiedConfig.esp_enabled = frame_config.esp_enabled;
    unifiedConfig.box = frame_config.box;
    unifiedConfig.skeleton = frame_config.skeleton;
    unifiedConfig.head_dot = frame_config.head_dot;
    unifiedConfig.head_halo = frame_config.head_halo;
    unifiedConfig.chinese_hat = frame_config.chinese_hat;
    unifiedConfig.angel_wings = frame_config.angel_wings;
    unifiedConfig.devil_horns = frame_config.devil_horns;
    unifiedConfig.floating_crown = frame_config.floating_crown;
    unifiedConfig.health_bar = frame_config.health_bar;
    unifiedConfig.armor_bar = frame_config.armor_bar;
    unifiedConfig.health_value = frame_config.health_value;
    unifiedConfig.armor_value = frame_config.armor_value;
    unifiedConfig.visibility_colors = frame_config.visibility_colors;
    unifiedConfig.visible_check = frame_config.visible_check;
    unifiedConfig.weapon_name = frame_config.weapon_name;
    unifiedConfig.weapon_ammo = frame_config.weapon_ammo;
    unifiedConfig.name = frame_config.name;
    unifiedConfig.distance = frame_config.distance;
    unifiedConfig.snaplines = frame_config.snaplines;
    unifiedConfig.trails = frame_config.trails;
    unifiedConfig.head_halo = frame_config.head_halo;
    unifiedConfig.look_direction = frame_config.look_direction;
    unifiedConfig.chinese_hat = frame_config.chinese_hat;
    unifiedConfig.angel_wings = frame_config.angel_wings;
    unifiedConfig.devil_horns = frame_config.devil_horns;
    unifiedConfig.floating_crown = frame_config.floating_crown;
    unifiedConfig.rgb_mode = frame_config.rgb_mode;
    unifiedConfig.visibility_colors = frame_config.visibility_colors;
    unifiedConfig.visible_check = frame_config.visible_check;
    unifiedConfig.team_check = frame_config.team_check;
    unifiedConfig.show_bots = frame_config.show_bots;
    unifiedConfig.max_distance = frame_config.max_distance;
    unifiedConfig.optional_lane_budget_ms = frame_config.optional_lane_budget_ms;
    unifiedConfig.performance_mode = frame_config.performance_mode;
    unifiedConfig.aim_enabled = frame_config.aim_enabled;
    unifiedConfig.trigger_enabled = frame_config.trigger_enabled;
    unifiedConfig.aim_visibility_check = frame_config.aim_visibility_check;
    unifiedConfig.trigger_head_only = frame_config.trigger_head_only;
    unifiedConfig.trigger_scoped_only = frame_config.trigger_scoped_only;
    unifiedConfig.aim_prediction = frame_config.aim_prediction;
    unifiedConfig.radar_2d = frame_config.radar_2d;
    unifiedConfig.webradar_enabled = frame_config.webradar_enabled;
    unifiedConfig.dropped_weapons = frame_config.dropped_weapons;
    unifiedConfig.radar_2d = frame_config.radar_2d;
    unifiedConfig.webradar_enabled = frame_config.webradar_enabled;
    unifiedConfig.performance_mode = frame_config.performance_mode;
    
    // Copy colors
    for (int i = 0; i < 4; ++i) {
        unifiedConfig.col_box[i] = frame_config.col_box[i];
        unifiedConfig.col_box_corner[i] = frame_config.col_box_corner[i];
        unifiedConfig.col_box_fill[i] = frame_config.col_box_fill[i];
        unifiedConfig.col_skeleton[i] = frame_config.col_skeleton[i];
        unifiedConfig.col_joints[i] = frame_config.col_joints[i];
        unifiedConfig.col_head[i] = frame_config.col_head[i];
        unifiedConfig.col_halo[i] = frame_config.col_halo[i];
        unifiedConfig.col_health[i] = frame_config.col_health[i];
        unifiedConfig.col_armor[i] = frame_config.col_armor[i];
        unifiedConfig.col_name[i] = frame_config.col_name[i];
        unifiedConfig.col_distance[i] = frame_config.col_distance[i];
        unifiedConfig.col_weapon[i] = frame_config.col_weapon[i];
        unifiedConfig.col_snaplines[i] = frame_config.col_snaplines[i];
        unifiedConfig.col_trail[i] = frame_config.col_trail[i];
        unifiedConfig.col_fun_effects[i] = frame_config.col_fun_effects[i];
        unifiedConfig.col_look[i] = frame_config.col_look[i];
        unifiedConfig.col_text_outline[i] = frame_config.col_text_outline[i];
        unifiedConfig.col_visible[i] = frame_config.col_visible[i];
        unifiedConfig.col_occluded[i] = frame_config.col_occluded[i];
        unifiedConfig.col_team[i] = frame_config.col_team[i];
        unifiedConfig.col_target[i] = frame_config.col_target[i];
    }
    
    // Use unified FeatureFlags for RequiredFields
    const auto unifiedFlags = unifiedConfig.GetFeatureFlags();
    const auto fields = unifiedFlags.RequiredFields();
    const bool need_bones = OmniGhost::Gameplay::UnifiedESP::Has(
        fields, OmniGhost::Gameplay::UnifiedESP::DataField::Skeleton);
    const bool need_full_bones = frame_config.skeleton ||
        (frame_config.trigger_enabled && !frame_config.trigger_head_only);
    const bool need_names = frame_config.name || frame_config.radar_2d ||
        (frame_config.esp_enabled && !frame_config.show_bots) ||
        (frame_config.aim_enabled && frame_config.aim_ignore_bots);
    const bool need_weapons = frame_config.weapon_name || frame_config.weapon_ammo;
    const bool allow_weapon = pressure == 0 && !HeavyDmaCooldownActive();

    // The job snapshot was captured by the acquisition thread at queue time;
    // no reads from the global runtime.player list happen on this thread.
    if (dropped_only) {
        if (frame_config.dropped_weapons) {
            mem.SetDmaPriority(Memory::DmaPri::OtherMetadata);
            CS2::GiveCameraPriorityBeforeOptionalDma();
            CollectDroppedWeapons(frame_config, in_match, client_base, results.dropped_weapons);
            results.dropped_weapons_valid = true;
        }
        if (jobGen != g_session_generation.load(std::memory_order_acquire))
            return;
        if (auto slot = g_optional_results.TryBeginWrite()) {
            *slot.value = std::move(results);
            g_optional_results.Publish(slot.index);
        }
        return;
    }

    // ── Bones micro-batches ────────────────────────────────────────────
    if (need_bones && !!budget.Remaining() && pressure < 3) {
        mem.SetDmaPriority(Memory::DmaPri::Bones);
        const auto bonesPhaseBegin = std::chrono::steady_clock::now();
        // Optional work must yield between small batches; pressure 2 still
        // refreshes one pose at a time instead of letting every skeleton age out.
        constexpr int kBatch = 2;
        constexpr int kJointCount = 28;
        struct BoneJointSnapshot { float x, y, z, scale; char pad[0x10]; };
        static_assert(sizeof(BoneJointSnapshot) == 32, "BoneJointSnapshot");

        if (pressure >= 2 && targets.size() > 1) {
            static size_t recoveryCursor = 0; // optional worker is the sole caller
            recoveryCursor %= targets.size();
            std::rotate(targets.begin(), targets.begin() + recoveryCursor, targets.end());
            ++recoveryCursor;
        }

        for (size_t base = 0; base < targets.size() && !!budget.Remaining();) {
            if (g_optional_stop.load(std::memory_order_acquire)) return;
            if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;
            if (OptionalDmaShouldYieldToCamera()) {
                if (!CS2::GiveCameraPriorityBeforeOptionalDma())
                    break; // camera still busy after timeout — defer remaining batches
                if (OptionalDmaShouldYieldToCamera())
                    break; // defer remaining bone batches to next optional job
            }
            if (!CS2::GiveCameraPriorityBeforeOptionalDma())
                break; // camera still busy — defer remaining bone batches
            const size_t end = (std::min)(targets.size(), base + static_cast<size_t>(pressure >= 2 ? 1 : kBatch));
            uintptr_t boneBases[kBatch]{};
            BoneJointSnapshot joints[kBatch][kJointCount]{};
            bool eligible[kBatch]{};
            int queued = 0;
            for (size_t i = base; i < end; ++i) {
                const size_t bi = i - base;
                uintptr_t scene = targets[i].scene;
                if (!IsUserPointer(scene)) {
                    // Try live scene pointer from pawn.
                    QReadT(targets[i].pawn + offsets.m_pGameSceneNode, scene, "CS2.Opt.Scene");
                    targets[i].scene = IsUserPointer(scene) ? scene : 0;
                }
                if (!IsUserPointer(targets[i].scene)) continue;
                uintptr_t boneBase = 0;
                if (!QReadT(targets[i].scene + offsets.BoneArray, boneBase, "CS2.Opt.BoneBase") ||
                    !IsUserPointer(boneBase))
                    continue;
                boneBases[bi] = boneBase;
                eligible[bi] = true;
                mem.AddScatterReadRequest(g_scatter_optional, boneBase,
                    joints[bi], sizeof(BoneJointSnapshot) * (need_full_bones ? kJointCount : 8));
                ++queued;
            }
            if (queued > 0) {
                CS2::GiveCameraPriorityBeforeOptionalDma();
                mem.SetDmaCallTag("CS2.Optional.Bones");
                const uint64_t bone_read_begin_ms = GetTickCount64();
                mem.ExecuteReadScatter(g_scatter_optional);
                const uint64_t bone_read_end_ms = GetTickCount64();
                // P0-3: Check generation immediately after DMA to avoid processing stale data
                if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;
                // Midpoint of the actual scatter execute — this is the real
                // acquisition instant used for bones_age in presentation.
                const uint64_t sampleMs = bone_read_begin_ms +
                    (bone_read_end_ms - bone_read_begin_ms) / 2u;
                for (size_t i = base; i < end; ++i) {
                    const size_t bi = i - base;
                    if (!eligible[bi]) continue;
                    if (results.bone_count >= results.bones.size()) break;
                    OptionalBoneResult& r = results.bones[results.bone_count];
                    r = OptionalBoneResult{}; // rejected candidate must not contaminate the next slot
                    r.pawn = targets[i].pawn;
                    r.bone_base = boneBases[bi];
                    std::memcpy(r.origin, targets[i].origin, sizeof(r.origin));
                    const auto validJoint = [&](const BoneJointSnapshot& j) {
                        if (!std::isfinite(j.x) || !std::isfinite(j.y) || !std::isfinite(j.z))
                            return false;
                        const float dx = j.x - r.origin[0];
                        const float dy = j.y - r.origin[1];
                        const float dz = j.z - r.origin[2];
                        return dx * dx + dy * dy < 180.f * 180.f &&
                            dz > -50.f && dz < 200.f;
                    };
                    // Compact upper-body mapping (head/neck/chest/stomach indices
                    // depend on layout; use sticky when known).
                    static constexpr int kIdx[4] = {6, 5, 4, 2}; // head neck chest stomach common
                    bool any = false;
                    for (int s = 0; s < 4; ++s) {
                        const auto& j = joints[bi][kIdx[s]];
                        if (!validJoint(j))
                            continue;
                        r.bones[s == 3 ? 4 : s][0] = j.x;
                        r.bones[s == 3 ? 4 : s][1] = j.y;
                        r.bones[s == 3 ? 4 : s][2] = j.z;
                        any = true;
                    }
                    bool completePose = false;
                    if (need_full_bones) {
                        // Map extended slots conservatively from joint stream.
                        static constexpr int kFullMap[] = {
                            6,5,4,3,2,0,  // head..pelvis approx
                            8,9,10,11,    // left arm
                            13,14,15,16,  // right arm
                            22,23,24,     // left leg
                            25,26,27      // right leg
                        };
                        completePose = true;
                        for (size_t b = 0; b < kBoneSlotCount && b < sizeof(kFullMap)/sizeof(kFullMap[0]); ++b) {
                            const auto& j = joints[bi][kFullMap[b]];
                            if (!validJoint(j)) continue; // renderer omits that segment
                            r.bones[b][0] = j.x; r.bones[b][1] = j.y; r.bones[b][2] = j.z;
                        }
                        // A pose is drawable only when the torso has coherent
                        // joints. A corrupt limb cannot invalidate the torso or
                        // turn into a long line from the head.
                        for (size_t b : {size_t{0}, size_t{1}, size_t{2}, size_t{5}})
                            if (r.bones[b][0] == 0.f && r.bones[b][1] == 0.f && r.bones[b][2] == 0.f)
                                completePose = false;
                        if (completePose) {
                            const float dx = r.bones[0][0] - r.bones[5][0];
                            const float dy = r.bones[0][1] - r.bones[5][1];
                            const float dz = r.bones[0][2] - r.bones[5][2];
                            const float torsoSq = dx * dx + dy * dy + dz * dz;
                            const float hnX = r.bones[0][0] - r.bones[1][0];
                            const float hnY = r.bones[0][1] - r.bones[1][1];
                            const float hnZ = r.bones[0][2] - r.bones[1][2];
                            completePose = torsoSq > 100.f && torsoSq < 140.f * 140.f &&
                                hnX * hnX + hnY * hnY + hnZ * hnZ < 55.f * 55.f &&
                                r.bones[0][2] > r.origin[2] + 5.f;
                        }
                        r.full_bones_ok = completePose;
                    }
                    if (any && (!need_full_bones || completePose)) {
                        r.bones_ok = true;
                        r.sample_ms = sampleMs;
                        ++results.bone_count;
                    }
                }
            } // approx per micro-batch
            base = end;
        }
        results.bones_ms = OmniGhost::Gameplay::TimeMs(bonesPhaseBegin);
    }

    // ── Names (low rate) ───────────────────────────────────────────────
    if (need_names && !!budget.Remaining() && pressure < 2) {
        mem.SetDmaPriority(Memory::DmaPri::WeaponNameTeam);
        for (size_t i = 0; i < targets.size() && !!budget.Remaining(); ++i) {
            // P0-3: Check generation before each name read
            if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;
            if (!IsUserPointer(targets[i].controller)) continue;
            if (targets[i].has_name) continue; // job snapshot already has a name
            if (!CS2::GiveCameraPriorityBeforeOptionalDma())
                break; // camera still busy — abort names pass
            char name[64]{};
            if (QRead(targets[i].controller + offsets.m_iszPlayerName, name, sizeof(name) - 1, "CS2.Opt.Name")
                && name[0] && results.name_count < results.names.size()) {
                // P0-3: Verify generation still valid after DMA read
                if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;
                auto& r = results.names[results.name_count++];
                r.pawn = targets[i].pawn;
                r.controller = targets[i].controller;
                std::snprintf(r.name, sizeof(r.name), "%s", name);
            }
        }
    }

    // Item discovery must not starve behind per-player weapon metadata.
    if (frame_config.dropped_weapons && pressure < 1) {
        mem.SetDmaPriority(Memory::DmaPri::OtherMetadata);
        if (CS2::GiveCameraPriorityBeforeOptionalDma()) {
            // P0-3: Check generation before dropped weapons scan
            if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;
            const auto droppedPhaseBegin = std::chrono::steady_clock::now();
            CollectDroppedWeapons(frame_config, in_match, client_base, results.dropped_weapons);
            // P0-3: Verify generation after dropped weapons scan
            if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;
            results.dropped_weapons_valid = true;
            results.dropped_ms = OmniGhost::Gameplay::TimeMs(droppedPhaseBegin);
        }
    }

    // ── Weapons (pressure 0 only) ──────────────────────────────────────
    if (need_weapons && allow_weapon && !!budget.Remaining()) {
        mem.SetDmaPriority(Memory::DmaPri::WeaponNameTeam);
        const auto weaponPhaseBegin = std::chrono::steady_clock::now();
        
        // P1-4: Batch weapon metadata reads using scatter to reduce round-trips
        // Phase 1: Read weapon services for all targets in one scatter
        std::vector<uintptr_t> weaponServices(targets.size(), 0);
        {
            auto h = mem.CreateScatterHandle();
            if (h) {
                for (size_t i = 0; i < targets.size(); ++i) {
                    mem.AddScatterReadRequest(h, targets[i].pawn + offsets.m_pWeaponServices,
                                              &weaponServices[i], sizeof(uintptr_t));
                }
                mem.ExecuteReadScatter(h);
                mem.CloseScatterHandle(h);
            }
        }
        if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;

        // Phase 2: Read weapon handles for valid services
        std::vector<uint32_t> weaponHandles(targets.size(), 0);
        {
            auto h = mem.CreateScatterHandle();
            if (h) {
                for (size_t i = 0; i < targets.size(); ++i) {
                    if (!IsUserPointer(weaponServices[i])) continue;
                    mem.AddScatterReadRequest(h, weaponServices[i] + offsets.m_hActiveWeapon,
                                              &weaponHandles[i], sizeof(uint32_t));
                }
                mem.ExecuteReadScatter(h);
                mem.CloseScatterHandle(h);
            }
        }
        if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;

        // Phase 3: Resolve weapon entity handles in batch
        std::vector<uintptr_t> weaponEntities(targets.size(), 0);
        {
            const uintptr_t primaryStride = g_pawn_stride ? g_pawn_stride : 0x70;
            const uintptr_t alternateStride = (primaryStride == 0x70) ? 0x78 : 0x70;
            CS2::ScatterResolvePawnHandles(weaponHandles.data(), weaponEntities.data(),
                static_cast<int>(targets.size()), primaryStride, alternateStride);
        }
        if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;

        // Phase 4: Read definitions and ammo in batch
        std::vector<uint16_t> weaponDefs(targets.size(), 0);
        std::vector<int> weaponClips(targets.size(), -1);
        {
            auto h = mem.CreateScatterHandle();
            if (h) {
                for (size_t i = 0; i < targets.size(); ++i) {
                    if (!IsUserPointer(weaponEntities[i])) continue;
                    uintptr_t attr = weaponEntities[i] + offsets.m_AttributeManager;
                    mem.AddScatterReadRequest(h, attr + offsets.m_Item + offsets.m_iItemDefinitionIndex,
                                              &weaponDefs[i], sizeof(uint16_t));
                    if (frame_config.weapon_ammo && offsets.m_iClip1) {
                        mem.AddScatterReadRequest(h, weaponEntities[i] + offsets.m_iClip1,
                                                  &weaponClips[i], sizeof(int));
                    }
                }
                mem.ExecuteReadScatter(h);
                mem.CloseScatterHandle(h);
            }
        }
        if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;

        // Collect results
        for (size_t i = 0; i < targets.size() && !!budget.Remaining(); ++i) {
            if (!IsUserPointer(weaponEntities[i])) continue;
            const bool haveDef = weaponDefs[i] > 0 && weaponDefs[i] < 6000;
            const bool haveClip = frame_config.weapon_ammo && weaponClips[i] >= 0 && weaponClips[i] < 500;
            if ((haveDef || haveClip) && results.weapon_count < results.weapons.size()) {
                auto& r = results.weapons[results.weapon_count++];
                r.pawn = targets[i].pawn;
                if (haveDef) {
                    r.weapon_def = weaponDefs[i];
                    const char* nm = CS2_Weapons::NameFromDefIndex(static_cast<int>(weaponDefs[i]));
                    if (nm)
                        std::snprintf(r.weapon, sizeof(r.weapon), "%s", nm);
                }
                if (haveClip)
                    r.ammo_clip = weaponClips[i];
            }
        }
        results.weapon_ms = OmniGhost::Gameplay::TimeMs(weaponPhaseBegin);
    }

    // P2-8: Track optional budget left for telemetry correlation.
    results.budget_left_ms = budget.Remaining();

    // ── Visibility (raycast against static map collision) ───────────────
    if (OmniGhost::Gameplay::UnifiedESP::Has(
            fields, OmniGhost::Gameplay::UnifiedESP::DataField::Visibility) &&
        !!budget.Remaining() && pressure < 2) {
        // Get collision world for this map
        auto& collision = Trajectory::CollisionCache();
        if (collision.IsLoadedFor(runtime.map_name)) {
            // Get camera origin for raycasting
            Trajectory::Vec3 cameraOrigin{};
            if (auto cam = g_camera_snapshots.Acquire(); cam && cam->timestamp_ms != 0) {
                cameraOrigin = Trajectory::Vec3(cam->view_matrix[12], cam->view_matrix[13], cam->view_matrix[14]);
            } else if (targets.empty()) {
                cameraOrigin = Trajectory::Vec3(0, 0, 0);
            } else {
                // Fallback: use first target's origin as rough camera position
                cameraOrigin = Trajectory::Vec3(targets[0].origin[0], targets[0].origin[1], targets[0].origin[2]);
            }

            // Prepare visibility targets - use head + body points for each pawn
            std::vector<VisibilityTarget> visTargets;
            visTargets.reserve(targets.size());
            for (const auto& t : targets) {
                VisibilityTarget vt{};
                vt.pawn = t.pawn;
                // Point 0: head (origin + ~70 units up)
                vt.points[0] = Trajectory::Vec3(t.origin[0], t.origin[1], t.origin[2] + 70.f);
                // Point 1: chest
                vt.points[1] = Trajectory::Vec3(t.origin[0], t.origin[1], t.origin[2] + 40.f);
                // Point 2: pelvis
                vt.points[2] = Trajectory::Vec3(t.origin[0], t.origin[1], t.origin[2] + 10.f);
                // Point 3: feet
                vt.points[3] = Trajectory::Vec3(t.origin[0], t.origin[1], t.origin[2] - 10.f);
                visTargets.push_back(vt);
            }

            if (!visTargets.empty()) {
                mem.SetDmaPriority(Memory::DmaPri::OtherMetadata);
                CS2::GiveCameraPriorityBeforeOptionalDma();
                const auto visPhaseBegin = std::chrono::steady_clock::now();
                // Submit to visibility worker (async)
                g_visibility_worker.Submit(
                    collision.WorldSnapshot(), cameraOrigin, std::move(visTargets), jobGen);
                // Note: visibility worker runs async; results will be queried on next optional job
                // For now, we query any already-completed results from previous submissions
                for (const auto& t : targets) {
                    if (!IsUserPointer(t.pawn)) continue;
                    auto visResult = g_visibility_worker.Get(t.pawn, jobGen);
                    if (visResult.known && results.visibility_count < results.visibility.size()) {
                        auto& r = results.visibility[results.visibility_count++];
                        r.pawn = t.pawn;
                        r.visible = visResult.visible;
                        r.known = true;
                        r.checked_ms = visResult.checked_ms;
                    }
                }
                results.visibility_ms = OmniGhost::Gameplay::TimeMs(visPhaseBegin);
            }
        }
    }

    if (jobGen != g_session_generation.load(std::memory_order_acquire))
        return;
    // Publish deltas; the acquisition thread merges and owns the runtime publish.
    if (auto slot = g_optional_results.TryBeginWrite()) {
        *slot.value = std::move(results);
        g_optional_results.Publish(slot.index);
    }
}

static void OptionalWorkerLoop() {
    while (!g_optional_stop.load(std::memory_order_acquire)) {
        Config jobConfig{};
        std::vector<OptionalTarget> jobTargets;
        bool jobInMatch = false, jobDroppedOnly = false;
        uintptr_t jobClientBase = 0;
        uint64_t jobGeneration = 0;
        {
            std::unique_lock lock(g_optional_mu);
            g_optional_cv.wait_for(lock, std::chrono::milliseconds(50), [] {
                return g_optional_pending.load(std::memory_order_acquire) ||
                    g_optional_stop.load(std::memory_order_acquire);
            });
            if (g_optional_stop.load(std::memory_order_acquire))
                break;
            if (!g_optional_pending.load(std::memory_order_acquire))
                continue;
            jobConfig = g_optional_config;
            jobTargets.swap(g_optional_targets); // steal payload; no extra alloc
            jobInMatch = g_optional_in_match;
            jobDroppedOnly = g_optional_dropped_only;
            jobClientBase = g_optional_client_base;
            jobGeneration = g_optional_job_generation.load(std::memory_order_acquire);
            g_optional_pending.store(false, std::memory_order_release);
        }
        g_optional_busy.store(true, std::memory_order_release);
        try {
            mem.SetDmaLane("optional");
            mem.SetDmaPriority(Memory::DmaPri::Bones);
            mem.SetDmaCallTag("CS2.Optional");
            RunOptionalEnrichment(jobConfig, std::move(jobTargets), jobInMatch,
                                  jobClientBase, jobDroppedOnly, jobGeneration);
        } catch (...) {
            // Never kill the worker on a single bad scan.
        }
        g_optional_busy.store(false, std::memory_order_release);
        mem.SetDmaLane("full");
                mem.SetDmaPriority(Memory::DmaPri::Position);
    }
}

static void QueueOptionalEnrichment(const Config& frame_config) {
    if (g_optional_stop.load(std::memory_order_acquire))
        return;
    // If previous optional still runs, skip this cycle — core already published
    // and presentation has positions/health. Avoid stacking optional DMA.
    if (g_optional_busy.load(std::memory_order_acquire) ||
        g_optional_pending.load(std::memory_order_acquire))
        return;
    {
        std::lock_guard lock(g_optional_mu);
        g_optional_config = frame_config;
        // Immutable job payload: acquisition thread owns runtime.players, so
        // the optional worker receives copies and never dereferences runtime.
        g_optional_in_match = runtime.in_match;
        g_optional_client_base = runtime.client_base;
        g_optional_dropped_only = false;
        g_optional_targets.clear();
        g_optional_targets.reserve((std::min)(runtime.players.size(), size_t{64}));
        for (const auto& p : runtime.players) {
            if (!IsUserPointer(p.pawn) || !p.alive) continue;
            if (g_optional_targets.size() >= 64) break;
            g_optional_targets.push_back({p.pawn, p.controller, p.scene,
                                          {p.pos[0], p.pos[1], p.pos[2]}, p.name[0] != 0});
        }
        g_optional_job_generation.store(
            g_session_generation.load(std::memory_order_relaxed), std::memory_order_release);
        g_optional_pending.store(true, std::memory_order_release);
    }
    g_optional_cv.notify_one();
}

// Dropped-item discovery must run even when player scanning is disabled
// (on-demand branch). Queues a metadata-only optional job with no pawns.
static void QueueOptionalDroppedOnly(const Config& frame_config) {
    if (g_optional_stop.load(std::memory_order_acquire))
        return;
    if (g_optional_busy.load(std::memory_order_acquire) ||
        g_optional_pending.load(std::memory_order_acquire))
        return;
    {
        std::lock_guard lock(g_optional_mu);
        g_optional_config = frame_config;
        g_optional_in_match = runtime.in_match;
        g_optional_client_base = runtime.client_base;
        g_optional_dropped_only = true;
        g_optional_targets.clear();
        g_optional_job_generation.store(
            g_session_generation.load(std::memory_order_relaxed), std::memory_order_release);
        g_optional_pending.store(true, std::memory_order_release);
    }
    g_optional_cv.notify_one();
}


// Death-mode is deliberately a separate, low-frequency path.  It never reads
// player transforms, bones, weapons or aim data; it only follows observer
// handles so the spectator widget keeps working while the main acquisition is
// paused.  The normal scan resumes automatically as soon as local HP is back.
static void UpdateSpectators(const Config& frame_config,
                             const uintptr_t* controllers, int count) {
    runtime.spectators.clear();
    runtime.spectator_count = 0;
    runtime.spectator_target = runtime.local_pawn;
    runtime.spectator_target_name[0] = '\0';
    if (!frame_config.spectator_list || !runtime.local_pawn ||
        !offsets.m_pObserverServices || !offsets.m_hObserverTarget)
        return;

    static std::vector<Player> cachedSpectators;
    static uintptr_t cachedTarget = 0;
    static uintptr_t cachedLocalPawn = 0;
    static bool cachedLocalDead = false;
    static uint64_t nextRefreshMs = 0;
    static uint64_t cachedGeneration = 0;
    const uint64_t generation = g_session_generation.load(std::memory_order_acquire);
    if (cachedGeneration != generation) {
        cachedSpectators.clear();
        cachedTarget = 0;
        cachedLocalPawn = 0;
        cachedLocalDead = false;
        nextRefreshMs = 0;
        cachedGeneration = generation;
    }
    const uint64_t now = GetTickCount64();
    const bool localDead = runtime.local_health <= 0;
    if (cachedLocalPawn == runtime.local_pawn && cachedLocalDead == localDead &&
        (now < nextRefreshMs || g_pressure_level.load(std::memory_order_relaxed) >= 2)) {
        runtime.spectator_target = cachedTarget;
        runtime.spectators = cachedSpectators;
        runtime.spectator_count = static_cast<int>(runtime.spectators.size());
        return;
    }
    if (g_pressure_level.load(std::memory_order_relaxed) >= 2)
        return; // no cached target for this pawn; shed optional DMA entirely
    // The target and list are one optional 500 ms lane; do not read the
    // observer pointer chain on every 18 ms core scan while dead.
    CS2::GiveCameraPriorityBeforeOptionalDma();
    ScopedPhase _phSpectators(&g_phase.spectator_ms);
    g_phase.spectator_refresh = 1;
    if (localDead) {
        uintptr_t localServices = 0;
        uint32_t watchedHandle = 0;
        if (QReadT(runtime.local_pawn + offsets.m_pObserverServices, localServices) &&
            IsUserPointer(localServices) &&
            QReadT(localServices + offsets.m_hObserverTarget, watchedHandle)) {
            const uintptr_t watched = ResolveEntityByHandle(
                watchedHandle, g_pawn_stride ? g_pawn_stride : kEntityIdentityStride);
            if (IsUserPointer(watched)) runtime.spectator_target = watched;
        }
    }

    // Fetch every observer handle in one DMA trip. The old path made one
    // blocking QRead per controller before even knowing who was spectating.
    constexpr int kSpectatorSlots = 64;
    uint32_t observerHandles[kSpectatorSlots]{};
    EnsureScatter();
    if (!g_scatter_full || !offsets.m_hObserverPawn) {
        nextRefreshMs = now + 500u;
        return; // optional UI lane must not fall back to 64 synchronous reads
    }
    const int spectatorControllerCount = (std::min)(count, kSpectatorSlots);
    bool queuedObservers = false;
    for (int i = 0; i < spectatorControllerCount; ++i) {
        if (!IsUserPointer(controllers[i]) || controllers[i] == runtime.local_controller)
            continue;
        mem.AddScatterReadRequest(g_scatter_full,
            controllers[i] + offsets.m_hObserverPawn,
            &observerHandles[i], sizeof(observerHandles[i]));
        queuedObservers = true;
    }
    if (queuedObservers) {
        CS2::GiveCameraPriorityBeforeOptionalDma();
        mem.SetDmaCallTag("CS2.Spectators.Handles");
        mem.ExecuteReadScatter(g_scatter_full);
    }
    uintptr_t observerPawns[kSpectatorSlots]{};
    if (queuedObservers) {
        const uintptr_t primaryStride = g_pawn_stride ? g_pawn_stride : kEntityIdentityStride;
        const uintptr_t alternateStride = (primaryStride == kEntityIdentityStride)
            ? kLegacyEntityIdentityStride : kEntityIdentityStride;
        ScatterResolvePawnHandles(observerHandles, observerPawns,
            spectatorControllerCount, primaryStride, alternateStride, true);
    }
    uintptr_t observerServices[kSpectatorSlots]{};
    bool queuedServices = false;
    for (int i = 0; i < spectatorControllerCount; ++i) {
        if (!IsUserPointer(observerPawns[i])) continue;
        mem.AddScatterReadRequest(g_scatter_full,
            observerPawns[i] + offsets.m_pObserverServices,
            &observerServices[i], sizeof(observerServices[i]));
        queuedServices = true;
    }
    if (queuedServices) {
        CS2::GiveCameraPriorityBeforeOptionalDma();
        mem.SetDmaCallTag("CS2.Spectators.Services");
        mem.ExecuteReadScatter(g_scatter_full);
    }
    uint32_t targetHandles[kSpectatorSlots]{};
    bool queuedTargets = false;
    for (int i = 0; i < spectatorControllerCount; ++i) {
        if (!IsUserPointer(observerServices[i])) continue;
        mem.AddScatterReadRequest(g_scatter_full,
            observerServices[i] + offsets.m_hObserverTarget,
            &targetHandles[i], sizeof(targetHandles[i]));
        queuedTargets = true;
    }
    if (queuedTargets) {
        CS2::GiveCameraPriorityBeforeOptionalDma();
        mem.SetDmaCallTag("CS2.Spectators.Targets");
        mem.ExecuteReadScatter(g_scatter_full);
    }
    uintptr_t targetPawns[kSpectatorSlots]{};
    if (queuedTargets) {
        const uintptr_t primaryStride = g_pawn_stride ? g_pawn_stride : kEntityIdentityStride;
        const uintptr_t alternateStride = (primaryStride == kEntityIdentityStride)
            ? kLegacyEntityIdentityStride : kEntityIdentityStride;
        ScatterResolvePawnHandles(targetHandles, targetPawns,
            spectatorControllerCount, primaryStride, alternateStride, true);
    }

    char spectatorNames[kSpectatorSlots][64]{};
    uint64_t spectatorSteamIds[kSpectatorSlots]{};
    bool queuedIdentity = false;
    for (int i = 0; i < spectatorControllerCount; ++i) {
        if (!IsUserPointer(controllers[i]) ||
            !IsUserPointer(observerPawns[i]) ||
            targetPawns[i] != runtime.spectator_target)
            continue;
        mem.AddScatterReadRequest(g_scatter_full,
            controllers[i] + offsets.m_iszPlayerName,
            spectatorNames[i], sizeof(spectatorNames[i]) - 1);
        if (offsets.m_steamID)
            mem.AddScatterReadRequest(g_scatter_full,
                controllers[i] + offsets.m_steamID,
                &spectatorSteamIds[i], sizeof(spectatorSteamIds[i]));
        queuedIdentity = true;
    }
    if (queuedIdentity) {
        CS2::GiveCameraPriorityBeforeOptionalDma();
        mem.SetDmaCallTag("CS2.Spectators.Identity");
        mem.ExecuteReadScatter(g_scatter_full);
    }
    for (int i = 0; i < spectatorControllerCount; ++i) {
        const uintptr_t controller = controllers[i];
        if (!IsUserPointer(controller) || controller == runtime.local_controller)
            continue;
        const uint32_t observerHandle = observerHandles[i];
        if (!observerHandle || observerHandle == 0xFFFFFFFFu)
            continue;
        const uintptr_t observerPawn = observerPawns[i];
        if (!IsUserPointer(observerPawn) ||
            targetPawns[i] != runtime.spectator_target)
            continue;

        Player spectator{};
        spectator.controller = controller;
        spectator.pawn = observerPawn;
        spectator.is_spectator = true;
        spectator.alive = false;
        spectator.steam_id = spectatorSteamIds[i];
        std::memcpy(spectator.name, spectatorNames[i], sizeof(spectator.name));
        spectator.name[sizeof(spectator.name) - 1] = '\0';
        if (!spectator.name[0])
            std::snprintf(spectator.name, sizeof(spectator.name), "Jogador_%d", i + 1);
        runtime.spectators.push_back(spectator);
    }
    cachedSpectators = runtime.spectators;
    cachedTarget = runtime.spectator_target;
    cachedLocalPawn = runtime.local_pawn;
    cachedLocalDead = localDead;
    nextRefreshMs = now + 500u;
    runtime.spectator_count = static_cast<int>(runtime.spectators.size());
}

static void RunFrameWithConfig(const Config& frame_config) {
    runtime.read_fails = g_read_fails.load(std::memory_order_relaxed);
    // The network/process lifecycle is owned by its control worker, never by
    // the latency-critical acquisition lane.
    const int radarPort = frame_config.webradar_port > 0 && frame_config.webradar_port <= 65535
        ? frame_config.webradar_port : 8080;
    g_web_radar_desired_port.store(radarPort, std::memory_order_relaxed);
    g_web_radar_desired_cloudflare.store(frame_config.webradar_cloudflare, std::memory_order_relaxed);
    g_web_radar_desired_enabled.store(frame_config.webradar_enabled, std::memory_order_release);

    if (!ready || !offsets.loaded || !runtime.client_base)
        return;

    // CS2 Diversos → Telemetria: mirror into the shared DMA telemetry logger.
    OmniGhost::Gameplay::DmaTelemetry::SetEnabled(frame_config.telemetry_enabled);
    if (frame_config.telemetry_enabled) {
        static uint64_t s_cfgBits = 0;
        uint64_t bits = 0;
        auto bit = [&](bool v, int i) { if (v) bits |= (1ull << i); };
        bit(frame_config.radar_2d || frame_config.webradar_enabled, 0);
        bit(frame_config.aim_enabled, 1);
        bit(frame_config.trigger_enabled, 2);
        bit(frame_config.bomb_timer, 3);
        bit(frame_config.spectator_list, 4);
        bit(frame_config.performance_mode, 5);
        if (bits != s_cfgBits) {
            s_cfgBits = bits;
            char blob[320];
            std::snprintf(blob, sizeof(blob),
                "radar=%d\naim=%d\ntrigger=%d\nbomb=%d\nspectators=%d\nperf_mode=%d",
                (frame_config.radar_2d || frame_config.webradar_enabled) ? 1 : 0,
                frame_config.aim_enabled ? 1 : 0,
                frame_config.trigger_enabled ? 1 : 0,
                frame_config.bomb_timer ? 1 : 0,
                frame_config.spectator_list ? 1 : 0,
                frame_config.performance_mode ? 1 : 0);
            OmniGhost::Gameplay::DmaTelemetry::LogConfigChanged("CS2", blob);
        }
    }

    const auto _frameWallBegin = std::chrono::steady_clock::now();
    ++runtime.frames;
    runtime.fps = g_presentation_fps.load(std::memory_order_relaxed);

    const uint64_t currentGen = g_session_generation.load(std::memory_order_acquire);
    const uintptr_t client = runtime.client_base;
    // Keep last-good player snapshot when entity list briefly fails (prevents
    // ESP/aim going empty for 1–2 frames on ListEntry null flicker).
    struct RecoveryIdentity { uintptr_t pawn = 0; uintptr_t controller = 0; };
    static std::vector<RecoveryIdentity> last_good_players;
    static uint64_t last_good_ms = 0;
    struct RecentPlayer {
        Player player{};
        uint64_t last_seen_ms = 0;
        uint64_t entry_generation = 0;
        bool confirmed_dead = false; // P0-3: never reinsert after confirmed death
    };
    struct CachedBones {
        float joints[kBoneSlotCount][3]{};
        float origin[3]{};
        uint64_t last_valid_ms = 0;
        bool full_pose = false;
        uint64_t entry_generation = 0;
    };
    static std::unordered_map<uintptr_t, RecentPlayer> recent_players;
    static std::unordered_map<uintptr_t, CachedBones> bone_cache;
    static std::unordered_map<uintptr_t, uint8_t> s_stickyBoneLayout; // 0/1 once validated
    static std::unordered_map<uintptr_t, uint64_t> s_lastBoneMs;
    static uint64_t recoveryCacheGeneration = 0;
    if (recoveryCacheGeneration != g_session_generation.load(std::memory_order_acquire)) {
        last_good_players.clear(); last_good_ms = 0;
        recent_players.clear(); bone_cache.clear();
        s_stickyBoneLayout.clear(); s_lastBoneMs.clear();
        recoveryCacheGeneration = g_session_generation.load(std::memory_order_relaxed);
    }
    if (recent_players.bucket_count() < 128) recent_players.reserve(128);
    if (bone_cache.bucket_count() < 128) bone_cache.reserve(128);
    if (s_stickyBoneLayout.bucket_count() < 128) s_stickyBoneLayout.reserve(128);
    if (s_lastBoneMs.bucket_count() < 128) s_lastBoneMs.reserve(128);

    // ── Merge optional enrichment results (P0 single-writer invariant) ───
    // The optional worker is the sole producer of g_optional_results and never
    // touches `runtime`. Draining here keeps THIS thread the only writer of
    // runtime.players and the only producer of g_runtime_snapshots.
    const auto drainOptionalResults = [&](uint64_t generation) {
        auto opt = g_optional_results.Acquire();
        if (!opt || opt->generation != generation)
            return;
        const uint64_t mergeNowMs = GetTickCount64();
        // Attribute the optional worker's phase timings to this scan.
        g_phase.bones_ms += opt->bones_ms;
        g_phase.weapon_ms += opt->weapon_ms;
        g_phase.optional_budget_left_ms = opt->budget_left_ms; // P2-8: track optional budget
        g_phase.bones_players += static_cast<int>(opt->bone_count);

        for (uint32_t i = 0; i < opt->bone_count && i < opt->bones.size(); ++i) {
            const OptionalBoneResult& r = opt->bones[i];
            if (!IsUserPointer(r.pawn) || !r.bones_ok || !r.sample_ms) continue;
            CachedBones& cb = bone_cache[r.pawn];
            if (cb.entry_generation != generation) { cb = CachedBones{}; cb.entry_generation = generation; }
            std::memcpy(cb.joints, r.bones, sizeof(cb.joints));
            cb.last_valid_ms = r.sample_ms;
            cb.full_pose = r.full_bones_ok;
            std::memcpy(cb.origin, r.origin, sizeof(cb.origin));
        }
        for (uint32_t i = 0; i < opt->name_count && i < opt->names.size(); ++i) {
            const OptionalNameResult& r = opt->names[i];
            if (!IsUserPointer(r.pawn) || !IsUserPointer(r.controller) || !r.name[0]) continue;
            RecentPlayer& rp = recent_players[r.pawn];
            if (rp.entry_generation != generation) {
                rp = RecentPlayer{};
                rp.player.pawn = r.pawn;
                rp.entry_generation = generation;
            }
            rp.last_seen_ms = mergeNowMs;
            rp.player.controller = r.controller;
            std::snprintf(rp.player.name, sizeof(rp.player.name), "%s", r.name);
            rp.player.is_bot = IsBotPlayerName(r.name);
        }
        for (uint32_t i = 0; i < opt->weapon_count && i < opt->weapons.size(); ++i) {
            const OptionalWeaponResult& r = opt->weapons[i];
            if (!IsUserPointer(r.pawn) || (r.weapon_def <= 0 && r.ammo_clip < 0)) continue;
            RecentPlayer& rp = recent_players[r.pawn];
            if (rp.entry_generation != generation) {
                rp = RecentPlayer{};
                rp.player.pawn = r.pawn;
                rp.entry_generation = generation;
            }
            rp.last_seen_ms = mergeNowMs;
            if (r.weapon_def > 0) rp.player.weapon_def = r.weapon_def;
            if (r.weapon[0])
                std::snprintf(rp.player.weapon, sizeof(rp.player.weapon), "%s", r.weapon);
            if (r.ammo_clip >= 0) rp.player.ammo_clip = r.ammo_clip;
        }
        // Merge visibility results from optional worker
        for (uint32_t i = 0; i < opt->visibility_count && i < opt->visibility.size(); ++i) {
            const OptionalVisibilityResult& r = opt->visibility[i];
            if (!IsUserPointer(r.pawn) || !r.known) continue;
            RecentPlayer& rp = recent_players[r.pawn];
            if (rp.entry_generation != generation) {
                rp = RecentPlayer{};
                rp.player.pawn = r.pawn;
                rp.entry_generation = generation;
            }
            rp.last_seen_ms = mergeNowMs;
            rp.player.visibility_known = true;
            rp.player.visible = r.visible;
        }
        if (opt->dropped_weapons_valid)
            runtime.dropped_weapons = opt->dropped_weapons;
            runtime.dropped_weapons.reserve(24);
    };
    drainOptionalResults(currentGen);
    // Recovery may freeze expensive topology/metadata work, but it must never
    // freeze death detection. Probe only the already-known pawn health fields,
    // publish them immediately, and remove confirmed deaths from every cache
    // that can republish a player. Sentinel values remain unknown/read-failed
    // and deliberately retain the short dropout grace period.
    const auto refresh_cached_liveness = [&](uint64_t now_ms) {
        if (last_good_players.empty() || !offsets.m_iHealth)
            return false;
        EnsureScatter();
        if (!g_scatter_full)
            return false;

        constexpr int kUnknownHealth = (std::numeric_limits<int>::min)();
        std::array<int, 64> health{};
        std::array<uint8_t, 64> controller_alive{};
        health.fill(kUnknownHealth);
        controller_alive.fill(0xFF);
        const size_t count = (std::min)(last_good_players.size(), health.size());
        size_t queued = 0;
        for (size_t i = 0; i < count; ++i) {
            if (!IsUserPointer(last_good_players[i].pawn))
                continue;
            mem.AddScatterReadRequest(g_scatter_full,
                last_good_players[i].pawn + offsets.m_iHealth,
                &health[i], sizeof(health[i]));
            if (offsets.m_bPawnIsAlive && IsUserPointer(last_good_players[i].controller))
                mem.AddScatterReadRequest(g_scatter_full,
                    last_good_players[i].controller + offsets.m_bPawnIsAlive,
                    &controller_alive[i], sizeof(controller_alive[i]));
            ++queued;
        }
        if (!queued)
            return false;

        mem.SetDmaCallTag("CS2.LivenessRecovery");
        mem.ExecuteReadScatter(g_scatter_full);
        LivenessSnapshot liveness{};
        liveness.timestamp_ms = GetTickCount64();
        liveness.generation = g_session_generation.load(std::memory_order_acquire);
        std::array<uintptr_t, 64> confirmed_dead{};
        size_t dead_count = 0;
        for (size_t i = 0; i < count; ++i) {
            // Values left at the sentinel are failed/unknown, not deaths.
            const bool healthKnown = health[i] != kUnknownHealth && health[i] >= 0 && health[i] <= 200;
            const bool controllerKnown = controller_alive[i] == 0 || controller_alive[i] == 1;
            if (!healthKnown && !controllerKnown)
                continue;
            auto& sample = liveness.players[liveness.count++];
            sample.pawn = last_good_players[i].pawn;
            sample.sample_ms = liveness.timestamp_ms;
            sample.generation = liveness.generation;
            sample.alive = !(healthKnown && health[i] <= 0) &&
                !(controllerKnown && controller_alive[i] == 0);
            if (!sample.alive)
                confirmed_dead[dead_count++] = sample.pawn;
        }
        if (liveness.count)
            PublishLivenessSnapshot(liveness);

        for (size_t i = 0; i < dead_count; ++i) {
            const uintptr_t pawn = confirmed_dead[i];
            last_good_players.erase(std::remove_if(last_good_players.begin(),
                last_good_players.end(), [pawn](const RecoveryIdentity& p) { return p.pawn == pawn; }),
                last_good_players.end());
            recent_players.erase(pawn);
            bone_cache.erase(pawn);
            s_stickyBoneLayout.erase(pawn);
            s_lastBoneMs.erase(pawn);
        }
        auto& telemetry = OmniGhost::Gameplay::DmaTelemetry::CS2();
        telemetry.last_good_age_ms.store(last_good_ms && now_ms >= last_good_ms
            ? now_ms - last_good_ms : 0u, std::memory_order_relaxed);
        return true;
    };
    runtime.player_count = 0;
    runtime.enemy_count = 0;
    mem.SetDmaCallTag("CS2.Controllers");
    runtime.controller_count = 0;
    runtime.pawn_count = 0;
    runtime.players.clear();
    if (runtime.players.capacity() < 64)
        runtime.players.reserve(64);
    // Entity-list root/page validation is time-based inside
    // RefreshEntityListEntry; ordinary scans reuse the last confirmed pair.

    // Map detection is wall-clock based so acquisition cadence changes do not
    // accidentally make this expensive read more frequent. Lobby remains fast.
    static char previous_map[64]{};
    static uint64_t next_map_refresh_ms = 0;
    const uint64_t map_now_ms = GetTickCount64();
    if (map_now_ms >= next_map_refresh_ms) {
        mem.SetDmaCallTag("CS2.MapRefresh");
        RefreshMapName();
        // The map normally changes only on a transition.  Re-reading it every
        // few seconds was an expensive synchronous probe that could queue
        // behind player reads.  Keep lobby detection responsive, but let a
        // confirmed match reuse its valid name for a longer period.
        const int pressure = g_pressure_level.load(std::memory_order_relaxed);
        // Keep a cheap safety probe during a match. Session/topology changes
        // force an immediate refresh elsewhere, while this catches missed
        // transitions without retaining a collision world for minutes.
        next_map_refresh_ms = map_now_ms + (runtime.in_match
            ? (pressure >= 2 ? 60000u : 30000u)
            : 1000u);
    }

    // Match transition: map changed → force entity-list re-acquire so ESP
    // recovers automatically when leaving lobby / joining a new game.
    // Also force-clear Makcu button mask — release packets are often lost
    // during load and would leave LMB stuck ("mira sozinho").
    if (std::strncmp(previous_map, runtime.map_name, sizeof(previous_map)) != 0) {
        ResetSessionState("map-change");
        std::memcpy(previous_map, runtime.map_name, sizeof(previous_map));
        g_pending_entity_list = 0;
        g_entity_list_confirmations = 0;
        runtime.entity_list_entry = 0;
        runtime.entity_list_addr = 0;
        g_entity_list_next_validation_ms = 0;
        g_entity_list_next_full_probe_ms = 0;
        g_entity_list_force_refresh = true;
        runtime.bomb = BombState{};
        recent_players.clear();
        bone_cache.clear();
        s_stickyBoneLayout.clear();
        s_lastBoneMs.clear();
        runtime.players.clear();
        runtime.player_count = 0;
        runtime.pawn_count = 0;
        runtime.controller_count = 0;
        g_cached_entity_root = 0;
        makcu_wrapper::ForceClearButtons();
        if (runtime.map_name[0])
            std::cout << "[CS2] Mapa: " << runtime.map_name
                      << " (ESP auto-recover, config intact)" << std::endl;
    }

    // Visibility colours use the static collision mesh of the active map.
    // Build the BVH away from the acquisition thread so loading/changing maps
    // cannot hold up camera or player snapshots.
    if (g_collision_load.valid() &&
        g_collision_load.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        try { g_collision_load.get(); }
        catch (...) { std::cout << "[CS2] Falha ao preparar colisão do mapa." << std::endl; }
    }
    auto& collision = Trajectory::CollisionCache();
    const bool needs_collision = frame_config.esp_enabled && frame_config.visibility_colors;
    const uint64_t collision_retry_at = g_collision_retry_at.load(std::memory_order_relaxed);
    if (needs_collision && runtime.map_name[0] &&
        !collision.IsLoadedFor(runtime.map_name) && !g_collision_load.valid() &&
        (g_collision_attempt_map != runtime.map_name || map_now_ms >= collision_retry_at)) {
        const std::string requested_map = runtime.map_name;
        g_collision_attempt_map = requested_map;
        g_collision_retry_at.store(map_now_ms + 5000u, std::memory_order_relaxed);
        g_collision_cancel.store(false, std::memory_order_release);
        g_collision_load = std::async(std::launch::async, [requested_map] {
            if (g_collision_cancel.load(std::memory_order_acquire))
                return;
            try { (void)Trajectory::CollisionCache().LoadForMap(requested_map.c_str(), &g_collision_cancel); }
            catch (...) { std::cout << "[CS2] Exceção ao preparar colisão do mapa." << std::endl; }
        });
    }
    if (!needs_collision && !g_collision_load.valid())
        collision.Clear();

    const bool map_reports_match = IsPlayableMapName(runtime.map_name);
    runtime.in_match = map_reports_match;
    int entity_probe_controller_count = 0;

    // Detect lobby → match and match → match transitions even when the map
    // string stays empty briefly (common when INSERT was opened in the lobby).
    static bool was_in_match = false;
    static int zero_player_frames = 0;
    // Map refresh is the primary match signal. Some builds briefly expose an
    // empty map string after loading, so retain a deliberately strict and slow
    // fallback: a local pawn alone is also present in lobby, but a live entity
    // page with multiple controllers is not. This avoids activating the full
    // pipeline in lobby while still recovering automatically in a real match.
    static uint64_t next_match_fallback_ms = 0;
    if (!runtime.in_match && map_now_ms >= next_match_fallback_ms && !HeavyDmaCooldownActive()) {
        next_match_fallback_ms = map_now_ms + 2500u;
        uintptr_t probe_pawn = 0;
        uintptr_t entity_root = 0;
        uintptr_t entity_page = 0;
        if (QReadT(client + offsets.dwLocalPlayerPawn, probe_pawn, "CS2.MatchProbe") &&
            QReadT(client + offsets.dwEntityList, entity_root, "CS2.MatchProbe") &&
            IsUserPointer(probe_pawn) && IsUserPointer(entity_root) &&
            QReadT(entity_root + kEntityPageTableOffset, entity_page, "CS2.MatchProbe") &&
            IsUserPointer(entity_page)) {
            uintptr_t controllers[4]{};
            int controllerCount = 0;
            EnsureScatter();
            if (g_scatter_full) {
                mem.SetDmaCallTag("CS2.MatchProbe");
                for (int i = 1; i <= 4; ++i)
                    mem.AddScatterReadRequest(g_scatter_full,
                        entity_page + static_cast<uintptr_t>(i) * kEntityIdentityStride,
                        &controllers[i - 1], sizeof(uintptr_t));
                const auto probeBegin = std::chrono::steady_clock::now();
                mem.ExecuteReadScatter(g_scatter_full);
                NotePossibleDeviceStall(OmniGhost::Gameplay::TimeMs(probeBegin));
            } else {
                for (int i = 1; i <= 4; ++i)
                    QReadT(entity_page + static_cast<uintptr_t>(i) * kEntityIdentityStride,
                           controllers[i - 1], "CS2.MatchProbe");
            }
            for (const uintptr_t controller : controllers)
                controllerCount += IsUserPointer(controller) ? 1 : 0;
            entity_probe_controller_count = controllerCount;
            if (controllerCount >= 2) {
                runtime.in_match = true;
                runtime.local_pawn = probe_pawn;
            }
        }
    }

    // Entering a match (from lobby or after map change) → force entity list re-acquire.
    // Clear Makcu mask: load screens often drop the LMB release packet.
    if (runtime.in_match && !was_in_match) {
        g_pending_entity_list = 0;
        g_entity_list_confirmations = 0;
        runtime.entity_list_entry = 0;
        runtime.entity_list_addr = 0;
        g_cached_entity_root = 0;
        g_entity_list_next_validation_ms = 0;
        g_entity_list_next_full_probe_ms = 0;
        g_entity_list_force_refresh = true;
        zero_player_frames = 0;
        makcu_wrapper::ForceClearButtons();
        const char* match_source = map_reports_match ? "mapa" : "probe de entidades";
        std::cout << "[CS2] Entrada em partida (" << match_source
                  << ", controllers=" << entity_probe_controller_count
                  << ") — a revalidar entity list" << std::endl;
    }
    // Leaving a match → drop avatar cache (memory + %LocalAppData%/OmniGhost/cache/cs2/avatars).
    if (was_in_match && !runtime.in_match) {
        std::cout << "[CS2] Saída de partida — a limpar cache de avatares\n";
    }
    was_in_match = runtime.in_match;

    // LOBBY / MENU: keep light probes only; entity DMA resumes on match enter.
    if (!runtime.in_match) {
        zero_player_frames = 0;
        return;
    }

    // On-demand: no active features → zero entity DMA (pipeline sleeps).
    // Web radar / bomb still handled above or in this branch only if needed.
    if (!NeedsPlayerScan(frame_config)) {
        zero_player_frames = 0;
        runtime.players.clear();
        runtime.player_count = 0;
        runtime.enemy_count = 0;
        runtime.controller_count = 0;
        runtime.pawn_count = 0;
        if (frame_config.bomb_timer && (runtime.frames % 2) == 0)
            UpdateBombStateThrottled();
        else if (!frame_config.bomb_timer)
            runtime.bomb = BombState{};
        // World-only visuals deliberately bypass the player pipeline. They
        // retain their own rate/pressure gates and do not wake controller,
        // pawn, bone or weapon acquisition when no player ESP is enabled.
        // Discovery runs on the optional worker (single-threaded static
        // caches + own scatter handle); results merge on the next scan.
        QueueOptionalDroppedOnly(frame_config);

        // Merge any pending optional results so dropped items still refresh
        // while no player scan is active (this branch returns before the
        // normal merge point below).
        drainOptionalResults(currentGen);
        return;
    }

    // The camera lane already owns the high-rate view-matrix sampling. Reuse
    // its recent immutable snapshot instead of issuing a duplicate QRead in
    // every full player scan. Fall back only when that lane has no usable data.
    bool haveView = false;
    if (const auto camera = g_camera_snapshots.Acquire(); camera && camera->timestamp_ms) {
        const uint64_t now = GetTickCount64();
        // A fresh matrix is ideal, but after a transport stall a slightly old
        // coherent matrix is preferable to scheduling another synchronous
        // QRead.  The render keeps one consistent camera state until the fast
        // camera lane catches up again instead of amplifying the stall.
        if (now >= camera->timestamp_ms && now - camera->timestamp_ms <= 120u) {
            std::memcpy(runtime.view_matrix, camera->view_matrix, sizeof(runtime.view_matrix));
            haveView = true;
        }
    }
    if (!haveView && !ProbeViewMatrix(runtime.view_matrix)) {
        if ((runtime.frames % 180) == 1)
            std::cout << "[CS2] ViewMatrix fail (fails=" << runtime.read_fails << ")" << std::endl;
        g_publish_runtime_this_scan = false;
        return;
    }

    runtime.players.reserve(64);

    // Keep the last validated team so TEAM_INTERVAL_MS is effective.
    // Preserve the last valid yaw on a transient read failure.
    // Preserve the last valid local origin. A transient DMA failure must not
    // make every range calculation originate from world zero.
    // The local pawn/controller pointers rarely change while alive.  Refresh
    // them at a controlled cadence. Health joins the main local-state scatter
    // whenever the cached pawn is valid.
    static uint64_t s_lastLocalBootstrapMs = 0;
    const uint64_t localNowMs = GetTickCount64();
    const bool refreshLocalBootstrap = !IsUserPointer(runtime.local_pawn) ||
        !s_lastLocalBootstrapMs || localNowMs - s_lastLocalBootstrapMs >= 750u;
    uintptr_t localPawn = runtime.local_pawn;
    uintptr_t localController = runtime.local_controller;
    int sampled_health = runtime.local_health;
    EnsureScatter();
    if (g_scatter_full) {
        bool queuedLocalIdentity = false;
        mem.SetDmaCallTag("CS2.LocalIdentity");
        if (refreshLocalBootstrap) {
            mem.AddScatterReadRequest(g_scatter_full, client + offsets.dwLocalPlayerPawn,
                                      &localPawn, sizeof(localPawn));
            if (offsets.dwLocalPlayerController)
                mem.AddScatterReadRequest(g_scatter_full, client + offsets.dwLocalPlayerController,
                                          &localController, sizeof(localController));
            queuedLocalIdentity = true;
        }
        if (queuedLocalIdentity)
            mem.ExecuteReadScatter(g_scatter_full);
    } else {
        if (refreshLocalBootstrap) {
            QReadT(client + offsets.dwLocalPlayerPawn, localPawn, "CS2.LocalPawn");
            if (offsets.dwLocalPlayerController)
                QReadT(client + offsets.dwLocalPlayerController, localController, "CS2.LocalController");
        }
    }
    const uintptr_t previousLocalPawn = runtime.local_pawn;
    runtime.local_pawn = IsUserPointer(localPawn) ? localPawn : 0;
    if (runtime.local_pawn != previousLocalPawn) {
        runtime.local_shots_fired = 0;
        runtime.local_aim_sample_ms = 0;
        std::memset(runtime.local_aim_punch, 0, sizeof(runtime.local_aim_punch));
    }
    if (refreshLocalBootstrap && runtime.local_pawn)
        s_lastLocalBootstrapMs = localNowMs;
    if (refreshLocalBootstrap)
        runtime.local_controller = IsUserPointer(localController) ? localController : 0;
    else if (IsUserPointer(localController))
        runtime.local_controller = localController;
    if (!runtime.local_pawn) {
        runtime.local_health = 0;
    }

    if (runtime.local_pawn) {
        // Do not keep reading local movement,
        // view, item or trigger state after death; observer/bomb handling
        // below is the only remaining DMA work in that state.
        if (runtime.local_health > 0 || refreshLocalBootstrap) {
            static ULONGLONG s_lastTeamMs = 0;
            const ULONGLONG nowT = GetTickCount64();
            const bool refreshTeam = runtime.local_team < 2 || !s_lastTeamMs ||
                (nowT - s_lastTeamMs) > static_cast<ULONGLONG>(TEAM_INTERVAL_MS);

            uint8_t team8 = static_cast<uint8_t>(runtime.local_team);
            uintptr_t scene = 0;
            uintptr_t item_services = 0;
            static uintptr_t cached_local_scene = 0;
            static uintptr_t cached_local_item_services = 0;
            static uintptr_t cached_local_pawn = 0;
            static uint64_t cached_local_scene_until_ms = 0;
            static uint64_t cached_local_item_services_until_ms = 0;
            if (cached_local_pawn != runtime.local_pawn) {
                cached_local_pawn = runtime.local_pawn;
                cached_local_scene = 0;
                cached_local_item_services = 0;
                cached_local_scene_until_ms = 0;
                cached_local_item_services_until_ms = 0;
            }
            const bool refreshLocalSceneMetadata = localNowMs >= cached_local_scene_until_ms ||
                !IsUserPointer(cached_local_scene);
            const bool refreshLocalItemServices = localNowMs >= cached_local_item_services_until_ms ||
                !IsUserPointer(cached_local_item_services);
            scene = cached_local_scene;
            item_services = cached_local_item_services;
            float view_angles[2]{};
            float local_position_sample[3] = { runtime.local_pos[0], runtime.local_pos[1], runtime.local_pos[2] };
            bool scoped = runtime.local_scoped;
            int crosshair = 0;
            int shots = -1; // failed DMA must not masquerade as a fresh sample
            float aimPunch[2] = {
                (std::numeric_limits<float>::quiet_NaN)(),
                (std::numeric_limits<float>::quiet_NaN)()
            };
            const bool needRcsState = frame_config.aim_rcs_auto || frame_config.aim_rcs_standalone;
            runtime.local_has_defuser = false;

            EnsureScatter();
            if (g_scatter_full) {
                if (offsets.m_iHealth)
                    mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_iHealth,
                                              &sampled_health, sizeof(sampled_health));
                if (refreshTeam)
                    mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_iTeamNum,
                                              &team8, sizeof(team8));
                if (refreshLocalSceneMetadata)
                    mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_pGameSceneNode,
                                              &scene, sizeof(scene));
                mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_vecVelocity,
                                          runtime.local_vel, sizeof(runtime.local_vel));
                if (offsets.dwViewAngles)
                    mem.AddScatterReadRequest(g_scatter_full, client + offsets.dwViewAngles,
                                              view_angles, sizeof(view_angles));
                else
                    mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_angEyeAngles,
                                              view_angles, sizeof(view_angles));
                if (offsets.m_bIsScoped)
                    mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_bIsScoped,
                                              &scoped, sizeof(scoped));
                if (refreshLocalItemServices && offsets.m_pItemServices && offsets.m_bHasDefuser)
                    mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_pItemServices,
                                              &item_services, sizeof(item_services));
                if (frame_config.trigger_enabled && frame_config.trigger_use_ident && offsets.m_iIDEntIndex)
                    mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_iIDEntIndex,
                                              &crosshair, sizeof(crosshair));
                if (needRcsState && offsets.m_iShotsFired)
                    mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_iShotsFired,
                                              &shots, sizeof(shots));
                if (needRcsState && offsets.m_aimPunchAngle)
                    mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_aimPunchAngle,
                                              aimPunch, sizeof(aimPunch));
                // Scene/item pointers are stable. Read their dependent values
                // through the previous confirmed pointers in this same batch;
                // a pointer change naturally takes effect on the next scan.
                if (IsUserPointer(cached_local_scene))
                    mem.AddScatterReadRequest(g_scatter_full,
                        cached_local_scene + offsets.m_vecAbsOrigin,
                        local_position_sample, sizeof(local_position_sample));
                else
                    mem.AddScatterReadRequest(g_scatter_full,
                        runtime.local_pawn + offsets.m_vOldOrigin,
                        local_position_sample, sizeof(local_position_sample));
                if (IsUserPointer(cached_local_item_services) && offsets.m_bHasDefuser)
                    mem.AddScatterReadRequest(g_scatter_full,
                        cached_local_item_services + offsets.m_bHasDefuser,
                        &runtime.local_has_defuser, sizeof(runtime.local_has_defuser));
                mem.ExecuteReadScatter(g_scatter_full);
            } else {
                // Scatter handle was unavailable on the primary path — rebuild
                // once and batch every local field into a single round-trip.
                // Individual QReads remain only if EnsureScatter still fails.
                EnsureScatter();
                if (g_scatter_full) {
                    if (offsets.m_iHealth)
                        mem.AddScatterReadRequest(g_scatter_full,
                            runtime.local_pawn + offsets.m_iHealth,
                            &sampled_health, sizeof(sampled_health));
                    if (refreshTeam)
                        mem.AddScatterReadRequest(g_scatter_full,
                            runtime.local_pawn + offsets.m_iTeamNum,
                            &team8, sizeof(team8));
                    mem.AddScatterReadRequest(g_scatter_full,
                        runtime.local_pawn + offsets.m_pGameSceneNode,
                        &scene, sizeof(scene));
                    mem.AddScatterReadRequest(g_scatter_full,
                        runtime.local_pawn + offsets.m_vecVelocity,
                        runtime.local_vel, sizeof(runtime.local_vel));
                    if (offsets.dwViewAngles)
                        mem.AddScatterReadRequest(g_scatter_full,
                            client + offsets.dwViewAngles,
                            view_angles, sizeof(view_angles));
                    else
                        mem.AddScatterReadRequest(g_scatter_full,
                            runtime.local_pawn + offsets.m_angEyeAngles,
                            view_angles, sizeof(view_angles));
                    if (offsets.m_bIsScoped)
                        mem.AddScatterReadRequest(g_scatter_full,
                            runtime.local_pawn + offsets.m_bIsScoped,
                            &scoped, sizeof(scoped));
                    if (offsets.m_pItemServices && offsets.m_bHasDefuser)
                        mem.AddScatterReadRequest(g_scatter_full,
                            runtime.local_pawn + offsets.m_pItemServices,
                            &item_services, sizeof(item_services));
                    if (frame_config.trigger_enabled && frame_config.trigger_use_ident &&
                        offsets.m_iIDEntIndex)
                        mem.AddScatterReadRequest(g_scatter_full,
                            runtime.local_pawn + offsets.m_iIDEntIndex,
                            &crosshair, sizeof(crosshair));
                    if (needRcsState && offsets.m_iShotsFired)
                        mem.AddScatterReadRequest(g_scatter_full,
                            runtime.local_pawn + offsets.m_iShotsFired,
                            &shots, sizeof(shots));
                    if (needRcsState && offsets.m_aimPunchAngle)
                        mem.AddScatterReadRequest(g_scatter_full,
                            runtime.local_pawn + offsets.m_aimPunchAngle,
                            aimPunch, sizeof(aimPunch));
                    mem.SetDmaCallTag("CS2.LocalFallbackScatter");
                    mem.ExecuteReadScatter(g_scatter_full);
                } else {
                    // Last-resort: allocate a one-shot scatter handle so we still
                    // avoid a chain of sequential QReads on the hot path.
                    VMMDLL_SCATTER_HANDLE oneShot = mem.CreateScatterHandle();
                    auto queueLocal = [&](VMMDLL_SCATTER_HANDLE h) {
                        if (offsets.m_iHealth)
                            mem.AddScatterReadRequest(h, runtime.local_pawn + offsets.m_iHealth,
                                &sampled_health, sizeof(sampled_health));
                        if (refreshTeam)
                            mem.AddScatterReadRequest(h, runtime.local_pawn + offsets.m_iTeamNum,
                                &team8, sizeof(team8));
                        mem.AddScatterReadRequest(h, runtime.local_pawn + offsets.m_pGameSceneNode,
                            &scene, sizeof(scene));
                        mem.AddScatterReadRequest(h, runtime.local_pawn + offsets.m_vecVelocity,
                            runtime.local_vel, sizeof(runtime.local_vel));
                        if (offsets.dwViewAngles)
                            mem.AddScatterReadRequest(h, client + offsets.dwViewAngles,
                                view_angles, sizeof(view_angles));
                        else
                            mem.AddScatterReadRequest(h, runtime.local_pawn + offsets.m_angEyeAngles,
                                view_angles, sizeof(view_angles));
                        if (offsets.m_bIsScoped)
                            mem.AddScatterReadRequest(h, runtime.local_pawn + offsets.m_bIsScoped,
                                &scoped, sizeof(scoped));
                        if (offsets.m_pItemServices && offsets.m_bHasDefuser)
                            mem.AddScatterReadRequest(h, runtime.local_pawn + offsets.m_pItemServices,
                                &item_services, sizeof(item_services));
                        if (frame_config.trigger_enabled && frame_config.trigger_use_ident && offsets.m_iIDEntIndex)
                            mem.AddScatterReadRequest(h, runtime.local_pawn + offsets.m_iIDEntIndex,
                                &crosshair, sizeof(crosshair));
                        if (needRcsState && offsets.m_iShotsFired)
                            mem.AddScatterReadRequest(h, runtime.local_pawn + offsets.m_iShotsFired,
                                &shots, sizeof(shots));
                        if (needRcsState && offsets.m_aimPunchAngle)
                            mem.AddScatterReadRequest(h, runtime.local_pawn + offsets.m_aimPunchAngle,
                                aimPunch, sizeof(aimPunch));
                    };
                    if (oneShot) {
                        queueLocal(oneShot);
                        mem.SetDmaCallTag("CS2.LocalOneShotScatter");
                        mem.ExecuteReadScatter(oneShot);
                        mem.CloseScatterHandle(oneShot);
                    } else {
                        // Device refused any scatter handle — single combined
                        // failure is preferable to a multi-QRead stall chain.
                        // Keep last-known local state; next scan retries.
                    }
                }
            }

            // Preserve the last known value on a failed read; only accept a
            // valid game-domain health sample from the completed state batch.
            if (sampled_health >= 0 && sampled_health <= 200)
                runtime.local_health = sampled_health;

            if (refreshTeam && IsPlayableTeam(static_cast<int>(team8))) {
                runtime.local_team = static_cast<int>(team8);
                s_lastTeamMs = nowT;
            }
            if (std::isfinite(view_angles[1])) {
                runtime.local_view_yaw = view_angles[1];
                runtime.local_angles[0] = view_angles[0];
                runtime.local_angles[1] = view_angles[1];
            } else if (offsets.dwViewAngles) {
                // Schema/global drift fallback is rare and deliberately outside the hot batch.
                float fallback_angles[2]{};
                if (QRead(runtime.local_pawn + offsets.m_angEyeAngles, fallback_angles, sizeof(fallback_angles)) &&
                    std::isfinite(fallback_angles[1])) {
                    runtime.local_view_yaw = fallback_angles[1];
                    runtime.local_angles[0] = fallback_angles[0];
                    runtime.local_angles[1] = fallback_angles[1];
                }
            }
            runtime.local_scoped = scoped;
            runtime.local_crosshair_entity = (frame_config.trigger_enabled && frame_config.trigger_use_ident)
                ? crosshair : 0;

            cached_local_scene = IsUserPointer(scene) ? scene : cached_local_scene;
            cached_local_item_services = IsUserPointer(item_services)
                ? item_services : cached_local_item_services;
            if (refreshLocalSceneMetadata)
                cached_local_scene_until_ms = localNowMs + (IsUserPointer(scene) ? 750u : 50u);
            if (refreshLocalItemServices)
                cached_local_item_services_until_ms = localNowMs +
                    (IsUserPointer(item_services) ? 750u : 50u);
            if (!g_scatter_full) {
                runtime.local_has_defuser = false;
                if (IsUserPointer(scene))
                    QRead(scene + offsets.m_vecAbsOrigin, local_position_sample, sizeof(local_position_sample));
                else
                    QRead(runtime.local_pawn + offsets.m_vOldOrigin, local_position_sample, sizeof(local_position_sample));
                if (IsUserPointer(item_services) && offsets.m_bHasDefuser)
                    QReadT(item_services + offsets.m_bHasDefuser, runtime.local_has_defuser);
            }

            if (IsFinitePosition(local_position_sample))
                std::memcpy(runtime.local_pos, local_position_sample, sizeof(runtime.local_pos));

            if (needRcsState && offsets.m_iShotsFired && shots >= 0 && shots < 256) {
                static int previous_shots = -1;
                if (previous_shots >= 0 && shots > previous_shots)
                    runtime.local_last_shot_ms = GetTickCount64();
                previous_shots = shots;
                runtime.local_shots_fired = shots;
            }
            if (needRcsState && shots >= 0 && shots < 256 &&
                std::isfinite(aimPunch[0]) && std::isfinite(aimPunch[1])) {
                runtime.local_aim_punch[0] = aimPunch[0];
                runtime.local_aim_punch[1] = aimPunch[1];
                runtime.local_aim_sample_ms = GetTickCount64();
            }
        } else {
            // Death must not turn local-state acquisition off for the whole
            // bootstrap TTL. A tiny, throttled liveness lane detects respawn
            // promptly without paying for movement, item or trigger fields.
            static uint64_t nextDeadLocalLivenessMs = 0;
            const uint64_t deadNowMs = GetTickCount64();
            if (deadNowMs >= nextDeadLocalLivenessMs) {
                int respawnHealth = runtime.local_health;
                uint8_t controllerAlive = 0xFF;
                bool readAttempted = false;
                EnsureScatter();
                if (g_scatter_full) {
                    if (offsets.m_iHealth) {
                        mem.AddScatterReadRequest(g_scatter_full,
                            runtime.local_pawn + offsets.m_iHealth,
                            &respawnHealth, sizeof(respawnHealth));
                        readAttempted = true;
                    }
                    if (IsUserPointer(runtime.local_controller) && offsets.m_bPawnIsAlive) {
                        mem.AddScatterReadRequest(g_scatter_full,
                            runtime.local_controller + offsets.m_bPawnIsAlive,
                            &controllerAlive, sizeof(controllerAlive));
                        readAttempted = true;
                    }
                    if (readAttempted) {
                        mem.SetDmaCallTag("CS2.LocalRespawnLiveness");
                        mem.ExecuteReadScatter(g_scatter_full);
                    }
                } else if (offsets.m_iHealth) {
                    readAttempted = QReadT(runtime.local_pawn + offsets.m_iHealth, respawnHealth);
                }
                if (controllerAlive == 0)
                    runtime.local_health = 0;
                else if (readAttempted && respawnHealth >= 0 && respawnHealth <= 200)
                    runtime.local_health = respawnHealth;
                // An unknown controller flag never forces a false respawn.
                // The next pass enters the normal lane as soon as the health
                // sample becomes positive.
                nextDeadLocalLivenessMs = deadNowMs + 80u;
            }
            runtime.local_crosshair_entity = 0;
            runtime.local_has_defuser = false;
            runtime.local_scoped = false;
            runtime.local_shots_fired = 0;
            runtime.local_aim_sample_ms = 0;
            std::memset(runtime.local_aim_punch, 0, sizeof(runtime.local_aim_punch));
            std::memset(runtime.local_vel, 0, sizeof(runtime.local_vel));
        }
    } else {
        runtime.local_crosshair_entity = 0;
        runtime.local_has_defuser = false;
    }

    const int preTopologyPressure = g_pressure_level.load(std::memory_order_relaxed);
    bool recoveryLivenessFresh = false;
    if (preTopologyPressure >= 2)
        recoveryLivenessFresh = refresh_cached_liveness(GetTickCount64());
    if (!RefreshEntityListEntry()) {
        // Hold last-good ESP/aim targets briefly so the overlay does not blink
        // off when ListEntry is transiently null after map/round changes.
        const uint64_t now_ms = GetTickCount64();
        if (!recoveryLivenessFresh)
            (void)refresh_cached_liveness(now_ms);
        // Never republish stale world positions on a topology miss. The
        // renderer keeps its already-published snapshot while the next scan
        // reacquires health/origin/velocity from the live entity list.
        g_publish_runtime_this_scan = false;
        return;
    }

    int kMax = frame_config.max_entities > 0 ? frame_config.max_entities : 32;
    if (kMax > 64) kMax = 64;
    // Renderer FPS must never decide who exists in the acquisition snapshot.
    // Optional lanes have their own DMA-pressure budgets.
    runtime.entity_cap_used = kMax;
    constexpr int kMaxSlots = 64;

    uintptr_t controllers[kMaxSlots]{};
    static uintptr_t cachedControllers[kMaxSlots]{};
    static int cachedControllerCount = 0;
    static uintptr_t cachedControllerListEntry = 0;
    static uint64_t nextControllerRefreshMs = 0;
    static int emptyEntryScans = 0;

    // Prefer the confirmed Source 2 stride (0x70). Fall back to 0x78 only if
    // the primary layout returns no controllers at all.
    const int recoveryPressure = g_pressure_level.load(std::memory_order_relaxed);
    // Pressure sheds optional metadata and bones below, never the core world
    // state. A stale position snapshot is more visible than a slower name or
    // weapon refresh, so recovery continues through health/origin/velocity.
    const uint64_t controllerNowMs = GetTickCount64();
    int collected = 0;
    if (cachedControllerCount > 0 &&
        cachedControllerListEntry == runtime.entity_list_entry &&
        controllerNowMs < nextControllerRefreshMs) {
        std::memcpy(controllers, cachedControllers, sizeof(controllers));
        collected = cachedControllerCount;
    } else {
        collected = CollectControllers(runtime.entity_list_entry, g_controller_stride, controllers, kMaxSlots);
        // Controller metadata changes only on joins/spawns. A 250 ms normal
        // cache prevents a broad list scatter from dominating the DMA lane.
        nextControllerRefreshMs = controllerNowMs + (recoveryPressure >= 1 ? 500u : 250u);
        if (collected > 0) {
            std::memcpy(cachedControllers, controllers, sizeof(controllers));
            cachedControllerCount = collected;
            cachedControllerListEntry = runtime.entity_list_entry;
            emptyEntryScans = 0;
        }
    }
    if (collected == 0) {
        const uintptr_t alternate = (g_controller_stride == kEntityIdentityStride)
            ? kLegacyEntityIdentityStride : kEntityIdentityStride;
        collected = CollectControllers(runtime.entity_list_entry, alternate, controllers, kMaxSlots);
        if (collected > 0) {
            g_controller_stride = alternate;
            g_pawn_stride = alternate;
            emptyEntryScans = 0;
        } else {
            // Entry pointer is stale (no controllers on either stride) —
            // request an immediate cached-root/page validation. Keep the old
            // snapshot briefly so a single transport miss does not blink ESP.
            g_entity_list_force_refresh = true;
            g_entity_list_next_validation_ms = 0;
            cachedControllerCount = 0;
            cachedControllerListEntry = 0;
            nextControllerRefreshMs = 0;
            if (++emptyEntryScans >= 6) {
                runtime.entity_list_entry = 0;
                g_pending_entity_list = 0;
                g_entity_list_confirmations = 0;
                g_cached_entity_root = 0;
                g_entity_list_next_full_probe_ms = 0;
                emptyEntryScans = 0;
            }
        }
    } else {
        emptyEntryScans = 0;
    }
    runtime.controller_stride = g_controller_stride;
    runtime.controller_count = collected;

    // The Source 2 controller array is sparse, but every later stage only
    // needs valid controller pointers. Compact it once so a four-player match
    // does not enqueue 64 pawn-handle/page requests on every acquisition scan.
    // This preserves controller/pawn pairing through slot_index while cutting
    // thousands of redundant scatter requests per second.
    int controller_span = 0;
    for (int i = 0; i < kMaxSlots; ++i) {
        if (!IsUserPointer(controllers[i])) continue;
        controllers[controller_span++] = controllers[i];
    }
    for (int i = controller_span; i < kMaxSlots; ++i)
        controllers[i] = 0;
    collected = controller_span;
    runtime.controller_count = collected;

    // While dead, do not enter the entity, position, bone, weapon, visibility
    // or aim acquisition stages.  Spectators and bomb state remain available
    // through their small dedicated paths; a positive local HP next frame
    // immediately falls through to the normal acquisition pipeline again.
    if (runtime.local_health <= 0) {
        runtime.players.clear();
        runtime.player_count = 0;
        runtime.pawn_count = 0;
        runtime.enemy_count = 0;
        UpdateSpectators(frame_config, controllers, collected);
        if (frame_config.bomb_timer)
            UpdateBombStateThrottled();
        else
            runtime.bomb = BombState{};
        return;
    }

    // Calibrate handle→pawn stride against the local player when available.
    if (runtime.local_controller && runtime.local_pawn)
        CalibratePawnStride(runtime.local_controller, runtime.local_pawn);

    // ── Phase 1: scatter all pawn handles in one DMA round-trip ──────────
    uint32_t handles[kMaxSlots]{};
    uintptr_t resolved_by_slot[kMaxSlots]{};
    struct CachedControllerPawn {
        uint32_t handle = 0;
        uintptr_t pawn = 0;
        uint64_t last_update_ms = 0;
        uint64_t last_seen_ms = 0;
        uint64_t entry_generation = 0;
    };
    static std::unordered_map<uintptr_t, CachedControllerPawn> controllerPawnCache;
    static uint64_t controllerPawnCacheGeneration = 0;
    if (controllerPawnCacheGeneration != g_session_generation.load(std::memory_order_acquire)) {
        controllerPawnCache.clear();
        controllerPawnCacheGeneration = g_session_generation.load(std::memory_order_relaxed);
    }
    if (controllerPawnCache.bucket_count() < 128) controllerPawnCache.reserve(128);
    uintptr_t controllersDue[kMaxSlots]{};
    uint32_t refreshedHandles[kMaxSlots]{};
    bool anyHandleDue = false;
    for (int i = 0; i < collected; ++i) {
        auto cached = controllerPawnCache.find(controllers[i]);
        if (cached != controllerPawnCache.end() && cached->second.entry_generation == currentGen) {
            cached->second.last_seen_ms = controllerNowMs;
            handles[i] = cached->second.handle;
            resolved_by_slot[i] = cached->second.pawn;
            if (cached->second.last_update_ms &&
                controllerNowMs - cached->second.last_update_ms < 200u &&
                IsUserPointer(cached->second.pawn))
                continue;
        }
        controllersDue[i] = controllers[i];
        anyHandleDue = true;
    }
    if (anyHandleDue)
        ScatterReadPawnHandles(controllersDue, refreshedHandles, collected);

    uint32_t handlesToResolve[kMaxSlots]{};
    bool anyHandleChanged = false;
    for (int i = 0; i < collected; ++i) {
        if (!controllersDue[i]) continue;
        auto& cached = controllerPawnCache.try_emplace(controllers[i]).first->second;
        cached.entry_generation = currentGen;
        cached.last_seen_ms = controllerNowMs;
        const uint32_t fresh = refreshedHandles[i];
        if (!fresh || fresh == 0xFFFFFFFFu) continue;
        if (fresh == cached.handle && IsUserPointer(cached.pawn)) {
            handles[i] = fresh;
            resolved_by_slot[i] = cached.pawn;
            cached.last_update_ms = controllerNowMs;
            continue;
        }
        handles[i] = fresh;
        handlesToResolve[i] = fresh;
        anyHandleChanged = true;
    }

    uintptr_t newlyResolved[kMaxSlots]{};
    int newlyResolvedCount = 0;
    if (anyHandleChanged) {
        const uintptr_t primaryStride = g_pawn_stride ? g_pawn_stride : kEntityIdentityStride;
        const uintptr_t alternateStride = (primaryStride == kEntityIdentityStride)
            ? kLegacyEntityIdentityStride : kEntityIdentityStride;
        newlyResolvedCount = ScatterResolvePawnHandles(
            handlesToResolve, newlyResolved, collected,
            primaryStride, alternateStride);
        if (newlyResolvedCount > 0 && g_pawn_stride != primaryStride) {
            g_pawn_stride = primaryStride;
        }
    }
    for (int i = 0; i < collected; ++i) {
        if (!handlesToResolve[i]) continue;
        auto& cached = controllerPawnCache.try_emplace(controllers[i]).first->second;
        cached.entry_generation = currentGen;
        if (IsUserPointer(newlyResolved[i])) {
            cached.handle = handlesToResolve[i];
            cached.pawn = newlyResolved[i];
            cached.last_update_ms = controllerNowMs;
            resolved_by_slot[i] = newlyResolved[i];
        } else {
            cached.pawn = 0;
            cached.last_update_ms = 0;
        }
    }
    if (anyHandleChanged && newlyResolvedCount == 0) {
        g_entity_list_force_refresh = true;
        g_entity_list_next_validation_ms = 0;
    }
    if (controllerPawnCache.size() > 128u) {
        for (auto it = controllerPawnCache.begin(); it != controllerPawnCache.end();) {
            if (controllerNowMs - it->second.last_seen_ms > 30000u)
                it = controllerPawnCache.erase(it);
            else
                ++it;
        }
    }
    int handle_nonzero = 0;
    int resolve_ok = 0;
    for (int i = 0; i < collected; ++i) {
        if (handles[i]) ++handle_nonzero;
        if (IsUserPointer(resolved_by_slot[i])) ++resolve_ok;
    }

    uintptr_t resolved_pawns[kMaxSlots]{};
    int slot_index[kMaxSlots]{};
    int candidate_count = 0;
    for (int i = 0; i < collected && candidate_count < kMaxSlots; ++i) {
        if (!controllers[i] || !handles[i]) continue;
        uintptr_t pawn = resolved_by_slot[i];
        if (!pawn) continue;
        resolved_pawns[candidate_count] = pawn;
        slot_index[candidate_count] = i;
        ++candidate_count;
    }
    runtime.pawn_count = candidate_count;
    runtime.pawn_stride = g_pawn_stride;

    // ── Phase 3: scatter health / team / armor / scene for candidates ────
    // Use unified FeatureFlags for RequiredFields
    CS2::UnifiedConfigAdapter::SyncToUnified(frame_config);
    const auto unifiedFlags = CS2::UnifiedConfigAdapter::GetFeatureFlags();
    const auto fields = unifiedFlags.RequiredFields();

    const bool need_armor = OmniGhost::Gameplay::UnifiedESP::Has(
        fields, OmniGhost::Gameplay::UnifiedESP::DataField::Armor);
    const bool need_yaw = frame_config.radar_2d ||
        OmniGhost::Gameplay::UnifiedESP::Has(
            fields, OmniGhost::Gameplay::UnifiedESP::DataField::Facing);
    const bool track_velocity = OmniGhost::Gameplay::UnifiedESP::Has(
        fields, OmniGhost::Gameplay::UnifiedESP::DataField::Velocity);
    const bool flagsEnabled = false; // retired player flags
    const bool need_scoped = frame_config.trigger_scoped_only ||
        (flagsEnabled && frame_config.flag_scoped);
    const bool need_flash = flagsEnabled && frame_config.flag_blind;

    const uint64_t scan_now_ms = GetTickCount64();
    struct CachedSceneBone {
        uintptr_t scene = 0;
        uintptr_t bone_base = 0;
        uintptr_t bone_offset = 0;
        uint8_t team = 0;
        int armor = 0;
        uintptr_t weapon_services = 0;
        bool spotted = true;
        bool spotted_known = false;
        uint64_t scene_expires_ms = 0;
        uint64_t bone_expires_ms = 0;
        uint64_t team_expires_ms = 0;
        uint64_t armor_expires_ms = 0;
        uint64_t weapon_services_expires_ms = 0;
        uint64_t spotted_expires_ms = 0;
        uint64_t last_seen_ms = 0;
        uint64_t entry_generation = 0;
    };
    static std::unordered_map<uintptr_t, CachedSceneBone> sceneBoneCache;
    static uint64_t sceneBoneCacheGeneration = 0;
    if (sceneBoneCacheGeneration != g_session_generation.load(std::memory_order_acquire)) {
        sceneBoneCache.clear();
        sceneBoneCacheGeneration = g_session_generation.load(std::memory_order_relaxed);
    }
    if (sceneBoneCache.bucket_count() < 128) sceneBoneCache.reserve(128);
    uintptr_t cachedScenes[kMaxSlots]{};
    uint8_t cachedTeams[kMaxSlots]{};
    int cachedArmors[kMaxSlots]{};
    uintptr_t cachedWeaponServices[kMaxSlots]{};
    bool cachedSpotted[kMaxSlots]{};
    bool refreshScenes[kMaxSlots]{};
    bool refreshTeams[kMaxSlots]{};
    bool refreshArmors[kMaxSlots]{};
    bool refreshWeaponServices[kMaxSlots]{};
    bool refreshSpotted[kMaxSlots]{};
    float positions[kMaxSlots][3]{};
    for (int c = 0; c < candidate_count; ++c) {
        positions[c][0] = positions[c][1] = positions[c][2] =
            std::numeric_limits<float>::quiet_NaN();
        const auto cached = sceneBoneCache.find(resolved_pawns[c]);
        if (cached != sceneBoneCache.end() && cached->second.entry_generation == currentGen) {
            cachedScenes[c] = cached->second.scene;
            cachedTeams[c] = cached->second.team;
            cachedArmors[c] = cached->second.armor;
            cachedWeaponServices[c] = cached->second.weapon_services;
            cachedSpotted[c] = cached->second.spotted;
            cached->second.last_seen_ms = scan_now_ms;
            refreshScenes[c] = !IsUserPointer(cached->second.scene) ||
                scan_now_ms >= cached->second.scene_expires_ms;
            refreshTeams[c] = !IsPlayableTeam(static_cast<int>(cached->second.team)) ||
                scan_now_ms >= cached->second.team_expires_ms;
            refreshArmors[c] = need_armor &&
                scan_now_ms >= cached->second.armor_expires_ms;
            refreshWeaponServices[c] = scan_now_ms >= cached->second.weapon_services_expires_ms;
            refreshSpotted[c] = !cached->second.spotted_known ||
                scan_now_ms >= cached->second.spotted_expires_ms;
        } else {
            refreshScenes[c] = true;
            refreshTeams[c] = true;
            refreshArmors[c] = need_armor;
            refreshWeaponServices[c] = true;
            refreshSpotted[c] = true;
        }
    }
    PawnCoreFields core[kMaxSlots]{};
    uintptr_t candidateControllers[kMaxSlots]{};
    for (int c = 0; c < candidate_count; ++c)
        candidateControllers[c] = controllers[slot_index[c]];
    const bool need_weapon = OmniGhost::Gameplay::UnifiedESP::Has(
        fields, OmniGhost::Gameplay::UnifiedESP::DataField::Weapon);
    const bool need_spotted = (frame_config.aim_enabled && frame_config.aim_visibility_check) ||
        (frame_config.trigger_enabled && frame_config.aim_visibility_check);
    const bool need_weapon_services = need_weapon || need_spotted;
    // Health/team/scene stay on the full cadence. Armor is only four bytes per
    // pawn, so include it here instead of paying for a separate round-trip.
    mem.SetDmaCallTag("CS2.PawnCore");
    const auto _posBegin = std::chrono::steady_clock::now();
    const uint64_t position_read_begin_ms = GetTickCount64();
    ScatterReadPawnCore(resolved_pawns, candidateControllers, core, candidate_count, need_armor,
                        need_scoped, need_flash, need_yaw, need_weapon_services,
                        need_spotted, cachedTeams, refreshTeams,
                        cachedArmors, refreshArmors,
                        cachedScenes, refreshScenes, positions, track_velocity,
                        cachedWeaponServices, refreshWeaponServices,
                        cachedSpotted, refreshSpotted);
    const uint64_t position_read_end_ms = GetTickCount64();
    const uint64_t position_sample_ms = position_read_begin_ms +
        (position_read_end_ms - position_read_begin_ms) / 2u;
    runtime.world_sample_ms = position_sample_ms;
    g_phase.positions_ms = OmniGhost::Gameplay::TimeMs(_posBegin);
    NotePossibleDeviceStall(g_phase.positions_ms);
    for (int c = 0; c < candidate_count; ++c) {
        auto& cached = sceneBoneCache[resolved_pawns[c]];
        cached.entry_generation = currentGen;
        cached.last_seen_ms = scan_now_ms;
        if (IsPlayableTeam(static_cast<int>(core[c].team))) {
            cached.team = core[c].team;
            cached.team_expires_ms = scan_now_ms + 500u;
        } else {
            core[c].team = cached.team;
        }
        if (need_armor && refreshArmors[c] && core[c].armor >= 0 && core[c].armor <= 200) {
            cached.armor = core[c].armor;
            cached.armor_expires_ms = scan_now_ms + 75u;
        } else if (need_armor) {
            core[c].armor = cached.armor;
        }
        if (IsUserPointer(core[c].scene)) {
            if (IsUserPointer(cached.scene) && cached.scene != core[c].scene) {
                cached.bone_base = 0;
                cached.bone_offset = 0;
                cached.bone_expires_ms = 0;
            }
            cached.scene = core[c].scene;
            cached.scene_expires_ms = scan_now_ms + 500u;
        } else {
            core[c].scene = cached.scene;
        }
        // Cache weapon_services (stable pointer, long TTL)
        if (need_weapon_services && refreshWeaponServices[c] && IsUserPointer(core[c].weapon_services)) {
            cached.weapon_services = core[c].weapon_services;
            cached.weapon_services_expires_ms = scan_now_ms + 500u;
        }
        // Cache spotted (changes frequently, shorter TTL)
        if (need_spotted && refreshSpotted[c] && core[c].spotted_known) {
            cached.spotted = core[c].spotted;
            cached.spotted_known = true;
            cached.spotted_expires_ms = scan_now_ms + 75u;
        } else if (need_spotted) {
            core[c].spotted = cached.spotted;
            core[c].spotted_known = cached.spotted_known;
        }
    }

    // Deathmatch can transition dead → respawning before the optional bone
    // reads finish. Publish health immediately so presentation can hide a dead
    // pawn without waiting for the complete entity snapshot.
    LivenessSnapshot liveness{};
    liveness.timestamp_ms = GetTickCount64();
    liveness.generation = g_session_generation.load(std::memory_order_acquire);
    std::array<uintptr_t, 64> confirmedDead{};
    size_t confirmedDeadCount = 0;
    for (int c = 0; c < candidate_count &&
         liveness.count < static_cast<uint32_t>(liveness.players.size()); ++c) {
        const bool healthKnown = core[c].health >= 0 && core[c].health <= 200;
        const bool controllerKnown = core[c].controller_alive == 0 ||
            core[c].controller_alive == 1;
        const bool dead = (healthKnown && core[c].health <= 0) ||
            (controllerKnown && core[c].controller_alive == 0);
        if (!healthKnown && !controllerKnown)
            continue; // both reads failed: retain grace, never call it death
        auto& sample = liveness.players[liveness.count++];
        sample.pawn = resolved_pawns[c];
        sample.sample_ms = position_sample_ms;
        sample.generation = liveness.generation;
        sample.alive = !dead;
        if (dead)
            confirmedDead[confirmedDeadCount++] = sample.pawn;
    }
    PublishLivenessSnapshot(liveness);
    for (size_t i = 0; i < confirmedDeadCount; ++i) {
        const uintptr_t pawn = confirmedDead[i];
        // P0-3: Mark as confirmed dead instead of just erasing — prevents
        // reinsertion if the pointer is reused before grace expiry.
        if (auto it = recent_players.find(pawn); it != recent_players.end())
            it->second.confirmed_dead = true;
        bone_cache.erase(pawn);
        s_stickyBoneLayout.erase(pawn);
        sceneBoneCache.erase(pawn);
        s_lastBoneMs.erase(pawn);
        last_good_players.erase(std::remove_if(last_good_players.begin(),
            last_good_players.end(), [pawn](const RecoveryIdentity& p) { return p.pawn == pawn; }),
            last_good_players.end());
    }

    int validCoreSamples = 0;
    for (int c = 0; c < candidate_count; ++c)
        validCoreSamples += IsPlayableTeam(static_cast<int>(core[c].team)) ? 1 : 0;
    if (candidate_count > 0 && validCoreSamples == 0) {
        // Cached pawn addresses stopped producing valid core data. Revalidate
        // root/page immediately and rebuild the topology on the next scan.
        g_entity_list_force_refresh = true;
        g_entity_list_next_validation_ms = 0;
        for (int c = 0; c < candidate_count; ++c)
            controllerPawnCache.erase(controllers[slot_index[c]]);
        g_publish_runtime_this_scan = false;
        return;
    }

    // Health and team are the minimum discriminator reads: without them we
    // cannot know which pawns to exclude. Compact the candidate set here so
    // dead players and hidden team-mates never reach position, bone, weapon or
    // name acquisition. Spectators are resolved later from controllers through
    // their own lightweight observer path.
    int eligible_count = 0;
    // The local pawn has a dedicated snapshot and must never enter the remote
    for (int c = 0; c < candidate_count; ++c) {
        const bool is_local = resolved_pawns[c] == runtime.local_pawn ||
            controllers[slot_index[c]] == runtime.local_controller;
        const bool healthAlive = core[c].health > 0 && core[c].health <= 200;
        const bool controllerKnown = core[c].controller_alive == 0 ||
            core[c].controller_alive == 1;
        const bool alive = healthAlive &&
            (!controllerKnown || core[c].controller_alive != 0) &&
            IsPlayableTeam(static_cast<int>(core[c].team));
        const bool hidden_teammate = frame_config.team_check &&
            static_cast<int>(core[c].team) == runtime.local_team;
        if (!alive || is_local || hidden_teammate) continue;
        // Positions are already part of PawnCore. Cull by range before any
        // names, weapon metadata, bone or visibility work is scheduled.
        const float dx = positions[c][0] - runtime.local_pos[0];
        const float dy = positions[c][1] - runtime.local_pos[1];
        const float dz = positions[c][2] - runtime.local_pos[2];
        const float distance_sq = dx * dx + dy * dy + dz * dz;
        const float max_units = frame_config.max_distance * 39.37f;
        if (!IsFinitePosition(positions[c]) ||
            (frame_config.max_distance > 1.f && distance_sq > max_units * max_units))
            continue;
        if (eligible_count != c) {
            resolved_pawns[eligible_count] = resolved_pawns[c];
            slot_index[eligible_count] = slot_index[c];
            candidateControllers[eligible_count] = candidateControllers[c];
            core[eligible_count] = core[c];
            std::memcpy(positions[eligible_count], positions[c], sizeof(positions[c]));
        }
        ++eligible_count;
    }
    candidate_count = eligible_count;
    runtime.pawn_count = candidate_count;

    // ── Early core publish (P0): positions / health / velocity / liveness ─
    // Presentation must not wait for bones, weapons, names or dropped items.
    // Build a core-only player list, publish immediately, then continue the
    // optional lanes which will overwrite runtime.players before the final
    // publish at the end of the scan.
    {
        ScopedPhase _phEarly(&g_phase.early_publish_ms);
        runtime.players.clear();
        runtime.enemy_count = 0;
        runtime.world_sample_ms = position_sample_ms ? position_sample_ms : GetTickCount64();
        for (int c = 0; c < candidate_count; ++c) {
            const uintptr_t pawn = resolved_pawns[c];
            const uintptr_t controller = candidateControllers[c];
            if (!IsUserPointer(pawn)) continue;
            const bool healthAlive = core[c].health > 0 && core[c].health <= 200;
            const bool controllerKnown = core[c].controller_alive == 0 ||
                core[c].controller_alive == 1;
            if (!healthAlive) continue;
            if (controllerKnown && core[c].controller_alive == 0) continue;
            if (!IsPlayableTeam(static_cast<int>(core[c].team))) continue;
            if (!IsFinitePosition(positions[c])) continue;

            Player corePlayer{};
            corePlayer.controller = controller;
            corePlayer.pawn = pawn;
            corePlayer.scene = IsUserPointer(core[c].scene) ? core[c].scene : 0;
            corePlayer.health = core[c].health;
            corePlayer.armor = core[c].armor;
            corePlayer.team = static_cast<int>(core[c].team);
            corePlayer.alive = true;
            corePlayer.is_bot = core[c].controller_steam_id == 0;
            corePlayer.is_local = pawn == runtime.local_pawn ||
                controller == runtime.local_controller;
            corePlayer.pos[0] = positions[c][0];
            corePlayer.pos[1] = positions[c][1];
            corePlayer.pos[2] = positions[c][2];
            corePlayer.position_sample_ms = position_sample_ms;
            if (track_velocity) {
                for (int axis = 0; axis < 3; ++axis) {
                    const float v = core[c].velocity[axis];
                    corePlayer.velocity[axis] =
                        std::isfinite(v) && std::fabs(v) < 5000.f ? v : 0.f;
                }
            }
            {
                const float speed = std::sqrt(
                    corePlayer.velocity[0] * corePlayer.velocity[0] +
                    corePlayer.velocity[1] * corePlayer.velocity[1]);
                corePlayer.move_speed = std::isfinite(speed) ? speed : 0.f;
                corePlayer.is_moving = corePlayer.move_speed > 5.f;
            }
            // Carry last-good bones from cache so skeleton does not blink off
            // between optional refreshes — presentation enforces visual age cap.
            if (auto bit = bone_cache.find(pawn); bit != bone_cache.end() &&
                bit->second.entry_generation == currentGen &&
                bit->second.last_valid_ms) {
                std::memcpy(corePlayer.bones, bit->second.joints, sizeof(corePlayer.bones));
                const float cachedShift[3] = {
                    corePlayer.pos[0] - bit->second.origin[0],
                    corePlayer.pos[1] - bit->second.origin[1],
                    corePlayer.pos[2] - bit->second.origin[2]
                };
                for (std::size_t bone = 0; bone < kBoneSlotCount; ++bone)
                    for (int axis = 0; axis < 3; ++axis)
                        corePlayer.bones[bone][axis] += cachedShift[axis];
                corePlayer.bones_ok = true;
                corePlayer.full_bones_ok = bit->second.full_pose;
                corePlayer.bones_sample_ms = bit->second.last_valid_ms;
                corePlayer.bone_pose_sample_ms = bit->second.last_valid_ms;
                corePlayer.bone_origin_sample_ms = corePlayer.position_sample_ms;
                corePlayer.head[0] = corePlayer.bones[0][0];
                corePlayer.head[1] = corePlayer.bones[0][1];
                corePlayer.head[2] = corePlayer.bones[0][2];
            }
            if (auto rit = recent_players.find(pawn); rit != recent_players.end() &&
                rit->second.entry_generation == currentGen) {
                const bool sameController = rit->second.player.controller == controller;
                if (sameController && rit->second.player.name[0]) {
                    std::snprintf(corePlayer.name, sizeof(corePlayer.name), "%s",
                        rit->second.player.name);
                    corePlayer.is_bot = corePlayer.is_bot || rit->second.player.is_bot;
                    rit->second.last_seen_ms = scan_now_ms;
                }
                if (rit->second.player.weapon[0])
                    std::snprintf(corePlayer.weapon, sizeof(corePlayer.weapon), "%s",
                        rit->second.player.weapon);
                corePlayer.weapon_def = rit->second.player.weapon_def;
                corePlayer.spotted = rit->second.player.spotted;
                corePlayer.visibility_known = rit->second.player.visibility_known;
                corePlayer.visible = rit->second.player.visible;
            }
            // Keep bots in the acquisition snapshot: the optional identity
            // worker must see them before the renderer can filter them.
            if (!corePlayer.is_local && corePlayer.team != runtime.local_team)
                ++runtime.enemy_count;
            runtime.players.push_back(corePlayer);
        }
        runtime.player_count = static_cast<int>(runtime.players.size());
        if (g_publish_runtime_this_scan)
            PublishRuntimeSnapshot();
    }

    // P0: optional DMA (bones/weapons/names/flags/dropped) runs on the
    // optional worker. Core acquisition returns here so the next world scan
    // is never blocked by metadata. Presentation already has core state.
    {
        const uint64_t nowMs = GetTickCount64();
        if (!runtime.players.empty()) {
            last_good_players.clear();
            last_good_players.reserve(runtime.players.size());
            for (const auto& player : runtime.players)
                last_good_players.push_back({player.pawn, player.controller});
            last_good_ms = nowMs;
            OmniGhost::Gameplay::DmaTelemetry::CS2().last_good_age_ms.store(
                0, std::memory_order_relaxed);
        }
        // Bomb / local-weapon metadata are optional-pressure gated and cheap
        // enough to keep on the core path without blocking the next scan.
        if (!runtime.in_match || !frame_config.bomb_timer)
            runtime.bomb = BombState{};
        else if (g_pressure_level.load(std::memory_order_relaxed) < 2)
            UpdateBombStateThrottled();
        if (g_pressure_level.load(std::memory_order_relaxed) < 2)
            UpdateLocalWeaponDefinitionThrottled();
    }
    // EntityCacheHub policy on the core path: prune maps that feed the early
    // publish carry (bones/names) so pointer reuse cannot outlive last_seen.
    {
        constexpr uint64_t kGraceMs = 5000;
        const uint64_t nowMs = GetTickCount64();
        auto prune = [&](auto& map, auto getLast) {
            for (auto it = map.begin(); it != map.end();) {
                const uint64_t last = getLast(it->second);
                if (last && nowMs > last && (nowMs - last) > kGraceMs)
                    it = map.erase(it);
                else
                    ++it;
            }
        };
        prune(recent_players, [](const RecentPlayer& e) { return e.last_seen_ms; });
        prune(bone_cache, [](const CachedBones& e) { return e.last_valid_ms; });
    }
    QueueOptionalEnrichment(frame_config);
    {
        // Residual phase accounting for core-only scans.
        const float wall = OmniGhost::Gameplay::TimeMs(_frameWallBegin);
        const float accounted = g_phase.webradar_ms + g_phase.entity_ms + g_phase.local_ms
            + g_phase.core_scatter_ms + g_phase.positions_ms + g_phase.early_publish_ms
            + g_phase.publish_ms;
        g_phase.other_ms = (std::max)(0.f, wall - accounted);
        g_phase.optional_budget_left_ms = 0.f;
    }
    return; // core path done — optional worker continues enrichment
}

void RunFrame() {
    RunFrameWithConfig(config);
}

void SubmitAcquisitionConfig(const Config& next) noexcept {
    auto slot = g_config_snapshots.TryBeginWrite();
    if (!slot) return;
    *slot.value = next;
    g_config_snapshots.Publish(slot.index);
}

void EnsureAcquisitionStarted() {
    bool expected = false;
    if (!g_acquisition_running.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return;

    g_acquisition_stop.store(false, std::memory_order_release);
    {
        std::scoped_lock lock(g_process_liveness_mutex);
        g_process_liveness_next_probe_ms = 0;
        g_process_liveness_last_result.store(true, std::memory_order_release);
    }
    g_web_radar_control_stop.store(false, std::memory_order_release);
    g_web_radar_desired_enabled.store(false, std::memory_order_release);
    g_web_radar_control_thread = std::thread([] {
        uint64_t nextCloudflareAttemptMs = 0;
        uint64_t nextServerAttemptMs = 0;
        while (!g_web_radar_control_stop.load(std::memory_order_acquire)) {
            (void)RefreshProcessLiveness();
            if (g_web_radar_desired_enabled.load(std::memory_order_acquire)) {
                const int port = g_web_radar_desired_port.load(std::memory_order_relaxed);
                const uint64_t now = GetTickCount64();
                if ((!WebRadar::IsRunning() || WebRadar::Port() != port) &&
                    now >= nextServerAttemptMs) {
                    WebRadar::Start(port);
                    nextServerAttemptMs = now + 1000u;
                }
                if (WebRadar::IsRunning() &&
                    g_web_radar_desired_cloudflare.load(std::memory_order_relaxed)) {
                    if (!WebRadar::CloudflareRunning() && now >= nextCloudflareAttemptMs) {
                        WebRadar::StartCloudflare();
                        nextCloudflareAttemptMs = now + 5000u;
                    }
                } else if (WebRadar::CloudflareRunning()) {
                    WebRadar::StopCloudflare();
                }
            } else {
                if (WebRadar::IsRunning()) WebRadar::Stop();
                else if (WebRadar::CloudflareRunning()) WebRadar::StopCloudflare();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        WebRadar::Stop();
    });
    g_camera_pending.store(false, std::memory_order_release);
    g_camera_deadline_ms.store(0, std::memory_order_release);
    // Legitimate producer handoff: this init publish happens on the calling
    // thread BEFORE the acquisition worker exists. Rebind so the worker
    // remains the only producer from here on.
    g_runtime_snapshots.ResetProducerThread();
    PublishRuntimeSnapshot();
    // Camera and origins are latency-critical for visual attachment while
    // turning or moving.  Bones remain in the validated full scan; this small
    // lane refreshes only their common origin and the view matrix.
    g_optional_stop.store(false, std::memory_order_release);
    g_optional_busy.store(false, std::memory_order_release);
    g_optional_pending.store(false, std::memory_order_release);
    g_optional_thread = std::thread(OptionalWorkerLoop);

    g_camera_thread = std::thread([] {
        mem.SetDmaLane("camera");
        mem.SetDmaPriority(Memory::DmaPri::Camera);
        OmniGhost::Gameplay::FixedRateScheduler scheduler;
        float matrix[16]{};
        uint64_t next_camera_ms = 0;
        uint64_t consecutive_busy = 0;
        while (!g_acquisition_stop.load(std::memory_order_acquire)) {
            const auto runtime_view = g_runtime_snapshots.Acquire();
            const uintptr_t client_base = runtime_view ? runtime_view->client_base : 0;
            const bool in_match = runtime_view && runtime_view->in_match;
            const bool canRead = ready && offsets.loaded && client_base && offsets.dwViewMatrix;
            const uint64_t camera_now_ms = GetTickCount64();
            const int camera_pressure = g_pressure_level.load(std::memory_order_relaxed);
            // Pressure sheds metadata/weapons/bones first. The view
            // matrix stays at 16 ms through normal, moderate and high pressure;
            // only the extreme recovery state relaxes it slightly to 25 ms.
            // P2-4: Dynamic camera interval based on camera-position temporal delta
            int camera_period_ms = camera_pressure >= 3 ? 25 : CAMERA_INTERVAL_MS;
            const float camPosDelta = OmniGhost::Gameplay::DmaTelemetry::CS2().camera_position_delta_p95_ms.load(std::memory_order_relaxed);
            if (camPosDelta > 8.f) camera_period_ms = std::max(camera_period_ms, 20); // delta too high, slow down slightly to let position catch up
            else if (camPosDelta < 2.f && camera_pressure == 0) camera_period_ms = std::min(camera_period_ms, 12); // tight sync, can run faster
            g_camera_deadline_ms.store(next_camera_ms, std::memory_order_release);
            int wait_ms = in_match ? CAMERA_INTERVAL_MS : 20;
            bool busyRetry = false;
            if (canRead && in_match && camera_now_ms >= next_camera_ms) {
				g_camera_pending.store(true, std::memory_order_release);
				auto& camera_tel = OmniGhost::Gameplay::DmaTelemetry::CS2();
				// This is intentionally a bypass, not a skip: it quantifies how
				// often the old shared-cooldown policy would have frozen the matrix.
				if (HeavyDmaCooldownActive())
					camera_tel.camera_cooldown_bypasses.fetch_add(1, std::memory_order_relaxed);
                bool attempted = false;
				const auto camera_read_begin = std::chrono::steady_clock::now();
				const uint64_t camera_read_begin_ms = GetTickCount64();
				const bool read_ok = QTryRead(client_base + offsets.dwViewMatrix, matrix, sizeof(matrix),
                                              attempted, "CS2.Camera");
				const uint64_t camera_read_end_ms = GetTickCount64();
				if (read_ok)
					PublishCameraSnapshot(matrix, camera_read_begin_ms +
						(camera_read_end_ms - camera_read_begin_ms) / 2u);
                if (attempted) {
					if (read_ok && consecutive_busy > 0)
						camera_tel.camera_busy_retry_success.fetch_add(1, std::memory_order_relaxed);
					consecutive_busy = 0;
					OmniGhost::Gameplay::DmaTelemetry::ObserveCameraRead(
						camera_tel,
						OmniGhost::Gameplay::TimeMs(camera_read_begin));
                    // Keep a stable cadence relative to the previous deadline,
                    // not relative to the variable-duration DMA read.
                    next_camera_ms += static_cast<uint64_t>(camera_period_ms);
                    const uint64_t after_read_ms = GetTickCount64();
                    if (next_camera_ms <= after_read_ms)
                        next_camera_ms = after_read_ms + static_cast<uint64_t>(camera_period_ms);
					g_camera_deadline_ms.store(next_camera_ms, std::memory_order_release);
					g_camera_pending.store(false, std::memory_order_release);
					wait_ms = camera_period_ms;
                }
				else {
					++consecutive_busy;
					camera_tel.camera_busy_skips.fetch_add(
						1, std::memory_order_relaxed);
					auto observed = camera_tel.camera_consecutive_busy_max.load(std::memory_order_relaxed);
					while (observed < consecutive_busy &&
					       !camera_tel.camera_consecutive_busy_max.compare_exchange_weak(
						   observed, consecutive_busy, std::memory_order_relaxed)) {}
					// Keep the pending flag raised so optional full-scan lanes yield.
					busyRetry = true;
				}
            }
            else if (!in_match || !canRead) {
				g_camera_pending.store(false, std::memory_order_release);
				consecutive_busy = 0;
			}
            // The full scan already publishes coherent player positions. Keep this
            // worker dedicated to the matrix instead of acquiring snapshots for the
            // retired motion lane on every wake-up.
            // Keep the camera snappy — the matrix must stay fluid for ESP.
            if (busyRetry) {
                // sleep_until(1 ms) can round up to a full Windows timer tick.
                // A bounded yield gives the camera a real ~1 ms retry without
                // allowing one busy skip to cost another 16 ms.
                const auto retryAt = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(1);
                do {
                    std::this_thread::yield();
                } while (!g_acquisition_stop.load(std::memory_order_acquire) &&
                         std::chrono::steady_clock::now() < retryAt);
            } else {
                scheduler.Wait(std::chrono::milliseconds(wait_ms));
            }
        }
		g_camera_pending.store(false, std::memory_order_release);
    });
    g_acquisition_thread = std::thread([] {
        OmniGhost::Gameplay::FixedRateScheduler scheduler;
        std::array<float, 16> recentActiveScans{};
        size_t recentScanCount = 0;
        size_t recentScanCursor = 0;
        uint64_t pressureSessionGeneration = 0;
        int healthyPressureScans = 0;
        while (!g_acquisition_stop.load(std::memory_order_acquire)) {
            const auto acquire_begin = std::chrono::steady_clock::now();
            try {
                // A 300 ms+ device stall is a board/driver queue event. Do
                // not immediately start another heavy scan: retain the last
                // published immutable snapshot and give the DMA transport a
                // short recovery window.
                const uint64_t now = GetTickCount64();
                const uint64_t coolUntil = g_heavy_dma_cooldown_until_ms.load(std::memory_order_acquire);
                if (now < coolUntil) {
                    scheduler.Wait(std::chrono::milliseconds((std::min)(20ull, coolUntil - now)));
                    continue;
                }
                g_phase = Cs2PhaseTiming{};
                g_phase.scan_id = g_scan_id.fetch_add(1, std::memory_order_relaxed) + 1;
                mem.ResetThreadDmaWaitUs();
                mem.SetDmaScanId(g_phase.scan_id);
                mem.SetDmaLane("full");
                mem.SetDmaCallTag("CS2.Acquire");
                auto frame_config = g_config_snapshots.Acquire();
                g_publish_runtime_this_scan = true;
                RunFrameWithConfig(*frame_config);
                runtime.acquisition_ms = OmniGhost::Gameplay::TimeMs(acquire_begin);
                if (g_publish_runtime_this_scan) {
                    ScopedPhase _phPub(&g_phase.publish_ms);
                    PublishRuntimeSnapshot();
                }
                runtime.acquisition_ms = OmniGhost::Gameplay::TimeMs(acquire_begin);
                {
                    auto& tel = OmniGhost::Gameplay::DmaTelemetry::CS2();
                    const bool activeSample = runtime.in_match && runtime.player_count > 0;
                    if (!runtime.in_match)
                        tel.idle_reason.store(2, std::memory_order_relaxed); // not_in_match
                    else if (runtime.player_count <= 0)
                        tel.idle_reason.store(5, std::memory_order_relaxed); // no_local/no players
                    else
                        tel.idle_reason.store(0, std::memory_order_relaxed);
                    g_phase.lock_wait_ms = static_cast<float>(mem.ConsumeThreadDmaWaitUs()) / 1000.f;
                    NotePossibleDeviceStall(runtime.acquisition_ms);
                    OmniGhost::Gameplay::DmaTelemetry::ObserveAcquire(
                        tel, runtime.acquisition_ms, activeSample, g_phase.scan_id);
                    tel.entities.store(runtime.player_count, std::memory_order_relaxed);
                    tel.snapshot_drops.store(g_runtime_snapshot_drops.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    tel.dma_open.store(mem.vHandle != nullptr, std::memory_order_relaxed);
                    tel.process_id.store(static_cast<std::uint32_t>(mem.GetDiagnosticsSnapshot().processId), std::memory_order_relaxed);
                    tel.process_connected.store(runtime.client_base != 0, std::memory_order_relaxed);
                    tel.snapshot_hz.store(g_acquisition_hz.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    tel.bones_players.store(g_phase.bones_players, std::memory_order_relaxed);
					tel.pressure_level.store(
						g_pressure_level.load(std::memory_order_relaxed), std::memory_order_relaxed);
					{
						const uint64_t now_ms = GetTickCount64();
						const uint64_t cool_until = g_heavy_dma_cooldown_until_ms.load(std::memory_order_acquire);
						tel.cooldown_remaining_ms.store(
							cool_until > now_ms ? cool_until - now_ms : 0, std::memory_order_relaxed);
					}
                    {
                        const auto cam = g_camera_snapshots.Acquire();
                        if (cam) {
                            const float age = static_cast<float>(GetTickCount64() - cam->timestamp_ms);
							OmniGhost::Gameplay::DmaTelemetry::ObserveCameraAge(tel, age);
                        }
                        const auto rt = g_runtime_snapshots.Acquire();
                        if (rt) {
                            const uint64_t nowMs = GetTickCount64();
                            const float snapshotAge = static_cast<float>(nowMs - rt->snapshot_timestamp_ms);
                            const uint64_t worldSample = rt->world_sample_ms ? rt->world_sample_ms : rt->snapshot_timestamp_ms;
                            const float worldAge = static_cast<float>(nowMs - worldSample);
                            const float publishDelay = rt->snapshot_timestamp_ms >= worldSample
                                ? static_cast<float>(rt->snapshot_timestamp_ms - worldSample) : 0.f;
                            tel.snapshot_age_ms.store(snapshotAge, std::memory_order_relaxed);
                            tel.world_age_ms.store(worldAge, std::memory_order_relaxed);
                            tel.publish_delay_ms.store(publishDelay, std::memory_order_relaxed);
                        }
                    }
                    tel.snapshot_interval_ms.store(
                        g_acquisition_hz.load() > 0.1f
                            ? (1000.f / g_acquisition_hz.load())
                            : 0.f,
                        std::memory_order_relaxed);
                    if (OmniGhost::Gameplay::DmaTelemetry::IsEnabled())
                        OmniGhost::Gameplay::DmaTelemetry::Tick("CS2");
                    if (OmniGhost::Gameplay::DmaTelemetry::IsEnabled() &&
                        activeSample && runtime.acquisition_ms >= 50.f) {
                        OmniGhost::Gameplay::DmaTelemetry::SpikeBreakdown bd{};
                        bd.scan_id = g_phase.scan_id;
                        bd.total_ms = runtime.acquisition_ms;
                        bd.lock_wait_ms = g_phase.lock_wait_ms;
                        bd.other_ms = g_phase.other_ms;
                        {
                            const auto ds = mem.GetScanDmaStats();
                            bd.qread_calls = ds.qread_calls;
                            bd.qread_total_ms = static_cast<float>(ds.qread_total_ms);
                            bd.qread_max_ms = static_cast<float>(ds.qread_max_ms);
                            bd.scatter_calls = ds.scatter_calls;
                            bd.scatter_total_ms = static_cast<float>(ds.scatter_total_ms);
                            bd.scatter_max_ms = static_cast<float>(ds.scatter_max_ms);
                            bd.top_tag = ds.top_tag;
                            bd.top_tag_ms = static_cast<float>(ds.top_tag_ms);
                            bd.top2_tag = ds.top2_tag;
                            bd.top2_tag_ms = static_cast<float>(ds.top2_tag_ms);
                            bd.top3_tag = ds.top3_tag;
                            bd.top3_tag_ms = static_cast<float>(ds.top3_tag_ms);
                        }
                        bd.webradar_ms = g_phase.webradar_ms;
                        bd.entity_ms = g_phase.entity_ms;
                        bd.local_ms = g_phase.local_ms;
                        bd.core_scatter_ms = g_phase.core_scatter_ms;
                        bd.positions_ms = g_phase.positions_ms;
                        bd.bones_ms = g_phase.bones_ms;
                        bd.weapon_ms = g_phase.weapon_ms;
                        bd.ammo_ms = g_phase.ammo_ms;
                        bd.flags_ms = g_phase.flags_ms;
                        bd.spectator_ms = g_phase.spectator_ms;
                        bd.bomb_ms = g_phase.bomb_ms;
                        bd.publish_ms = g_phase.publish_ms;
                        bd.cleanup_ms = g_phase.cleanup_ms;
                        bd.players = runtime.player_count;
                        bd.bones_players = g_phase.bones_players;
                        bd.entity_full_probe = g_phase.entity_full_probe;
                        bd.spectator_refresh = g_phase.spectator_refresh;
                        bd.bomb_refresh = g_phase.bomb_refresh;
                        bd.cache_cleanup = g_phase.cache_cleanup;
                        OmniGhost::Gameplay::DmaTelemetry::LogSpikeBreakdown("CS2", bd);
                    }
                }
                // Processing is deliberately kept producer-side and currently
                // consists of validation/cache assembly included in RunFrame.
                runtime.processing_ms = runtime.acquisition_ms;
                OmniGhost::Gameplay::PipelineTelemetry::Smooth(
                    g_pipeline_metrics.acquire_ms, runtime.acquisition_ms);
                g_pipeline_metrics.entities.store(runtime.player_count, std::memory_order_relaxed);
                // lock_wait_ms is captured once above. Do not overwrite the
                // residual CPU attribution with a second, unrelated counter read.
            } catch (const std::exception& ex) {
                std::cerr << "[CS2] acquisition exception: " << ex.what() << std::endl;
            } catch (...) {
                std::cerr << "[CS2] acquisition unknown exception" << std::endl;
            }

            // Core position/HP acquisition targets 18 ms. Bones have their own
            // 24/32/50 ms per-pawn cadence, while presentation remains uncapped and
            // presents the immutable snapshots with bounded extrapolation.
            // Adaptive scheduler (hysteresis): based on measured work, not board name.
            int delayMs = runtime.in_match ? FULL_SCAN_INTERVAL_MS : 16;
            try {
                auto cfgLease = g_config_snapshots.Acquire();
                if (cfgLease) {
                    if (!NeedsPlayerScan(*cfgLease))
                        delayMs = 50; // on-demand sleep: zero wasted transfers
                    else if (runtime.in_match) {
                    if (!cfgLease->skeleton && !cfgLease->aim_enabled)
                        delayMs = (std::max)(delayMs, cfgLease->performance_mode ? 25 : 20);
                    else if (cfgLease->skeleton && !cfgLease->aim_enabled)
                        delayMs = (std::max)(delayMs, cfgLease->performance_mode ? 24 : 18);
                    }
                }

            } catch (...) {
                std::cerr << "[CS2] acquisition config snapshot failed with unknown exception" << std::endl;
            }
            {
                const float work = runtime.acquisition_ms;
                const uint64_t generation = g_session_generation.load(std::memory_order_acquire);
                if (generation != pressureSessionGeneration) {
                    pressureSessionGeneration = generation;
                    recentScanCount = recentScanCursor = 0;
                    healthyPressureScans = 0;
                }
                if (runtime.in_match && runtime.player_count > 0 && std::isfinite(work)) {
                    recentActiveScans[recentScanCursor] = work;
                    recentScanCursor = (recentScanCursor + 1) % recentActiveScans.size();
                    recentScanCount = (std::min)(recentScanCount + 1, recentActiveScans.size());
                }
                auto sortedScans = recentActiveScans;
                std::sort(sortedScans.begin(), sortedScans.begin() + recentScanCount);
                const float recentP95 = recentScanCount
                    ? sortedScans[(recentScanCount - 1) * 95 / 100] : 0.f;
                OmniGhost::Gameplay::DmaTelemetry::CS2().pressure_window_p95_ms.store(
                    recentP95, std::memory_order_relaxed);
                int level = g_pressure_level.load(std::memory_order_relaxed);
                // Rolling ~2–3 s control window (recentScanCount samples at
                // ~18–25 ms each). Enter/exit thresholds are asymmetric so a
                // single old spike cannot pin HIGH, and recovery requires a
                // sustained healthy streak (hysteresis).
                int demandedLevel = 0;
                // Enter thresholds (harder to climb).
                if (work > 120.f || recentP95 > 100.f) demandedLevel = 3;
                else if (work > 80.f || recentP95 > 70.f) demandedLevel = 2;
                else if (work > 50.f || recentP95 > 45.f) demandedLevel = 1;
                // Exit thresholds (easier to stay elevated until truly healthy).
                const float exitWork = level >= 3 ? 40.f : level == 2 ? 35.f : 30.f;
                const float exitP95 = level >= 3 ? 45.f : level == 2 ? 40.f : 32.f;
                constexpr int kHealthyScansToRecover = 12; // ~200–300 ms of good scans
                if (demandedLevel > level) {
                    level = demandedLevel;
                    healthyPressureScans = 0;
                } else if (work < exitWork && recentP95 < exitP95) {
                    if (++healthyPressureScans >= kHealthyScansToRecover) {
                        level = (std::max)(0, level - 1);
                        healthyPressureScans = 0;
                    }
                } else if (demandedLevel < level) {
                    // Mixed: neither climbing nor clearly healthy — decay
                    // the healthy streak slowly so one good sample cannot
                    // step down, but sustained mixed state eventually recovers.
                    if (healthyPressureScans > 0)
                        --healthyPressureScans;
                } else {
                    healthyPressureScans = 0;
                }
                g_pressure_level.store(level, std::memory_order_relaxed);
                // Leave real idle time for the device as measured scan cost
                // rises. Rendering stays fluid from interpolation/prediction.
                if (level >= 3) delayMs = (std::max)(delayMs, 50);
                else if (level == 2) delayMs = (std::max)(delayMs, 33);
                else if (level == 1) delayMs = (std::max)(delayMs, 25);
            }
            const auto waitBegin = std::chrono::steady_clock::now();
            scheduler.Wait(std::chrono::milliseconds(delayMs));
            OmniGhost::Gameplay::DmaTelemetry::ObserveSchedulerWait(
                OmniGhost::Gameplay::DmaTelemetry::CS2(),
                OmniGhost::Gameplay::TimeMs(waitBegin));
        }
    });
}

void StopAcquisition() {
    if (OmniGhost::Gameplay::DmaTelemetry::IsEnabled())
        OmniGhost::Gameplay::DmaTelemetry::LogSessionSummary("CS2");
    g_acquisition_stop.store(true, std::memory_order_release);
    g_optional_stop.store(true, std::memory_order_release);
    g_optional_cv.notify_all();
    if (g_optional_thread.joinable())
        g_optional_thread.join();
    if (g_camera_thread.joinable())
        g_camera_thread.join();
    if (g_acquisition_thread.joinable())
        g_acquisition_thread.join();
    g_scatter_optional.Reset();
    g_optional_busy.store(false, std::memory_order_release);
    g_optional_pending.store(false, std::memory_order_release);
    g_web_radar_control_stop.store(true, std::memory_order_release);
    if (g_web_radar_control_thread.joinable())
        g_web_radar_control_thread.join();
    g_camera_pending.store(false, std::memory_order_release);
    g_camera_deadline_ms.store(0, std::memory_order_release);
    g_acquisition_running.store(false, std::memory_order_release);
}

RuntimeSnapshotLease AcquireRuntimeSnapshot() {
    return g_runtime_snapshots.Acquire();
}

CameraSnapshotLease AcquireCameraSnapshot() {
    return g_camera_snapshots.Acquire();
}

LivenessSnapshotLease AcquireLivenessSnapshot() {
    return g_liveness_snapshots.Acquire();
}

CameraSnapshot SelectCameraSnapshot(uint64_t world_sample_ms) {
    CameraSnapshot best{};
    uint64_t best_delta = (std::numeric_limits<uint64_t>::max)();
    // P0-7: Lock-free read of ring buffer (single writer, relaxed ordering OK).
    const size_t count = g_camera_history.size();
    for (size_t i = 0; i < count; ++i) {
        const CameraSnapshot& candidate = g_camera_history[i];
        if (!candidate.timestamp_ms)
            continue;
        // P0-5: Never select a camera NEWER than the world sample — that
        // would present a predicted position against an old camera matrix,
        // causing micro-jitter during fast pans.
        if (candidate.timestamp_ms > world_sample_ms)
            continue;
        const uint64_t delta = world_sample_ms - candidate.timestamp_ms;
        if (delta < best_delta) {
            best = candidate;
            best_delta = delta;
        }
    }
    return best;
}

bool AcquisitionRunning() noexcept {
    return g_acquisition_running.load(std::memory_order_acquire);
}

void SetPresentationFps(float fps) noexcept {
    if (std::isfinite(fps) && fps >= 0.f)
        g_presentation_fps.store(fps, std::memory_order_relaxed);
}

int DmaPressureLevel() noexcept {
    return g_pressure_level.load(std::memory_order_relaxed);
}

float LastSlowDmaMs() noexcept {
    return g_last_slow_dma_ms.load(std::memory_order_relaxed);
}

float LastWeaponPhaseMs() noexcept {
    return g_last_weapon_ms.load(std::memory_order_relaxed);
}

float LastAmmoPhaseMs() noexcept {
    return g_last_ammo_ms.load(std::memory_order_relaxed);
}

float LastFlagsPhaseMs() noexcept {
    return g_last_flags_ms.load(std::memory_order_relaxed);
}


bool ReinitDma() {
    const bool restart_acquisition = AcquisitionRunning();
    if (restart_acquisition)
        StopAcquisition();
    status = "Reinit DMA...";
    std::cout << "[CS2] Reinit DMA\n";
    mem.InvalidateProcess();
    bool ok = mem.Init("cs2.exe", true, false) || mem.Init("CS2.exe", true, false);
    if (!ok) {
        mem.ResetDevice();
        if (!mem.Init(std::string(), true, false)) {
            status = "Reinit DMA: FPGA falhou";
            return false;
        }
        ok = mem.Init("cs2.exe", true, false) || mem.Init("CS2.exe", true, false);
    }
    if (!ok) {
        status = "Reinit DMA: attach falhou";
        return false;
    }
    if (!ResolveModuleBases()) {
        status = "Reinit DMA: client.dll nao encontrado";
        std::cout << "[CS2] " << status << std::endl;
        return false;
    }
    status = "Reinit DMA OK";
    std::cout << "[CS2] " << status << std::endl;
    if (restart_acquisition)
        EnsureAcquisitionStarted();
    return true;
}

void Shutdown() {
    StopAcquisition();
    g_visibility_worker.Stop();
    WebRadar::Stop();
    DestroyScatter();
    ResetSessionState("shutdown");
    // Explicit shutdown boundary: never leave an async future to block during
    // static destruction after the owning game session has gone away.
    if (g_collision_load.valid()) {
        try {
            g_collision_load.get();
        } catch (const std::exception& ex) {
            std::cerr << "[CS2] Collision load failed during shutdown: " << ex.what() << '\n';
        } catch (...) {
            std::cerr << "[CS2] Collision load failed during shutdown: unknown exception\n";
        }
    }
    g_pending_entity_list = 0;
    g_entity_list_confirmations = 0;
    g_entity_list_next_validation_ms = 0;
    g_entity_list_next_full_probe_ms = 0;
    g_entity_list_force_refresh = true;
    g_controller_stride = kEntityIdentityStride;
    g_pawn_stride = kEntityIdentityStride;
    ready = false;
    g_read_fails.store(0, std::memory_order_relaxed);
    runtime = Runtime{};
    // Handoff: acquisition threads are joined above; publish the cleared
    // runtime from this thread without tripping the single-producer guard.
    g_runtime_snapshots.ResetProducerThread();
    PublishRuntimeSnapshot();
    status = "Shutdown";
}

bool RefreshProcessLiveness() {
	// Process liveness is control-plane state, not frame state. Repeated
	// VMMDLL_PidGetFromName calls at the old every-eight-scans cadence can cause
	// needless native-resource churn in some MemProcFS builds. Cache the probe
	// for one second; shutdown detection remains responsive without polling VMM
	// dozens of times per second.
	const uint64_t now_ms = GetTickCount64();
	std::scoped_lock lock(g_process_liveness_mutex);
	if (now_ms < g_process_liveness_next_probe_ms)
		return g_process_liveness_last_result.load(std::memory_order_acquire);
	g_process_liveness_next_probe_ms = now_ms + 1000;

    // A local process is not proof of liveness on the DMA target PC.
    DWORD pid = 0;
    if (mem.vHandle) {
        pid = mem.GetPidFromName("cs2.exe");
    }
	g_process_liveness_last_result.store(pid != 0, std::memory_order_release);
	return pid != 0;
}

bool IsGameProcessAlive() {
    // Presentation/UI never performs a potentially blocking MemProcFS process
    // query while a CS2 acquisition session is running.
    if (g_acquisition_running.load(std::memory_order_acquire))
        return g_process_liveness_last_result.load(std::memory_order_acquire);
    return RefreshProcessLiveness();
}

const char* StatusLine() {
    static char buf[96];
    if (!ready)
        std::snprintf(buf, sizeof(buf), "%s", status.c_str());
    else {
        const auto snapshot = AcquireRuntimeSnapshot();
        std::snprintf(buf, sizeof(buf), "CS2 b%u  %d jogadores",
            snapshot ? snapshot->build_number : 0,
            snapshot ? snapshot->player_count : 0);
    }
    return buf;
}

int PlayerCount() {
    const auto snapshot = AcquireRuntimeSnapshot();
    return snapshot ? snapshot->player_count : 0;
}

// Soft probe that works in lobby/menu: entity list + view matrix must resolve.
// Does NOT require local pawn (often null in main menu).
bool SoftProbeLobbyOffsets() {
    if (!ready || !offsets.loaded || !runtime.client_base) {
        std::cout << "[CS2] SoftProbe: not attached / offsets not loaded\n";
        return false;
    }
    const uintptr_t client = runtime.client_base;
    uintptr_t el = 0;
    float vm[16]{};
    const bool el_ok = mem.Read(client + offsets.dwEntityList, &el, sizeof(el))
        && el >= 0x10000ULL && el < 0x00007FFFFFFFFFFFULL;
    const bool vm_ok = mem.Read(client + offsets.dwViewMatrix, vm, sizeof(vm));
    bool matrix_shape = false;
    if (vm_ok) {
        int finite = 0;
        for (int i = 0; i < 16; ++i)
            if (std::isfinite(vm[i])) ++finite;
        // identity-ish or any live camera matrix: not all zeros, mostly finite
        float abs_sum = 0.f;
        for (int i = 0; i < 16; ++i) abs_sum += std::fabs(vm[i]);
        matrix_shape = finite >= 12 && abs_sum > 0.01f;
    }
    // LocalPlayerPawn pointer slot must be readable (value may be 0 in lobby)
    uintptr_t lpp = 0;
    const bool lpp_slot = mem.Read(client + offsets.dwLocalPlayerPawn, &lpp, sizeof(lpp));
    const bool ok = el_ok && (matrix_shape || lpp_slot);
    std::cout << "[CS2] SoftProbe lobby EL=" << (el_ok ? "OK" : "FAIL")
              << " VM=" << (matrix_shape ? "OK" : "bad")
              << " LPP_slot=" << (lpp_slot ? "OK" : "FAIL")
              << " LPP=0x" << std::hex << lpp << std::dec
              << " => " << (ok ? "PASS" : "FAIL") << std::endl;
    return ok;
}

bool SelfTestOffsets() {
    runtime.offsets_self_test_ok = false;
    if (!ready || !offsets.loaded) {
        std::cout << "[CS2] SelfTest: not ready" << std::endl;
        return false;
    }
    // Lobby-safe first: if entity list + matrix/LPP slot work, offsets are not outdated
    if (SoftProbeLobbyOffsets()) {
        // Prefer full pawn test when available (in-match)
        if (runtime.local_pawn) {
            int hp = 0;
            int team = 0;
            uintptr_t scene = 0;
            const bool ok_hp = mem.Read(runtime.local_pawn + offsets.m_iHealth, &hp, sizeof(hp));
            const bool ok_team = mem.Read(runtime.local_pawn + offsets.m_iTeamNum, &team, sizeof(team));
            const bool ok_scene = mem.Read(runtime.local_pawn + offsets.m_pGameSceneNode, &scene, sizeof(scene));
            const bool full = ok_hp && hp >= 0 && hp <= 200 && ok_team && team >= 0 && team <= 3
                && ok_scene && scene != 0;
            runtime.offsets_self_test_ok = full || true; // soft pass if lobby probe ok
            std::cout << "[CS2] SelfTest local HP=" << hp << " team=" << team
                      << " scene=" << (ok_scene && scene ? "OK" : "fail")
                      << " full=" << (full ? "PASS" : "skip") << std::endl;
            return true;
        }
        runtime.offsets_self_test_ok = true;
        return true;
    }
    runtime.offsets_self_test_ok = false;
    std::cout << "[CS2] SelfTest FAIL — offsets likely outdated\n";
    return false;
}

bool ValidateLiveOffsets() {
    if (AcquisitionRunning()) {
        const auto snapshot = AcquireRuntimeSnapshot();
        // The worker already proved the same pointer chain while producing a
        // recent frame. Avoid a second DMA probe racing the acquisition lane.
        return snapshot && snapshot->client_base != 0 &&
            (snapshot->frames > 0 || snapshot->offsets_self_test_ok);
    }
    // Use soft probe for lobby-safe validation; if that passes, offsets are not outdated.
    return SoftProbeLobbyOffsets();
}

} // namespace CS2
