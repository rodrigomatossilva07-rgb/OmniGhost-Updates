#include "cs2_game.h"
#include "platform/session_log.h"
#include "../src/platform/offset_auto.h"
#include "../src/platform/app_paths.h"
#include "../src/platform/embedded_offsets.h"
#include "aimbot/cs2_aim.h"
#include "weapons/cs2_weapons.h"
#include "weapons/cs2_weapon_icons.h"
#include "Memory/Memory.h"
#include "globals.h"
#include "gameplay/esp_core.h"
#include "gameplay/entity_cache_hub.h"
#include "gameplay/frame_pipeline.h"
#include "gameplay/scoped_phase.h"
#include "trajectory/cs2_map_collision_cache.h"
#include "trajectory/cs2_visibility.h"
#include "gameplay/unified_esp.h"
#include "runtime/cs2_camera.h"
#include "runtime/cs2_runtime.h"
#include "visibility/cs2_visibility_worker.h"
#include "collision/cs2_collision_loader.h"
#include "radar/cs2_web_radar.h"
#include "telemetry/cs2_telemetry.h"
#include "core/cs2_core_world.h"
#include "metadata/cs2_metadata.h"
#include "state/cs2_cache_manager.h"

#include <Windows.h>
#include <TlHelp32.h>
#include <xmmintrin.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
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
#include "gameplay/camera_priority.h"

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



namespace {
    char s_status[128] = "idle";
    std::atomic<uint32_t> s_statusLen{5};
    std::mutex s_statusMutex;
}

void SetStatus(std::string_view text) {
    if (text.size() > sizeof(s_status) - 1)
        text = text.substr(0, sizeof(s_status) - 1);
    std::lock_guard lock(s_statusMutex);
    s_statusLen.store(0, std::memory_order_release);
    std::memcpy(s_status, text.data(), text.size());
    s_status[text.size()] = '\0';
    s_statusLen.store(static_cast<uint32_t>(text.size()), std::memory_order_release);
}

std::string GetStatus() {
    const uint32_t len = s_statusLen.load(std::memory_order_acquire);
    if (len == 0 || len >= sizeof(s_status))
        return {};
    return std::string(s_status, len);
}

std::string_view GetStatusView() {
    static thread_local char viewBuffer[sizeof(s_status)];
    const uint32_t len = s_statusLen.load(std::memory_order_acquire);
    if (len == 0 || len >= sizeof(s_status))
        return {};
    std::memcpy(viewBuffer, s_status, len);
    viewBuffer[len] = '\0';
    return std::string_view(viewBuffer, len);
}

std::string offsets_source = "não carregados";

namespace {

using CoreWorld::InsideExpandedFrustum;
using CoreWorld::IsFinitePosition;
using CoreWorld::IsPlayableMapName;
using CoreWorld::IsPlayableTeam;
using Metadata::DroppedWeaponAllowed;
using Metadata::FormatDroppedWeaponName;
using Metadata::IsBotPlayerName;
using Metadata::IsDroppedWeaponClass;








namespace {


struct DynamicIntervals {
    int camera_ms = 16;
    int bones_ms = 24;
    int full_scan_ms = 18;
    int weapon_ms = 200;
    int weapon_fallback_ms = 3000;
    int entity_list_ms = 1000;
    int entity_list_full_probe_ms = 15000;
    int name_ms = 1000;
    int bone_reliability_hold_ms = kBoneReliabilityHoldMs;
    int team_ms = 500;


    void Update(int entity_count, float dma_latency_ms, int pressure_level, float ) {


        float entity_mult = 1.0f;
        if (entity_count > 48) entity_mult = 1.5f;
        else if (entity_count > 32) entity_mult = 1.25f;
        else if (entity_count > 16) entity_mult = 1.1f;


        float latency_mult = 1.0f + std::min(dma_latency_ms / 50.f, 1.0f);


        float pressure_mult = 1.0f;
        if (pressure_level >= 3) pressure_mult = 2.0f;
        else if (pressure_level == 2) pressure_mult = 1.5f;
        else if (pressure_level == 1) pressure_mult = 1.25f;


        float total_mult = std::max({entity_mult, latency_mult, pressure_mult});
        total_mult = std::clamp(total_mult, 1.0f, 3.0f);

        bones_ms = static_cast<int>(std::round(24.f * total_mult));
        full_scan_ms = static_cast<int>(std::round(18.f * total_mult));

        camera_ms = 16;
    }
};

DynamicIntervals g_intervals;


constexpr float kOptionalBudgetMs = 6.f;


constexpr int CAMERA_INTERVAL_MS = 16;
constexpr int BONES_INTERVAL_MS = 24;
constexpr int FULL_SCAN_INTERVAL_MS = 18;
constexpr int WEAPON_INTERVAL_MS = 200;
constexpr int WEAPON_FALLBACK_INTERVAL_MS = 3000;
constexpr int ENTITY_LIST_INTERVAL_MS = 1000;
constexpr int ENTITY_LIST_FULL_PROBE_INTERVAL_MS = 15000;
constexpr int NAME_INTERVAL_MS = 1000;
constexpr int BONE_RELIABILITY_HOLD_MS = kBoneReliabilityHoldMs;
constexpr int TEAM_INTERVAL_MS = 500;


constexpr float PRESSURE_ENTER_L1_WORK = 35.f;
constexpr float PRESSURE_ENTER_L1_P95 = 32.f;
constexpr float PRESSURE_ENTER_L2_WORK = 55.f;
constexpr float PRESSURE_ENTER_L2_P95 = 50.f;
constexpr float PRESSURE_ENTER_L3_WORK = 80.f;
constexpr float PRESSURE_ENTER_L3_P95 = 70.f;
constexpr float PRESSURE_EXIT_L3_WORK = 30.f;
constexpr float PRESSURE_EXIT_L3_P95 = 38.f;
constexpr float PRESSURE_EXIT_L2_WORK = 26.f;
constexpr float PRESSURE_EXIT_L2_P95 = 30.f;
constexpr float PRESSURE_EXIT_L1_WORK = 20.f;
constexpr float PRESSURE_EXIT_L1_P95 = 24.f;
constexpr int PRESSURE_HEALTHY_SCANS_TO_RECOVER = 12;
}



enum class AcquisitionState : uint8_t {
    Uninitialized = 0,
    Starting,
    Running,
    Stopping,
    Stopped
};


struct OptionalTarget;
struct OptionalBoneResult;
struct OptionalNameResult;
struct OptionalWeaponResult;
struct OptionalVisibilityResult;
struct OptionalResults;

struct AcquisitionStateMachine {
    std::atomic<AcquisitionState> state{AcquisitionState::Uninitialized};
    std::atomic<bool> optional_busy{false};
    std::atomic<bool> optional_pending{false};
    std::mutex optional_mu;
    std::condition_variable optional_cv;
    Config optional_config{};
    std::atomic<uint64_t> optional_job_generation{0};


    bool g_optional_in_match = false;
    bool g_optional_dropped_only = false;
    uintptr_t g_optional_client_base = 0;
    std::vector<OptionalTarget> g_optional_targets;


    bool TryStart() {
        AcquisitionState current = state.load(std::memory_order_acquire);
        while (current == AcquisitionState::Uninitialized ||
               current == AcquisitionState::Stopped) {
            if (state.compare_exchange_weak(current, AcquisitionState::Starting,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire))
                return true;
        }
        return false;
    }
    void MarkRunning() {




        AcquisitionState expected = AcquisitionState::Starting;
        state.compare_exchange_strong(expected, AcquisitionState::Running,
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire);
    }
    void MarkStopped() {
        state.store(AcquisitionState::Stopped, std::memory_order_release);
    }
    bool IsRunning() const noexcept {
        return state.load(std::memory_order_acquire) == AcquisitionState::Running;
    }
    bool IsStoppingOrStopped() const noexcept {
        auto s = state.load(std::memory_order_acquire);
        return s == AcquisitionState::Stopping || s == AcquisitionState::Stopped;
    }


    bool TryQueueOptional() {
        if (optional_busy.load(std::memory_order_acquire) ||
            optional_pending.load(std::memory_order_acquire) ||
            IsStoppingOrStopped()) {
            return false;
        }
        optional_pending.store(true, std::memory_order_release);
        return true;
    }
    bool ClaimOptional(Config& out_config, std::vector<OptionalTarget>& out_targets,
                       bool& out_in_match, bool& out_dropped_only,
                       uintptr_t& out_client_base, uint64_t& out_generation) {
        std::unique_lock lock(optional_mu);
        optional_cv.wait(lock, [this] {
            return optional_pending.load(std::memory_order_acquire) ||
                   IsStoppingOrStopped();
        });
        if (IsStoppingOrStopped() || !optional_pending.load(std::memory_order_acquire))
            return false;
        out_config = optional_config;
        out_targets.swap(g_optional_targets);
        out_in_match = g_optional_in_match;
        out_dropped_only = g_optional_dropped_only;
        out_client_base = g_optional_client_base;
        out_generation = optional_job_generation.load(std::memory_order_acquire);
        optional_pending.store(false, std::memory_order_release);
        optional_busy.store(true, std::memory_order_release);
        return true;
    }
    void ReleaseOptional() {
        optional_busy.store(false, std::memory_order_release);
    }
    void RequestStop() {
        state.store(AcquisitionState::Stopping, std::memory_order_release);
        optional_pending.store(true, std::memory_order_release);
        optional_cv.notify_all();
    }


    bool IsAcquisitionRunning() const noexcept { return IsRunning(); }
};








struct OptionalTarget {
    uintptr_t pawn = 0;
    uintptr_t controller = 0;
    uintptr_t scene = 0;
    float origin[3]{};
    uint64_t position_sample_ms = 0;
    bool has_name = false;
};
struct OptionalBoneResult {
    uintptr_t pawn = 0;
    uintptr_t controller = 0;
    uintptr_t bone_base = 0;

    std::array<std::array<float, 3>, kBoneSlotCount> bones{};
    float origin[3]{};
    uint64_t sample_ms = 0;
    bool bones_ok = false;
    bool full_bones_ok = false;
};
struct OptionalNameResult { uintptr_t pawn = 0; uintptr_t controller = 0; char name[64]{}; };
struct OptionalWeaponResult { uintptr_t pawn = 0; int weapon_def = 0; char weapon[32]{}; int ammo_clip = -1; int ammo_reserve = -1; };
struct OptionalVisibilityResult {
    uintptr_t pawn = 0;
    bool visible = false;
    bool known = false;
    uint8_t point_known_mask = 0;
    uint8_t point_visible_mask = 0;
    uint64_t checked_ms = 0;
};

constexpr size_t kOptionalMaxPlayers = 32;
struct OptionalResults {
    uint64_t generation = 0;
    std::array<OptionalBoneResult, kOptionalMaxPlayers> bones{};
    std::array<OptionalNameResult, kOptionalMaxPlayers> names{};
    std::array<OptionalWeaponResult, kOptionalMaxPlayers> weapons{};
    std::array<OptionalVisibilityResult, kOptionalMaxPlayers> visibility{};
    uint32_t name_count = 0, weapon_count = 0;
    uint32_t bone_count = 0;
    uint32_t visibility_count = 0;
    float bones_ms = 0.f;
    float weapon_ms = 0.f;
    float visibility_ms = 0.f;
    float dropped_ms = 0.f;
    float budget_left_ms = 0.f;
    std::vector<DroppedWeapon> dropped_weapons;
    bool dropped_weapons_valid = false;
};


AcquisitionStateMachine g_acqState;

std::atomic<float> g_presentation_fps{0.f};
bool g_publish_runtime_this_scan = true;
auto& g_session_generation = CacheManager::session_generation;
std::mutex g_process_liveness_mutex;
uint64_t g_process_liveness_next_probe_ms = 0;
std::atomic_bool g_process_liveness_last_result{true};
std::thread g_acquisition_thread;
std::thread g_camera_thread;


std::thread g_optional_thread;



std::vector<OptionalTarget> g_optional_targets;
bool g_optional_in_match = false;
bool g_optional_dropped_only = false;
uintptr_t g_optional_client_base = 0;

OmniGhost::Gameplay::PipelineTelemetry g_pipeline_metrics;

OmniGhost::Gameplay::SnapshotExchange<OptionalResults, 3> g_optional_results;

VisibilityPipeline::Worker g_visibility_worker;






void PublishRuntimeSnapshot() {
    RuntimePipeline::Publish(
        runtime,
        g_session_generation.load(std::memory_order_acquire),
        g_read_fails.load(std::memory_order_relaxed));
}

void PublishLivenessSnapshot(const LivenessSnapshot& liveness) {
    RuntimePipeline::PublishLiveness(liveness);
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



static bool HeavyDmaCooldownActive();
static void NotePossibleDeviceStall(float duration_ms);


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




bool QTryRead(uintptr_t addr, void* buf, size_t size, bool& attempted,
              const char* tag = "QTryRead") {
    attempted = false;
    if (!addr || !buf || !size || !mem.vHandle || addr < 0x10000)
        return false;


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
ScatterHandleOwner g_scatter_optional;
Collision::Loader g_collision_loader;


using Cs2PhaseTiming = Telemetry::PhaseTiming;
auto& g_scan_id = Telemetry::scan_id;
auto& g_phase = Telemetry::phase;
auto& g_pressure_level = Telemetry::pressure_level;
auto& g_heavy_dma_cooldown_until_ms = Telemetry::heavy_dma_cooldown_until_ms;
auto& g_last_slow_dma_ms = Telemetry::last_slow_dma_ms;
auto& g_last_weapon_ms = Telemetry::last_weapon_ms;
auto& g_last_ammo_ms = Telemetry::last_ammo_ms;
auto& g_last_flags_ms = Telemetry::last_flags_ms;

static bool HeavyDmaCooldownActive() {
    return Telemetry::HeavyDmaCooldownActive();
}

static void NotePossibleDeviceStall(float duration_ms) {
    Telemetry::NotePossibleDeviceStall(duration_ms);
}





std::atomic<uintptr_t> g_cached_entity_root{0};

void EnsureScatter() {
    g_scatter_full.Ensure();
}

void DestroyScatter() {
    g_scatter_full.Reset();
    g_scatter_optional.Reset();
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

constexpr EntityHandleParts DecodeEntityHandle(uint32_t handle) noexcept {
    return { static_cast<uintptr_t>((handle & kEntityHandlePageMask) >> kEntityHandlePageShift),
             static_cast<uintptr_t>(handle & kEntityHandleIndexMask),
             handle != 0 && handle != 0xFFFFFFFFu };
}

static_assert(DecodeEntityHandle(0x201u).page == 1u);
static_assert(DecodeEntityHandle(0x201u).index == 1u);



static bool HasEnabledPlayerVisuals(const Config& c) {
    if (!c.esp_enabled) return false;
    return c.box || c.box_corner || c.skeleton || c.health_bar || c.armor_bar ||
        c.health_value || c.armor_value || c.name || c.distance || c.head_dot ||
        c.weapon_name || c.weapon_ammo || c.snaplines || c.trails || c.head_halo ||
        c.look_direction || c.chinese_hat || c.angel_wings || c.devil_horns ||
        c.floating_crown;
}



bool NeedsPlayerScan(const Config& frame_config) {

    return HasEnabledPlayerVisuals(frame_config)
        || frame_config.aim_enabled
        || frame_config.trigger_enabled
        || frame_config.spectator_list
        || frame_config.bomb_timer
        || frame_config.radar_2d
        || frame_config.web_radar_enabled
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
    uintptr_t boneArrayInModelState = 0;
    JsonClassU64(schema, "CModelState", "m_boneArray", boneArrayInModelState);
    offsets.BoneArray = offsets.m_modelState && boneArrayInModelState
        ? offsets.m_modelState + boneArrayInModelState : 0;
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

}

#include "offsets/cs2_offsets_pipeline.inl"
#include "session/cs2_session.inl"
#include "dropped_items/cs2_dropped_items.inl"
#include "core/cs2_core_acquisition.inl"
#include "bomb/cs2_bomb.inl"



bool GiveCameraPriorityBeforeOptionalDma() {
    struct CameraState : OmniGhost::Gameplay::CameraPriority::ICameraState {
        bool IsPending() const noexcept override { return Camera::Pending(); }
        uint64_t GetDeadlineMs() const noexcept override { return Camera::Deadline(); }
    };
    static CameraState state;
    return OmniGhost::Gameplay::CameraPriority::GiveCameraPriority(state);
}



[[maybe_unused]] static bool OptionalDmaShouldYieldToCamera() noexcept {
    struct CameraState : OmniGhost::Gameplay::CameraPriority::ICameraState {
        bool IsPending() const noexcept override { return Camera::Pending(); }
        uint64_t GetDeadlineMs() const noexcept override { return Camera::Deadline(); }
    };
    static CameraState state;
    return OmniGhost::Gameplay::CameraPriority::ShouldYieldToCamera(state);
}



static bool ShouldYieldToCameraQuick() noexcept {
    struct CameraState : OmniGhost::Gameplay::CameraPriority::ICameraState {
        bool IsPending() const noexcept override { return Camera::Pending(); }
        uint64_t GetDeadlineMs() const noexcept override { return Camera::Deadline(); }
    };
    static CameraState state;
    return OmniGhost::Gameplay::CameraPriority::ShouldYieldToCamera(state);
}





#include "bones/cs2_bones_pipeline.inl"
#include "runtime/cs2_optional_lane.inl"
#include "spectators/cs2_spectators.inl"
#include "core/cs2_world_pipeline.inl"
#include "runtime/cs2_runtime_threads.inl"
#include "session/cs2_lifecycle.inl"
#include "offsets/cs2_offset_validation.inl"
}
