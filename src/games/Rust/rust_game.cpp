#include "rust_game.h"
#include "rust_esp.h"
#include "src/project/third_party/DMALibrary/Memory/Memory.h"
#include "src/platform/offset_source.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

extern Memory mem;

namespace Rust {
Config config{};
Runtime runtime{};
std::atomic_bool ready{false};

// Snapshot de offsets carregado do JSON
OmniGhost::EmbeddedOffsets::Snapshot g_offset_snapshot{};

namespace {
constexpr const char* kProcessName = "RustClient.exe";
constexpr std::uintptr_t kMinPointer = 0x10000;
constexpr std::uintptr_t kMaxPointer = 0x00007FFFFFFFFFFFull;
constexpr std::uint32_t kMaxEntities = 8192;
struct Vec3 { float x{}, y{}, z{}; };

bool IsPointer(std::uintptr_t value) { return value >= kMinPointer && value <= kMaxPointer && (value & 7u) == 0; }
bool IsVector(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
           std::fabs(v.x) < 100000.f && std::fabs(v.y) < 100000.f && std::fabs(v.z) < 100000.f;
}
bool ReadExact(std::uintptr_t address, void* out, std::size_t size) {
    if (!IsPointer(address) || !out || !size) return false;
    std::size_t read = 0;
    return mem.ReadWithCount(address, out, size, read) && read == size;
}
template<class T> bool ReadValue(std::uintptr_t address, T& out) {
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

// Helper para obter offset do snapshot
inline std::uintptr_t GetOffset(const char* path, std::uintptr_t fallback = 0) {
    std::uint64_t value = 0;
    if (g_offset_snapshot.TryGet(path, value) && value != 0)
        return static_cast<std::uintptr_t>(value);
    return fallback;
}

// Offsets resolvidos em runtime
struct RuntimeOffsets {
    std::uintptr_t baseNetworkableRva = 0;
    std::uintptr_t cameraRva = 0;
    std::uintptr_t baseNetworkable_staticFields = 0;
    std::uintptr_t baseNetworkable_clientEntities = 0;
    std::uintptr_t baseNetworkable_entityList = 0;
    std::uintptr_t baseNetworkable_objectDictionary = 0;
    std::uintptr_t baseNetworkable_entityContent = 0;
    std::uintptr_t baseNetworkable_entitySize = 0;
    std::uintptr_t baseNetworkable_bufferList = 0;
    std::uintptr_t baseNetworkable_prefabId = 0;
    std::uintptr_t baseCamera_staticFields = 0;
    std::uintptr_t baseCamera_wrapperClass = 0;
    std::uintptr_t baseCamera_parentStaticFields = 0;
    std::uintptr_t baseCamera_matrix = 0;
    std::uintptr_t baseCamera_viewMatrix = 0;
    std::uintptr_t baseCamera_projectionMatrix = 0;
    std::uintptr_t baseCamera_position = 0;
    std::uintptr_t baseCombatEntity_lifeState = 0;
    std::uintptr_t baseCombatEntity_model = 0;
    std::uintptr_t baseCombatEntity_health = 0;
    std::uintptr_t baseCombatEntity_maxHealth = 0;
    std::uintptr_t basePlayer_playerModel = 0;
    std::uintptr_t playerModel_position = 0;
    std::uintptr_t managedArray_length = 0;
    std::uintptr_t managedArray_data = 0;
} g_offsets;

bool LoadRuntimeOffsets() {
    // Carrega do JSON externo ou embedded
    auto result = OmniGhost::OffsetSource::LoadSnapshot(OmniGhost::OffsetSource::GameId::Rust);
    if (!result.ok) {
        runtime.status = "Falha ao carregar offsets: " + result.detail;
        return false;
    }
    g_offset_snapshot = std::move(result.snapshot);

    // Resolve offsets necessários
    g_offsets.baseNetworkableRva = GetOffset("_root.basenetworkable_pointer");
    g_offsets.cameraRva = GetOffset("_root.camera_pointer");
    g_offsets.baseNetworkable_staticFields = GetOffset("BaseNetworkable.static_fields");
    g_offsets.baseNetworkable_clientEntities = GetOffset("BaseNetworkable.client_entities");
    g_offsets.baseNetworkable_entityList = GetOffset("BaseNetworkable.entity_list");
    g_offsets.baseNetworkable_objectDictionary = GetOffset("BaseNetworkable.object_dictionary");
    g_offsets.baseNetworkable_entityContent = GetOffset("BaseNetworkable.entity_content");
    g_offsets.baseNetworkable_entitySize = GetOffset("BaseNetworkable.entity_size");
    g_offsets.baseNetworkable_bufferList = GetOffset("BaseNetworkable.buffer_list");
    g_offsets.baseNetworkable_prefabId = GetOffset("BaseNetworkable.prefabID");
    g_offsets.baseCamera_staticFields = GetOffset("BaseCamera.static_fields");
    g_offsets.baseCamera_wrapperClass = GetOffset("BaseCamera.wrapper_class");
    g_offsets.baseCamera_parentStaticFields = GetOffset("BaseCamera.parent_static_fields");
    g_offsets.baseCamera_matrix = GetOffset("BaseCamera.matrix");
    g_offsets.baseCamera_viewMatrix = GetOffset("BaseCamera.viewMatrix");
    g_offsets.baseCamera_projectionMatrix = GetOffset("BaseCamera.projMatrix");
    g_offsets.baseCamera_position = GetOffset("BaseCamera.position");
    g_offsets.baseCombatEntity_lifeState = GetOffset("BaseCombatEntity.lifestate");
    g_offsets.baseCombatEntity_model = GetOffset("BaseCombatEntity.model");
    g_offsets.baseCombatEntity_health = GetOffset("BaseCombatEntity._health");
    g_offsets.baseCombatEntity_maxHealth = GetOffset("BaseCombatEntity._maxHealth");
    g_offsets.basePlayer_playerModel = GetOffset("BasePlayer.playerModel");
    g_offsets.playerModel_position = GetOffset("PlayerModel.position");
    g_offsets.managedArray_length = GetOffset("ManagedArray.length", 0x18);
    g_offsets.managedArray_data = GetOffset("ManagedArray.data", 0x20);

    // Valida offsets críticos
    if (!g_offsets.baseNetworkableRva || !g_offsets.cameraRva ||
        !g_offsets.baseNetworkable_clientEntities || !g_offsets.basePlayer_playerModel ||
        !g_offsets.playerModel_position || !g_offsets.baseCamera_matrix) {
        runtime.status = "Offsets críticos ausentes no JSON";
        return false;
    }

    runtime.status = "Offsets carregados do JSON (" + result.storage + ") build " + g_offset_snapshot.metadata.build;
    return true;
}

bool TryManagedBuffer(std::uintptr_t node, std::vector<std::uintptr_t>& entities) {
    std::int32_t count = 0;
    std::uintptr_t array = 0;
    if (!ReadValue(node + g_offsets.baseNetworkable_entitySize, count) || count <= 0 ||
        count > static_cast<std::int32_t>(kMaxEntities) ||
        !ReadPointer(node + g_offsets.baseNetworkable_bufferList, array))
        return false;

    std::uint64_t capacity = 0;
    if (!ReadValue(array + g_offsets.managedArray_length, capacity) ||
        capacity < static_cast<std::uint64_t>(count) || capacity > kMaxEntities)
        return false;

    entities.resize(static_cast<std::size_t>(count));
    if (!ReadExact(array + g_offsets.managedArray_data, entities.data(), entities.size() * sizeof(std::uintptr_t))) {
        entities.clear();
        return false;
    }
    entities.erase(std::remove_if(entities.begin(), entities.end(),
        [](std::uintptr_t p) { return !IsPointer(p); }), entities.end());
    return !entities.empty();
}

bool ReadCollection(std::vector<std::uintptr_t>& entities) {
    const std::uintptr_t rootAddress = runtime.game_assembly + g_offsets.baseNetworkableRva;
    std::vector<std::uintptr_t> frontier;
    AddUnique(frontier, rootAddress);
    std::uintptr_t root = 0;
    if (ReadPointer(rootAddress, root)) AddUnique(frontier, root);
    if (frontier.empty()) {
        runtime.collection_status = "BaseNetworkable RVA fora do modulo";
        return false;
    }
    for (int depth = 0; depth < 7; ++depth) {
        const auto current = frontier;
        for (std::uintptr_t node : current) {
            if (TryManagedBuffer(node, entities)) {
                runtime.entity_list = node;
                runtime.entity_count = static_cast<std::uint32_t>(entities.size());
                runtime.collection_status = "Colecao validada";
                return true;
            }
            AddPointerAt(frontier, node, g_offsets.baseNetworkable_staticFields);
            AddPointerAt(frontier, node, g_offsets.baseNetworkable_clientEntities);
            AddPointerAt(frontier, node, g_offsets.baseNetworkable_entityList);
            AddPointerAt(frontier, node, g_offsets.baseNetworkable_objectDictionary);
            AddPointerAt(frontier, node, g_offsets.baseNetworkable_entityContent);
            AddPointerAt(frontier, node, g_offsets.baseNetworkable_bufferList);
        }
    }
    runtime.collection_status = "Cadeia de entidades nao validada";
    return false;
}

bool TryPlayer(std::uintptr_t candidate, Player& player) {
    std::uintptr_t model = 0;
    Vec3 position{};
    float health = 0.f;
    std::uint8_t life = 0xff;
    if (!ReadPointer(candidate + g_offsets.basePlayer_playerModel, model) ||
        !ReadValue(model + g_offsets.playerModel_position, position) || !IsVector(position) ||
        !ReadValue(candidate + g_offsets.baseCombatEntity_health, health) ||
        !ReadValue(candidate + g_offsets.baseCombatEntity_lifeState, life) ||
        !std::isfinite(health) || health < 0.f || health > 1000.f || life > 4)
        return false;

    player = {};
    player.address = candidate;
    player.feet[0] = position.x;
    player.feet[1] = position.y;
    player.feet[2] = position.z;
    player.head[0] = position.x;
    player.head[1] = position.y + 1.72f;
    player.head[2] = position.z;
    player.health = health;
    player.alive = health > 0.01f && life == 0;
    return true;
}

void ReadPlayers(const std::vector<std::uintptr_t>& raw) {
    runtime.players.clear();
    for (std::uintptr_t entry : raw) {
        std::vector<std::uintptr_t> candidates{entry};
        AddPointerAt(candidates, entry, 0x10);
        AddPointerAt(candidates, entry, 0x30);
        for (std::size_t i = 0; i < candidates.size() && i < 8; ++i) {
            AddPointerAt(candidates, candidates[i], 0x10);
            AddPointerAt(candidates, candidates[i], 0x30);
        }
        Player player{};
        bool valid = false;
        for (std::uintptr_t candidate : candidates) {
            if (TryPlayer(candidate, player)) { valid = true; break; }
        }
        if (!valid) continue;
        if (std::none_of(runtime.players.begin(), runtime.players.end(),
            [&](const Player& p) { return p.address == player.address; }))
            runtime.players.push_back(player);
    }
    runtime.valid_player_count = static_cast<std::uint32_t>(runtime.players.size());
    runtime.transform_status = runtime.players.empty()
        ? "Sem jogadores validos" : "Posicoes PlayerModel validadas";
}

int MatrixScore(const float* m) {
    int score = 0;
    for (const Player& p : runtime.players) {
        if (!p.alive) continue;
        const float* v = p.head;
        const float w = m[3]*v[0] + m[7]*v[1] + m[11]*v[2] + m[15];
        if (!std::isfinite(w) || std::fabs(w) < .001f) continue;
        const float x = (m[0]*v[0] + m[4]*v[1] + m[8]*v[2] + m[12]) / w;
        const float y = (m[1]*v[0] + m[5]*v[1] + m[9]*v[2] + m[13]) / w;
        if (std::isfinite(x) && std::isfinite(y) && std::fabs(x) < 1.25f && std::fabs(y) < 1.25f)
            ++score;
    }
    return score;
}

bool ReadCamera() {
    const std::uintptr_t rootAddress = runtime.game_assembly + g_offsets.cameraRva;
    std::vector<std::uintptr_t> candidates;
    AddUnique(candidates, rootAddress);
    std::uintptr_t root = 0;
    if (ReadPointer(rootAddress, root)) AddUnique(candidates, root);
    if (candidates.empty()) {
        runtime.camera_status = "Camera RVA fora do modulo";
        return false;
    }
    for (int depth = 0; depth < 5; ++depth) {
        const auto current = candidates;
        for (std::uintptr_t node : current) {
            AddPointerAt(candidates, node, g_offsets.baseCamera_staticFields);
            AddPointerAt(candidates, node, g_offsets.baseCamera_wrapperClass);
            AddPointerAt(candidates, node, g_offsets.baseCamera_parentStaticFields);
            AddPointerAt(candidates, node, 0x10);
        }
    }
    int bestScore = 0;
    std::uintptr_t bestOwner = 0;
    std::uintptr_t bestOffset = 0;
    float best[16]{};
    const std::array<std::uintptr_t, 3> offsets{
        g_offsets.baseCamera_matrix, g_offsets.baseCamera_viewMatrix, g_offsets.baseCamera_projectionMatrix
    };
    for (std::uintptr_t owner : candidates) {
        for (std::uintptr_t offset : offsets) {
            float matrix[16]{};
            if (!ReadExact(owner + offset, matrix, sizeof(matrix))) continue;
            bool finite = true;
            for (float value : matrix) finite = finite && std::isfinite(value) && std::fabs(value) < 1000000.f;
            if (!finite) continue;
            const int score = MatrixScore(matrix);
            if (score > bestScore) {
                bestScore = score; bestOwner = owner; bestOffset = offset;
                std::memcpy(best, matrix, sizeof(best));
            }
        }
    }
    if (!bestScore) {
        runtime.camera_status = "Matriz nao validada por projecao";
        return false;
    }
    runtime.camera = bestOwner;
    runtime.camera_matrix_offset = bestOffset;
    std::memcpy(runtime.view_matrix, best, sizeof(best));
    runtime.camera_status = "Matriz validada por projecao";
    runtime.last_camera_sample_ms = GetTickCount64();
    return true;
}

bool HasOffsets() {
    return g_offsets.baseNetworkableRva && g_offsets.cameraRva && g_offsets.baseNetworkable_clientEntities &&
           g_offsets.basePlayer_playerModel && g_offsets.playerModel_position && g_offsets.baseCamera_matrix;
}

void Update(bool processProbe) {
    if (processProbe) {
        const auto diagnostics = mem.GetDiagnosticsSnapshot();
        runtime.device_ok = diagnostics.deviceOpen;
        runtime.process_ok = diagnostics.processInitialized && mem.GetPidFromName(kProcessName) != 0;
        runtime.modules_ok = runtime.game_assembly > kMinPointer && runtime.unity_player > kMinPointer;
    }
    runtime.offset_catalog_ok = HasOffsets();
    std::vector<std::uintptr_t> entities;
    runtime.entity_source_ok = runtime.modules_ok && ReadCollection(entities);
    if (runtime.entity_source_ok) ReadPlayers(entities); else runtime.players.clear();
    runtime.camera_ok = runtime.entity_source_ok && !runtime.players.empty() && ReadCamera();
    runtime.acquisition_ok = runtime.entity_source_ok && runtime.camera_ok;
    if (runtime.entity_source_ok) runtime.last_world_sample_ms = GetTickCount64();

    if (!runtime.device_ok) runtime.status = "DMA/FPGA indisponivel";
    else if (!runtime.process_ok) runtime.status = "RustClient.exe nao encontrado";
    else if (!runtime.modules_ok) runtime.status = "GameAssembly.dll/UnityPlayer.dll indisponiveis";
    else if (!runtime.offset_catalog_ok) runtime.status = "Offsets Rust build 25231061 invalidos";
    else if (!runtime.entity_source_ok) runtime.status = runtime.collection_status;
    else if (runtime.players.empty()) runtime.status = runtime.transform_status;
    else if (!runtime.camera_ok) runtime.status = runtime.camera_status;
    else runtime.status = "Rust Box 2D ativa";
}

} // namespace

bool Attach() {
    Shutdown();
    runtime.status = "A ligar ao Rust...";

    // Carrega offsets do JSON
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
    runtime.game_assembly = mem.GetBaseDaddy("GameAssembly.dll");
    runtime.unity_player = mem.GetBaseDaddy("UnityPlayer.dll");
    Update(true);
    ready.store(runtime.process_ok && runtime.modules_ok && runtime.offset_catalog_ok,
                std::memory_order_release);
    return ready.load(std::memory_order_acquire);
}

void Shutdown() noexcept { ready.store(false, std::memory_order_release); runtime = Runtime{}; g_offsets = {}; }

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
    ESP::Draw(runtime, config);
}

bool IsGameProcessAlive() { return mem.GetPidFromName(kProcessName) != 0; }
bool ValidateLiveOffsets() { Update(true); return runtime.modules_ok && runtime.offset_catalog_ok; }
const char* StatusLine() { return runtime.status.c_str(); }

}
