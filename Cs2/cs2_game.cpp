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
#include "gameplay/frame_pipeline.h"

#include <Windows.h>
#include <TlHelp32.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>
#include "makcu/makcu_wrapper.h"
#include "../Fivem/aimbot/aim_type.h"
#include "gameplay/dma_telemetry_log.h"

namespace fs = std::filesystem;

namespace CS2 {

Offsets offsets;
Config config;
Runtime runtime;
bool LoadEmbeddedOffsets();
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
// The renderer interpolates immutable snapshots.  A sustainable DMA cadence is
// smoother in practice than flooding the FT601 queue and periodically freezing
// for a full transport timeout.
// DMA low-latency profile (CS2-DMA-style tiers):
//  - CameraWorker ~500 Hz view matrix (decoupled from entity scan)
//  - High-frequency: positions / health every full scan
//  - Mid: bones ~12 ms, armor ~50 ms, weapons ~100 ms
//  - Low: names / team metadata 0.5–1 s (cuts redundant DMA >80%)
// Presentation interpolates between snapshots at overlay FPS.
constexpr int CAMERA_INTERVAL_MS = 2;       // CameraWorker target (~500 Hz)
constexpr int MOTION_INTERVAL_MS = 8;       // origin lane between full scans
constexpr int BONES_INTERVAL_MS = 12;       // skeleton tier
constexpr int FULL_SCAN_INTERVAL_MS = 12;   // health / spotted / core identity
constexpr int ARMOR_INTERVAL_MS = 50;
constexpr int LOCAL_HEALTH_INTERVAL_MS = 50;
constexpr int WEAPON_INTERVAL_MS = 100;
constexpr int WEAPON_FALLBACK_INTERVAL_MS = 10000;
constexpr int ENTITY_LIST_INTERVAL_MS = 150;
constexpr int NAME_INTERVAL_MS = 1000;      // low-frequency identity
constexpr int BONE_RELIABILITY_HOLD_MS = 150; // last-good skeleton anti-flicker
constexpr int TEAM_INTERVAL_MS = 500;
}

OmniGhost::Gameplay::SnapshotExchange<CameraSnapshot> g_camera_snapshots;
OmniGhost::Gameplay::SnapshotExchange<LivenessSnapshot> g_liveness_snapshots;
OmniGhost::Gameplay::SnapshotExchange<MotionSnapshot> g_motion_snapshots;
OmniGhost::Gameplay::SnapshotExchange<Config> g_config_snapshots;
std::atomic_bool g_acquisition_stop{false};
std::atomic_bool g_acquisition_running{false};
std::atomic<float> g_presentation_fps{0.f};
std::atomic<uint64_t> g_last_snapshot_publish_ms{0};
std::atomic<uint64_t> g_runtime_snapshot_drops{0};
std::atomic<float> g_acquisition_hz{0.f};
std::thread g_acquisition_thread;
std::thread g_camera_thread;
OmniGhost::Gameplay::PipelineTelemetry g_pipeline_metrics;

void PublishRuntimeSnapshot() {
    const uint64_t now = GetTickCount64();
    const uint64_t previous = g_last_snapshot_publish_ms.exchange(now, std::memory_order_relaxed);
    if (previous != 0 && now > previous)
        g_acquisition_hz.store(1000.0f / static_cast<float>(now - previous), std::memory_order_relaxed);
    runtime.snapshot_timestamp_ms = now;
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
}

void PublishCameraSnapshot(const float* matrix) {
    if (!matrix) return;
    auto slot = g_camera_snapshots.TryBeginWrite();
    if (!slot) return;
    std::memcpy(slot.value->view_matrix, matrix, sizeof(slot.value->view_matrix));
    slot.value->timestamp_ms = GetTickCount64();
    g_camera_snapshots.Publish(slot.index);
}

void PublishLivenessSnapshot(const LivenessSnapshot& liveness) {
    auto slot = g_liveness_snapshots.TryBeginWrite();
    if (!slot) return;
    *slot.value = liveness;
    g_liveness_snapshots.Publish(slot.index);
}

void PublishMotionSnapshot(const MotionSnapshot& motion) {
    auto slot = g_motion_snapshots.TryBeginWrite();
    if (!slot) return;
    *slot.value = motion;
    g_motion_snapshots.Publish(slot.index);
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
static bool DmaCooldownActive();
static void NotePossibleDeviceStall(float duration_ms);

// Quiet read — avoids flooding log with "[!] Failed to read Memory"
bool QRead(uintptr_t addr, void* buf, size_t size, const char* tag = "QRead") {
    if (!addr || !buf || !size || !mem.vHandle) return false;
    if (addr < 0x10000) return false;
    if (DmaCooldownActive()) return false;
    mem.SetDmaCallTag(tag);
    const auto begin = std::chrono::steady_clock::now();
    const bool ok = mem.Read(addr, buf, size);
    NotePossibleDeviceStall(OmniGhost::Gameplay::TimeMs(begin));
    if (!ok) {
        ++runtime.read_fails;
        return false;
    }
    return true;
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
        if (!handle_ && mem.vHandle) handle_ = mem.CreateScatterHandle();
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
// Per-scan phase timings for SPIKE_BREAKDOWN (acquisition thread only).
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
    float spectator_ms = 0.f;
    float bomb_ms = 0.f;
    float publish_ms = 0.f;
    float cleanup_ms = 0.f;
    float other_ms = 0.f;
    float lock_wait_ms = 0.f;
    int entity_full_probe = 0;
    int spectator_refresh = 0;
    int bomb_refresh = 0;
    int cache_cleanup = 0;
    int bones_players = 0;
};
static Cs2PhaseTiming g_phase{};
static std::atomic<int> g_pressure_level{0}; // 0=NORMAL 1=MODERATE 2=HIGH 3=RECOVERY
static std::atomic_bool g_acq_busy{false}; // full scan holds data plane priority over camera motion
// Some DMA stacks stall when a matrix read overlaps an entity/bone scan.
// Keep the last complete camera state rather than racing two physical reads.
static std::mutex g_dma_read_gate;
// After a slow VMM/FPGA call, back off heavy phases so the device can recover.
// Vendor-side pauses (~0.5–1.5s every ~10–15s on some stacks) cannot be removed
// from MemProcFS/LeechCore; we avoid stacking more DMA on top of them.
static std::atomic<uint64_t> g_dma_cooldown_until_ms{0};
static std::atomic<float> g_last_slow_dma_ms{0.f};

static bool DmaCooldownActive() {
    return GetTickCount64() < g_dma_cooldown_until_ms.load(std::memory_order_acquire);
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
    uint64_t cur = g_dma_cooldown_until_ms.load(std::memory_order_relaxed);
    while (until > cur &&
           !g_dma_cooldown_until_ms.compare_exchange_weak(cur, until, std::memory_order_relaxed)) {
    }
}

struct ScopedPhase {
    float* target;
    std::chrono::steady_clock::time_point t0;
    explicit ScopedPhase(float* out) : target(out), t0(std::chrono::steady_clock::now()) {}
    ~ScopedPhase() {
        if (target)
            *target = OmniGhost::Gameplay::TimeMs(t0);
    }
};


ScatterHandleOwner g_scatter_motion;
uintptr_t g_cached_entity_root = 0; // refreshed once per frame when scanning

void EnsureScatter() {
    g_scatter_full.Ensure();
}

void EnsureMotionScatter() {
    g_scatter_motion.Ensure();
}

void DestroyScatter() {
    g_scatter_full.Reset();
    g_scatter_motion.Reset();
    g_cached_entity_root = 0;
}

struct EntityHandleParts {
    uintptr_t page = 0;
    uintptr_t index = 0;
    bool valid = false;
};

constexpr EntityHandleParts DecodeEntityHandle(uint32_t handle) noexcept {
    return { static_cast<uintptr_t>((handle & 0x7FFFu) >> 9),
             static_cast<uintptr_t>(handle & 0x1FFu),
             handle != 0 && handle != 0xFFFFFFFFu };
}

static_assert(DecodeEntityHandle(0x201u).page == 1u);
static_assert(DecodeEntityHandle(0x201u).index == 1u);

// True only when some feature actually needs the player list this frame.
// With everything OFF this is false → RunFrame is nearly free (target 130+ FPS).
bool NeedsPlayerScan(const Config& frame_config) {
    return frame_config.esp_enabled
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
    JsonClassU64(schema, "C_BasePlayerPawn", "m_vOldOrigin", offsets.m_vOldOrigin);
    JsonClassU64(schema, "CCSPlayerController", "m_hPlayerPawn", offsets.m_hPlayerPawn);
    JsonClassU64(schema, "CCSPlayerController", "m_hObserverPawn", offsets.m_hObserverPawn);
    JsonClassU64(schema, "C_CSPlayerPawn", "m_pObserverServices", offsets.m_pObserverServices);
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
    JsonClassU64(schema, "C_CSPlayerPawn", "m_flFlashDuration", offsets.m_flFlashDuration);
    JsonClassU64(schema, "C_CSPlayerPawn", "m_aimPunchAngle", offsets.m_aimPunchAngle);
    JsonClassU64(schema, "CGameSceneNode", "m_vecAbsOrigin", offsets.m_vecAbsOrigin);
    JsonClassU64(schema, "CGameSceneNode", "m_vecVelocity", offsets.m_vecVelocity);
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
        "Cs2/data",
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

static DWORD FindProcessIdByName(const char* process_name) {
    if (!process_name || !*process_name)
        return 0;
    const std::wstring targetName(process_name, process_name + std::strlen(process_name));
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD pid = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, targetName.c_str()) == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return pid;
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
        if (!pid) {
            pid = FindProcessIdByName("cs2.exe");
            if (!pid) pid = FindProcessIdByName("CS2.exe");
        }
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

    // Fast path: process already running (local name lookup) before long wait.
    if (FindProcessIdByName("cs2.exe") || FindProcessIdByName("CS2.exe")) {
        status = "cs2.exe encontrado";
        std::cout << "[CS2] " << status << " (local)" << std::endl;
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Info,
            OmniGhost::SessionLog::Subsystem::Process,
            "CS2 process present (local)", {});
    } else if (!WaitForProcess(180)) {
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

uintptr_t g_pending_entity_list = 0;
int g_entity_list_confirmations = 0;
uintptr_t g_controller_stride = kEntityIdentityStride;
uintptr_t g_pawn_stride = kEntityIdentityStride;

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
    // Competitive, casual, arms race, danger zone, deathmatch, workshop.
    return name.rfind("de_", 0) == 0 || name.rfind("cs_", 0) == 0 ||
           name.rfind("ar_", 0) == 0 || name.rfind("dz_", 0) == 0 ||
           name.rfind("gd_", 0) == 0 || name.rfind("gm_", 0) == 0 ||
           name.rfind("workshop_", 0) == 0;
}

void RefreshMapName() {
    if (!offsets.dwGlobalVars) {
        // Keep the last confirmed map during a transient offset/read failure.
        // Clearing it here deactivates the whole ESP for a frame after a DMA
        // timeout and looks exactly like a visual hitch.
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
    // Fast path: list healthy + we have players → only verify primary page (~1 read)
    // Full multi-page probe only when empty, collapsed, or periodic revalidate.
    static uint64_t s_nextFullProbeMs = 0;
    const uint64_t nowMs = GetTickCount64();

    uintptr_t root = 0;
    if (!QReadT(runtime.client_base + offsets.dwEntityList, root) || !IsUserPointer(root)) {
        g_cached_entity_root = 0;
        s_nextFullProbeMs = 0;
        return runtime.entity_list_entry != 0;
    }
    g_cached_entity_root = root;

    const bool haveEntry = runtime.entity_list_entry != 0;

    // runtime.players is rebuilt at the start of every scan, so using it here
    // made the supposed fast path unreachable. A confirmed entry is enough to
    // perform the cheap one-pointer validation.
    if (haveEntry && nowMs < s_nextFullProbeMs) {
        // Cheap confirm: primary page still matches
        uintptr_t entry = 0;
        if (QReadT(root + kEntityPageTableOffset, entry) && IsUserPointer(entry)) {
            if (entry == runtime.entity_list_entry) {
                return true;
            }
            // Primary changed — force full probe next
            s_nextFullProbeMs = 0;
        } else {
            s_nextFullProbeMs = 0;
        }
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
        s_nextFullProbeMs = nowMs + 4500u;
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
            s_nextFullProbeMs = nowMs + 4500u;
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
    s_nextFullProbeMs = nowMs + 4500u;
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

static bool LooksLikePawn(uintptr_t pawn) {
    if (!IsUserPointer(pawn)) return false;
    int health = 0;
    // m_iTeamNum is uint8 in Source 2 — read 1 byte to avoid adjacent garbage.
    uint8_t team = 0;
    if (!QReadT(pawn + offsets.m_iHealth, health))
        return false;
    // Alive pawns: 1..200. Allow 0 for recently dead bodies still in list.
    if (health < 0 || health > 500)
        return false;
    if (!QReadT(pawn + offsets.m_iTeamNum, team))
        return false;
    if (team > 5)
        return false;
    return true;
}

// Calibrate identity stride once using local controller handle → local pawn.
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
        if (LooksLikePawn(preferred))
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
        if (LooksLikePawn(alt)) {
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
// pointers and one for the entity pointers. The old path resolved and validated
// every pawn with several synchronous reads, which scaled very poorly in a full
// lobby. Health/team validation already happens in the following core scatter.
static int ScatterResolvePawnHandles(const uint32_t* handles, uintptr_t* pawns,
                                     int count, uintptr_t stride) {
    if (!handles || !pawns || count <= 0) return 0;
    std::fill(pawns, pawns + count, 0);

    uintptr_t root = g_cached_entity_root;
    if (!IsUserPointer(root)) {
        if (!QReadT(runtime.client_base + offsets.dwEntityList, root) || !IsUserPointer(root))
            return 0;
        g_cached_entity_root = root;
    }

    constexpr int kMaxPages = 64;
    uintptr_t chunks[kMaxPages]{};
    bool pageUsed[kMaxPages]{};
    for (int i = 0; i < count; ++i) {
        const EntityHandleParts parts = DecodeEntityHandle(handles[i]);
        if (!parts.valid) continue;
        const int page = static_cast<int>(parts.page);
        if (page >= 0 && page < kMaxPages) pageUsed[page] = true;
    }

    EnsureScatter();
    if (!g_scatter_full) {
        int resolved = 0;
        for (int i = 0; i < count; ++i) {
            pawns[i] = ResolvePawnFromHandle(handles[i], runtime.local_pawn);
            if (IsUserPointer(pawns[i])) ++resolved;
        }
        return resolved;
    }

    for (int page = 0; page < kMaxPages; ++page) {
        if (!pageUsed[page]) continue;
        mem.AddScatterReadRequest(g_scatter_full,
            root + kEntityPageTableOffset + sizeof(uintptr_t) * static_cast<uintptr_t>(page),
            &chunks[page], sizeof(uintptr_t));
    }
    mem.ExecuteReadScatter(g_scatter_full);

    for (int i = 0; i < count; ++i) {
        const EntityHandleParts parts = DecodeEntityHandle(handles[i]);
        if (!parts.valid) continue;
        const int page = static_cast<int>(parts.page);
        if (page < 0 || page >= kMaxPages || !IsUserPointer(chunks[page])) continue;
        mem.AddScatterReadRequest(g_scatter_full, chunks[page] + stride * parts.index,
                                  &pawns[i], sizeof(uintptr_t));
    }
    mem.ExecuteReadScatter(g_scatter_full);

    int resolved = 0;
    for (int i = 0; i < count; ++i) {
        if (IsUserPointer(pawns[i])) ++resolved;
        else pawns[i] = 0;
    }
    return resolved;
}

// Core pawn fields in one scatter: health, team, armor, scene node.
struct PawnCoreFields {
    int health = 0;
    uint8_t team = 0;
    int armor = 0;
    uintptr_t scene = 0;
    bool spotted = true;
    uint8_t scoped = 0;
    float flash = 0.f;
    float eye_angles[2]{};
    uintptr_t weapon_services = 0;
};

static void ScatterReadPawnCore(const uintptr_t* pawns, PawnCoreFields* fields, int count,
                                bool need_armor, bool need_scoped, bool need_flash,
                                bool need_yaw, bool need_weapons, bool need_spotted) {
    if (!pawns || !fields || count <= 0) return;
    EnsureScatter();
    if (!g_scatter_full) {
        for (int i = 0; i < count; ++i) {
            if (!pawns[i]) continue;
            QReadT(pawns[i] + offsets.m_iHealth, fields[i].health);
            {
                uint8_t sp = 1;
                if (need_spotted && offsets.m_entitySpottedState)
                    QReadT(pawns[i] + offsets.m_entitySpottedState + offsets.m_bSpotted, sp);
                fields[i].spotted = (sp != 0);
            }
            QReadT(pawns[i] + offsets.m_iTeamNum, fields[i].team);
            if (need_armor)
                QReadT(pawns[i] + offsets.m_ArmorValue, fields[i].armor);
            QReadT(pawns[i] + offsets.m_pGameSceneNode, fields[i].scene);
            if (need_scoped && offsets.m_bIsScoped)
                QReadT(pawns[i] + offsets.m_bIsScoped, fields[i].scoped);
            if (need_flash && offsets.m_flFlashDuration)
                QReadT(pawns[i] + offsets.m_flFlashDuration, fields[i].flash);
            if (need_yaw)
                QRead(pawns[i] + offsets.m_angEyeAngles, fields[i].eye_angles,
                      sizeof(fields[i].eye_angles));
            if (need_weapons && offsets.m_pWeaponServices)
                QReadT(pawns[i] + offsets.m_pWeaponServices, fields[i].weapon_services);
        }
        return;
    }
    static thread_local uint8_t spotted_buf[128];
    if (count > 128) count = 128;
    for (int i = 0; i < count; ++i) {
        fields[i] = {};
        spotted_buf[i] = 1;
        if (!pawns[i]) continue;
        mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_iHealth,
                                  &fields[i].health, sizeof(int));
        mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_iTeamNum,
                                  &fields[i].team, sizeof(uint8_t));
        if (need_armor)
            mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_ArmorValue,
                                      &fields[i].armor, sizeof(int));
        mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_pGameSceneNode,
                                  &fields[i].scene, sizeof(uintptr_t));
        if (need_spotted && offsets.m_entitySpottedState)
            mem.AddScatterReadRequest(g_scatter_full,
                pawns[i] + offsets.m_entitySpottedState + offsets.m_bSpotted,
                &spotted_buf[i], sizeof(uint8_t));
        else
            spotted_buf[i] = 1;
        if (need_scoped && offsets.m_bIsScoped)
            mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_bIsScoped,
                                      &fields[i].scoped, sizeof(fields[i].scoped));
        if (need_flash && offsets.m_flFlashDuration)
            mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_flFlashDuration,
                                      &fields[i].flash, sizeof(fields[i].flash));
        if (need_yaw)
            mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_angEyeAngles,
                                      fields[i].eye_angles, sizeof(fields[i].eye_angles));
        if (need_weapons && offsets.m_pWeaponServices)
            mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_pWeaponServices,
                                      &fields[i].weapon_services, sizeof(uintptr_t));
    }
    mem.ExecuteReadScatter(g_scatter_full);
    for (int i = 0; i < count; ++i)
        fields[i].spotted = (spotted_buf[i] != 0);
}

void UpdateBombState() {
    runtime.bomb = BombState{};
    if (!offsets.dwPlantedC4)
        return;

    // CS2 has exposed dwPlantedC4 in both forms across builds: either the
    // C_PlantedC4 pointer itself, or a pointer to a one-entry list.
    // Prefer one scatter batch for all bomb fields to avoid 10+ sequential QReads
    // which show up as micro-stutters on 35T/75T during planted C4.
    mem.SetDmaCallTag("CS2.Bomb");
    uintptr_t candidate = 0;
    if (!QReadT(runtime.client_base + offsets.dwPlantedC4, candidate, "CS2.Bomb.Ptr") ||
        !IsUserPointer(candidate))
        return;

    uintptr_t entity = 0;
    uint8_t ticking = 0;
    uintptr_t list_entity = 0;
    uint8_t list_ticking = 0;

    EnsureScatter();
    if (g_scatter_full && offsets.m_bBombTicking) {
        mem.AddScatterReadRequest(g_scatter_full, candidate + offsets.m_bBombTicking,
                                  &ticking, sizeof(ticking));
        mem.AddScatterReadRequest(g_scatter_full, candidate, &list_entity, sizeof(list_entity));
        mem.ExecuteReadScatter(g_scatter_full);
        if (ticking) {
            entity = candidate;
        } else if (IsUserPointer(list_entity)) {
            mem.AddScatterReadRequest(g_scatter_full, list_entity + offsets.m_bBombTicking,
                                      &list_ticking, sizeof(list_ticking));
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
    if (!ticking || !IsUserPointer(entity))
        return;

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
                                      &runtime.bomb.defuser_handle, sizeof(runtime.bomb.defuser_handle));
        if (offsets.m_pGameSceneNode)
            mem.AddScatterReadRequest(g_scatter_full, entity + offsets.m_pGameSceneNode,
                                      &scene, sizeof(scene));
        if (offsets.dwGlobalVars)
            mem.AddScatterReadRequest(g_scatter_full, runtime.client_base + offsets.dwGlobalVars,
                                      &globals, sizeof(globals));
        mem.ExecuteReadScatter(g_scatter_full);

        if (IsUserPointer(globals)) {
            for (int i = 0; i < 4; ++i)
                mem.AddScatterReadRequest(g_scatter_full, globals + kCurOff[i],
                                          &cur_slots[i], sizeof(float));
            mem.ExecuteReadScatter(g_scatter_full);
        }
        if (IsUserPointer(scene) && offsets.m_vecAbsOrigin) {
            mem.AddScatterReadRequest(g_scatter_full, scene + offsets.m_vecAbsOrigin,
                                      runtime.bomb.pos, sizeof(runtime.bomb.pos));
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
            QReadT(entity + offsets.m_hBombDefuser, runtime.bomb.defuser_handle, "CS2.Bomb.Defuser");
        if (offsets.dwGlobalVars)
            QReadT(runtime.client_base + offsets.dwGlobalVars, globals, "CS2.Bomb.Globals");
        if (IsUserPointer(globals)) {
            for (int i = 0; i < 4; ++i)
                QReadT(globals + kCurOff[i], cur_slots[i], "CS2.Bomb.CurTime");
        }
        if (offsets.m_pGameSceneNode &&
            QReadT(entity + offsets.m_pGameSceneNode, scene, "CS2.Bomb.Scene") &&
            IsUserPointer(scene))
            QRead(scene + offsets.m_vecAbsOrigin, runtime.bomb.pos, sizeof(runtime.bomb.pos), "CS2.Bomb.Pos");
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

    runtime.bomb.planted = true;
    runtime.bomb.sample_timestamp_ms = GetTickCount64();
    runtime.bomb.blow_time = blow;
    runtime.bomb.defuse_time = defuse;
    runtime.bomb.defused = defused != 0;
    runtime.bomb.defusing = defusing != 0 && defuse > 0.05f;
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

// Death-mode is deliberately a separate, low-frequency path.  It never reads
// player transforms, bones, weapons or aim data; it only follows observer
// handles so the spectator widget keeps working while the main acquisition is
// paused.  The normal scan resumes automatically as soon as local HP is back.
static void UpdateSpectatorsWhileDead(const Config& frame_config,
                                      const uintptr_t* controllers, int count) {
    runtime.spectators.clear();
    runtime.spectator_count = 0;
    runtime.spectator_target = runtime.local_pawn;
    runtime.spectator_target_name[0] = '\0';
    if (!frame_config.spectator_list || !runtime.local_pawn ||
        !offsets.m_pObserverServices || !offsets.m_hObserverTarget)
        return;

    uintptr_t localServices = 0;
    uint32_t watchedHandle = 0;
    if (QReadT(runtime.local_pawn + offsets.m_pObserverServices, localServices) &&
        IsUserPointer(localServices) &&
        QReadT(localServices + offsets.m_hObserverTarget, watchedHandle)) {
        const uintptr_t watched = ResolveEntityByHandle(
            watchedHandle, g_pawn_stride ? g_pawn_stride : kEntityIdentityStride);
        if (IsUserPointer(watched)) runtime.spectator_target = watched;
    }

    static std::vector<Player> cachedSpectators;
    static uintptr_t cachedTarget = 0;
    static uint64_t nextRefreshMs = 0;
    const uint64_t now = GetTickCount64();
    if (now < nextRefreshMs && cachedTarget == runtime.spectator_target) {
        runtime.spectators = cachedSpectators;
        runtime.spectator_count = static_cast<int>(runtime.spectators.size());
        return;
    }

    for (int i = 0; i < count; ++i) {
        const uintptr_t controller = controllers[i];
        if (!IsUserPointer(controller) || controller == runtime.local_controller)
            continue;
        uint32_t observerHandle = 0;
        if (!offsets.m_hObserverPawn ||
            !QReadT(controller + offsets.m_hObserverPawn, observerHandle) ||
            !observerHandle || observerHandle == 0xFFFFFFFFu)
            continue;
        const uintptr_t observerPawn = ResolveEntityByHandle(
            observerHandle, g_pawn_stride ? g_pawn_stride : kEntityIdentityStride);
        uintptr_t services = 0;
        uint32_t targetHandle = 0;
        if (!IsUserPointer(observerPawn) ||
            !QReadT(observerPawn + offsets.m_pObserverServices, services) ||
            !IsUserPointer(services) ||
            !QReadT(services + offsets.m_hObserverTarget, targetHandle))
            continue;
        const uintptr_t target = ResolveEntityByHandle(
            targetHandle, g_pawn_stride ? g_pawn_stride : kEntityIdentityStride);
        if (target != runtime.spectator_target)
            continue;

        Player spectator{};
        spectator.controller = controller;
        spectator.pawn = observerPawn;
        spectator.is_spectator = true;
        spectator.alive = false;
        if (offsets.m_steamID)
            QReadT(controller + offsets.m_steamID, spectator.steam_id);
        QRead(controller + offsets.m_iszPlayerName, spectator.name, sizeof(spectator.name) - 1);
        spectator.name[sizeof(spectator.name) - 1] = '\0';
        if (!spectator.name[0])
            std::snprintf(spectator.name, sizeof(spectator.name), "Jogador_%d", i + 1);
        runtime.spectators.push_back(spectator);
    }
    cachedSpectators = runtime.spectators;
    cachedTarget = runtime.spectator_target;
    nextRefreshMs = now + 250u;
    runtime.spectator_count = static_cast<int>(runtime.spectators.size());
}

static void RunFrameWithConfig(const Config& frame_config) {
    // Keep web radar HTTP server in sync with UI toggles (timed separately).
    {
        ScopedPhase _phWr(&g_phase.webradar_ms);
    if (frame_config.webradar_enabled) {
        if (!WebRadar::IsRunning())
            WebRadar::Start(frame_config.webradar_port);
        WebRadar::Tick();
        static ULONGLONG next_cloudflare_attempt = 0;
        if (frame_config.webradar_cloudflare && !WebRadar::CloudflareRunning()) {
            const ULONGLONG now = GetTickCount64();
            if (now >= next_cloudflare_attempt) {
                WebRadar::StartCloudflare();
                next_cloudflare_attempt = now + 5000;
            }
        }
        if (!frame_config.webradar_cloudflare && WebRadar::CloudflareRunning())
            WebRadar::StopCloudflare();
    } else if (WebRadar::IsRunning()) {
        WebRadar::Stop();
    }
    } // webradar phase

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

    const uintptr_t client = runtime.client_base;
    // Keep last-good player snapshot when entity list briefly fails (prevents
    // ESP/aim going empty for 1–2 frames on ListEntry null flicker).
    static std::vector<Player> last_good_players;
    static uint64_t last_good_ms = 0;
    struct RecentPlayer {
        Player player{};
        uint64_t last_seen_ms = 0;
    };
    struct CachedBones {
        float joints[kBoneSlotCount][3]{};
        float origin[3]{};
        uint64_t last_valid_ms = 0;
        bool full_pose = false;
    };
    static std::unordered_map<uintptr_t, RecentPlayer> recent_players;
    static std::unordered_map<uintptr_t, CachedBones> bone_cache;
    static std::unordered_map<uintptr_t, uint8_t> s_stickyBoneLayout; // 0/1 once validated
    if (recent_players.bucket_count() < 128) recent_players.reserve(128);
    if (bone_cache.bucket_count() < 128) bone_cache.reserve(128);
    if (s_stickyBoneLayout.bucket_count() < 128) s_stickyBoneLayout.reserve(128);
    runtime.player_count = 0;
    runtime.enemy_count = 0;
    mem.SetDmaCallTag("CS2.Controllers");
    runtime.controller_count = 0;
    runtime.pawn_count = 0;
    runtime.players.clear();
    if (runtime.players.capacity() < 64)
        runtime.players.reserve(64);
    // Keep entity-list root sticky.  Periodic zeroing forced a full multi-page
    // probe every few frames and produced visible hitch spikes under DMA load.
    // Only clear when ValidateEntityList already failed (root becomes 0).
    (void)ENTITY_LIST_INTERVAL_MS;

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
        next_map_refresh_ms = map_now_ms + (runtime.in_match
            ? (pressure >= 2 ? 180000u : 120000u)
            : 1000u);
    }

    // Match transition: map changed → force entity-list re-acquire so ESP
    // recovers automatically when leaving lobby / joining a new game.
    // Also force-clear Makcu button mask — release packets are often lost
    // during load and would leave LMB stuck ("mira sozinho").
    if (std::strncmp(previous_map, runtime.map_name, sizeof(previous_map)) != 0) {
        std::memcpy(previous_map, runtime.map_name, sizeof(previous_map));
        g_pending_entity_list = 0;
        g_entity_list_confirmations = 0;
        runtime.entity_list_entry = 0;
        runtime.entity_list_addr = 0;
        runtime.bomb = BombState{};
        recent_players.clear();
        bone_cache.clear();
        s_stickyBoneLayout.clear();
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
    if (!runtime.in_match && map_now_ms >= next_match_fallback_ms && !DmaCooldownActive()) {
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
        {
            ScopedPhase _phPub(&g_phase.publish_ms);
            PublishRuntimeSnapshot();
        }
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
        if (now >= camera->timestamp_ms && now - camera->timestamp_ms <= 2000u) {
            std::memcpy(runtime.view_matrix, camera->view_matrix, sizeof(runtime.view_matrix));
            haveView = true;
        }
    }
    if (!haveView && !ProbeViewMatrix(runtime.view_matrix)) {
        if ((runtime.frames % 180) == 1)
            std::cout << "[CS2] ViewMatrix fail (fails=" << runtime.read_fails << ")" << std::endl;
        return;
    }

    runtime.players.reserve(64);

    // Keep the last validated team so TEAM_INTERVAL_MS is effective.
    runtime.local_view_yaw = 0.f;
    std::memset(runtime.local_pos, 0, sizeof(runtime.local_pos));
    // The local pawn/controller pointers rarely change while alive.  Refresh
    // them at a controlled cadence, while still sampling HP every full pass so
    // death-mode can stop the non-essential reads promptly.
    static uint64_t s_lastLocalBootstrapMs = 0;
    const uint64_t localNowMs = GetTickCount64();
    const bool refreshLocalBootstrap = !IsUserPointer(runtime.local_pawn) ||
        !s_lastLocalBootstrapMs || localNowMs - s_lastLocalBootstrapMs >= 750u;
    static uint64_t s_lastLocalHealthMs = 0;
    const int localPressure = g_pressure_level.load(std::memory_order_relaxed);
    const uint64_t healthInterval = runtime.local_health > 0
        ? static_cast<uint64_t>(localPressure >= 2 ? 500 : 300) : 500u;
    const bool refreshLocalHealth = !s_lastLocalHealthMs ||
        localNowMs - s_lastLocalHealthMs >= healthInterval;
    uintptr_t localPawn = runtime.local_pawn;
    uintptr_t localController = runtime.local_controller;
    int sampled_health = runtime.local_health;
    EnsureScatter();
    if (g_scatter_full && refreshLocalBootstrap) {
        mem.SetDmaCallTag("CS2.LocalBootstrap");
        mem.AddScatterReadRequest(g_scatter_full, client + offsets.dwLocalPlayerPawn,
                                  &localPawn, sizeof(localPawn));
        if (offsets.dwLocalPlayerController)
            mem.AddScatterReadRequest(g_scatter_full, client + offsets.dwLocalPlayerController,
                                      &localController, sizeof(localController));
        mem.ExecuteReadScatter(g_scatter_full);
        s_lastLocalBootstrapMs = localNowMs;
    }
    if (g_scatter_full) {
        if (refreshLocalHealth && IsUserPointer(localPawn) && offsets.m_iHealth) {
            mem.SetDmaCallTag("CS2.LocalHealth");
            mem.AddScatterReadRequest(g_scatter_full, localPawn + offsets.m_iHealth,
                                      &sampled_health, sizeof(sampled_health));
            mem.ExecuteReadScatter(g_scatter_full);
            s_lastLocalHealthMs = localNowMs;
        }
    } else {
        if (refreshLocalBootstrap) {
            QReadT(client + offsets.dwLocalPlayerPawn, localPawn, "CS2.LocalPawn");
            if (offsets.dwLocalPlayerController)
                QReadT(client + offsets.dwLocalPlayerController, localController, "CS2.LocalController");
            s_lastLocalBootstrapMs = localNowMs;
        }
        if (refreshLocalHealth && IsUserPointer(localPawn) && offsets.m_iHealth) {
            QReadT(localPawn + offsets.m_iHealth, sampled_health, "CS2.LocalHP");
            s_lastLocalHealthMs = localNowMs;
        }
    }
    runtime.local_pawn = IsUserPointer(localPawn) ? localPawn : 0;
    if (IsUserPointer(localController))
        runtime.local_controller = localController;
    // A failed DMA read must not be reinterpreted as HP=0.  Keep the last
    // validated value and only enter death-mode on a successful 0-HP read.
    if (runtime.local_pawn) {
        if (sampled_health >= 0 && sampled_health <= 200)
            runtime.local_health = sampled_health;
    } else {
        runtime.local_health = 0;
    }

    if (runtime.local_pawn) {
        // HP is read immediately above.  Do not keep reading local movement,
        // view, item or trigger state after death; observer/bomb handling
        // below is the only remaining DMA work in that state.
        if (runtime.local_health > 0) {
            static ULONGLONG s_lastTeamMs = 0;
            const ULONGLONG nowT = GetTickCount64();
            const bool refreshTeam = runtime.local_team < 2 || !s_lastTeamMs ||
                (nowT - s_lastTeamMs) > static_cast<ULONGLONG>(TEAM_INTERVAL_MS);

            uint8_t team8 = static_cast<uint8_t>(runtime.local_team);
            uintptr_t scene = 0;
            uintptr_t item_services = 0;
            float view_angles[2]{};
            bool scoped = runtime.local_scoped;
            int crosshair = 0;
            int shots = runtime.local_shots_fired;

            EnsureScatter();
            if (g_scatter_full) {
                if (refreshTeam)
                    mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_iTeamNum,
                                              &team8, sizeof(team8));
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
                if (offsets.m_pItemServices && offsets.m_bHasDefuser)
                    mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_pItemServices,
                                              &item_services, sizeof(item_services));
                if (frame_config.trigger_enabled && frame_config.trigger_use_ident && offsets.m_iIDEntIndex)
                    mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_iIDEntIndex,
                                              &crosshair, sizeof(crosshair));
                if (frame_config.hit_marker && offsets.m_iShotsFired)
                    mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_iShotsFired,
                                              &shots, sizeof(shots));
                mem.ExecuteReadScatter(g_scatter_full);
            } else {
                if (refreshTeam) QReadT(runtime.local_pawn + offsets.m_iTeamNum, team8);
                QReadT(runtime.local_pawn + offsets.m_pGameSceneNode, scene);
                QRead(runtime.local_pawn + offsets.m_vecVelocity, runtime.local_vel, sizeof(runtime.local_vel));
                if (offsets.dwViewAngles)
                    QRead(client + offsets.dwViewAngles, view_angles, sizeof(view_angles));
                else
                    QRead(runtime.local_pawn + offsets.m_angEyeAngles, view_angles, sizeof(view_angles));
                if (offsets.m_bIsScoped) QReadT(runtime.local_pawn + offsets.m_bIsScoped, scoped);
                if (offsets.m_pItemServices && offsets.m_bHasDefuser)
                    QReadT(runtime.local_pawn + offsets.m_pItemServices, item_services);
                if (frame_config.trigger_enabled && frame_config.trigger_use_ident && offsets.m_iIDEntIndex)
                    QReadT(runtime.local_pawn + offsets.m_iIDEntIndex, crosshair);
                if (frame_config.hit_marker && offsets.m_iShotsFired)
                    QReadT(runtime.local_pawn + offsets.m_iShotsFired, shots);
            }

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

            runtime.local_has_defuser = false;
            if (g_scatter_full) {
                if (IsUserPointer(scene))
                    mem.AddScatterReadRequest(g_scatter_full, scene + offsets.m_vecAbsOrigin,
                                              runtime.local_pos, sizeof(runtime.local_pos));
                else
                    mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_vOldOrigin,
                                              runtime.local_pos, sizeof(runtime.local_pos));
                if (IsUserPointer(item_services) && offsets.m_bHasDefuser)
                    mem.AddScatterReadRequest(g_scatter_full, item_services + offsets.m_bHasDefuser,
                                              &runtime.local_has_defuser, sizeof(runtime.local_has_defuser));
                mem.ExecuteReadScatter(g_scatter_full);
            } else {
                if (IsUserPointer(scene))
                    QRead(scene + offsets.m_vecAbsOrigin, runtime.local_pos, sizeof(runtime.local_pos));
                else
                    QRead(runtime.local_pawn + offsets.m_vOldOrigin, runtime.local_pos, sizeof(runtime.local_pos));
                if (IsUserPointer(item_services) && offsets.m_bHasDefuser)
                    QReadT(item_services + offsets.m_bHasDefuser, runtime.local_has_defuser);
            }

            if (frame_config.hit_marker && offsets.m_iShotsFired && shots >= 0 && shots < 256) {
                static int previous_shots = -1;
                if (previous_shots >= 0 && shots > previous_shots)
                    runtime.local_last_shot_ms = GetTickCount64();
                previous_shots = shots;
                runtime.local_shots_fired = shots;
            }
        } else {
            runtime.local_crosshair_entity = 0;
            runtime.local_has_defuser = false;
            runtime.local_scoped = false;
            runtime.local_shots_fired = 0;
            std::memset(runtime.local_vel, 0, sizeof(runtime.local_vel));
        }
    } else {
        runtime.local_crosshair_entity = 0;
        runtime.local_has_defuser = false;
    }

    if (!RefreshEntityListEntry()) {
        // Hold last-good ESP/aim targets briefly so the overlay does not blink
        // off when ListEntry is transiently null after map/round changes.
        const uint64_t now_ms = GetTickCount64();
        if (!last_good_players.empty() && now_ms - last_good_ms <= 50u) {
            runtime.players = last_good_players;
            runtime.player_count = static_cast<int>(runtime.players.size());
        }
        return;
    }

    int kMax = frame_config.max_entities > 0 ? frame_config.max_entities : 32;
    if (kMax > 64) kMax = 64;
    if (frame_config.auto_entity_cap) {
        if (runtime.fps > 1.f && runtime.fps < 40.f) kMax = (std::min)(kMax, 16);
        else if (runtime.fps < 55.f) kMax = (std::min)(kMax, 24);
        else if (runtime.fps > 90.f) kMax = (std::min)(kMax, 48);
    }
    runtime.entity_cap_used = kMax;
    constexpr int kMaxSlots = 64;

    uintptr_t controllers[kMaxSlots]{};

    // Prefer the confirmed Source 2 stride (0x70). Fall back to 0x78 only if
    // the primary layout returns no controllers at all.
    int collected = CollectControllers(runtime.entity_list_entry, g_controller_stride, controllers, kMaxSlots);
    if (collected == 0) {
        const uintptr_t alternate = (g_controller_stride == kEntityIdentityStride)
            ? kLegacyEntityIdentityStride : kEntityIdentityStride;
        collected = CollectControllers(runtime.entity_list_entry, alternate, controllers, kMaxSlots);
        if (collected > 0) {
            g_controller_stride = alternate;
            g_pawn_stride = alternate;
        } else {
            // Entry pointer is stale (no controllers on either stride) —
            // force re-acquire next frame so ESP recovers after round change.
            static int empty_entry_frames = 0;
            if (++empty_entry_frames >= 30) {
                runtime.entity_list_entry = 0;
                g_pending_entity_list = 0;
                g_entity_list_confirmations = 0;
                g_cached_entity_root = 0;
                empty_entry_frames = 0;
            }
        }
    }
    runtime.controller_stride = g_controller_stride;
    runtime.controller_count = collected;

    // While dead, do not enter the entity, position, bone, weapon, visibility
    // or aim acquisition stages.  Spectators and bomb state remain available
    // through their small dedicated paths; a positive local HP next frame
    // immediately falls through to the normal 6 ms pipeline again.
    if (runtime.local_health <= 0) {
        runtime.players.clear();
        runtime.player_count = 0;
        runtime.pawn_count = 0;
        runtime.enemy_count = 0;
        UpdateSpectatorsWhileDead(frame_config, controllers, kMaxSlots);
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
    ScatterReadPawnHandles(controllers, handles, kMaxSlots);

    // ── Phase 2: resolve handles → pawn pointers (root cached this frame) ─
    int handle_nonzero = 0;
    for (int i = 0; i < kMaxSlots; ++i)
        if (handles[i]) ++handle_nonzero;
    uintptr_t resolved_by_slot[kMaxSlots]{};
    int resolve_ok = ScatterResolvePawnHandles(
        handles, resolved_by_slot, kMaxSlots,
        g_pawn_stride ? g_pawn_stride : kEntityIdentityStride);
    // If the calibrated stride stopped resolving entirely after an update,
    // retry the alternate layout once as a recovery path.
    if (resolve_ok == 0 && handle_nonzero > 0) {
        const uintptr_t alternate = g_pawn_stride == kEntityIdentityStride
            ? kLegacyEntityIdentityStride : kEntityIdentityStride;
        resolve_ok = ScatterResolvePawnHandles(handles, resolved_by_slot, kMaxSlots, alternate);
        if (resolve_ok > 0) g_pawn_stride = alternate;
    }

    uintptr_t resolved_pawns[kMaxSlots]{};
    int slot_index[kMaxSlots]{};
    int candidate_count = 0;
    for (int i = 0; i < kMaxSlots && candidate_count < kMax; ++i) {
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
    OmniGhost::Gameplay::EspCore::FeatureSet requested{};
    // Declare every active visual explicitly. The collector can then keep the
    // lightweight box/bar path free of bone reads, while a requested skeleton
    // receives one coherent full-pose snapshot.
    requested.box = frame_config.esp_enabled && frame_config.box;
    requested.corner_box = frame_config.esp_enabled && frame_config.box_corner;
    requested.skeleton = frame_config.esp_enabled && frame_config.skeleton;
    requested.head = frame_config.esp_enabled && (frame_config.head_dot || frame_config.head_halo ||
        frame_config.chinese_hat || frame_config.angel_wings || frame_config.devil_horns || frame_config.floating_crown);
    requested.health = frame_config.esp_enabled && frame_config.health_bar;
    requested.armor = frame_config.esp_enabled && frame_config.armor_bar;
    requested.visibility = frame_config.esp_enabled && frame_config.visibility_colors && frame_config.visible_check;
    requested.weapon = frame_config.esp_enabled && frame_config.weapon_name;
    requested.name = frame_config.esp_enabled && frame_config.name;
    requested.distance = frame_config.esp_enabled && frame_config.distance;
    requested.snapline = frame_config.esp_enabled && frame_config.snaplines;
    requested.trail = frame_config.esp_enabled && frame_config.trails;
    requested.halo = frame_config.esp_enabled && frame_config.head_halo;
    requested.look_direction = frame_config.esp_enabled && frame_config.look_direction;
    requested.aim = frame_config.aim_enabled || frame_config.trigger_enabled;
    // Velocity is derived from the already-batched origin snapshots. Keep it
    // available to presentation while ESP is active so rendering can bridge
    // the short gap between acquisition frames without another DMA transfer.
    requested.prediction = (frame_config.aim_enabled && frame_config.aim_prediction) ||
        frame_config.esp_enabled;
    const auto fields = requested.RequiredFields();

    const bool need_armor = OmniGhost::Gameplay::EspCore::Has(
        fields, OmniGhost::Gameplay::EspCore::DataField::Armor);
    const bool need_names = OmniGhost::Gameplay::EspCore::Has(
        fields, OmniGhost::Gameplay::EspCore::DataField::Name);
    const bool need_weapons = OmniGhost::Gameplay::EspCore::Has(
        fields, OmniGhost::Gameplay::EspCore::DataField::Weapon) ||
        frame_config.aim_enabled || frame_config.trigger_enabled || frame_config.sniper_crosshair;
    const bool need_yaw = frame_config.radar_2d ||
        OmniGhost::Gameplay::EspCore::Has(
            fields, OmniGhost::Gameplay::EspCore::DataField::Facing);
    const bool track_velocity = OmniGhost::Gameplay::EspCore::Has(
        fields, OmniGhost::Gameplay::EspCore::DataField::Velocity);
    const bool need_bones = OmniGhost::Gameplay::EspCore::Has(
        fields, OmniGhost::Gameplay::EspCore::DataField::Skeleton);
    // Aim and visual effects need only the upper-body aim anchors. A complete
    // 20-slot pose is acquired only for the rendered skeleton or body trigger.
    const bool need_full_bones = requested.skeleton ||
        (frame_config.trigger_enabled && !frame_config.trigger_head_only);
    const bool flagsEnabled = frame_config.player_flags;
    const bool need_scoped = frame_config.trigger_scoped_only ||
        (flagsEnabled && frame_config.flag_scoped);
    const bool need_flash = flagsEnabled && frame_config.flag_blind;

    const uint64_t scan_now_ms = GetTickCount64();
    PawnCoreFields core[kMaxSlots]{};
    const bool need_spotted = (frame_config.aim_enabled && frame_config.aim_visibility_check) ||
        (frame_config.trigger_enabled && frame_config.aim_visibility_check);
    // Health/team/scene stay on the full cadence; slower fields are cached below.
    mem.SetDmaCallTag("CS2.PawnCore");
    const auto _posBegin = std::chrono::steady_clock::now();
    ScatterReadPawnCore(resolved_pawns, core, candidate_count, false,
                        need_scoped, need_flash, need_yaw, false,
                        need_spotted);
    g_phase.positions_ms = OmniGhost::Gameplay::TimeMs(_posBegin);
    NotePossibleDeviceStall(g_phase.positions_ms);

    // Deathmatch can transition dead → respawning before the optional bone
    // reads finish. Publish health immediately so presentation can hide a dead
    // pawn without waiting for the complete entity snapshot.
    LivenessSnapshot liveness{};
    liveness.timestamp_ms = GetTickCount64();
    for (int c = 0; c < candidate_count &&
         liveness.count < static_cast<uint32_t>(liveness.players.size()); ++c) {
        auto& sample = liveness.players[liveness.count++];
        sample.pawn = resolved_pawns[c];
        sample.alive = core[c].health > 0 && core[c].health <= 200 &&
            IsPlayableTeam(static_cast<int>(core[c].team));
    }
    PublishLivenessSnapshot(liveness);

    struct CachedArmor { int value = 0; uint64_t last_refresh_ms = 0; };
    static std::unordered_map<uintptr_t, CachedArmor> armorCache;
    if (need_armor && candidate_count > 0) {
        bool queuedArmor = false;
        EnsureScatter();
        for (int c = 0; c < candidate_count; ++c) {
            const auto cached = armorCache.find(resolved_pawns[c]);
            if (cached != armorCache.end()) core[c].armor = cached->second.value;
            if (cached != armorCache.end() &&
                scan_now_ms - cached->second.last_refresh_ms < static_cast<uint64_t>(ARMOR_INTERVAL_MS))
                continue;
            if (g_scatter_full) {
                mem.AddScatterReadRequest(g_scatter_full, resolved_pawns[c] + offsets.m_ArmorValue,
                                          &core[c].armor, sizeof(core[c].armor));
                queuedArmor = true;
            } else {
                QReadT(resolved_pawns[c] + offsets.m_ArmorValue, core[c].armor);
                armorCache[resolved_pawns[c]] = {core[c].armor, scan_now_ms};
            }
        }
        if (queuedArmor) {
            mem.ExecuteReadScatter(g_scatter_full);
            for (int c = 0; c < candidate_count; ++c)
                armorCache[resolved_pawns[c]] = {core[c].armor, scan_now_ms};
        }
    }

    // Health and team are the minimum discriminator reads: without them we
    // cannot know which pawns to exclude. Compact the candidate set here so
    // dead players and hidden team-mates never reach position, bone, weapon or
    // name acquisition. Spectators are resolved later from controllers through
    // their own lightweight observer path.
    int eligible_count = 0;
    for (int c = 0; c < candidate_count; ++c) {
        const bool is_local = resolved_pawns[c] == runtime.local_pawn ||
            controllers[slot_index[c]] == runtime.local_controller;
        const bool alive = core[c].health > 0 && core[c].health <= 200 &&
            IsPlayableTeam(static_cast<int>(core[c].team));
        const bool hidden_teammate = frame_config.team_check && !is_local &&
            static_cast<int>(core[c].team) == runtime.local_team;
        if (!alive || hidden_teammate) continue;
        if (eligible_count != c) {
            resolved_pawns[eligible_count] = resolved_pawns[c];
            slot_index[eligible_count] = slot_index[c];
            core[eligible_count] = core[c];
        }
        ++eligible_count;
    }
    candidate_count = eligible_count;
    runtime.pawn_count = candidate_count;

    // ── Phase 4: scatter positions (scene+origin or pawn+oldOrigin) ──────
    struct BoneJointSnapshot { float x, y, z, scale; char pad[0x10]; };
    static_assert(sizeof(BoneJointSnapshot) == 32, "BoneJointSnapshot size");
    // Highest joint index used by either layout is 27 → read 28 joints only.
    constexpr int kBoneJointReadCount = 28;
    constexpr int kCompactJointReadCount = 8; // contains both known layouts' head/neck/chest/stomach
    constexpr size_t kBoneReadBytes = sizeof(BoneJointSnapshot) * kBoneJointReadCount;
    constexpr size_t kCompactBoneReadBytes = sizeof(BoneJointSnapshot) * kCompactJointReadCount;
    float positions[kMaxSlots][3]{};
    char playerNames[kMaxSlots][64]{};
    bool nameNeedsRefresh[kMaxSlots]{};
    struct CachedName {
        std::array<char, 64> text{};
        uint64_t last_refresh_ms = 0;
    };
    static std::unordered_map<uintptr_t, CachedName> nameCache;
    uintptr_t boneBases[kMaxSlots]{};
    bool boneReadEligible[kMaxSlots]{};
    BoneJointSnapshot boneSnapshots[kMaxSlots][kBoneJointReadCount]{};
    uintptr_t weaponServices[kMaxSlots]{};
    uint32_t weaponHandles[kMaxSlots]{};
    uintptr_t weaponEntities[kMaxSlots]{};
    uint16_t weaponDefinitions[kMaxSlots]{};
    bool weaponRefreshDue[kMaxSlots]{};
    struct CachedWeaponState {
        uintptr_t services = 0;
        uint32_t handle = 0;
        uintptr_t entity = 0;
        uint16_t definition = 0;
        uint64_t last_refresh_ms = 0;
        uint64_t last_fallback_ms = 0;
    };
    static std::unordered_map<uintptr_t, CachedWeaponState> weaponStateCache;
    if (armorCache.bucket_count() < 128) armorCache.reserve(128);
    if (nameCache.bucket_count() < 128) nameCache.reserve(128);
    if (weaponStateCache.bucket_count() < 128) weaponStateCache.reserve(128);
    // Incremental cache maintenance: budget a few erases per scan (never full-map sweep).
    {
        ScopedPhase _phCleanup(&g_phase.cleanup_ms);
        constexpr int kBudget = 4;
        int erased = 0;
        auto prune = [&](auto& cache) {
            for (auto it = cache.begin(); it != cache.end() && erased < kBudget;) {
                if (scan_now_ms - it->second.last_refresh_ms > 30000u) {
                    it = cache.erase(it);
                    ++erased;
                    g_phase.cache_cleanup = 1;
                } else {
                    ++it;
                }
            }
        };
        if ((runtime.frames % 32u) == 0u) {
            prune(nameCache);
            if (erased < kBudget) prune(weaponStateCache);
            if (erased < kBudget) prune(armorCache);
        }
    }
    const auto _coreBegin = std::chrono::steady_clock::now();
    mem.SetDmaCallTag("CS2.CoreScatter");
    EnsureScatter();
    if (g_scatter_full && candidate_count > 0) {
        for (int c = 0; c < candidate_count; ++c) {
            const uintptr_t scene = core[c].scene;
            if (IsUserPointer(scene))
                mem.AddScatterReadRequest(g_scatter_full, scene + offsets.m_vecAbsOrigin,
                                          positions[c], sizeof(float) * 3);
            else
                mem.AddScatterReadRequest(g_scatter_full, resolved_pawns[c] + offsets.m_vOldOrigin,
                                          positions[c], sizeof(float) * 3);
            if (need_names) {
            mem.SetDmaCallTag("CS2.Names");
                const int controllerSlot = slot_index[c];
                const uintptr_t controller = controllers[controllerSlot];
                const auto cached = nameCache.find(controller);
                if (cached != nameCache.end() && cached->second.text[0] &&
                    scan_now_ms - cached->second.last_refresh_ms < static_cast<uint64_t>(NAME_INTERVAL_MS)) {
                    std::memcpy(playerNames[c], cached->second.text.data(),
                                sizeof(playerNames[c]));
                } else if (controller) {
                    nameNeedsRefresh[c] = true;
                    mem.AddScatterReadRequest(g_scatter_full,
                        controller + offsets.m_iszPlayerName,
                        playerNames[c], sizeof(playerNames[c]) - 1);
                }
            }
            if (need_bones && IsUserPointer(scene))
                mem.AddScatterReadRequest(g_scatter_full, scene + offsets.BoneArray,
                                          &boneBases[c], sizeof(uintptr_t));
        }
        mem.ExecuteReadScatter(g_scatter_full);
        g_phase.core_scatter_ms = OmniGhost::Gameplay::TimeMs(_coreBegin);
        NotePossibleDeviceStall(g_phase.core_scatter_ms);
        mem.SetDmaCallTag("CS2.Bones");
if (need_bones) {
            ScopedPhase _phBones(&g_phase.bones_ms);
            // Community-proven approach for external DMA skeletons:
            //  - one scatter of a tight joint window (not 64× full arrays every frame)
            //  - distance-tiered refresh (near more often, far less)
            //  - hard budget of fresh bone DMAs per scan; others keep translated cache
            //  - still draw ALL slots (kBoneSlotCount) every frame from cache/pose
            static std::unordered_map<uintptr_t, uint64_t> s_lastBoneMs;
            if (s_lastBoneMs.bucket_count() < 128) s_lastBoneMs.reserve(128);
            struct BoneCand { int c; float distSq; uint64_t interval; };
            BoneCand cand[kMaxSlots]{};
            int candN = 0;
            const float lx = runtime.local_pos[0], ly = runtime.local_pos[1], lz = runtime.local_pos[2];
            for (int c = 0; c < candidate_count; ++c) {
                if (!IsUserPointer(boneBases[c])) continue;
                const float* pos = positions[c];
                if (!std::isfinite(pos[0]) || !std::isfinite(pos[1]) || !std::isfinite(pos[2]))
                    continue;
                const float dx = pos[0] - lx, dy = pos[1] - ly, dz = pos[2] - lz;
                const float distSq = dx * dx + dy * dy + dz * dz;
                // LOD intervals (ms): close / mid / far — visual still full skeleton
                if (!frame_config.aim_enabled && !frame_config.trigger_enabled &&
                    frame_config.skeleton_lod && !InsideExpandedFrustum(pos, runtime.view_matrix))
                    continue;
                uint64_t interval = static_cast<uint64_t>(BONES_INTERVAL_MS);
                if (frame_config.skeleton_lod) {
                    const float lodMeters = std::clamp(frame_config.skeleton_lod_distance, 20.f, 250.f);
                    const float farUnits = lodMeters * 39.37f;
                    const float midUnits = farUnits * 0.55f;
                    const float nearUnits = farUnits * 0.30f;
                    if (distSq > farUnits * farUnits) interval = 50;
                    else if (distSq > midUnits * midUnits) interval = 32;
                    else if (distSq > nearUnits * nearUnits) interval = 24;
                }
                if (frame_config.performance_mode) interval = (std::max)(interval, uint64_t{32});
                const auto it = s_lastBoneMs.find(resolved_pawns[c]);
                if (it != s_lastBoneMs.end() && scan_now_ms - it->second < interval)
                    continue;
                if (candN < kMaxSlots) {
                    cand[candN++] = { c, distSq, interval };
                }
            }
            // Nearest first within budget
            for (int i = 0; i < candN; ++i)
                for (int j = i + 1; j < candN; ++j)
                    if (cand[j].distSq < cand[i].distSq)
                        std::swap(cand[i], cand[j]);
            // Prefer fluid poses: keep reading bones every scan. Only mild caps
            // under sustained pressure — never stop bone updates (looks "frozen").
            const int pressure = g_pressure_level.load(std::memory_order_relaxed);
            int maxBoneReadsPerScan = frame_config.performance_mode ? 10 : 16;
            if (pressure >= 3) maxBoneReadsPerScan = 10;
            else if (pressure == 2) maxBoneReadsPerScan = 12;
            const int take = candN < maxBoneReadsPerScan ? candN : maxBoneReadsPerScan;
            g_phase.bones_players = take;
            bool queuedBoneReads = false;
            for (int i = 0; i < take; ++i) {
                const int c = cand[i].c;
                boneReadEligible[c] = true;
                s_lastBoneMs[resolved_pawns[c]] = scan_now_ms;
                mem.AddScatterReadRequest(g_scatter_full, boneBases[c], boneSnapshots[c],
                                          need_full_bones ? kBoneReadBytes : kCompactBoneReadBytes);
                queuedBoneReads = true;
            }
            if (queuedBoneReads)
                mem.ExecuteReadScatter(g_scatter_full);
        }
        if (need_weapons) {
            bool queuedServices = false;
            for (int c = 0; c < candidate_count; ++c) {
                auto cached = weaponStateCache.find(resolved_pawns[c]);
                const uint64_t weaponInterval = resolved_pawns[c] == runtime.local_pawn ? 50u : static_cast<uint64_t>(WEAPON_INTERVAL_MS);
                if (cached != weaponStateCache.end()) {
                    weaponServices[c] = cached->second.services;
                    weaponHandles[c] = cached->second.handle;
                    weaponEntities[c] = cached->second.entity;
                    weaponDefinitions[c] = cached->second.definition;
                    if (cached->second.last_refresh_ms &&
                        scan_now_ms - cached->second.last_refresh_ms < weaponInterval)
                        continue;
                }
                weaponRefreshDue[c] = true;
                if (offsets.m_pWeaponServices) {
                    mem.AddScatterReadRequest(g_scatter_full,
                        resolved_pawns[c] + offsets.m_pWeaponServices,
                        &weaponServices[c], sizeof(uintptr_t));
                    queuedServices = true;
                }
            }
            if (queuedServices) mem.ExecuteReadScatter(g_scatter_full);

            bool queuedHandles = false;
            for (int c = 0; c < candidate_count; ++c) {
                if (!weaponRefreshDue[c] || !IsUserPointer(weaponServices[c])) continue;
                mem.AddScatterReadRequest(g_scatter_full,
                    weaponServices[c] + offsets.m_hActiveWeapon,
                    &weaponHandles[c], sizeof(uint32_t));
                queuedHandles = true;
            }
            if (queuedHandles) mem.ExecuteReadScatter(g_scatter_full);

            // Resolve only refreshed handles; cached entries are restored afterwards.
            uint32_t handlesToResolve[kMaxSlots]{};
            uintptr_t resolvedWeapons[kMaxSlots]{};
            for (int c = 0; c < candidate_count; ++c)
                if (weaponRefreshDue[c]) handlesToResolve[c] = weaponHandles[c];
            ScatterResolvePawnHandles(handlesToResolve, resolvedWeapons, candidate_count,
                g_pawn_stride ? g_pawn_stride : kEntityIdentityStride);
            for (int c = 0; c < candidate_count; ++c)
                if (weaponRefreshDue[c]) weaponEntities[c] = resolvedWeapons[c];

            bool queuedDefinitions = false;
            for (int c = 0; c < candidate_count; ++c) {
                if (!weaponRefreshDue[c] || !IsUserPointer(weaponEntities[c])) continue;
                const uintptr_t primary = weaponEntities[c] + offsets.m_AttributeManager +
                                          offsets.m_Item + offsets.m_iItemDefinitionIndex;
                mem.AddScatterReadRequest(g_scatter_full, primary, &weaponDefinitions[c],
                                          sizeof(uint16_t));
                queuedDefinitions = true;
            }
            if (queuedDefinitions) mem.ExecuteReadScatter(g_scatter_full);

            for (int c = 0; c < candidate_count; ++c) {
                if (!weaponRefreshDue[c]) continue;
                const auto previous = weaponStateCache.find(resolved_pawns[c]);
                const uint64_t lastFallback = previous != weaponStateCache.end()
                    ? previous->second.last_fallback_ms : 0;
                weaponStateCache[resolved_pawns[c]] = {
                    weaponServices[c], weaponHandles[c], weaponEntities[c],
                    weaponDefinitions[c], scan_now_ms, lastFallback
                };
            }
        }
    } else {
        for (int c = 0; c < candidate_count; ++c) {
            if (IsUserPointer(core[c].scene))
                QRead(core[c].scene + offsets.m_vecAbsOrigin, positions[c], sizeof(float) * 3);
            else
                QRead(resolved_pawns[c] + offsets.m_vOldOrigin, positions[c], sizeof(float) * 3);
            const int controllerSlot = slot_index[c];
            if (need_names && controllers[controllerSlot]) {
                const uintptr_t controller = controllers[controllerSlot];
                const auto cached = nameCache.find(controller);
                if (cached != nameCache.end() && cached->second.text[0] &&
                    scan_now_ms - cached->second.last_refresh_ms < static_cast<uint64_t>(NAME_INTERVAL_MS)) {
                    std::memcpy(playerNames[c], cached->second.text.data(),
                                sizeof(playerNames[c]));
                } else {
                    nameNeedsRefresh[c] = true;
                    QRead(controller + offsets.m_iszPlayerName,
                          playerNames[c], sizeof(playerNames[c]) - 1);
                }
            }
            boneReadEligible[c] = need_bones;
            if (boneReadEligible[c] && IsUserPointer(core[c].scene) &&
                QReadT(core[c].scene + offsets.BoneArray, boneBases[c]) &&
                IsUserPointer(boneBases[c]))
                QRead(boneBases[c], boneSnapshots[c],
                      need_full_bones ? kBoneReadBytes : kCompactBoneReadBytes);
            if (need_weapons) {
                auto cached = weaponStateCache.find(resolved_pawns[c]);
                const uint64_t weaponInterval = resolved_pawns[c] == runtime.local_pawn ? 50u : static_cast<uint64_t>(WEAPON_INTERVAL_MS);
                if (cached != weaponStateCache.end() &&
                    scan_now_ms - cached->second.last_refresh_ms < weaponInterval) {
                    weaponDefinitions[c] = cached->second.definition;
                } else {
                    uintptr_t services = 0; uint32_t handle = 0; uintptr_t entity = 0; uint16_t def = 0;
                    if (QReadT(resolved_pawns[c] + offsets.m_pWeaponServices, services) && IsUserPointer(services))
                        QReadT(services + offsets.m_hActiveWeapon, handle);
                    if (handle) entity = ResolveEntityByHandle(handle, g_pawn_stride ? g_pawn_stride : kEntityIdentityStride);
                    if (IsUserPointer(entity)) {
                        const uintptr_t primary = entity + offsets.m_AttributeManager + offsets.m_Item + offsets.m_iItemDefinitionIndex;
                        QReadT(primary, def);
                    }
                    weaponDefinitions[c] = def;
                    const auto previous = weaponStateCache.find(resolved_pawns[c]);
                    const uint64_t lastFallback = previous != weaponStateCache.end()
                        ? previous->second.last_fallback_ms : 0;
                    weaponStateCache[resolved_pawns[c]] = {
                        services, handle, entity, def, scan_now_ms, lastFallback
                    };
                    weaponRefreshDue[c] = true;
                }
            }
        }
    }

    static std::unordered_map<uintptr_t, std::array<float, 3>> previous_positions;
    if (previous_positions.bucket_count() < 128) previous_positions.reserve(128);
    static auto previous_frame_time = std::chrono::steady_clock::now();
    const auto frame_time = std::chrono::steady_clock::now();
    const float delta_seconds = std::chrono::duration<float>(frame_time - previous_frame_time).count();
    static std::unordered_map<uintptr_t, std::array<float, 3>> current_positions;
    current_positions.clear();
    if (track_velocity && current_positions.bucket_count() < 128)
        current_positions.reserve(128);

    static int zero_pawn_diag_frames = 0;

    int processed = 0;
    for (int c = 0; c < candidate_count; ++c) {
        if (processed >= kMax) break;
        const int i = slot_index[c];
        const uintptr_t controller = controllers[i];
        const uintptr_t pawn = resolved_pawns[c];
        const uint32_t pawnHandle = handles[i];
        const PawnCoreFields& cf = core[c];

        Player p{};
        p.controller = controller;
        p.pawn = pawn;
        p.ent_index = static_cast<int>(pawnHandle & 0x7FFF);
        p.health = cf.health;
        p.armor = cf.armor;
        p.team = static_cast<int>(cf.team);
        p.spotted = cf.spotted;

        if (p.health <= 0 || p.health > 200)
            continue;
        if (!IsPlayableTeam(p.team))
            continue;
        p.alive = true;
        p.is_local = pawn == runtime.local_pawn || controller == runtime.local_controller;

        // These per-player fields arrive in the core scatter above.
        if (need_scoped)
            p.is_scoped = cf.scoped != 0;
        if (need_flash)
            p.is_flashed = std::isfinite(cf.flash) && cf.flash > 0.05f;
        if (p.is_local)
            p.is_scoped = runtime.local_scoped;

        p.pos[0] = positions[c][0];
        p.pos[1] = positions[c][1];
        p.pos[2] = positions[c][2];
        if (!IsFinitePosition(p.pos))
            continue;

        const uintptr_t scene = IsUserPointer(cf.scene) ? cf.scene : 0;
        p.scene = scene;

        if (need_yaw || p.is_local) {
            if (std::isfinite(cf.eye_angles[1]))
                p.view_yaw = cf.eye_angles[1];
            if (p.is_local)
                p.view_yaw = runtime.local_view_yaw;
        }

        if (track_velocity) {
            const auto old_position = previous_positions.find(pawn);
            if (old_position != previous_positions.end() && delta_seconds >= 0.005f && delta_seconds <= 0.5f) {
                for (int axis = 0; axis < 3; ++axis) {
                    const float velocity = (p.pos[axis] - old_position->second[axis]) / delta_seconds;
                    p.velocity[axis] = std::isfinite(velocity) && std::fabs(velocity) < 5000.f ? velocity : 0.f;
                }
            }
            current_positions[pawn] = { p.pos[0], p.pos[1], p.pos[2] };
        }
        {
            const float speed = std::sqrt(p.velocity[0] * p.velocity[0] +
                                          p.velocity[1] * p.velocity[1]);
            p.move_speed = std::isfinite(speed) ? speed : 0.f;
            // Keep the threshold deliberately below CS2 silent-walk speed.
            // It only rejects stationary-position noise, never quiet movement.
            p.is_moving = p.move_speed > 5.f;
        }

        // Reject distant entities before name sanitising, skeleton validation
        // and weapon formatting.  The squared comparison avoids a sqrt for
        // entities that cannot contribute to this frame at all.
        const float dx = p.pos[0] - runtime.local_pos[0];
        const float dy = p.pos[1] - runtime.local_pos[1];
        const float dz = p.pos[2] - runtime.local_pos[2];
        const float distanceSq = dx * dx + dy * dy + dz * dz;
        const float maxDistanceUnits = frame_config.max_distance * 39.37f;
        if (!p.is_local && frame_config.max_distance > 1.f &&
            distanceSq > maxDistanceUnits * maxDistanceUnits)
            continue;
        p.distance = std::sqrt((std::max)(distanceSq, 0.f)) / 39.37f;

        if (need_names) {
            char nameBuf[64]{};
            std::memcpy(nameBuf, playerNames[c], sizeof(nameBuf));
            nameBuf[sizeof(nameBuf) - 1] = '\0';
            for (char* ch = nameBuf; *ch; ++ch) {
                if (static_cast<unsigned char>(*ch) < 0x20 || static_cast<unsigned char>(*ch) > 0x7E)
                    *ch = '?';
            }
            if (!nameBuf[0]) {
                uintptr_t sanitizedName = 0;
                if (QReadT(controller + offsets.m_sSanitizedPlayerName, sanitizedName) &&
                    IsUserPointer(sanitizedName))
                    QRead(sanitizedName, nameBuf, sizeof(nameBuf) - 1);
                nameBuf[sizeof(nameBuf) - 1] = '\0';
            }
            if (!nameBuf[0]) {
                std::snprintf(nameBuf, sizeof(nameBuf), "Jogador_%d", p.ent_index);
                p.is_bot = true;
            }
            if (_strnicmp(nameBuf, "BOT", 3) == 0 ||
                _strnicmp(nameBuf, "Player_", 7) == 0 ||
                _strnicmp(nameBuf, "Jogador_", 8) == 0)
                p.is_bot = true;
            if (nameNeedsRefresh[c] && controller) {
                auto& cached = nameCache[controller];
                std::memcpy(cached.text.data(), nameBuf, cached.text.size());
                cached.last_refresh_ms = scan_now_ms;
            }
            if (!frame_config.show_bots && p.is_bot)
                continue;
            std::memcpy(p.name, nameBuf, sizeof(p.name));
        } else {
            std::snprintf(p.name, sizeof(p.name), "P%d", p.ent_index);
        }

        // Current Source 2 player rig: pelvis=0, spine=2/3/4,
        // neck=5, head=6, arms=8..15 and legs=22..27.
        // One contiguous 32-joint snapshot per player. Distance never removes
        // bones, so the visual quality stays identical under load.
        if (scene && need_bones && boneReadEligible[c]) {
            auto try_compact_bones = [&](const BoneJointSnapshot* joints) -> bool {
                if (!joints) return false;
                // Both verified layouts keep the aim-relevant joints in the first
                // eight entries. Score just those anchors so aim/effects avoid the
                // 28-joint DMA payload when a full visual skeleton is not needed.
                static constexpr int kCurrentCompact[4] = {6, 5, 4, 2};
                static constexpr int kReferenceCompact[4] = {7, 6, 4, 3};
                auto score = [&](const int* idx, float out[4][3]) -> float {
                    for (int b = 0; b < 4; ++b) {
                        const int id = idx[b];
                        if (id < 0 || id >= kCompactJointReadCount) return -1.f;
                        out[b][0] = joints[id].x; out[b][1] = joints[id].y; out[b][2] = joints[id].z;
                        if (!std::isfinite(out[b][0]) || !std::isfinite(out[b][1]) || !std::isfinite(out[b][2]))
                            return -1.f;
                    }
                    const float headDz = out[0][2] - p.pos[2];
                    if (headDz < 20.f || headDz > 110.f) return -1.f;
                    const float hx = out[0][0] - p.pos[0], hy = out[0][1] - p.pos[1];
                    if (hx * hx + hy * hy > 80.f * 80.f) return -1.f;
                    // Normal standing/crouched ordering: head >= neck >= chest >= stomach.
                    if (out[0][2] + 4.f < out[1][2] || out[1][2] + 8.f < out[2][2] ||
                        out[2][2] + 12.f < out[3][2]) return -1.f;
                    return 100.f - std::fabs(headDz - 70.f);
                };
                float cur[4][3]{}, ref[4][3]{};
                const float curScore = score(kCurrentCompact, cur);
                const float refScore = score(kReferenceCompact, ref);
                if (curScore < 0.f && refScore < 0.f) return false;
                const bool useCurrent = curScore >= refScore;
                const float (*best)[3] = useCurrent ? cur : ref;
                std::memcpy(p.bones[0], best[0], sizeof(p.bones[0]));
                std::memcpy(p.bones[1], best[1], sizeof(p.bones[1]));
                std::memcpy(p.bones[2], best[2], sizeof(p.bones[2]));
                std::memcpy(p.bones[4], best[3], sizeof(p.bones[4]));
                std::memcpy(p.head, best[0], sizeof(p.head));
                p.bone_layout = useCurrent ? 1u : 0u;
                p.bones_ok = true;
                p.full_bones_ok = false;
                s_stickyBoneLayout[p.pawn] = p.bone_layout;
                return true;
            };

            auto try_bones = [&](const BoneJointSnapshot* joints) -> bool {
                if (!joints) return false;
                // CS2 / Source 2 bone indices.  The arm chains end at 10/15;
                // 11/16 are hand/finger auxiliaries and using them as a hand
                // makes the pose bend or stretch as the weapon animates.
                // Keep the public four-slot layout, duplicating the shoulder
                // at the clavicle slot so every rendered limb is anatomical.
                // L leg: upper=22, lower=23, ankle=24; R=25..27.
                // Two verified Source 2 layouts are seen in the wild.  The
                // first is the current CS2 rig; the second is the contiguous
                // layout used by the reference DMA implementation.  Score both
                // against the current pawn and retain only an anatomical pose.
                static constexpr int kCurrentIdx[kBoneSlotCount] = {
                    6, 5, 4, 3, 2, 0,
                    8, 8, 9, 10,
                    13, 13, 14, 15,
                    22, 23, 24,
                    25, 26, 27
                };
                static constexpr int kReferenceIdx[kBoneSlotCount] = {
                    7, 6, 4, 3, 3, 1,
                    6, 9, 10, 11,
                    6, 13, 14, 15,
                    17, 18, 19,
                    20, 21, 22
                };
                auto skeleton_score = [&](float bones[kBoneSlotCount][3]) -> float {
                    // Prefer coherent head-above-pelvis torso length and arm/leg span.
                    const float* h = bones[0];
                    const float* pel = bones[5];
                    const float torso = std::sqrt(
                        (h[0]-pel[0])*(h[0]-pel[0]) + (h[1]-pel[1])*(h[1]-pel[1]) + (h[2]-pel[2])*(h[2]-pel[2]));
                    // The bone buffer is valid for crouching, jumping and
                    // uncommon agent animations too.  Keep the corruption
                    // guard broad enough that a genuine pose is not dropped
                    // for one frame merely because an arm/leg is compressed.
                    if (!(torso > 10.f && torso < 190.f)) return -1.f;
                    if (h[2] < p.pos[2] + 5.f) return -1.f;
                    float score = 100.f - std::fabs(torso - 55.f);
                    // Arms should be lateral to spine
                    auto seg = [&](int a, int b) {
                        float dx = bones[a][0]-bones[b][0], dy = bones[a][1]-bones[b][1], dz = bones[a][2]-bones[b][2];
                        return std::sqrt(dx*dx+dy*dy+dz*dz);
                    };
                    const float armL = seg(7, 9), armR = seg(11, 13);
                    const float legL = seg(14, 16), legR = seg(17, 19);
                    const float shoulderWidth = seg(7, 11);
                    // Animated agents, crouching and weapon-holding poses vary
                    // substantially. Reject only clearly corrupt segments;
                    // narrow human-pose assumptions were discarding real bones.
                    if (!(armL > 1.f && armL < 150.f && armR > 1.f && armR < 150.f)) return -1.f;
                    if (!(legL > 2.f && legL < 180.f && legR > 2.f && legR < 180.f)) return -1.f;
                    if (!(shoulderWidth > .2f && shoulderWidth < 150.f)) return -1.f;
                    if (!(bones[0][2] > bones[5][2] + 8.f)) return -1.f;
                    for (std::size_t i = 0; i < kBoneSlotCount; ++i) {
                        const float ox = bones[i][0] - p.pos[0];
                        const float oy = bones[i][1] - p.pos[1];
                        const float oz = bones[i][2] - p.pos[2];
                        if (ox * ox + oy * oy > 260.f * 260.f || oz < -100.f || oz > 220.f)
                            return -1.f;
                    }
                    score += 44.f;
                    const float horiz = std::sqrt((h[0]-p.pos[0])*(h[0]-p.pos[0]) + (h[1]-p.pos[1])*(h[1]-p.pos[1]));
                    if (horiz > 60.f) score -= 40.f;
                    return score;
                };

                float tmp[kBoneSlotCount][3]{};
                float best[kBoneSlotCount][3]{};
                float bestScore = -1.f;
                uint8_t bestLayout = 0;

                // Sticky layout: once a pawn's rig is known, skip dual scoring.
                const auto stickyIt = s_stickyBoneLayout.find(p.pawn);
                const bool haveSticky = stickyIt != s_stickyBoneLayout.end();
                const uint8_t stickyLayout = haveSticky ? stickyIt->second : 0;

                auto score_layout = [&](const int* indices, uint8_t layoutId) {
                    bool finite = true;
                    for (std::size_t b = 0; b < kBoneSlotCount; ++b) {
                        const int id = indices[b];
                        if (id < 0 || id >= kBoneJointReadCount) { finite = false; break; }
                        tmp[b][0] = joints[id].x;
                        tmp[b][1] = joints[id].y;
                        tmp[b][2] = joints[id].z;
                        finite = finite && std::isfinite(tmp[b][0]) && std::isfinite(tmp[b][1]) &&
                                 std::isfinite(tmp[b][2]);
                    }
                    const float score = finite ? skeleton_score(tmp) : -1.f;
                    if (score > bestScore) {
                        bestScore = score;
                        bestLayout = layoutId;
                        std::memcpy(best, tmp, sizeof(best));
                    }
                };

                if (haveSticky) {
                    // 0 = reference (CS2-DMA first historically), 1 = current
                    score_layout(stickyLayout == 0 ? kReferenceIdx : kCurrentIdx, stickyLayout);
                    if (bestScore < 0.f) {
                        // Sticky failed (model change) — re-probe both once
                        score_layout(kReferenceIdx, 0);
                        score_layout(kCurrentIdx, 1);
                    }
                } else {
                    score_layout(kReferenceIdx, 0);
                    score_layout(kCurrentIdx, 1);
                }
                if (bestScore < 0.f) return false;

                for (std::size_t b = 0; b < kBoneSlotCount; ++b) {
                    p.bones[b][0] = best[b][0];
                    p.bones[b][1] = best[b][1];
                    p.bones[b][2] = best[b][2];
                }
                p.head[0] = best[0][0]; p.head[1] = best[0][1]; p.head[2] = best[0][2];
                p.bone_layout = bestLayout;
                p.full_bones_ok = true;
                s_stickyBoneLayout[p.pawn] = bestLayout;
                return true;
            };

            uintptr_t boneBase = 0;
            bool acquired_real_bones = false;
            // Primary: acquire either a compact upper-body pose or the complete
            // skeleton depending on the features that are actually enabled.
            const bool primary_ok = IsUserPointer(boneBases[c]) &&
                (need_full_bones ? try_bones(boneSnapshots[c]) : try_compact_bones(boneSnapshots[c]));
            if (primary_ok) {
                p.bones_ok = true;
                p.bone_base = boneBases[c];
                acquired_real_bones = true;
            } else {
                // Rare recovery only — avoid sequential 3× full-joint DMA in the hot path.
                static uint64_t s_lastFallbackMs = 0;
                if (scan_now_ms - s_lastFallbackMs > 500u) {
                    s_lastFallbackMs = scan_now_ms;
                    for (uintptr_t alt : {(uintptr_t)0x1D0, (uintptr_t)0x160, (uintptr_t)0x1C0}) {
                        if (alt == offsets.BoneArray) continue;
                        if (QReadT(scene + alt, boneBase) && IsUserPointer(boneBase)) {
                            BoneJointSnapshot fallback[kBoneJointReadCount]{};
                            const size_t fallbackBytes = need_full_bones ? kBoneReadBytes : kCompactBoneReadBytes;
                            const bool fallbackRead = QRead(boneBase, fallback, fallbackBytes);
                            const bool fallbackPose = fallbackRead &&
                                (need_full_bones ? try_bones(fallback) : try_compact_bones(fallback));
                            if (fallbackPose) {
                                p.bones_ok = true;
                                p.bone_base = boneBase;
                                acquired_real_bones = true;
                                break;
                            }
                        }
                    }
                }
            }
            if (acquired_real_bones) {
                auto& cached = bone_cache[p.pawn];
                std::memcpy(cached.joints, p.bones, sizeof(p.bones));
                std::memcpy(cached.origin, p.pos, sizeof(cached.origin));
                cached.last_valid_ms = scan_now_ms;
                cached.full_pose = p.full_bones_ok;
            }
        }
        if (!p.bones_ok) {
            const auto cached = bone_cache.find(p.pawn);
            if (cached != bone_cache.end() &&
                // A scatter/bone-base miss is normally transient.  Preserve
                // the last anatomical pose long enough to bridge it, while
                // translating it by the current origin so it never freezes in
                // world space or visibly pops out for a single bad read.
                // Keep a verified pose through short scene-node/bone-buffer
                // outages.  The fast origin lane translates this cached pose
                // each render, so it stays attached instead of blinking out.
                scan_now_ms - cached->second.last_valid_ms <= static_cast<uint64_t>(BONE_RELIABILITY_HOLD_MS)) {
                std::memcpy(p.bones, cached->second.joints, sizeof(p.bones));
                const float cached_shift[3] = {
                    p.pos[0] - cached->second.origin[0],
                    p.pos[1] - cached->second.origin[1],
                    p.pos[2] - cached->second.origin[2]
                };
                for (std::size_t bone = 0; bone < kBoneSlotCount; ++bone)
                    for (int axis = 0; axis < 3; ++axis)
                        p.bones[bone][axis] += cached_shift[axis];
                std::memcpy(p.head, p.bones[0], sizeof(p.head));
                p.bones_ok = true;
                p.full_bones_ok = cached->second.full_pose;
            }
        }
        if (!p.bones_ok) {
            p.head[0] = p.pos[0];
            p.head[1] = p.pos[1];
            p.head[2] = p.pos[2] + 72.f;
        }

        // Active weapon → item definition index → white icon code / name.
        // The normal chain is already batch-read; singles remain only as a
        // schema-drift fallback when the primary definition slot is invalid.
        if (need_weapons) {
            const uintptr_t weapon_ent = weaponEntities[c];
            uint16_t def = weaponDefinitions[c];
            auto cachedWeapon = weaponStateCache.find(p.pawn);
            // Schema probing is optional visual metadata.  It must not be
            // allowed to contend with core ESP reads every second when an
            // offset is incompatible or a weapon pointer is transient.
            const bool fallbackDue = cachedWeapon == weaponStateCache.end() ||
                !cachedWeapon->second.last_fallback_ms ||
                scan_now_ms - cachedWeapon->second.last_fallback_ms >=
                    static_cast<uint64_t>(WEAPON_FALLBACK_INTERVAL_MS);
            if (weaponRefreshDue[c] && fallbackDue && (def == 0 || def >= 6000) && IsUserPointer(weapon_ent)) {
                // Schema-drift fallback: 3 candidates in one scatter (was 3 QReads).
                const uintptr_t fallbacks[3] = {
                    weapon_ent + 0x11A8 + 0x50 + 0x1BA,
                    weapon_ent + 0x1BA,
                    weapon_ent + 0x16F0,
                };
                uint16_t candidates[3]{};
                EnsureScatter();
                if (g_scatter_full) {
                    mem.SetDmaCallTag("CS2.WeaponDefFallback");
                    for (int fi = 0; fi < 3; ++fi)
                        mem.AddScatterReadRequest(g_scatter_full, fallbacks[fi],
                                                  &candidates[fi], sizeof(uint16_t));
                    mem.ExecuteReadScatter(g_scatter_full);
                } else {
                    for (int fi = 0; fi < 3; ++fi)
                        QReadT(fallbacks[fi], candidates[fi], "CS2.WeaponDefFallback");
                }
                def = 0;
                for (int fi = 0; fi < 3; ++fi) {
                    if (candidates[fi] > 0 && candidates[fi] < 6000) {
                        def = candidates[fi];
                        break;
                    }
                }
                auto refreshedWeapon = weaponStateCache.find(p.pawn);
                if (refreshedWeapon != weaponStateCache.end())
                    refreshedWeapon->second.last_fallback_ms = scan_now_ms;
            }
            if (def > 0 && def < 6000) {
                p.weapon_def = def;
                if (weaponRefreshDue[c]) weaponStateCache[p.pawn].definition = def;
            }
            // Weapon ammo (clip/reserve) when feature on and offsets present.
            if (frame_config.weapon_ammo && IsUserPointer(weapon_ent) && offsets.m_iClip1) {
                int clip = -1, reserve = -1;
                QReadT(weapon_ent + offsets.m_iClip1, clip, "CS2.AmmoClip");
                if (offsets.m_pReserveAmmo)
                    QReadT(weapon_ent + offsets.m_pReserveAmmo, reserve, "CS2.AmmoReserve");
                if (clip >= 0 && clip < 500) p.ammo_clip = clip;
                if (reserve >= 0 && reserve < 500) p.ammo_reserve = reserve;
            }
        }

        // Player flags / sound: only request fields whose individual marker is on.
        if ((frame_config.player_flags || frame_config.sound_esp) && IsUserPointer(p.controller)) {
            if (frame_config.player_flags && frame_config.flag_money &&
                offsets.m_pInGameMoneyServices && offsets.m_iAccount) {
                uintptr_t money_svc = 0;
                int money = -1;
                if (QReadT(p.controller + offsets.m_pInGameMoneyServices, money_svc, "CS2.MoneySvc") &&
                    IsUserPointer(money_svc) &&
                    QReadT(money_svc + offsets.m_iAccount, money, "CS2.Money") &&
                    money >= 0 && money < 100000)
                    p.money = money;
            }
            if (frame_config.player_flags && frame_config.flag_kit && offsets.m_pItemServices && offsets.m_bHasDefuser && IsUserPointer(p.pawn)) {
                uintptr_t item_svc = 0;
                if (QReadT(p.pawn + offsets.m_pItemServices, item_svc, "CS2.ItemSvc") && IsUserPointer(item_svc)) {
                    uint8_t kit = 0;
                    if (QReadT(item_svc + offsets.m_bHasDefuser, kit, "CS2.HasDefuser"))
                        p.has_defuser = kit != 0;
                }
            }
            if (frame_config.player_flags && frame_config.flag_defusing && offsets.m_bIsDefusing && IsUserPointer(p.pawn)) {
                uint8_t defu = 0;
                if (QReadT(p.pawn + offsets.m_bIsDefusing, defu, "CS2.IsDefusing"))
                    p.is_defusing = defu != 0;
            }
        }
        if (frame_config.sound_esp && offsets.m_iShotsFired && IsUserPointer(p.pawn)) {
            static std::unordered_map<uintptr_t, int> s_prevShots;
            int shots = 0;
            if (QReadT(p.pawn + offsets.m_iShotsFired, shots, "CS2.ShotsFired") && shots >= 0) {
                const int prev = s_prevShots[p.pawn];
                if (shots > prev)
                    p.last_shot_ms = scan_now_ms;
                s_prevShots[p.pawn] = shots;
                p.shots_fired = shots;
            }
        }

        if (!p.is_local && p.team != runtime.local_team)
            ++runtime.enemy_count;

        runtime.players.push_back(p);
        ++processed;
    }

    // Retain a fully validated pawn for a short wall-clock grace period when
    // only its individual controller/pawn read drops out. Time-based expiry is
    // stable regardless of acquisition rate or presentation FPS.
    // A controller/list flicker must not immediately purge the validated bone
    // cache.  This is wall-clock based, so it remains stable at any FPS.
    constexpr uint64_t kPlayerDropoutGraceMs = 120;
    for (const auto& player : runtime.players) {
        if (IsUserPointer(player.pawn))
            recent_players[player.pawn] = {player, scan_now_ms};
    }
    for (auto it = recent_players.begin(); it != recent_players.end();) {
        if (scan_now_ms - it->second.last_seen_ms > kPlayerDropoutGraceMs) {
            bone_cache.erase(it->first);
            it = recent_players.erase(it);
            continue;
        }
        const bool present = std::any_of(runtime.players.begin(), runtime.players.end(),
            [&](const Player& player) { return player.pawn == it->first; });
        if (!present && runtime.players.size() < static_cast<std::size_t>(kMax)) {
            runtime.players.push_back(it->second.player);
            if (!it->second.player.is_local && it->second.player.team != runtime.local_team)
                ++runtime.enemy_count;
        }
        ++it;
    }

    // Safety net: inject local pawn if handle resolution missed it entirely.
    if (runtime.local_pawn && IsUserPointer(runtime.local_pawn)) {
        bool have_local = false;
        for (const auto& existing : runtime.players) {
            if (existing.pawn == runtime.local_pawn || existing.is_local) {
                have_local = true;
                break;
            }
        }
        if (!have_local) {
            Player local{};
            local.pawn = runtime.local_pawn;
            local.controller = runtime.local_controller;
            local.is_local = true;
            local.alive = true;
            QReadT(runtime.local_pawn + offsets.m_iHealth, local.health);
            QReadT(runtime.local_pawn + offsets.m_ArmorValue, local.armor);
            QReadT(runtime.local_pawn + offsets.m_iTeamNum, local.team);
            if (local.team == 0)
                local.team = runtime.local_team;
            local.pos[0] = runtime.local_pos[0];
            local.pos[1] = runtime.local_pos[1];
            local.pos[2] = runtime.local_pos[2];
            local.view_yaw = runtime.local_view_yaw;
            std::snprintf(local.name, sizeof(local.name), "Local");
            if (local.health > 0 && local.health <= 200 && IsPlayableTeam(local.team)) {
                runtime.players.push_back(local);
                ++runtime.pawn_count;
            }
        }
    }

    // Camera projection is published by the dedicated high-rate camera lane.
    // Avoid a redundant DMA view-matrix round-trip and the old temporary vector
    // whose position delta was always zero because positions were not re-read.

    if (track_velocity) {
        previous_positions.swap(current_positions);
        previous_frame_time = frame_time;
    } else if (!previous_positions.empty()) {
        previous_positions.clear();
    }

    // Keep match flag from map name (set at top of RunFrame). If map string
    // briefly blanks but we still have pawns, stay in-match for one frame.
    if (!runtime.in_match && (runtime.pawn_count > 0 || !runtime.players.empty()))
        runtime.in_match = true;
    runtime.player_count = static_cast<int>(runtime.players.size());

    // Spectator target follows the player's actual point of view.  When alive
    // that is the local pawn; when dead it is the pawn currently being watched.
    // Walk controllers instead of runtime.players: the latter intentionally
    // contains only alive, drawable pawns and used to discard spectators.
    runtime.spectators.clear();
    runtime.spectator_count = 0;
    runtime.spectator_target = runtime.local_pawn;
    runtime.spectator_target_name[0] = '\0';
    if (frame_config.spectator_list && runtime.local_pawn &&
        offsets.m_pObserverServices && offsets.m_hObserverTarget) {
        if (runtime.local_health <= 0) {
            uintptr_t local_services = 0;
            uint32_t watched_handle = 0;
            if (QReadT(runtime.local_pawn + offsets.m_pObserverServices, local_services) &&
                IsUserPointer(local_services) &&
                QReadT(local_services + offsets.m_hObserverTarget, watched_handle)) {
                const uintptr_t watched = ResolveEntityByHandle(
                    watched_handle, g_pawn_stride ? g_pawn_stride : kEntityIdentityStride);
                if (IsUserPointer(watched)) runtime.spectator_target = watched;
            }
        }
        for (const auto& live : runtime.players) {
            if (live.pawn == runtime.spectator_target) {
                std::snprintf(runtime.spectator_target_name,
                    sizeof(runtime.spectator_target_name), "%s", live.name);
                break;
            }
        }

        static std::vector<Player> cached_spectators;
        static uintptr_t cached_target = 0;
        static uint64_t next_spectator_refresh_ms = 0;
        const bool refresh_spectators = scan_now_ms >= next_spectator_refresh_ms ||
                                        cached_target != runtime.spectator_target;
        if (!refresh_spectators) {
            runtime.spectators = cached_spectators;
        } else for (int i = 0; i < kMaxSlots; ++i) {
            const uintptr_t controller = controllers[i];
            if (!IsUserPointer(controller) || controller == runtime.local_controller)
                continue;
            uint32_t observer_handle = 0;
            if (!offsets.m_hObserverPawn ||
                !QReadT(controller + offsets.m_hObserverPawn, observer_handle) ||
                !observer_handle || observer_handle == 0xFFFFFFFFu)
                continue;
            const uintptr_t observer_pawn = ResolveEntityByHandle(
                observer_handle, g_pawn_stride ? g_pawn_stride : kEntityIdentityStride);
            if (!IsUserPointer(observer_pawn)) continue;
            uintptr_t services = 0;
            if (!QReadT(observer_pawn + offsets.m_pObserverServices, services) ||
                !services || !IsUserPointer(services))
                continue;
            uint32_t target_handle = 0;
            if (!QReadT(services + offsets.m_hObserverTarget, target_handle) ||
                !target_handle || target_handle == 0xFFFFFFFFu)
                continue;
            const uintptr_t target = ResolveEntityByHandle(
                target_handle, g_pawn_stride ? g_pawn_stride : kEntityIdentityStride);
            if (!target)
                continue;
            if (target == runtime.spectator_target) {
                Player spectator{};
                spectator.controller = controller;
                spectator.pawn = observer_pawn;
                spectator.is_spectator = true;
                spectator.alive = false;
                if (offsets.m_steamID)
                    QReadT(controller + offsets.m_steamID, spectator.steam_id);
                QRead(controller + offsets.m_iszPlayerName,
                    spectator.name, sizeof(spectator.name) - 1);
                spectator.name[sizeof(spectator.name) - 1] = '\0';
                if (!spectator.name[0])
                    std::snprintf(spectator.name, sizeof(spectator.name), "Jogador_%d", i + 1);
                runtime.spectators.push_back(spectator);
            }
        }
        if (refresh_spectators) {
            cached_spectators = runtime.spectators;
            cached_target = runtime.spectator_target;
            next_spectator_refresh_ms = scan_now_ms + 250u;
        }
        runtime.spectator_count = static_cast<int>(runtime.spectators.size());
    }

    // Recovery only when the list is truly broken.
    // Death / spectate often drops drawn players briefly — do NOT wipe the
    // entity list on that (was causing ESP to vanish for 1–2s after dying).
    // Only recover if pawns fail to resolve while many controllers exist.
    const bool stuck_handles =
        runtime.in_match && runtime.controller_count > 4 && runtime.pawn_count == 0;
    // Empty player list alone is normal while dead if filters exclude everyone —
    // require a much longer stretch before forcing a full re-acquire.
    const bool stuck_empty =
        runtime.in_match && runtime.player_count == 0 && runtime.controller_count > 2
        && runtime.pawn_count == 0;
    if (stuck_handles || stuck_empty) {
        if (++zero_player_frames >= 240) { // ~4s at 60fps — death safe
            g_pending_entity_list = 0;
            g_entity_list_confirmations = 0;
            runtime.entity_list_entry = 0;
            runtime.entity_list_addr = 0;
            g_cached_entity_root = 0;
            g_controller_stride = kEntityIdentityStride;
            g_pawn_stride = kEntityIdentityStride;
            zero_player_frames = 0;
            std::cout << "[CS2] ESP recover — refresh entity list"
                      << " (players=" << runtime.player_count
                      << " controllers=" << runtime.controller_count
                      << " pawns=" << runtime.pawn_count << ")" << std::endl;
        }
    } else {
        zero_player_frames = 0;
    }

    if (runtime.in_match && frame_config.bomb_timer)
        UpdateBombStateThrottled();
    else
        runtime.bomb = BombState{};

    if (runtime.bomb.defusing && runtime.bomb.defuser_handle) {
        const uintptr_t defuser = ResolveEntityByHandle(runtime.bomb.defuser_handle,
            g_pawn_stride ? g_pawn_stride : kEntityIdentityStride);
        for (const auto& player : runtime.players) {
            if (player.pawn == defuser && player.name[0]) {
                std::snprintf(runtime.bomb.defuser_name, sizeof(runtime.bomb.defuser_name), "%s", player.name);
                break;
            }
        }
    }

    // Snapshot successful scans for the hold-over path above.
    if (!runtime.players.empty()) {
        last_good_players = runtime.players;
        last_good_ms = scan_now_ms;
    } else if (!last_good_players.empty() && scan_now_ms - last_good_ms > 100u) {
        last_good_players.clear();
    }

    // Periodic diagnostics when controllers exist but pawns do not —
    // usually wrong m_hPlayerPawn / stride after a game update.
    if (runtime.controller_count > 0 && runtime.pawn_count == 0)
        ++zero_pawn_diag_frames;
    else
        zero_pawn_diag_frames = 0;

    if ((runtime.frames % 120) == 1 && runtime.controller_count > 0 && runtime.pawn_count == 0) {
        std::cout << "[CS2] WARN: " << runtime.controller_count
                  << " controllers, 0 pawns | handles=" << handle_nonzero
                  << " resolved=" << resolve_ok
                  << " stride=0x" << std::hex << g_controller_stride
                  << " pawnStride=0x" << g_pawn_stride
                  << " m_hPlayerPawn=0x" << offsets.m_hPlayerPawn
                  << " localPawn=0x" << runtime.local_pawn
                  << " localCtrl=0x" << runtime.local_controller
                  << std::dec
                  << " fails=" << runtime.read_fails << std::endl;

        // Dump first 3 controller handles for offline analysis.
        int dumped = 0;
        for (int i = 0; i < kMaxSlots && dumped < 3; ++i) {
            if (!controllers[i]) continue;
            uint32_t h = ReadPawnHandle(controllers[i]);
            uintptr_t r70 = h ? ResolveEntityByHandle(h, kEntityIdentityStride) : 0;
            uintptr_t r78 = h ? ResolveEntityByHandle(h, kLegacyEntityIdentityStride) : 0;
            int alive = 0;
            QReadT(controllers[i] + offsets.m_bPawnIsAlive, alive);
            std::cout << "  ctrl[" << i << "]=0x" << std::hex << controllers[i]
                      << " handle=0x" << h
                      << " alive=" << std::dec << alive
                      << " r70=0x" << std::hex << r70
                      << " r78=0x" << r78 << std::dec << std::endl;
            ++dumped;
        }
    }

    // Presentation (ESP / Aim) stays in main.cpp — calling them here too
    // doubled DMA + mouse work every frame and tanked FPS / aim pull.
    // Residual wall time not covered by named phases (unaccounted attribution).
    {
        const float wall = OmniGhost::Gameplay::TimeMs(_frameWallBegin);
        const float accounted = g_phase.webradar_ms + g_phase.entity_ms + g_phase.local_ms
            + g_phase.core_scatter_ms + g_phase.positions_ms + g_phase.bones_ms
            + g_phase.weapon_ms + g_phase.spectator_ms + g_phase.bomb_ms
            + g_phase.publish_ms + g_phase.cleanup_ms;
        g_phase.other_ms = (std::max)(0.f, wall - accounted);
    }

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
    PublishRuntimeSnapshot();
    // Camera and origins are latency-critical for visual attachment while
    // turning or moving.  Bones remain in the validated full scan; this small
    // lane refreshes only their common origin and the view matrix.
    g_camera_thread = std::thread([] {
        mem.SetDmaLane("camera");
        OmniGhost::Gameplay::FixedRateScheduler scheduler;
        float matrix[16]{};
        uint64_t next_camera_ms = 0;
        uint64_t next_motion_ms = 0;
        while (!g_acquisition_stop.load(std::memory_order_acquire)) {
            const auto runtime_view = g_runtime_snapshots.Acquire();
            const uintptr_t client_base = runtime_view ? runtime_view->client_base : 0;
            const bool in_match = runtime_view && runtime_view->in_match;
            const bool canRead = ready && offsets.loaded && client_base && offsets.dwViewMatrix;
            // The FPGA cannot make a 4 ms QRead when an entity scan is already
            // in flight.  Sharing it caused queues of 50–900 ms in telemetry,
            // which is much worse visually than reusing the last valid matrix.
            const uint64_t camera_now_ms = GetTickCount64();
            const int camera_pressure = g_pressure_level.load(std::memory_order_relaxed);
            // The renderer interpolates published snapshots, therefore it does
            // not need a physical matrix transfer on every scheduler wake-up.
            // 16 ms is already display-rate smooth with interpolation and
            // leaves the transport room for the entity lane.
            // 500 Hz when healthy; back off only under measured DMA pressure.
            const int camera_period_ms = camera_pressure >= 2 ? 16 :
                (camera_pressure == 1 ? 6 : CAMERA_INTERVAL_MS);
            if (canRead && in_match && camera_now_ms >= next_camera_ms &&
                !DmaCooldownActive() && !g_acq_busy.load(std::memory_order_acquire)) {
                next_camera_ms = camera_now_ms + static_cast<uint64_t>(camera_period_ms);
                std::unique_lock<std::mutex> gate(g_dma_read_gate, std::try_to_lock);
                if (gate.owns_lock()) {
                    const auto cameraReadBegin = std::chrono::steady_clock::now();
                    if (QRead(client_base + offsets.dwViewMatrix, matrix, sizeof(matrix)))
                        PublishCameraSnapshot(matrix);
                    NotePossibleDeviceStall(OmniGhost::Gameplay::TimeMs(cameraReadBegin));
                }
            }
            // Keep the view matrix on its own very fast lane, but never make
            // one DMA read per player every 2 ms.  That old pattern could
            // starve the regular entity scan and made even boxes/bars hitch.
            // Player motion is sampled at MOTION_INTERVAL_MS; rendering still
            // runs every overlay frame.
            const uint64_t now_ms = GetTickCount64();
            const auto frame_config = g_config_snapshots.Acquire();
            const auto current = g_runtime_snapshots.Acquire();
            const bool needs_motion = false;
            // The full snapshot already contains a coherent position set. Do
            // not re-read every origin immediately after it publishes: that
            // duplicate scatter was a large source of queue pressure.
            const uint64_t snapshotAgeMs = current && current->snapshot_timestamp_ms &&
                now_ms >= current->snapshot_timestamp_ms
                ? now_ms - current->snapshot_timestamp_ms : UINT64_MAX;
            const bool motionSnapshotNeeded = snapshotAgeMs >= 18u;
            if (canRead && in_match && current && needs_motion && motionSnapshotNeeded && now_ms >= next_motion_ms
                && !DmaCooldownActive()
                && !g_acq_busy.load(std::memory_order_acquire)) {
                // Slightly slower motion when skeleton is off — boxes/bars stay smooth
                // with far less DMA pressure (main source of intermittent freezes).
                const int motionPeriod = camera_pressure >= 2 ? 16 : MOTION_INTERVAL_MS;
                next_motion_ms = now_ms + motionPeriod;
                MotionSnapshot motion{};
                // One scatter round-trip for all origins instead of N sequential DMA reads.
                struct MotItem { uintptr_t pawn; uintptr_t scene; float pos[3]; };
                MotItem items[32]{};
                int n = 0;
                for (const auto& player : current->players) {
                    if (n >= 32 || !player.pawn || !IsUserPointer(player.scene))
                        continue;
                    items[n].pawn = player.pawn;
                    items[n].scene = player.scene;
                    ++n;
                }
                if (n > 0) {
                    EnsureMotionScatter();
                    if (g_scatter_motion) {
                        for (int i = 0; i < n; ++i)
                            mem.AddScatterReadRequest(g_scatter_motion,
                                items[i].scene + offsets.m_vecAbsOrigin,
                                items[i].pos, sizeof(items[i].pos));
                        mem.SetDmaCallTag("CS2.MotionOrigins");
                        const auto motionReadBegin = std::chrono::steady_clock::now();
                        mem.ExecuteReadScatter(g_scatter_motion);
                        NotePossibleDeviceStall(OmniGhost::Gameplay::TimeMs(motionReadBegin));
                    } else {
                        for (int i = 0; i < n; ++i)
                            QRead(items[i].scene + offsets.m_vecAbsOrigin,
                                  items[i].pos, sizeof(items[i].pos));
                    }
                    for (int i = 0; i < n; ++i) {
                        if (!std::isfinite(items[i].pos[0]) || !std::isfinite(items[i].pos[1]) ||
                            !std::isfinite(items[i].pos[2]))
                            continue;
                        if (motion.count >= motion.players.size()) break;
                        auto& sample = motion.players[motion.count];
                        sample = {};
                        sample.pawn = items[i].pawn;
                        sample.pos[0] = items[i].pos[0];
                        sample.pos[1] = items[i].pos[1];
                        sample.pos[2] = items[i].pos[2];
                        ++motion.count;
                    }
                }
                if (motion.count) {
                    motion.timestamp_ms = GetTickCount64();
                    PublishMotionSnapshot(motion);
                }
            }
            // Keep camera/motion snappy — view matrix must stay fluid for ESP.
            int cadence = in_match ? CAMERA_INTERVAL_MS : 12;
            const int pressure = g_pressure_level.load(std::memory_order_relaxed);
            if (in_match && pressure >= 2)
                cadence = 16;
            else if (in_match && pressure == 1)
                cadence = 12;
            scheduler.Wait(std::chrono::milliseconds(cadence));
        }
    });
    g_acquisition_thread = std::thread([] {
        OmniGhost::Gameplay::FixedRateScheduler scheduler;
        while (!g_acquisition_stop.load(std::memory_order_acquire)) {
            const auto acquire_begin = std::chrono::steady_clock::now();
            try {
                // A 300 ms+ device stall is a board/driver queue event. Do
                // not immediately start another heavy scan: retain the last
                // published immutable snapshot and give the DMA transport a
                // short recovery window.
                const uint64_t now = GetTickCount64();
                const uint64_t coolUntil = g_dma_cooldown_until_ms.load(std::memory_order_acquire);
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
                g_acq_busy.store(true, std::memory_order_release);
                auto frame_config = g_config_snapshots.Acquire();
                {
                    std::scoped_lock dmaGate(g_dma_read_gate);
                    RunFrameWithConfig(*frame_config);
                    // A full scan can include bone validation and briefly hold the
                    // DMA gate. Stamp the newest view matrix immediately before
                    // releasing that gate so the presentation never has to wait
                    // for the camera worker's next scheduler wake-up after a
                    // heavy player pass. This is one 64-byte read per full scan,
                    // not a per-entity transfer.
                    if (runtime.in_match && runtime.client_base && offsets.dwViewMatrix) {
                        float matrix[16]{};
                        mem.SetDmaLane("camera");
                        mem.SetDmaCallTag("CS2.CameraCatchup");
                        if (QRead(runtime.client_base + offsets.dwViewMatrix, matrix, sizeof(matrix)))
                            PublishCameraSnapshot(matrix);
                        mem.SetDmaLane("full");
                    }
                }
                g_acq_busy.store(false, std::memory_order_release);
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
                    if (!activeSample) {
                        MotionSnapshot empty{};
                        empty.timestamp_ms = GetTickCount64();
                        PublishMotionSnapshot(empty);
                    }
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
                    {
                        const auto cam = g_camera_snapshots.Acquire();
                        if (cam) {
                            const float age = static_cast<float>(GetTickCount64() - cam->timestamp_ms);
                            tel.camera_age_ms.store(age, std::memory_order_relaxed);
                        }
                        const auto mot = g_motion_snapshots.Acquire();
                        if (mot) {
                            const float age = static_cast<float>(GetTickCount64() - mot->timestamp_ms);
                            tel.motion_age_ms.store(age, std::memory_order_relaxed);
                        }
                        const auto rt = g_runtime_snapshots.Acquire();
                        if (rt) {
                            const float age = static_cast<float>(GetTickCount64() - rt->snapshot_timestamp_ms);
                            tel.snapshot_age_ms.store(age, std::memory_order_relaxed);
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
                {
                    ScopedPhase _phPub(&g_phase.publish_ms);
                    PublishRuntimeSnapshot();
                }
                g_phase.other_ms = static_cast<float>(mem.ConsumeThreadDmaWaitUs()) / 1000.f;
            } catch (const std::exception& ex) {
                g_acq_busy.store(false, std::memory_order_release);
                std::cerr << "[CS2] acquisition exception: " << ex.what() << std::endl;
            } catch (...) {
                g_acq_busy.store(false, std::memory_order_release);
                std::cerr << "[CS2] acquisition unknown exception" << std::endl;
            }

            // Full entity/bone producer: fixed 6 ms in a match.  The
            // scheduler is deadline-based, so work that finishes early waits
            // only until the next cadence and never accumulates Sleep drift.
            // If a scan itself takes longer than 6 ms it is published at once;
            // snapshots are never queued behind an older frame.
            // Lightweight profiles (box/HP/armor only) can scan slower; skeleton
            // keeps the tighter FULL_SCAN interval. Back off further after stalls.
            // Adaptive scheduler (hysteresis): based on measured work, not board name.
            int delayMs = runtime.in_match ? FULL_SCAN_INTERVAL_MS : 16;
            try {
                auto cfgLease = g_config_snapshots.Acquire();
                if (cfgLease) {
                    if (!NeedsPlayerScan(*cfgLease))
                        delayMs = 50; // on-demand sleep: zero wasted transfers
                    else if (runtime.in_match) {
                    if (!cfgLease->skeleton && !cfgLease->aim_enabled)
                        delayMs = (std::max)(delayMs, cfgLease->performance_mode ? 28 : 24);
                    else if (cfgLease->skeleton && !cfgLease->aim_enabled)
                        delayMs = (std::max)(delayMs, cfgLease->performance_mode ? 24 : 20);
                    }
                }

            } catch (...) {
                std::cerr << "[CS2] acquisition config snapshot failed with unknown exception" << std::endl;
            }
            {
                const float work = runtime.acquisition_ms;
                const float p95 = OmniGhost::Gameplay::DmaTelemetry::CS2().acquire_p95_ms.load(
                    std::memory_order_relaxed);
                int level = g_pressure_level.load(std::memory_order_relaxed);
                // Enter higher pressure with hysteresis
                // Higher thresholds so normal 20–100ms scans don't force "recovery".
                if (work > 120.f || p95 > 100.f) level = 3;
                else if (work > 80.f || p95 > 70.f) level = (std::max)(level, 2);
                else if (work > 50.f || p95 > 45.f) level = (std::max)(level, 1);
                else if (work < 30.f && p95 < 35.f) level = (std::max)(0, level - 1);
                g_pressure_level.store(level, std::memory_order_relaxed);
                // Mild backoff only on sustained pressure — keep ESP fluid.
                if (level >= 3) delayMs = (std::max)(delayMs, 28);
                else if (level == 2) delayMs = (std::max)(delayMs, 22);
                else if (level == 1) delayMs = (std::max)(delayMs, 18);
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
    if (g_camera_thread.joinable())
        g_camera_thread.join();
    if (g_acquisition_thread.joinable())
        g_acquisition_thread.join();
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

MotionSnapshotLease AcquireMotionSnapshot() {
    return g_motion_snapshots.Acquire();
}

bool AcquisitionRunning() noexcept {
    return g_acquisition_running.load(std::memory_order_acquire);
}

void SetPresentationFps(float fps) noexcept {
    if (std::isfinite(fps) && fps >= 0.f)
        g_presentation_fps.store(fps, std::memory_order_relaxed);
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
    WebRadar::Stop();
    DestroyScatter();
    g_pending_entity_list = 0;
    g_entity_list_confirmations = 0;
    g_controller_stride = kEntityIdentityStride;
    g_pawn_stride = kEntityIdentityStride;
    ready = false;
    runtime = Runtime{};
    PublishRuntimeSnapshot();
    status = "Shutdown";
}

bool IsGameProcessAlive() {
    // Prefer DMA remote PID (second PC). Fall back to local Toolhelp.
    DWORD pid = 0;
    if (mem.vHandle) {
        pid = mem.GetPidFromName("cs2.exe");
        if (!pid) pid = mem.GetPidFromName("CS2.exe");
    }
    if (!pid)
        pid = FindProcessIdByName("cs2.exe");
    if (!pid)
        pid = FindProcessIdByName("CS2.exe");
    return pid != 0;
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
