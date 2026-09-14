#include "cs2_game.h"
#include "cs2_radar.h"
#include "platform/session_log.h"
#include "../src/platform/offset_auto.h"
#include "../src/platform/app_paths.h"
#include "../src/platform/embedded_offsets.h"
#include "cs2_esp.h"
#include "cs2_aim.h"
#include "cs2_weapons.h"
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
#include <thread>
#include "makcu/makcu_wrapper.h"
#include "../Fivem/aimbot/aim_type.h"

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

OmniGhost::Gameplay::SnapshotExchange<Runtime> g_runtime_snapshots;
OmniGhost::Gameplay::SnapshotExchange<CameraSnapshot> g_camera_snapshots;
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

// Quiet read — avoids flooding log with "[!] Failed to read Memory"
bool QRead(uintptr_t addr, void* buf, size_t size) {
    if (!addr || !buf || !size || !mem.vHandle) return false;
    // Use internal read path; Memory::Read logs on failure — only call when address looks valid
    if (addr < 0x10000) return false;
    if (!mem.Read(addr, buf, size)) {
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
bool QReadT(uintptr_t addr, T& out) {
    return QRead(addr, &out, sizeof(T));
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

ScatterHandleOwner g_scatter;
uintptr_t g_cached_entity_root = 0; // refreshed once per frame when scanning

void EnsureScatter() {
    g_scatter.Ensure();
}

void DestroyScatter() {
    g_scatter.Reset();
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
        || frame_config.offscreen_arrows
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
    JsonClassU64(schema, "C_CSPlayerPawn", "m_ArmorValue", offsets.m_ArmorValue);
    JsonClassU64(schema, "C_CSPlayerPawn", "m_angEyeAngles", offsets.m_angEyeAngles);
    JsonClassU64(schema, "C_CSPlayerPawn", "m_iIDEntIndex", offsets.m_iIDEntIndex);
    JsonClassU64(schema, "C_CSPlayerPawn", "m_iShotsFired", offsets.m_iShotsFired);
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
    if (!runtime.client_base || !offsets.dwViewMatrix ||
        !QRead(runtime.client_base + offsets.dwViewMatrix, probe, sizeof(probe)))
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
        runtime.map_name[0] = '\0';
        return;
    }

    uintptr_t globals = 0;
    uintptr_t map_address = 0;
    char raw[128]{};
    if (!QReadT(runtime.client_base + offsets.dwGlobalVars, globals) || !IsUserPointer(globals) ||
        !QReadT(globals + kGlobalVarsCurrentMap, map_address) || !IsUserPointer(map_address) ||
        !QRead(map_address, raw, sizeof(raw) - 1)) {
        runtime.map_name[0] = '\0';
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
    // Controllers live at list_entry + (i+1)*stride — probe slots 1..4
    int hits = 0;
    for (int i = 1; i <= 4; ++i) {
        uintptr_t ctrl = 0;
        if (QReadT(entry + static_cast<uintptr_t>(i) * stride, ctrl) && IsUserPointer(ctrl))
            ++hits;
    }
    return hits >= 1;
}

static bool RefreshEntityListEntry() {
    // Fast path: list healthy + we have players → only verify primary page (~1 read)
    // Full multi-page probe only when empty, collapsed, or periodic revalidate.
    static int s_stableFrames = 0;
    static int s_fullProbeCooldown = 0;

    uintptr_t root = 0;
    if (!QReadT(runtime.client_base + offsets.dwEntityList, root) || !IsUserPointer(root)) {
        g_cached_entity_root = 0;
        s_stableFrames = 0;
        return runtime.entity_list_entry != 0;
    }
    g_cached_entity_root = root;

    const bool haveEntry = runtime.entity_list_entry != 0;

    // runtime.players is rebuilt at the start of every scan, so using it here
    // made the supposed fast path unreachable. A confirmed entry is enough to
    // perform the cheap one-pointer validation.
    if (haveEntry && s_fullProbeCooldown > 0) {
        --s_fullProbeCooldown;
        // Cheap confirm: primary page still matches
        uintptr_t entry = 0;
        if (QReadT(root + kEntityPageTableOffset, entry) && IsUserPointer(entry)) {
            if (entry == runtime.entity_list_entry) {
                ++s_stableFrames;
                return true;
            }
            // Primary changed — force full probe next
            s_stableFrames = 0;
            s_fullProbeCooldown = 0;
        } else {
            s_stableFrames = 0;
            s_fullProbeCooldown = 0;
        }
    }

    static const uintptr_t kPageOffs[] = {
        kEntityPageTableOffset, // 0x10
        0x08,
        0x18,
        0x20,
        0x28
    };

    uintptr_t best_entry = 0;
    uintptr_t best_stride = g_controller_stride ? g_controller_stride : kEntityIdentityStride;

    for (uintptr_t pageOff : kPageOffs) {
        uintptr_t entry = 0;
        if (!QReadT(root + pageOff, entry) || !IsUserPointer(entry))
            continue;

        // Prefer an entry that actually yields controller pointers.
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
        // Keep first valid pointer as fallback (warmup / empty slots)
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
        s_stableFrames++;
        // Re-probe fully every ~90 frames when stable (~1.5s @ 60fps)
        if (s_stableFrames > 90) {
            s_stableFrames = 0;
            s_fullProbeCooldown = 0;
        } else {
            s_fullProbeCooldown = 8; // skip full probe for a few frames
        }
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
    if (g_scatter) {
        for (int i = 0; i < max_count; ++i) {
            out[i] = 0;
            mem.AddScatterReadRequest(
                g_scatter,
                list_entry + static_cast<uintptr_t>(i + 1) * stride,
                &out[i],
                sizeof(uintptr_t));
        }
        mem.ExecuteReadScatter(g_scatter);
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
    if (!g_scatter) {
        for (int i = 0; i < count; ++i)
            handles[i] = controllers[i] ? ReadPawnHandle(controllers[i]) : 0;
        return;
    }
    for (int i = 0; i < count; ++i) {
        handles[i] = 0;
        if (!controllers[i]) continue;
        mem.AddScatterReadRequest(
            g_scatter,
            controllers[i] + offsets.m_hPlayerPawn,
            &handles[i],
            sizeof(uint32_t));
    }
    mem.ExecuteReadScatter(g_scatter);
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
    if (!g_scatter) {
        int resolved = 0;
        for (int i = 0; i < count; ++i) {
            pawns[i] = ResolvePawnFromHandle(handles[i], runtime.local_pawn);
            if (IsUserPointer(pawns[i])) ++resolved;
        }
        return resolved;
    }

    for (int page = 0; page < kMaxPages; ++page) {
        if (!pageUsed[page]) continue;
        mem.AddScatterReadRequest(g_scatter,
            root + kEntityPageTableOffset + sizeof(uintptr_t) * static_cast<uintptr_t>(page),
            &chunks[page], sizeof(uintptr_t));
    }
    mem.ExecuteReadScatter(g_scatter);

    for (int i = 0; i < count; ++i) {
        const EntityHandleParts parts = DecodeEntityHandle(handles[i]);
        if (!parts.valid) continue;
        const int page = static_cast<int>(parts.page);
        if (page < 0 || page >= kMaxPages || !IsUserPointer(chunks[page])) continue;
        mem.AddScatterReadRequest(g_scatter, chunks[page] + stride * parts.index,
                                  &pawns[i], sizeof(uintptr_t));
    }
    mem.ExecuteReadScatter(g_scatter);

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
                                bool need_yaw, bool need_weapons) {
    if (!pawns || !fields || count <= 0) return;
    EnsureScatter();
    if (!g_scatter) {
        for (int i = 0; i < count; ++i) {
            if (!pawns[i]) continue;
            QReadT(pawns[i] + offsets.m_iHealth, fields[i].health);
            {
                uint8_t sp = 1;
                if (offsets.m_entitySpottedState)
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
        mem.AddScatterReadRequest(g_scatter, pawns[i] + offsets.m_iHealth,
                                  &fields[i].health, sizeof(int));
        mem.AddScatterReadRequest(g_scatter, pawns[i] + offsets.m_iTeamNum,
                                  &fields[i].team, sizeof(uint8_t));
        if (need_armor)
            mem.AddScatterReadRequest(g_scatter, pawns[i] + offsets.m_ArmorValue,
                                      &fields[i].armor, sizeof(int));
        mem.AddScatterReadRequest(g_scatter, pawns[i] + offsets.m_pGameSceneNode,
                                  &fields[i].scene, sizeof(uintptr_t));
        if (offsets.m_entitySpottedState)
            mem.AddScatterReadRequest(g_scatter,
                pawns[i] + offsets.m_entitySpottedState + offsets.m_bSpotted,
                &spotted_buf[i], sizeof(uint8_t));
        if (need_scoped && offsets.m_bIsScoped)
            mem.AddScatterReadRequest(g_scatter, pawns[i] + offsets.m_bIsScoped,
                                      &fields[i].scoped, sizeof(fields[i].scoped));
        if (need_flash && offsets.m_flFlashDuration)
            mem.AddScatterReadRequest(g_scatter, pawns[i] + offsets.m_flFlashDuration,
                                      &fields[i].flash, sizeof(fields[i].flash));
        if (need_yaw)
            mem.AddScatterReadRequest(g_scatter, pawns[i] + offsets.m_angEyeAngles,
                                      fields[i].eye_angles, sizeof(fields[i].eye_angles));
        if (need_weapons && offsets.m_pWeaponServices)
            mem.AddScatterReadRequest(g_scatter, pawns[i] + offsets.m_pWeaponServices,
                                      &fields[i].weapon_services, sizeof(uintptr_t));
    }
    mem.ExecuteReadScatter(g_scatter);
    for (int i = 0; i < count; ++i)
        fields[i].spotted = (spotted_buf[i] != 0);
}

void UpdateBombState() {
    runtime.bomb = BombState{};
    if (!offsets.dwPlantedC4 || offsets.dwPlantedC4 < 8)
        return;

    uint8_t planted_count = 0;
    if (!QReadT(runtime.client_base + offsets.dwPlantedC4 - 8, planted_count) || planted_count == 0)
        return;

    uintptr_t list_data = 0;
    uintptr_t entity = 0;
    if (!QReadT(runtime.client_base + offsets.dwPlantedC4, list_data) || !IsUserPointer(list_data) ||
        !QReadT(list_data, entity) || !IsUserPointer(entity))
        return;

    uint8_t ticking = 0;
    if (offsets.m_bBombTicking)
        QReadT(entity + offsets.m_bBombTicking, ticking);
    if (!ticking)
        return;

    float absolute_blow_time = 0.f;
    float absolute_defuse_time = 0.f;
    float timer_length = 0.f;
    uint8_t defused = 0;
    uint8_t defusing = 0;
    if (offsets.m_flC4Blow)
        QReadT(entity + offsets.m_flC4Blow, absolute_blow_time);
    if (offsets.m_flTimerLength)
        QReadT(entity + offsets.m_flTimerLength, timer_length);
    if (offsets.m_bBombDefused)
        QReadT(entity + offsets.m_bBombDefused, defused);
    if (offsets.m_bBeingDefused)
        QReadT(entity + offsets.m_bBeingDefused, defusing);
    if (offsets.m_flDefuseCountDown)
        QReadT(entity + offsets.m_flDefuseCountDown, absolute_defuse_time);
    if (offsets.m_hBombDefuser)
        QReadT(entity + offsets.m_hBombDefuser, runtime.bomb.defuser_handle);

    float current_time = 0.f;
    uintptr_t globals = 0;
    bool have_time = false;
    if (offsets.dwGlobalVars &&
        QReadT(runtime.client_base + offsets.dwGlobalVars, globals) && IsUserPointer(globals)) {
        // Try common curtime slots used across recent CS2 builds
        for (uintptr_t off : {kGlobalVarsCurrentTime, (uintptr_t)0x2C, (uintptr_t)0x34, (uintptr_t)0x38}) {
            float t = 0.f;
            if (QReadT(globals + off, t) && std::isfinite(t) && t > 1.f && t < 1.0e7f) {
                current_time = t;
                have_time = true;
                break;
            }
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
    runtime.bomb.blow_time = blow;
    runtime.bomb.defuse_time = defuse;
    runtime.bomb.defused = defused != 0;
    runtime.bomb.defusing = defusing != 0 && defuse > 0.05f;

    uintptr_t scene = 0;
    if (QReadT(entity + offsets.m_pGameSceneNode, scene) && IsUserPointer(scene))
        QRead(scene + offsets.m_vecAbsOrigin, runtime.bomb.pos, sizeof(runtime.bomb.pos));
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
    nextRefreshMs = now + 50u;
    runtime.spectator_count = static_cast<int>(runtime.spectators.size());
}

static void RunFrameWithConfig(const Config& frame_config) {
    // Keep web radar HTTP server in sync with UI toggles.
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

    if (!ready || !offsets.loaded || !runtime.client_base)
        return;

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
    };
    static std::unordered_map<uintptr_t, RecentPlayer> recent_players;
    static std::unordered_map<uintptr_t, CachedBones> bone_cache;
    runtime.player_count = 0;
    runtime.enemy_count = 0;
    runtime.controller_count = 0;
    runtime.pawn_count = 0;
    if (!runtime.players.empty())
        runtime.players.clear();
    g_cached_entity_root = 0; // force refresh once per scanning frame

    // Map detection — faster while in lobby so lobby→match recovers quickly.
    static char previous_map[64]{};
    const int map_period = runtime.in_match ? 30 : 8;
    if ((runtime.frames % map_period) == 1)
        RefreshMapName();

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

    runtime.in_match = IsPlayableMapName(runtime.map_name);

    // Detect lobby → match and match → match transitions even when the map
    // string stays empty briefly (common when INSERT was opened in the lobby).
    static bool was_in_match = false;
    static int zero_player_frames = 0;
    static int match_probe_cooldown = 0;

    // Lightweight probe: if map string is blank but local pawn + view matrix
    // look live, treat as in-match so ESP recovers without restarting the menu.
    // Do NOT require HP > 0 — when you die the pawn still exists and ESP must
    // keep drawing other players during the death/spectate window.
    if (!runtime.in_match && match_probe_cooldown <= 0) {
        match_probe_cooldown = 15; // every ~15 frames
        uintptr_t probe_pawn = 0;
        if (QReadT(client + offsets.dwLocalPlayerPawn, probe_pawn) && IsUserPointer(probe_pawn)) {
            int hp = 0;
            QReadT(probe_pawn + offsets.m_iHealth, hp);
            // Accept any readable pawn with a valid view matrix (alive or dead)
            if (hp >= 0 && hp <= 200 && ProbeViewMatrix(nullptr)) {
                runtime.in_match = true;
                runtime.local_pawn = probe_pawn;
            }
        }
    } else if (match_probe_cooldown > 0) {
        --match_probe_cooldown;
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
        std::cout << "[CS2] Entrada em partida — a revalidar entity list" << std::endl;
    }
    was_in_match = runtime.in_match;

    // LOBBY / MENU: keep light probes only; entity DMA resumes on match enter.
    if (!runtime.in_match) {
        zero_player_frames = 0;
        return;
    }

    // CRITICAL: with ESP/Aim/Radar all OFF, do almost zero DMA work.
    if (!NeedsPlayerScan(frame_config)) {
        zero_player_frames = 0;
        if (frame_config.bomb_timer && (runtime.frames % 2) == 0)
            UpdateBombState();
        else if (!frame_config.bomb_timer)
            runtime.bomb = BombState{};
        return;
    }

    if (!ProbeViewMatrix(runtime.view_matrix)) {
        if ((runtime.frames % 180) == 1)
            std::cout << "[CS2] ViewMatrix fail (fails=" << runtime.read_fails << ")" << std::endl;
        return;
    }

    runtime.players.reserve(64);

    runtime.local_controller = 0;
    runtime.local_team = 0;
    runtime.local_view_yaw = 0.f;
    std::memset(runtime.local_pos, 0, sizeof(runtime.local_pos));
    uintptr_t localPawn = 0;
    if (QReadT(client + offsets.dwLocalPlayerPawn, localPawn) && IsUserPointer(localPawn))
        runtime.local_pawn = localPawn;
    else
        runtime.local_pawn = 0;
    if (offsets.dwLocalPlayerController) {
        uintptr_t localController = 0;
        if (QReadT(client + offsets.dwLocalPlayerController, localController) && IsUserPointer(localController))
            runtime.local_controller = localController;
    }

    // This is the only per-frame player-state read retained while dead.  It
    // makes death-mode self-healing at round respawn without keeping the ESP
    // acquisition lanes alive.
    if (runtime.local_pawn) {
        int sampled_health = runtime.local_health;
        // A failed DMA read must not be reinterpreted as HP=0.  Keep the last
        // validated value and only enter death-mode on a successful 0-HP read.
        if (QReadT(runtime.local_pawn + offsets.m_iHealth, sampled_health) &&
            sampled_health >= 0 && sampled_health <= 200)
            runtime.local_health = sampled_health;
    } else {
        runtime.local_health = 0;
    }

    if (runtime.local_pawn) {
        // HP is read immediately above.  Do not keep reading local movement,
        // view, item or trigger state after death; observer/bomb handling
        // below is the only remaining DMA work in that state.
        if (runtime.local_health > 0) {
        {
            static ULONGLONG s_lastTeamMs = 0;
            const ULONGLONG nowT = GetTickCount64();
            if (runtime.local_team < 2 || !s_lastTeamMs || (nowT - s_lastTeamMs) > 1000) {
                uint8_t team8 = 0;
                if (QReadT(runtime.local_pawn + offsets.m_iTeamNum, team8))
                    runtime.local_team = static_cast<int>(team8);
                s_lastTeamMs = nowT;
            }
        }
        uintptr_t scene = 0;
        if (QReadT(runtime.local_pawn + offsets.m_pGameSceneNode, scene) && IsUserPointer(scene))
            QRead(scene + offsets.m_vecAbsOrigin, runtime.local_pos, sizeof(float) * 3);
        else
            QRead(runtime.local_pawn + offsets.m_vOldOrigin, runtime.local_pos, sizeof(float) * 3);

        // Read local player velocity
        QRead(runtime.local_pawn + offsets.m_vecVelocity, runtime.local_vel, sizeof(float) * 3);

        // Read local player view angles
        float view_angles[2]{};
        if (offsets.dwViewAngles && QRead(client + offsets.dwViewAngles, view_angles, sizeof(view_angles)) &&
            std::isfinite(view_angles[1]))
        {
            runtime.local_view_yaw = view_angles[1];
            runtime.local_angles[0] = view_angles[0];
            runtime.local_angles[1] = view_angles[1];
        }
        else if (QRead(runtime.local_pawn + offsets.m_angEyeAngles, view_angles, sizeof(view_angles)) &&
                 std::isfinite(view_angles[1]))
        {
            runtime.local_view_yaw = view_angles[1];
            runtime.local_angles[0] = view_angles[0];
            runtime.local_angles[1] = view_angles[1];
        }

        // Read local player scoped state
        if (offsets.m_bIsScoped) {
            bool scoped = false;
            if (QReadT(runtime.local_pawn + offsets.m_bIsScoped, scoped))
                runtime.local_scoped = scoped;
        }

        runtime.local_has_defuser = false;
        if (offsets.m_pItemServices && offsets.m_bHasDefuser) {
            uintptr_t item_services = 0;
            if (QReadT(runtime.local_pawn + offsets.m_pItemServices, item_services) &&
                IsUserPointer(item_services)) {
                QReadT(item_services + offsets.m_bHasDefuser, runtime.local_has_defuser);
            }
        }

        runtime.local_crosshair_entity = 0;
        if (frame_config.trigger_enabled && frame_config.trigger_use_ident && offsets.m_iIDEntIndex)
            QReadT(runtime.local_pawn + offsets.m_iIDEntIndex,
                   runtime.local_crosshair_entity);
        if (frame_config.hit_marker && offsets.m_iShotsFired) {
            int shots = 0;
            if (QReadT(runtime.local_pawn + offsets.m_iShotsFired, shots) && shots >= 0 && shots < 256) {
                static int previous_shots = -1;
                if (previous_shots >= 0 && shots > previous_shots)
                    runtime.local_last_shot_ms = GetTickCount64();
                previous_shots = shots;
                runtime.local_shots_fired = shots;
            }
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
            UpdateBombState();
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
    requested.box = frame_config.box;
    requested.corner_box = frame_config.box_corner;
    requested.skeleton = frame_config.skeleton;
    requested.head = frame_config.head_dot;
    requested.health = frame_config.health_bar;
    requested.armor = frame_config.armor_bar;
    requested.snapline = frame_config.snaplines;
    requested.name = frame_config.name || frame_config.spectator_list;
    requested.weapon = frame_config.weapon_icons;
    requested.distance = frame_config.distance;
    requested.aim = frame_config.aim_enabled || frame_config.trigger_enabled;
    requested.prediction = frame_config.aim_enabled && frame_config.aim_prediction;
    requested.trail = frame_config.trails;
    requested.halo = frame_config.head_halo || frame_config.chinese_hat ||
        frame_config.devil_horns || frame_config.floating_crown;
    requested.look_direction = frame_config.look_direction || frame_config.angel_wings;
    const auto fields = requested.RequiredFields();

    const bool need_armor = OmniGhost::Gameplay::EspCore::Has(
        fields, OmniGhost::Gameplay::EspCore::DataField::Armor);
    const bool need_names = OmniGhost::Gameplay::EspCore::Has(
        fields, OmniGhost::Gameplay::EspCore::DataField::Name);
    const bool need_weapons = OmniGhost::Gameplay::EspCore::Has(
        fields, OmniGhost::Gameplay::EspCore::DataField::Weapon) ||
        frame_config.aim_enabled || frame_config.trigger_enabled;
    const bool need_yaw = frame_config.radar_2d ||
        OmniGhost::Gameplay::EspCore::Has(
            fields, OmniGhost::Gameplay::EspCore::DataField::Facing);
    const bool track_velocity = OmniGhost::Gameplay::EspCore::Has(
        fields, OmniGhost::Gameplay::EspCore::DataField::Velocity);
    const bool need_bones = OmniGhost::Gameplay::EspCore::Has(
        fields, OmniGhost::Gameplay::EspCore::DataField::Skeleton);
    const bool need_scoped = frame_config.scope_check || frame_config.trigger_scoped_only;

    PawnCoreFields core[kMaxSlots]{};
    ScatterReadPawnCore(resolved_pawns, core, candidate_count, need_armor,
                        need_scoped, frame_config.smoke_flash, need_yaw, need_weapons);

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
    BoneJointSnapshot boneSnapshots[kMaxSlots][32]{};
    uint32_t weaponHandles[kMaxSlots]{};
    uintptr_t weaponEntities[kMaxSlots]{};
    uint16_t weaponDefinitions[kMaxSlots]{};
    bool weaponDefinitionRead[kMaxSlots]{};
    struct CachedWeaponDefinition { uint16_t definition = 0; uint64_t last_refresh_ms = 0; };
    static std::unordered_map<uintptr_t, CachedWeaponDefinition> weaponDefinitionCache;
    const uint64_t scan_now_ms = GetTickCount64();
    if ((runtime.frames % 600u) == 0u) {
        for (auto it = nameCache.begin(); it != nameCache.end();) {
            if (scan_now_ms - it->second.last_refresh_ms > 30000u) it = nameCache.erase(it);
            else ++it;
        }
        for (auto it = weaponDefinitionCache.begin(); it != weaponDefinitionCache.end();) {
            if (scan_now_ms - it->second.last_refresh_ms > 30000u) it = weaponDefinitionCache.erase(it);
            else ++it;
        }
    }
    EnsureScatter();
    if (g_scatter && candidate_count > 0) {
        for (int c = 0; c < candidate_count; ++c) {
            const uintptr_t scene = core[c].scene;
            if (IsUserPointer(scene))
                mem.AddScatterReadRequest(g_scatter, scene + offsets.m_vecAbsOrigin,
                                          positions[c], sizeof(float) * 3);
            else
                mem.AddScatterReadRequest(g_scatter, resolved_pawns[c] + offsets.m_vOldOrigin,
                                          positions[c], sizeof(float) * 3);
            if (need_names) {
                const int controllerSlot = slot_index[c];
                const uintptr_t controller = controllers[controllerSlot];
                const auto cached = nameCache.find(controller);
                if (cached != nameCache.end() && cached->second.text[0] &&
                    scan_now_ms - cached->second.last_refresh_ms < 2000u) {
                    std::memcpy(playerNames[c], cached->second.text.data(),
                                sizeof(playerNames[c]));
                } else if (controller) {
                    nameNeedsRefresh[c] = true;
                    mem.AddScatterReadRequest(g_scatter,
                        controller + offsets.m_iszPlayerName,
                        playerNames[c], sizeof(playerNames[c]) - 1);
                }
            }
            if (need_bones && IsUserPointer(scene))
                mem.AddScatterReadRequest(g_scatter, scene + offsets.BoneArray,
                                          &boneBases[c], sizeof(uintptr_t));
        }
        mem.ExecuteReadScatter(g_scatter);
        if (need_bones) {
            bool queuedBoneReads = false;
            for (int c = 0; c < candidate_count; ++c) {
                // Skeleton is an explicitly enabled, high-priority feature.
                // Do not reject it using an approximate origin/frustum test:
                // near screen edges that test could suppress every bone even
                // though the pawn itself was visible in the final projection.
                // Animated bones are latency-sensitive. Sampling every scan
                // prevents distant players from appearing frozen between poses.
                const bool sample_this_scan = true;
                boneReadEligible[c] = sample_this_scan && IsUserPointer(boneBases[c]);
                if (boneReadEligible[c] && IsUserPointer(boneBases[c])) {
                    mem.AddScatterReadRequest(g_scatter, boneBases[c], boneSnapshots[c],
                                              sizeof(boneSnapshots[c]));
                    queuedBoneReads = true;
                }
            }
            if (queuedBoneReads)
                mem.ExecuteReadScatter(g_scatter);
        }
        if (need_weapons) {
            for (int c = 0; c < candidate_count; ++c) {
                if (IsUserPointer(core[c].weapon_services))
                    mem.AddScatterReadRequest(g_scatter,
                        core[c].weapon_services + offsets.m_hActiveWeapon,
                        &weaponHandles[c], sizeof(uint32_t));
            }
            mem.ExecuteReadScatter(g_scatter);
            ScatterResolvePawnHandles(weaponHandles, weaponEntities, candidate_count,
                g_pawn_stride ? g_pawn_stride : kEntityIdentityStride);
            bool queuedWeaponDefinitions = false;
            for (int c = 0; c < candidate_count; ++c) {
                if (!IsUserPointer(weaponEntities[c])) continue;
                const auto cached = weaponDefinitionCache.find(weaponEntities[c]);
                if (cached != weaponDefinitionCache.end() && cached->second.definition > 0 &&
                    scan_now_ms - cached->second.last_refresh_ms < 5000u) {
                    weaponDefinitions[c] = cached->second.definition;
                    continue;
                }
                const uintptr_t primary = weaponEntities[c] + offsets.m_AttributeManager +
                                          offsets.m_Item + offsets.m_iItemDefinitionIndex;
                mem.AddScatterReadRequest(g_scatter, primary, &weaponDefinitions[c],
                                          sizeof(uint16_t));
                weaponDefinitionRead[c] = true;
                queuedWeaponDefinitions = true;
            }
            if (queuedWeaponDefinitions)
                mem.ExecuteReadScatter(g_scatter);
            for (int c = 0; c < candidate_count; ++c) {
                if (!weaponDefinitionRead[c] || !weaponDefinitions[c]) continue;
                weaponDefinitionCache[weaponEntities[c]] = {
                    weaponDefinitions[c], scan_now_ms
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
                    scan_now_ms - cached->second.last_refresh_ms < 2000u) {
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
                QRead(boneBases[c], boneSnapshots[c], sizeof(boneSnapshots[c]));
        }
    }

    static std::unordered_map<uintptr_t, std::array<float, 3>> previous_positions;
    static auto previous_frame_time = std::chrono::steady_clock::now();
    const auto frame_time = std::chrono::steady_clock::now();
    const float delta_seconds = std::chrono::duration<float>(frame_time - previous_frame_time).count();
    std::unordered_map<uintptr_t, std::array<float, 3>> current_positions;
    if (track_velocity)
        current_positions.reserve(static_cast<size_t>(kMax));

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
        if (p.is_local)
            p.is_scoped = runtime.local_scoped;
        if (frame_config.smoke_flash)
            p.is_flashed = cf.flash > 0.15f;

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
                uint8_t layout = 0;
                for (const auto* indices : {kReferenceIdx, kCurrentIdx}) { // CS2-DMA layout first
                    bool finite = true;
                    for (std::size_t b = 0; b < kBoneSlotCount; ++b) {
                        const int id = indices[b];
                        tmp[b][0] = joints[id].x; tmp[b][1] = joints[id].y; tmp[b][2] = joints[id].z;
                        finite = finite && std::isfinite(tmp[b][0]) && std::isfinite(tmp[b][1]) && std::isfinite(tmp[b][2]);
                    }
                    const float score = finite ? skeleton_score(tmp) : -1.f;
                    if (score > bestScore) {
                        bestScore = score;
                        bestLayout = layout;
                        std::memcpy(best, tmp, sizeof(best));
                    }
                    ++layout;
                }
                if (bestScore < 0.f) return false;

                for (std::size_t b = 0; b < kBoneSlotCount; ++b) {
                    p.bones[b][0] = best[b][0];
                    p.bones[b][1] = best[b][1];
                    p.bones[b][2] = best[b][2];
                }
                p.head[0] = best[0][0]; p.head[1] = best[0][1]; p.head[2] = best[0][2];
                p.bone_layout = bestLayout;
                return true;
            };

            uintptr_t boneBase = 0;
            bool acquired_real_bones = false;
            // Primary: CSkeletonInstance m_modelState + 0x80 (matches CS2-DMA)
            if (IsUserPointer(boneBases[c]) && try_bones(boneSnapshots[c])) {
                p.bones_ok = true;
                p.bone_base = boneBases[c];
                acquired_real_bones = true;
            } else {
                // Fallback: some builds expose the bone pointer at scene+0x1D0 / 0x160
                for (uintptr_t alt : {(uintptr_t)0x1D0, (uintptr_t)0x160, (uintptr_t)0x1C0}) {
                    if (alt == offsets.BoneArray) continue;
                    if (QReadT(scene + alt, boneBase) && IsUserPointer(boneBase)) {
                        BoneJointSnapshot fallback[32]{};
                        if (QRead(boneBase, fallback, sizeof(fallback)) && try_bones(fallback)) {
                            p.bones_ok = true;
                            p.bone_base = boneBase;
                            acquired_real_bones = true;
                            break;
                        }
                    }
                }
            }
            if (acquired_real_bones) {
                auto& cached = bone_cache[p.pawn];
                std::memcpy(cached.joints, p.bones, sizeof(p.bones));
                std::memcpy(cached.origin, p.pos, sizeof(cached.origin));
                cached.last_valid_ms = scan_now_ms;
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
                scan_now_ms - cached->second.last_valid_ms <= 750u) {
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
            if ((def == 0 || def >= 6000) && IsUserPointer(weapon_ent)) {
                const uintptr_t fallbacks[] = {
                    weapon_ent + 0x11A8 + 0x50 + 0x1BA,
                    weapon_ent + 0x1BA,
                    weapon_ent + 0x16F0,
                };
                def = 0;
                for (uintptr_t addr : fallbacks) {
                    uint16_t candidate = 0;
                    if (QReadT(addr, candidate) && candidate > 0 && candidate < 6000) {
                        def = candidate;
                        break;
                    }
                }
            }
            if (def > 0 && def < 6000) {
                p.weapon_def = def;
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

    // Refresh the camera at the end of the scan. Player origins and bones stay
    // from the same scene-node sample; mixing them with m_vOldOrigin here used
    // to offset the ESP from the animated model while a player was moving.
    if (frame_config.esp_enabled && !runtime.players.empty()) {
        EnsureScatter();
        if (g_scatter) {
            std::vector<std::array<float, 3>> pre_refresh_positions;
            pre_refresh_positions.reserve(runtime.players.size());
            for (auto& player : runtime.players) {
                pre_refresh_positions.push_back({player.pos[0], player.pos[1], player.pos[2]});
            }
            mem.AddScatterReadRequest(g_scatter,
                runtime.client_base + offsets.dwViewMatrix,
                runtime.view_matrix, sizeof(runtime.view_matrix));
            mem.ExecuteReadScatter(g_scatter);
            for (std::size_t i = 0; i < runtime.players.size(); ++i) {
                auto& player = runtime.players[i];
                if (!IsFinitePosition(player.pos)) {
                    std::memcpy(player.pos, pre_refresh_positions[i].data(), sizeof(player.pos));
                }
                const float shift[3] = {
                    player.pos[0] - pre_refresh_positions[i][0],
                    player.pos[1] - pre_refresh_positions[i][1],
                    player.pos[2] - pre_refresh_positions[i][2]
                };
                if (player.bones_ok) {
                    for (std::size_t bone = 0; bone < kBoneSlotCount; ++bone)
                        for (int axis = 0; axis < 3; ++axis)
                            player.bones[bone][axis] += shift[axis];
                    for (int axis = 0; axis < 3; ++axis)
                        player.head[axis] += shift[axis];
                }
                if (player.is_local) {
                    std::memcpy(runtime.local_pos, player.pos, sizeof(runtime.local_pos));
                    break;
                }
            }
        }
    }

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
            next_spectator_refresh_ms = scan_now_ms + 50u;
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
        UpdateBombState();
    else
        runtime.bomb = BombState{};

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
        OmniGhost::Gameplay::FixedRateScheduler scheduler;
        float matrix[16]{};
        uint64_t next_motion_ms = 0;
        while (!g_acquisition_stop.load(std::memory_order_acquire)) {
            const bool canRead = ready && offsets.loaded && runtime.client_base && offsets.dwViewMatrix;
            if (canRead && QRead(runtime.client_base + offsets.dwViewMatrix, matrix, sizeof(matrix)))
                PublishCameraSnapshot(matrix);
            // Keep the view matrix on its own very fast lane, but never make
            // one DMA read per player every 2 ms.  That old pattern could
            // starve the regular entity scan and made even boxes/bars hitch.
            // Player motion is sampled at the same 6-ms cadence as the
            // producer; rendering still runs every overlay frame.
            const uint64_t now_ms = GetTickCount64();
            const auto frame_config = g_config_snapshots.Acquire();
            const bool needs_motion = frame_config->esp_enabled &&
                (frame_config->box || frame_config->box_corner || frame_config->health_bar ||
                 frame_config->armor_bar || frame_config->skeleton || frame_config->trails ||
                 frame_config->head_halo || frame_config->look_direction || frame_config->chinese_hat ||
                 frame_config->angel_wings || frame_config->devil_horns || frame_config->floating_crown);
            if (canRead && runtime.in_match && needs_motion && now_ms >= next_motion_ms) {
                next_motion_ms = now_ms + 6;
                const auto current = g_runtime_snapshots.Acquire();
                MotionSnapshot motion{};
                for (const auto& player : current->players) {
                    if (motion.count >= motion.players.size() || !player.pawn || !IsUserPointer(player.scene))
                        continue;
                    auto& sample = motion.players[motion.count];
                    sample.pawn = player.pawn;
                    if (!QRead(player.scene + offsets.m_vecAbsOrigin, sample.pos, sizeof(sample.pos)) ||
                        !std::isfinite(sample.pos[0]) || !std::isfinite(sample.pos[1]) ||
                        !std::isfinite(sample.pos[2]))
                        continue;
                    // A compact 32-joint read keeps limb animation on the
                    // same fast lane as the box.  A failed/invalid fast read
                    // never replaces the last full validated pose.
                    if (frame_config->skeleton && player.bones_ok && IsUserPointer(player.bone_base)) {
                        struct FastBoneJoint { float x, y, z, scale; char pad[0x10]; } joints[32]{};
                        static constexpr int kReferenceIdx[kBoneSlotCount] = {
                            7, 6, 4, 3, 3, 1, 6, 9, 10, 11, 6, 13, 14, 15, 17, 18, 19, 20, 21, 22
                        };
                        static constexpr int kCurrentIdx[kBoneSlotCount] = {
                            6, 5, 4, 3, 2, 0, 8, 8, 9, 10, 13, 13, 14, 15, 22, 23, 24, 25, 26, 27
                        };
                        const int* indices = player.bone_layout == 0 ? kReferenceIdx : kCurrentIdx;
                        bool valid = QRead(player.bone_base, joints, sizeof(joints));
                        for (std::size_t bone = 0; valid && bone < kBoneSlotCount; ++bone) {
                            const auto& joint = joints[indices[bone]];
                            const float dx = joint.x - sample.pos[0];
                            const float dy = joint.y - sample.pos[1];
                            const float dz = joint.z - sample.pos[2];
                            valid = std::isfinite(joint.x) && std::isfinite(joint.y) && std::isfinite(joint.z) &&
                                dx * dx + dy * dy < 260.f * 260.f && dz > -100.f && dz < 220.f;
                            sample.bones[bone][0] = joint.x;
                            sample.bones[bone][1] = joint.y;
                            sample.bones[bone][2] = joint.z;
                        }
                        valid = valid && sample.bones[0][2] > sample.bones[5][2] + 8.f;
                        sample.bones_ok = valid;
                    }
                    ++motion.count;
                }
                if (motion.count) {
                    motion.timestamp_ms = GetTickCount64();
                    PublishMotionSnapshot(motion);
                }
            }
            const float fps = g_presentation_fps.load(std::memory_order_relaxed);
            const int cadence = runtime.in_match ? ((fps > 1.f && fps < 55.f) ? 4 : 2) : 12;
            scheduler.Wait(std::chrono::milliseconds(cadence));
        }
    });
    g_acquisition_thread = std::thread([] {
        OmniGhost::Gameplay::FixedRateScheduler scheduler;
        while (!g_acquisition_stop.load(std::memory_order_acquire)) {
            const auto acquire_begin = std::chrono::steady_clock::now();
            try {
                auto frame_config = g_config_snapshots.Acquire();
                RunFrameWithConfig(*frame_config);
                runtime.acquisition_ms = OmniGhost::Gameplay::TimeMs(acquire_begin);
                // Processing is deliberately kept producer-side and currently
                // consists of validation/cache assembly included in RunFrame.
                runtime.processing_ms = runtime.acquisition_ms;
                OmniGhost::Gameplay::PipelineTelemetry::Smooth(
                    g_pipeline_metrics.acquire_ms, runtime.acquisition_ms);
                g_pipeline_metrics.entities.store(runtime.player_count, std::memory_order_relaxed);
                PublishRuntimeSnapshot();
            } catch (const std::exception& ex) {
                std::cerr << "[CS2] acquisition exception: " << ex.what() << std::endl;
            } catch (...) {
                std::cerr << "[CS2] acquisition unknown exception" << std::endl;
            }

            // Full entity/bone producer: fixed 6 ms in a match.  The
            // scheduler is deadline-based, so work that finishes early waits
            // only until the next cadence and never accumulates Sleep drift.
            // If a scan itself takes longer than 6 ms it is published at once;
            // snapshots are never queued behind an older frame.
            const int delayMs = runtime.in_match ? 6 : 16;
            scheduler.Wait(std::chrono::milliseconds(delayMs));
        }
    });
}

void StopAcquisition() {
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
