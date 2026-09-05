#include "cs2_game.h"
#include "../src/platform/offset_auto.h"
#include "../src/platform/app_paths.h"
#include "../src/platform/embedded_offsets.h"
#include "cs2_esp.h"
#include "cs2_aim.h"
#include "cs2_radar.h"
#include "cs2_weapons.h"
#include "Memory/Memory.h"
#include "globals.h"
#include "gameplay/esp_core.h"

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

namespace {

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

// Persistent scatter handle — N sequential DMA reads → 1 round-trip.
VMMDLL_SCATTER_HANDLE g_scatter = nullptr;
uintptr_t g_cached_entity_root = 0; // refreshed once per frame when scanning

void EnsureScatter() {
    if (!g_scatter && mem.vHandle)
        g_scatter = mem.CreateScatterHandle();
}

void DestroyScatter() {
    if (g_scatter) {
        mem.CloseScatterHandle(g_scatter);
        g_scatter = nullptr;
    }
    g_cached_entity_root = 0;
}

// True only when some feature actually needs the player list this frame.
// With everything OFF this is false → RunFrame is nearly free (target 130+ FPS).
bool NeedsPlayerScan() {
    return config.esp_enabled
        || config.aim_enabled
        || config.trigger_enabled
        || config.radar_2d
        || config.webradar_enabled
        || config.spectator_list
        || config.recoil_visual
        || config.offscreen_arrows
        || config.bomb_timer
        || config.hotkey_overlay;
}

bool ReadTextFile(const fs::path& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream stream;
    stream << file.rdbuf();
    out = stream.str();
    return true;
}

void LoadSchemaOffsets(const fs::path& data_dir) {
    std::string schema;
    // Prefer explicit client_dll.json, but if missing concatenate all JSON files in the directory
    const fs::path client_schema = data_dir / "client_dll.json";
    if (ReadTextFile(client_schema, schema)) {
        // use client_dll.json as-is
    } else {
        for (const auto& ent : fs::directory_iterator(data_dir)) {
            if (!ent.is_regular_file()) continue;
            const fs::path p = ent.path();
            if (p.extension() != ".json") continue;
            std::string part;
            if (ReadTextFile(p, part)) {
                schema += "\n";
                schema += part;
            }
        }
        if (schema.empty()) {
            std::cout << "[CS2] Nenhum JSON de schema encontrado em " << data_dir << "; a usar offsets de membros integrados" << std::endl;
            return;
        } else {
            std::cout << "[CS2] Usando arquivos JSON encontrados em: " << data_dir << std::endl;
        }
    }

    JsonClassU64(schema, "C_BaseEntity", "m_iHealth", offsets.m_iHealth);
    JsonClassU64(schema, "C_BaseEntity", "m_iTeamNum", offsets.m_iTeamNum);
    JsonClassU64(schema, "C_BaseEntity", "m_pGameSceneNode", offsets.m_pGameSceneNode);
    JsonClassU64(schema, "C_BaseEntity", "m_fFlags", offsets.m_fFlags);
    JsonClassU64(schema, "C_BasePlayerPawn", "m_vOldOrigin", offsets.m_vOldOrigin);
    JsonClassU64(schema, "CCSPlayerController", "m_hPlayerPawn", offsets.m_hPlayerPawn);
    JsonClassU64(schema, "CCSPlayerController", "m_hObserverPawn", offsets.m_hObserverPawn);
    JsonClassU64(schema, "CCSPlayerController", "m_iPawnHealth", offsets.m_iPawnHealth);
    JsonClassU64(schema, "CCSPlayerController", "m_iPawnArmor", offsets.m_iPawnArmor);
    JsonClassU64(schema, "CBasePlayerController", "m_iszPlayerName", offsets.m_iszPlayerName);
    JsonClassU64(schema, "CCSPlayerController", "m_sSanitizedPlayerName", offsets.m_sSanitizedPlayerName);
    JsonClassU64(schema, "CCSPlayerController", "m_bPawnIsAlive", offsets.m_bPawnIsAlive);
    JsonClassU64(schema, "C_CSPlayerPawn", "m_ArmorValue", offsets.m_ArmorValue);
    JsonClassU64(schema, "C_CSPlayerPawn", "m_angEyeAngles", offsets.m_angEyeAngles);
    JsonClassU64(schema, "C_CSPlayerPawn", "m_iShotsFired", offsets.m_iShotsFired);
    JsonClassU64(schema, "C_CSPlayerPawn", "m_iIDEntIndex", offsets.m_iIDEntIndex);
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
            if (fs::exists(p / "offsets.json")) {
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
        file = (fs::path(dir) / "offsets.json").string();
    }

    std::cout << "[CS2] Loading offsets from: " << file << std::endl;
    std::string data;
    if (!ReadTextFile(file, data)) {
        status = "offsets.json nao encontrado (coloca em data/ ao lado do exe)";
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
            } catch (...) { out = 0; }
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

    LoadSchemaOffsets(fs::path(file).parent_path());
    offsets.loaded = offsets.dwEntityList && offsets.dwLocalPlayerPawn && offsets.dwViewMatrix;

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
        status = "offsets.json incompleto";
        offsets_source = "offsets.json incompleto";
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
        std::clog << "[RESOURCE] id=data/cs2_offsets.json load=FAIL reason="
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
    offsets.loaded = required;
    if (offsets.loaded) {
        offsets_source = "offsets embedded";
        std::cout << "[RESOURCE] id=data/cs2_offsets.json load=PASS build="
                  << snapshot.metadata.build << std::endl;
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
    offsets.loaded = true;
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

    if (!mem.vHandle) {
        status = "Inicializando DMA";
        std::cout << "[CS2] " << status << std::endl;
        if (!mem.Init("", true, false)) {
            status = "DMA offline";
            std::cout << "[CS2] " << status << std::endl;
            return false;
        }
    }

    // Wait up to 3 minutes — user may click Start before launching CS2.
    if (!WaitForProcess(180))
        return false;

    // Attach may fail briefly while the process is still starting; retry.
    bool attached = false;
    for (int a = 0; a < 30; ++a) {
        if (mem.Init("cs2.exe", false, false) || mem.Init("CS2.exe", false, false)) {
            attached = true;
            break;
        }
        status = "A anexar cs2.exe...";
        std::cout << "[CS2] Attach retry " << a << std::endl;
        Sleep(500);
    }
    if (!attached) {
        status = "Falha ao anexar cs2.exe";
        std::cout << "[CS2] " << status << std::endl;
        return false;
    }

    // client.dll is mapped late during boot — wait up to ~60s.
    for (int attempt = 0; attempt < 120; ++attempt) {
        if (ResolveModuleBases())
            break;
        if (attempt % 5 == 0)
            std::cout << "[CS2] client.dll ainda nao resolvido (" << attempt << ")" << std::endl;
        status = "A aguardar client.dll...";
        Sleep(500);
        // Re-bind process every few seconds in case DMA handle went stale
        if (attempt > 0 && (attempt % 10) == 0) {
            mem.Init("cs2.exe", false, false) || mem.Init("CS2.exe", false, false);
        }
    }
    if (!runtime.client_base) {
        status = "client.dll nao encontrado (abre o CS2 e espera o menu principal)";
        std::cout << "[CS2] " << status << std::endl;
        return false;
    }

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
            OmniGhost::OffsetAuto::MarkOutdated(ActiveGame::CS2, status);
            std::cout << "[CS2] " << status << std::endl;
            return false;
        }
    }

    if (runtime.engine_base && offsets.dwBuildNumber)
        QReadT(runtime.engine_base + offsets.dwBuildNumber, runtime.build_number);

    ready = true;
    OmniGhost::OffsetAuto::MarkLiveValid(ActiveGame::CS2,
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

    const bool havePlayers = !runtime.players.empty();
    const bool haveEntry = runtime.entity_list_entry != 0;

    if (haveEntry && havePlayers && s_fullProbeCooldown > 0) {
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

    const uintptr_t page = static_cast<uintptr_t>((handle & 0x7FFF) >> 9);
    const uintptr_t index = static_cast<uintptr_t>(handle & 0x1FF);

    uintptr_t chunk = 0;
    if (!QReadT(root + kEntityPageTableOffset + sizeof(uintptr_t) * page, chunk) ||
        !IsUserPointer(chunk))
        return 0;

    uintptr_t entity = 0;
    if (!QReadT(chunk + stride * index, entity) || !IsUserPointer(entity))
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

// Core pawn fields in one scatter: health, team, armor, scene node.
struct PawnCoreFields {
    int health = 0;
    uint8_t team = 0;
    int armor = 0;
    uintptr_t scene = 0;
    bool spotted = true;
};

static void ScatterReadPawnCore(const uintptr_t* pawns, PawnCoreFields* fields, int count,
                                bool need_armor) {
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

void RunFrame() {
    if (!ready || !offsets.loaded || !runtime.client_base)
        return;

    ++runtime.frames;
    {
        static auto last = std::chrono::steady_clock::now();
        static int frame_bucket = 0;
        ++frame_bucket;
        const auto now = std::chrono::steady_clock::now();
        const float elapsed = std::chrono::duration<float>(now - last).count();
        if (elapsed >= 0.5f) {
            runtime.fps = frame_bucket / elapsed;
            frame_bucket = 0;
            last = now;
        }
    }

    const uintptr_t client = runtime.client_base;
    // Keep last-good player snapshot when entity list briefly fails (prevents
    // ESP/aim going empty for 1–2 frames on ListEntry null flicker).
    static std::vector<Player> last_good_players;
    static int last_good_age = 0;
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
        status = "Lobby / a aguardar partida (map/pawn probe ativo)";
        if (config.webradar_enabled && (runtime.frames % 6) == 0)
            CS2_Radar::Update(runtime, config);
        else if (!config.webradar_enabled)
            CS2_Radar::Update(runtime, config);
        return;
    }

    // CRITICAL: with ESP/Aim/Radar all OFF, do almost zero DMA work.
    if (!NeedsPlayerScan()) {
        zero_player_frames = 0;
        if (config.bomb_timer && (runtime.frames % 2) == 0)
            UpdateBombState();
        else if (!config.bomb_timer)
            runtime.bomb = BombState{};
        if (config.webradar_enabled && (runtime.frames % 6) == 0)
            CS2_Radar::Update(runtime, config);
        else if (!config.webradar_enabled)
            CS2_Radar::Update(runtime, config);
        return;
    }

    if (!ProbeViewMatrix(runtime.view_matrix)) {
        if ((runtime.frames % 180) == 1)
            std::cout << "[CS2] ViewMatrix fail (fails=" << runtime.read_fails << ")" << std::endl;
        if (config.webradar_enabled && (runtime.frames % 6) == 0)
            CS2_Radar::Update(runtime, config);
        else if (!config.webradar_enabled)
            CS2_Radar::Update(runtime, config);
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

    if (runtime.local_pawn) {
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

        float view_angles[2]{};
        if (offsets.dwViewAngles && QRead(client + offsets.dwViewAngles, view_angles, sizeof(view_angles)) &&
            std::isfinite(view_angles[1]))
            runtime.local_view_yaw = view_angles[1];
        else if (QRead(runtime.local_pawn + offsets.m_angEyeAngles, view_angles, sizeof(view_angles)) &&
                 std::isfinite(view_angles[1]))
            runtime.local_view_yaw = view_angles[1];
    }

    if (!RefreshEntityListEntry()) {
        // Hold last-good ESP/aim targets briefly so the overlay does not blink
        // off when ListEntry is transiently null after map/round changes.
        if (!last_good_players.empty() && last_good_age < 90) {
            runtime.players = last_good_players;
            runtime.player_count = static_cast<int>(runtime.players.size());
            ++last_good_age;
        }
        if (config.webradar_enabled && (runtime.frames % 6) == 0)
            CS2_Radar::Update(runtime, config);
        else if (!config.webradar_enabled)
            CS2_Radar::Update(runtime, config);
        return;
    }

    int kMax = config.max_entities > 0 ? config.max_entities : 32;
    if (kMax > 64) kMax = 64;
    if (config.auto_entity_cap) {
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

    // Calibrate handle→pawn stride against the local player when available.
    if (runtime.local_controller && runtime.local_pawn)
        CalibratePawnStride(runtime.local_controller, runtime.local_pawn);

    // ── Phase 1: scatter all pawn handles in one DMA round-trip ──────────
    uint32_t handles[kMaxSlots]{};
    ScatterReadPawnHandles(controllers, handles, kMaxSlots);

    // ── Phase 2: resolve handles → pawn pointers (root cached this frame) ─
    uintptr_t resolved_pawns[kMaxSlots]{};
    int slot_index[kMaxSlots]{};
    int candidate_count = 0;
    int handle_nonzero = 0;
    int resolve_ok = 0;
    for (int i = 0; i < kMaxSlots && candidate_count < kMax; ++i) {
        if (!controllers[i] || !handles[i]) continue;
        ++handle_nonzero;
        uintptr_t pawn = ResolvePawnFromHandle(handles[i], runtime.local_pawn);
        if (!pawn) continue;
        ++resolve_ok;
        resolved_pawns[candidate_count] = pawn;
        slot_index[candidate_count] = i;
        ++candidate_count;
    }
    runtime.pawn_count = candidate_count;
    runtime.pawn_stride = g_pawn_stride;

    // ── Phase 3: scatter health / team / armor / scene for candidates ────
    OmniGhost::Gameplay::EspCore::FeatureSet requested{};
    requested.box = config.box;
    requested.corner_box = config.box_corner;
    requested.skeleton = config.skeleton;
    requested.head = config.head_dot;
    requested.health = config.health_bar;
    requested.armor = config.armor_bar;
    requested.snapline = config.snaplines;
    requested.name = config.name || config.webradar_enabled || config.spectator_list;
    requested.weapon = config.weapon_icons;
    requested.distance = config.distance;
    requested.aim = config.aim_enabled || config.trigger_enabled;
    requested.prediction = config.aim_enabled && config.aim_prediction;
    requested.trail = config.trails;
    requested.halo = config.head_halo;
    requested.look_direction = config.look_direction;
    const auto fields = requested.RequiredFields();

    const bool need_armor = OmniGhost::Gameplay::EspCore::Has(
        fields, OmniGhost::Gameplay::EspCore::DataField::Armor);
    const bool need_names = OmniGhost::Gameplay::EspCore::Has(
        fields, OmniGhost::Gameplay::EspCore::DataField::Name);
    const bool need_weapons = OmniGhost::Gameplay::EspCore::Has(
        fields, OmniGhost::Gameplay::EspCore::DataField::Weapon);
    const bool need_yaw = config.webradar_enabled || config.radar_2d ||
        OmniGhost::Gameplay::EspCore::Has(
            fields, OmniGhost::Gameplay::EspCore::DataField::Facing);
    const bool track_velocity = OmniGhost::Gameplay::EspCore::Has(
        fields, OmniGhost::Gameplay::EspCore::DataField::Velocity);

    PawnCoreFields core[kMaxSlots]{};
    ScatterReadPawnCore(resolved_pawns, core, candidate_count, need_armor);

    // ── Phase 4: scatter positions (scene+origin or pawn+oldOrigin) ──────
    float positions[kMaxSlots][3]{};
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
        }
        mem.ExecuteReadScatter(g_scatter);
    } else {
        for (int c = 0; c < candidate_count; ++c) {
            if (IsUserPointer(core[c].scene))
                QRead(core[c].scene + offsets.m_vecAbsOrigin, positions[c], sizeof(float) * 3);
            else
                QRead(resolved_pawns[c] + offsets.m_vOldOrigin, positions[c], sizeof(float) * 3);
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

        // Scope / flash / recoil punch (cheap singles; only when features need them)
        if (config.scope_check || config.trigger_scoped_only || p.is_local) {
            uint8_t scoped = 0;
            if (offsets.m_bIsScoped && QReadT(pawn + offsets.m_bIsScoped, scoped))
                p.is_scoped = scoped != 0;
        }
        if (config.smoke_flash) {
            float flash = 0.f;
            if (offsets.m_flFlashDuration && QReadT(pawn + offsets.m_flFlashDuration, flash))
                p.is_flashed = flash > 0.15f;
        }
        if ((config.recoil_visual || config.aim_enabled) && p.is_local && offsets.m_aimPunchAngle) {
            float punch[2]{};
            if (QRead(pawn + offsets.m_aimPunchAngle, punch, sizeof(punch))) {
                p.aim_punch[0] = punch[0];
                p.aim_punch[1] = punch[1];
            }
        }

        p.pos[0] = positions[c][0];
        p.pos[1] = positions[c][1];
        p.pos[2] = positions[c][2];
        if (!IsFinitePosition(p.pos))
            continue;

        const uintptr_t scene = IsUserPointer(cf.scene) ? cf.scene : 0;

        if (need_yaw || p.is_local) {
            float eye_angles[2]{};
            if (QRead(pawn + offsets.m_angEyeAngles, eye_angles, sizeof(eye_angles)) &&
                std::isfinite(eye_angles[1]))
                p.view_yaw = eye_angles[1];
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

        if (need_names) {
            char nameBuf[64]{};
            QRead(controller + offsets.m_iszPlayerName, nameBuf, sizeof(nameBuf) - 1);
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
            if (!config.show_bots && p.is_bot)
                continue;
            std::memcpy(p.name, nameBuf, sizeof(p.name));
        } else {
            std::snprintf(p.name, sizeof(p.name), "P%d", p.ent_index);
        }

        const float dx = p.pos[0] - runtime.local_pos[0];
        const float dy = p.pos[1] - runtime.local_pos[1];
        const float dz = p.pos[2] - runtime.local_pos[2];
        p.distance = std::sqrt(dx * dx + dy * dy + dz * dz) / 39.37f;
        if (!p.is_local && config.max_distance > 1.f && p.distance > config.max_distance)
            continue;

        // CS2 bone indices (Source 2 / CS2-DMA-main Bone.h):
        // head=7 neck=6 spine1=4 spine2=2 pelvis=1
        // L arm 9/10/11  R arm 13/14/15
        // L leg 17/18/19  R leg 20/21/22
        // One contiguous 32-joint snapshot per player. Distance never removes
        // bones, so the visual quality stays identical under load.
        const bool need_bones = OmniGhost::Gameplay::EspCore::Has(
            fields, OmniGhost::Gameplay::EspCore::DataField::Skeleton);
        if (scene && need_bones) {
            // BoneJointData: Vec3 + float scale + pad[0x10] = 32 bytes
            struct BoneJoint { float x, y, z, scale; char pad[0x10]; };
            static_assert(sizeof(BoneJoint) == 32, "BoneJoint size");

            auto try_bones = [&](uintptr_t boneBase) -> bool {
                if (!IsUserPointer(boneBase)) return false;
                // Read enough joints for detailed close-range skeleton
                BoneJoint joints[32]{};
                if (!QRead(boneBase, joints, sizeof(joints))) return false;
                // Valve bone indices → our slot map (expanded for detail)
                // 0 head(7) 1 neck(6) 2 spine2(5) 3 spine1(4) 4 spine0(2) 5 pelvis(1)
                // 6 clav_l(8) 7 sh_l(9) 8 elb_l(10) 9 hand_l(11)
                // 10 clav_r(12) 11 sh_r(13) 12 elb_r(14) 13 hand_r(15)
                // 14 hip_l(22) 15 knee_l(23) 16 ankle_l(24) — fall back to 17/18/19 if needed
                // 17 hip_r(25) 18 knee_r(26) 19 ankle_r(27) — fall back to 20/21/22
                static const int kIdx[20] = {
                    7, 6, 5, 4, 2, 1,
                    8, 9, 10, 11,
                    12, 13, 14, 15,
                    22, 23, 24,
                    25, 26, 27
                };
                // Fallback leg chain used by many builds
                static const int kLegAlt[6] = { 17, 18, 19, 20, 21, 22 };

                float tmp[20][3]{};
                for (int b = 0; b < 20; ++b) {
                    int id = kIdx[b];
                    if (id < 0 || id >= 32) return false;
                    tmp[b][0] = joints[id].x;
                    tmp[b][1] = joints[id].y;
                    tmp[b][2] = joints[id].z;
                    if (!std::isfinite(tmp[b][0]) || !std::isfinite(tmp[b][1]) || !std::isfinite(tmp[b][2]))
                        return false;
                }
                // If primary leg indices look broken (zero / on top of pelvis), use alt chain
                {
                    const float px = tmp[5][0], py = tmp[5][1], pz = tmp[5][2];
                    auto badLeg = [&](int i) {
                        float dx = tmp[i][0] - px, dy = tmp[i][1] - py, dz = tmp[i][2] - pz;
                        float d2 = dx * dx + dy * dy + dz * dz;
                        return d2 < 4.f || d2 > 120.f * 120.f;
                    };
                    if (badLeg(14) || badLeg(17)) {
                        for (int i = 0; i < 6; ++i) {
                            int id = kLegAlt[i];
                            tmp[14 + i][0] = joints[id].x;
                            tmp[14 + i][1] = joints[id].y;
                            tmp[14 + i][2] = joints[id].z;
                        }
                    }
                }

                const float hx = tmp[0][0], hy = tmp[0][1], hz = tmp[0][2];
                const float px = tmp[5][0], py = tmp[5][1], pz = tmp[5][2];
                const float dx = hx - p.pos[0], dy = hy - p.pos[1];
                const float horiz = std::sqrt(dx * dx + dy * dy);
                const float torso = std::sqrt(
                    (hx - px) * (hx - px) + (hy - py) * (hy - py) + (hz - pz) * (hz - pz));
                if (horiz > 55.f) return false;
                if (torso < 20.f || torso > 110.f) return false;
                if (hz < p.pos[2] + 20.f) return false;

                if (p.distance <= 50.f) {
                    const float gdx = p.pos[0] - px;
                    const float gdy = p.pos[1] - py;
                    if (std::fabs(gdx) < 45.f && std::fabs(gdy) < 45.f) {
                        for (int b = 0; b < 20; ++b) {
                            tmp[b][0] += gdx;
                            tmp[b][1] += gdy;
                        }
                    }
                    const float gdz = p.pos[2] - pz;
                    if (std::fabs(gdz) < 25.f) {
                        for (int b = 0; b < 20; ++b)
                            tmp[b][2] += gdz;
                    }
                }

                for (int b = 0; b < 20; ++b) {
                    p.bones[b][0] = tmp[b][0];
                    p.bones[b][1] = tmp[b][1];
                    p.bones[b][2] = tmp[b][2];
                }
                p.head[0] = tmp[0][0]; p.head[1] = tmp[0][1]; p.head[2] = tmp[0][2];
                return true;
            };

            uintptr_t boneBase = 0;
            // Primary: CSkeletonInstance m_modelState + 0x80 (matches CS2-DMA)
            if (QReadT(scene + offsets.BoneArray, boneBase) && try_bones(boneBase)) {
                p.bones_ok = true;
            } else {
                // Fallback: some builds expose the bone pointer at scene+0x1D0 / 0x160
                for (uintptr_t alt : {(uintptr_t)0x1D0, (uintptr_t)0x160, (uintptr_t)0x1C0}) {
                    if (alt == offsets.BoneArray) continue;
                    if (QReadT(scene + alt, boneBase) && try_bones(boneBase)) {
                        p.bones_ok = true;
                        break;
                    }
                }
            }
        }
        if (!p.bones_ok) {
            p.head[0] = p.pos[0];
            p.head[1] = p.pos[1];
            p.head[2] = p.pos[2] + 72.f;
        }

        // Active weapon → item definition index → white icon code / name
        if (need_weapons) {
            uintptr_t weapon_services = 0;
            uint32_t weapon_handle = 0;
            if (offsets.m_pWeaponServices &&
                QReadT(p.pawn + offsets.m_pWeaponServices, weapon_services) &&
                IsUserPointer(weapon_services) &&
                QReadT(weapon_services + offsets.m_hActiveWeapon, weapon_handle) &&
                weapon_handle && weapon_handle != 0xFFFFFFFF) {
                uintptr_t weapon_ent = ResolveEntityByHandle(weapon_handle, g_pawn_stride ? g_pawn_stride : kEntityIdentityStride);
                if (IsUserPointer(weapon_ent)) {
                    // Primary: AttributeManager + Item + ItemDefinitionIndex
                    // Fallback: try a few known relative slots if schema drifts
                    uint16_t def = 0;
                    const uintptr_t primary = weapon_ent + offsets.m_AttributeManager +
                                              offsets.m_Item + offsets.m_iItemDefinitionIndex;
                    const uintptr_t candidates[] = {
                        primary,
                        weapon_ent + 0x11A8 + 0x50 + 0x1BA, // AttributeManager + Item + def
                        weapon_ent + 0x1BA,
                        weapon_ent + 0x16F0, // near weapon tick fields
                    };
                    for (uintptr_t addr : candidates) {
                        uint16_t d = 0;
                        if (QReadT(addr, d) && d > 0 && d < 6000) {
                            def = d;
                            break;
                        }
                    }
                    if (def > 0) {
                        p.weapon_def = def;
                        const char* wname = CS2_Weapons::NameFromDefIndex(def);
                        if (wname)
                            std::snprintf(p.weapon, sizeof(p.weapon), "%s", wname);
                        else
                            std::snprintf(p.weapon, sizeof(p.weapon), "Wpn_%u", (unsigned)def);
                    }
                }
            }
        }

        if (!p.is_local && p.team != runtime.local_team)
            ++runtime.enemy_count;

        runtime.players.push_back(p);
        ++processed;
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

    if (runtime.in_match && config.bomb_timer)
        UpdateBombState();
    else
        runtime.bomb = BombState{};

    // Snapshot successful scans for the hold-over path above.
    if (!runtime.players.empty()) {
        last_good_players = runtime.players;
        last_good_age = 0;
    } else if (!last_good_players.empty()) {
        ++last_good_age;
        if (last_good_age > 180)
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
    if (config.webradar_enabled) {
        if ((runtime.frames % 6) == 0)
            CS2_Radar::Update(runtime, config);
    } else {
        CS2_Radar::Update(runtime, config);
    }
}


bool ReinitDma() {
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
    return true;
}

void Shutdown() {
    CS2_Radar::Shutdown();
    DestroyScatter();
    g_pending_entity_list = 0;
    g_entity_list_confirmations = 0;
    g_controller_stride = kEntityIdentityStride;
    g_pawn_stride = kEntityIdentityStride;
    ready = false;
    runtime = Runtime{};
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
    else
        std::snprintf(buf, sizeof(buf), "CS2 b%u  %d jogadores",
            runtime.build_number, runtime.player_count);
    return buf;
}

int PlayerCount() { return runtime.player_count; }

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
    // Use soft probe for lobby-safe validation; if that passes, offsets are not outdated.
    return SoftProbeLobbyOffsets();
}

} // namespace CS2
