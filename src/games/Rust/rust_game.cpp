#include "rust_game.h"
#include "rust_dtb.h"
#include "rust_decrypt.h"
#include "rust_entities.h"
#include "rust_esp.h"
#include "rust_aim.h"
#include "../DMALibrary/Memory/Memory.h"

#include <fstream>
#include <iostream>
#include <filesystem>
#include <cstring>
#include <cstdlib>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

extern Memory mem;

namespace Rust {

Offsets offsets{};
Runtime runtime{};
bool ready = false;
char status[160] = "Rust offline";

namespace {
bool JsonU64(const std::string& src, const char* key, uintptr_t& out) {
    const std::string pat = std::string("\"") + key + "\"";
    auto pos = src.find(pat);
    if (pos == std::string::npos) return false;
    pos = src.find(':', pos);
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\r' || src[pos] == '\n')) ++pos;
    if (pos < src.size() && src[pos] == '"') ++pos;
    char* end = nullptr;
    unsigned long long v = strtoull(src.c_str() + pos, &end, 0);
    if (end == src.c_str() + pos) return false;
    out = static_cast<uintptr_t>(v);
    return true;
}

bool JsonStr(const std::string& src, const char* key, char* dst, size_t dstn) {
    const std::string pat = std::string("\"") + key + "\"";
    auto pos = src.find(pat);
    if (pos == std::string::npos) return false;
    pos = src.find(':', pos);
    if (pos == std::string::npos) return false;
    pos = src.find('"', pos);
    if (pos == std::string::npos) return false;
    ++pos;
    auto end = src.find('"', pos);
    if (end == std::string::npos) return false;
    size_t n = (std::min)(dstn - 1, end - pos);
    std::memcpy(dst, src.c_str() + pos, n);
    dst[n] = 0;
    return true;
}

std::vector<std::filesystem::path> SearchPaths(const char* explicitPath) {
    std::vector<std::filesystem::path> paths;
    if (explicitPath && *explicitPath) {
        paths.emplace_back(explicitPath);
        return paths;
    }
    char buf[MAX_PATH]{};
    if (GetModuleFileNameA(nullptr, buf, MAX_PATH)) {
        auto exeDir = std::filesystem::path(buf).parent_path();
        paths.push_back(exeDir / "data" / "rust_offsets.json");
        paths.push_back(exeDir / "rust_offsets.json");
    }
    paths.push_back(std::filesystem::path("data") / "rust_offsets.json");
    paths.push_back("rust_offsets.json");
    return paths;
}
} // namespace

bool LoadOffsetsFromJson(const char* explicit_path) {
    offsets = Offsets{};
    for (const auto& p : SearchPaths(explicit_path)) {
        std::ifstream in(p, std::ios::binary);
        if (!in) continue;
        std::string j((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (j.size() < 64) continue;

        auto grab = [&](const char* key, uintptr_t& dst) {
            uintptr_t v = 0;
            if (JsonU64(j, key, v) && v) dst = v;
        };

        grab("BaseNetworkable_TypeInfo", offsets.BaseNetworkable_TypeInfo);
        grab("base_networkable", offsets.BaseNetworkable_TypeInfo);
        grab("base_networkable_static", offsets.BaseNetworkable_TypeInfo);
        grab("MainCamera_TypeInfo", offsets.MainCamera_TypeInfo);
        grab("main_camera_c", offsets.MainCamera_TypeInfo);
        grab("il2cpphandle", offsets.Il2CppHandle_RVA);
        grab("gchandle_base_rva", offsets.Il2CppHandle_RVA);

        grab("staticFields", offsets.staticFields);
        grab("static_fields", offsets.staticFields);
        grab("client_entities", offsets.clientEntities);
        grab("entity_list", offsets.entityList);
        grab("entity_content", offsets.buffer);
        grab("entities", offsets.buffer);
        grab("entity_size", offsets.bufferSize);

        grab("playerModel", offsets.playerModel);
        grab("player_model", offsets.playerModel);
        grab("playerFlags", offsets.playerFlags);
        grab("player_flags", offsets.playerFlags);
        grab("displayName", offsets.displayName);
        grab("display_name", offsets.displayName);
        grab("currentTeam", offsets.currentTeam);
        grab("current_team", offsets.currentTeam);
        grab("clActiveItem", offsets.clActiveItem);
        grab("cl_active_item", offsets.clActiveItem);
        grab("inventory", offsets.inventory);
        grab("player_inventory", offsets.inventory);
        grab("playerInput", offsets.playerInput);
        grab("player_input", offsets.playerInput);
        grab("playerEyes", offsets.playerEyes);
        grab("player_eyes", offsets.playerEyes);
        grab("movement", offsets.movement);
        grab("base_movement", offsets.movement);
        grab("visiblePlayerList", offsets.visiblePlayerList);
        grab("visible_player_list", offsets.visiblePlayerList);

        grab("lifestate", offsets.lifestate);
        grab("_health", offsets.health);
        grab("_maxHealth", offsets.maxHealth);
        grab("model", offsets.baseModel);
        grab("modelPosition", offsets.modelPosition);
        grab("position", offsets.modelPosition);
        grab("newVelocity", offsets.modelVelocity);
        grab("velocity", offsets.modelVelocity);
        grab("isNpc", offsets.isNpc);
        grab("viewMatrix", offsets.viewMatrix);
        grab("view_matrix", offsets.viewMatrix);
        grab("boneTransforms", offsets.boneTransforms);

        JsonStr(j, "version", offsets.version, sizeof(offsets.version));
        Decrypt::LoadDecryptFromJson(j);
        Decrypt::ApplyBuiltinDefaults();

        offsets.loaded = offsets.BaseNetworkable_TypeInfo && offsets.MainCamera_TypeInfo;
        if (offsets.loaded) {
            std::snprintf(offsets.source, sizeof(offsets.source), "json");
            std::cout << "[Rust] Offsets from file " << p.string()
                      << " BN=0x" << std::hex << offsets.BaseNetworkable_TypeInfo
                      << " CAM=0x" << offsets.MainCamera_TypeInfo
                      << " IL2=0x" << offsets.Il2CppHandle_RVA << std::dec << "\n";
            return true;
        }
    }

    // Embedded resource path (OffsetSource) — best-effort via same JSON next to project data during dev
    std::snprintf(offsets.source, sizeof(offsets.source), "fallback");
    offsets.BaseNetworkable_TypeInfo = 0x10BE8EC8ull;
    offsets.MainCamera_TypeInfo = 0x10C62B28ull;
    offsets.Il2CppHandle_RVA = 0x10CA7920ull;
    offsets.loaded = true;
    Decrypt::ops.gchandle_base_rva = offsets.Il2CppHandle_RVA;
    Decrypt::ApplyBuiltinDefaults();
    std::cout << "[Rust] Offsets fallback seeds BN/CAM/IL2 (build 25231061)\n";
    return true;
}

bool Attach() {
    ready = false;
    runtime = Runtime{};
    std::snprintf(runtime.status, sizeof(runtime.status), "A anexar RustClient.exe");
    std::snprintf(status, sizeof(status), "%s", runtime.status);
    std::cout << "[Rust] attach begin vHandle=" << (mem.vHandle ? "yes" : "no") << "\n";

    if (!LoadOffsetsFromJson(nullptr)) {
        std::snprintf(runtime.status, sizeof(runtime.status), "Offsets Rust em falta");
        std::snprintf(status, sizeof(status), "%s", runtime.status);
        return false;
    }
    std::snprintf(runtime.offsets_version, sizeof(runtime.offsets_version), "%s", offsets.version);
    std::cout << "[Rust] Offsets source=" << offsets.source
              << " BN=0x" << std::hex << offsets.BaseNetworkable_TypeInfo
              << " CAM=0x" << offsets.MainCamera_TypeInfo << std::dec << "\n";

    // Paridade com CS2: so abre FPGA se ainda nao existir sessao VMM.
    // Reabrir com Init("", true) quando vHandle ja existe pode fechar/falhar a sessao.
    if (!mem.vHandle) {
        std::snprintf(runtime.status, sizeof(runtime.status), "Inicializando DMA");
        std::snprintf(status, sizeof(status), "%s", runtime.status);
        std::cout << "[Rust] DMA init (nova sessao FPGA)\n";
        if (!mem.Init(std::string(), true, false)) {
            std::snprintf(runtime.status, sizeof(runtime.status),
                "DMA offline (FPGA nao abriu sessao funcional)");
            std::snprintf(status, sizeof(status), "%s", runtime.status);
            std::cout << "[Rust] DMA open failed (same path as CS2)\n";
            return false;
        }
        std::cout << "[Rust] FPGA session open vHandle=yes\n";
    } else {
        std::cout << "[Rust] Reutilizando sessao DMA ja aberta (ex. apos CS2)\n";
    }

    const char* kProc = "RustClient.exe";
    DWORD pid = mem.GetPidFromName(kProc);
    if (!pid) {
        std::snprintf(runtime.status, sizeof(runtime.status),
            "Abre o Rust no PC principal e tenta de novo");
        std::snprintf(status, sizeof(status), "%s", runtime.status);
        std::cout << "[Rust] PID not found\n";
        return false;
    }
    runtime.pid = pid;
    std::cout << "[Rust] PID=" << pid << "\n";

    // Bind ao processo (sem reabrir device). FixCr3 corre dentro do Init bind.
    std::snprintf(runtime.status, sizeof(runtime.status), "DTB fix / a mapear GameAssembly...");
    std::snprintf(status, sizeof(status), "%s", runtime.status);
    auto dtb = Dtb::EnsureGameAssembly(kProc);
    if (!dtb.ok || !dtb.game_assembly) {
        // Segundo intento: bind explicito como o CS2 faz com cs2.exe
        if (mem.Init(kProc, false, false)) {
            dtb = Dtb::EnsureGameAssembly(kProc);
        }
    }
    if (!dtb.ok || !dtb.game_assembly) {
        std::snprintf(runtime.status, sizeof(runtime.status), "%s",
            dtb.detail.empty() ? "DTB/GameAssembly falhou" : dtb.detail.c_str());
        std::snprintf(status, sizeof(status), "%s", runtime.status);
        return false;
    }

    runtime.game_assembly = dtb.game_assembly;
    runtime.dtb = dtb.dtb;
    runtime.attached = true;
    Decrypt::SetGameAssembly(runtime.game_assembly);
    Decrypt::ops.gchandle_base_rva = offsets.Il2CppHandle_RVA;
    Decrypt::chain.static_fields = offsets.staticFields;
    Decrypt::chain.client_entities = offsets.clientEntities;
    Decrypt::chain.entity_list = offsets.entityList;
    Decrypt::chain.buffer = offsets.buffer;
    Decrypt::chain.count = offsets.bufferSize;

    std::cout << "[Rust] GameAssembly=0x" << std::hex << runtime.game_assembly << std::dec << "\n";
    std::cout << "[Rust] decrypt probe: " << Decrypt::ProbeEntityChainStatus() << "\n";

    Entities::Refresh();
    runtime.ready = true;
    ready = true;
    std::snprintf(runtime.status, sizeof(runtime.status), "Online | players=%d", runtime.player_count);
    std::snprintf(status, sizeof(status), "%s", runtime.status);
    std::cout << "[Rust] entities=" << runtime.entity_count
              << " players=" << runtime.player_count
              << " local=" << (runtime.local_player ? "ok" : "fail") << "\n";
    std::cout << "[Rust] Online\n";
    return true;
}

void Shutdown() {
    Entities::Clear();
    ready = false;
    runtime = Runtime{};
    std::snprintf(status, sizeof(status), "Rust offline");
    std::cout << "[Rust] Shutdown\n";
}

bool IsAlive() {
    if (!runtime.attached) return false;
    return mem.GetPidFromName("RustClient.exe") != 0;
}

bool ValidateOffsets() {
    return offsets.loaded && offsets.BaseNetworkable_TypeInfo && offsets.MainCamera_TypeInfo;
}

const char* StatusText() {
    return status;
}

void RunFrame() {
    if (!runtime.attached) {
        static ULONGLONG last = 0;
        const ULONGLONG now = GetTickCount64();
        if (now - last > 2500) {
            last = now;
            Attach();
        }
        return;
    }
    if (!IsAlive()) {
        std::cout << "[Rust] Processo terminou — a voltar a Waiting\n";
        Shutdown();
        std::snprintf(status, sizeof(status), "Processo Rust terminou");
        return;
    }
    ++runtime.frames;
    Entities::Refresh();
    ESP::Draw(runtime, config);
    Aim::Run(runtime, config);
    std::snprintf(runtime.status, sizeof(runtime.status), "Online | players=%d | GA=ok", runtime.player_count);
    std::snprintf(status, sizeof(status), "%s", runtime.status);
}

std::vector<Player> SnapshotPlayers() {
    return Entities::Players();
}

} // namespace Rust

