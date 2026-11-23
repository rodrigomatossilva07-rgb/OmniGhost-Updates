#include "fivem_game.h"
#include "platform/offset_source.h"
#include "src/project/third_party/DMALibrary/Memory/Memory.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

extern Memory mem;

namespace FiveM {

Config config{};
Runtime runtime{};
std::atomic_bool ready{false};


OmniGhost::EmbeddedOffsets::Snapshot g_offset_snapshot{};

namespace {
constexpr const char* kProcessName = "GTAProcess.exe";
constexpr std::uintptr_t kMinPointer = 0x10000;
constexpr std::uintptr_t kMaxPointer = 0x00007FFFFFFFFFFFull;
constexpr std::uint32_t kMaxEntities = 8192;
constexpr std::uint32_t kMaxPlayers = 256;

struct Vec3 { float x{}, y{}, z{}; };

bool IsPointer(std::uintptr_t value) {
    return value >= kMinPointer && value <= kMaxPointer && (value & 7u) == 0;
}

bool IsVector(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
           std::fabs(v.x) < 100000.f && std::fabs(v.y) < 100000.f && std::fabs(v.z) < 100000.f;
}

bool ReadExact(std::uintptr_t address, void* out, std::size_t size) {
    if (!IsPointer(address) || !out || !size) return false;
    std::size_t read = 0;
    return mem.ReadWithCount(address, out, size, read) && read == size;
}

template<class T>
bool ReadValue(std::uintptr_t address, T& out) {
    out = T{};
    return ReadExact(address, &out, sizeof(out));
}

bool ReadPointer(std::uintptr_t address, std::uintptr_t& out) {
    return ReadValue(address, out) && IsPointer(out);
}

void AddUnique(std::vector<std::uintptr_t>& values, std::uintptr_t value) {
    if (IsPointer(value) && std::find(values.begin(), values.end(), value) == values.end())
        values.push_back(value);
}

void AddPointerAt(std::vector<std::uintptr_t>& values, std::uintptr_t base, std::uintptr_t offset) {
    std::uintptr_t value = 0;
    if (ReadPointer(base + offset, value)) AddUnique(values, value);
}


inline std::uintptr_t GetOffset(const char* path, std::uintptr_t fallback = 0) {
    std::uint64_t value = 0;
    if (g_offset_snapshot.TryGet(path, value) && value != 0)
        return static_cast<std::uintptr_t>(value);
    return fallback;
}


struct RuntimeOffsets {
    std::uintptr_t world = 0;
    std::uintptr_t replay = 0;
    std::uintptr_t viewport = 0;
    std::uintptr_t camera = 0;
    std::uintptr_t player_info = 0;
    std::uintptr_t bone_list = 0;
    std::uintptr_t bone_matrix = 0;
    std::uintptr_t player_health = 0;
    std::uintptr_t player_position = 0;
    std::uintptr_t ped_visible_flag = 0;
    std::uintptr_t object_pool = 0;
    std::uintptr_t vehicle_pool = 0;
    std::uintptr_t ped_pool = 0;
    std::uintptr_t network_player_mgr = 0;
    std::uintptr_t blip_list = 0;
    std::uintptr_t waypoint = 0;
    std::uintptr_t aim_cped = 0;
} g_offsets;

bool LoadRuntimeOffsets() {

    auto result = OmniGhost::OffsetSource::LoadSnapshot(OmniGhost::OffsetSource::GameId::FiveM);
    if (!result.ok) {
        runtime.status = "Falha ao carregar offsets: " + result.detail;
        return false;
    }
    g_offset_snapshot = std::move(result.snapshot);



    g_offsets.world = GetOffset("builds.3258.world", GetOffset("_root.world"));
    g_offsets.replay = GetOffset("builds.3258.replay", GetOffset("_root.replay"));
    g_offsets.viewport = GetOffset("builds.3258.viewport", GetOffset("_root.viewport"));
    g_offsets.camera = GetOffset("builds.3258.camera", GetOffset("_root.camera"));


    g_offsets.player_info = GetOffset("CPed.player_info", GetOffset("_root.player_info"));
    g_offsets.bone_list = GetOffset("CPed.bone_list", GetOffset("_root.bone_list"));
    g_offsets.bone_matrix = GetOffset("CPed.bone_matrix", GetOffset("_root.bone_matrix"));
    g_offsets.player_health = GetOffset("CPed.health", GetOffset("_root.player_health"));
    g_offsets.player_position = GetOffset("CPed.position", GetOffset("_root.player_position"));
    g_offsets.ped_visible_flag = GetOffset("CPed.visible_flag", GetOffset("_root.ped_visible_flag"));


    g_offsets.object_pool = GetOffset("builds.3258.object_pool", GetOffset("_root.object_pool"));
    g_offsets.vehicle_pool = GetOffset("builds.3258.vehicle_pool", GetOffset("_root.vehicle_pool"));
    g_offsets.ped_pool = GetOffset("builds.3258.ped_pool", GetOffset("_root.ped_pool"));
    g_offsets.network_player_mgr = GetOffset("builds.3258.network_player_mgr", GetOffset("_root.network_player_mgr"));
    g_offsets.blip_list = GetOffset("builds.3258.blip_list", GetOffset("_root.blip_list"));
    g_offsets.waypoint = GetOffset("builds.3258.waypoint", GetOffset("_root.waypoint"));
    g_offsets.aim_cped = GetOffset("builds.3258.aim_cped", GetOffset("_root.aim_cped"));


    if (!g_offsets.world || !g_offsets.player_health || !g_offsets.player_position ||
        !g_offsets.bone_list || !g_offsets.bone_matrix) {
        runtime.status = "Offsets críticos ausentes no JSON";
        return false;
    }

    runtime.status = "Offsets carregados do JSON (" + result.storage + ") build " + g_offset_snapshot.metadata.build;
    return true;
}

bool TryPlayer(std::uintptr_t ped, Player& player) {
    float health = 0.f;
    Vec3 position{};
    std::uint8_t visible = 0;

    if (!ReadValue(ped + g_offsets.player_health, health) ||
        !ReadValue(ped + g_offsets.player_position, position) || !IsVector(position) ||
        !std::isfinite(health) || health <= 0.f || health > 1000.f)
        return false;


    if (g_offsets.ped_visible_flag) {
        ReadValue(ped + g_offsets.ped_visible_flag, visible);
    }

    player = {};
    player.address = ped;
    player.pos[0] = position.x;
    player.pos[1] = position.y;
    player.pos[2] = position.z;
    player.head[0] = position.x;
    player.head[1] = position.y + 1.72f;
    player.head[2] = position.z;
    player.health = health;
    player.alive = health > 0.01f;
    player.visible = visible & 0x4;
    return true;
}

bool ReadPedPool(std::vector<std::uintptr_t>& peds) {
    if (!g_offsets.ped_pool) return false;

    std::uintptr_t pool = 0;
    if (!ReadPointer(runtime.game_base + g_offsets.ped_pool, pool) || !IsPointer(pool))
        return false;

    std::int32_t count = 0;
    if (!ReadValue(pool + 0x10, count) || count <= 0 || count > static_cast<std::int32_t>(kMaxEntities))
        return false;

    std::uintptr_t list = 0;
    if (!ReadPointer(pool + 0x18, list) || !IsPointer(list))
        return false;

    peds.resize(static_cast<std::size_t>(count));
    if (!ReadExact(list, peds.data(), peds.size() * sizeof(std::uintptr_t))) {
        peds.clear();
        return false;
    }

    peds.erase(std::remove_if(peds.begin(), peds.end(),
        [](std::uintptr_t p) { return !IsPointer(p); }), peds.end());
    return !peds.empty();
}

bool ReadCollection(std::vector<std::uintptr_t>& peds) {
    return ReadPedPool(peds);
}

void ReadPlayers(const std::vector<std::uintptr_t>& raw) {
    runtime.players.clear();
    for (std::uintptr_t ped : raw) {
        Player player{};
        if (TryPlayer(ped, player)) {
            runtime.players.push_back(player);
        }
    }
    runtime.valid_player_count = static_cast<std::uint32_t>(runtime.players.size());
    runtime.transform_status = runtime.players.empty()
        ? "Sem peds validos" : "Posicoes Ped validadas";
}

bool ReadCamera() {
    if (!g_offsets.camera) {
        runtime.camera_status = "Camera offset nao disponivel";
        return false;
    }

    const std::uintptr_t camAddr = runtime.game_base + g_offsets.camera;
    std::uintptr_t camera = 0;
    if (!ReadPointer(camAddr, camera) || !IsPointer(camera)) {
        runtime.camera_status = "Camera RVA fora do modulo";
        return false;
    }


    float matrix[16]{};
    if (!ReadExact(camera + 0x10, matrix, sizeof(matrix))) {
        runtime.camera_status = "Falha ao ler matriz da camera";
        return false;
    }

    bool finite = true;
    for (float value : matrix) finite = finite && std::isfinite(value) && std::fabs(value) < 1000000.f;
    if (!finite) {
        runtime.camera_status = "Matriz invalida (NaN/Inf)";
        return false;
    }

    runtime.camera = camera;
    std::memcpy(runtime.view_matrix, matrix, sizeof(matrix));
    runtime.camera_status = "Matriz da camera validada";
    runtime.last_camera_sample_ms = GetTickCount64();
    return true;
}

bool HasOffsets() {
    return g_offsets.world && g_offsets.player_health && g_offsets.player_position &&
           g_offsets.bone_list && g_offsets.bone_matrix;
}

void Update(bool processProbe) {
    if (processProbe) {
        const auto diagnostics = mem.GetDiagnosticsSnapshot();
        runtime.device_ok = diagnostics.deviceOpen;
        runtime.process_ok = diagnostics.processInitialized && mem.GetPidFromName(kProcessName) != 0;
        runtime.modules_ok = runtime.game_base > kMinPointer;
    }
    runtime.offset_catalog_ok = HasOffsets();

    std::vector<std::uintptr_t> peds;
    runtime.entity_source_ok = runtime.modules_ok && ReadCollection(peds);
    if (runtime.entity_source_ok) ReadPlayers(peds); else runtime.players.clear();

    runtime.camera_ok = runtime.entity_source_ok && !runtime.players.empty() && ReadCamera();
    runtime.acquisition_ok = runtime.entity_source_ok && runtime.camera_ok;

    if (runtime.entity_source_ok) runtime.last_world_sample_ms = GetTickCount64();

    if (!runtime.device_ok) runtime.status = "DMA/FPGA indisponivel";
    else if (!runtime.process_ok) runtime.status = "GTAProcess.exe nao encontrado";
    else if (!runtime.modules_ok) runtime.status = "Modulo do jogo indisponivel";
    else if (!runtime.offset_catalog_ok) runtime.status = "Offsets FiveM build " + g_offset_snapshot.metadata.build + " invalidos";
    else if (!runtime.entity_source_ok) runtime.status = runtime.collection_status;
    else if (runtime.players.empty()) runtime.status = runtime.transform_status;
    else if (!runtime.camera_ok) runtime.status = runtime.camera_status;
    else runtime.status = "FiveM ESP ativa";
}

}

bool Attach() {
    Shutdown();
    runtime.status = "A ligar ao FiveM...";


    if (!LoadRuntimeOffsets()) {
        Update(true);
        return false;
    }

    if (!mem.Init(std::string(), true, false) && !mem.GetDiagnosticsSnapshot().deviceOpen) {
        Update(true); return false;
    }
    runtime.pid = mem.GetPidFromName(kProcessName);
    if (!runtime.pid || !mem.Init(kProcessName, false, false)) {
        Update(true); return false;
    }
    runtime.game_base = mem.GetBaseDaddy(kProcessName);
    Update(true);
    ready.store(runtime.process_ok && runtime.modules_ok && runtime.offset_catalog_ok,
                std::memory_order_release);
    return ready.load(std::memory_order_acquire);
}

void Shutdown() noexcept {
    ready.store(false, std::memory_order_release);
    runtime = Runtime{};
    g_offsets = {};
}

void Tick() {
    if (!ready.load(std::memory_order_acquire)) return;
    const std::uint64_t now = GetTickCount64();
    if (now - runtime.last_probe_ms >= 1000) {
        runtime.last_probe_ms = now; Update(true);
        if (!runtime.process_ok) ready.store(false, std::memory_order_release);
    } else if (now - runtime.last_acquisition_attempt_ms >= 16) {
        runtime.last_acquisition_attempt_ms = now; Update(false);
    }
    ++runtime.frames;
}

bool IsGameProcessAlive() { return mem.GetPidFromName(kProcessName) != 0; }
bool ValidateLiveOffsets() { Update(true); return runtime.modules_ok && runtime.offset_catalog_ok; }
const char* StatusLine() { return runtime.status.c_str(); }

}
