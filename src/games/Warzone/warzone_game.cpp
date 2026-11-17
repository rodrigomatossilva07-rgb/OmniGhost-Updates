#include "warzone_game.h"
#include "platform/offset_source.h"
#include "src/project/third_party/DMALibrary/Memory/Memory.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

extern Memory mem;

namespace Warzone {

Config config{};
Runtime runtime{};
std::atomic_bool ready{false};

// Snapshot de offsets carregado do JSON
OmniGhost::EmbeddedOffsets::Snapshot g_offset_snapshot{};

namespace {
constexpr const char* kProcessName = "cod.exe";
constexpr std::uintptr_t kMinPointer = 0x10000;
constexpr std::uintptr_t kMaxPointer = 0x00007FFFFFFFFFFFull;
constexpr std::uint32_t kMaxEntities = 8192;
constexpr std::uint32_t kPlayerSize = 0x2838;

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

// Helper para obter offset do snapshot
inline std::uintptr_t GetOffset(const char* path, std::uintptr_t fallback = 0) {
    std::uint64_t value = 0;
    if (g_offset_snapshot.TryGet(path, value) && value != 0)
        return static_cast<std::uintptr_t>(value);
    return fallback;
}

// Offsets resolvidos em runtime
struct RuntimeOffsets {
    std::uintptr_t ref_def = 0;
    std::uintptr_t name_array = 0;
    std::uintptr_t camera_base = 0;
    std::uintptr_t view_matrix = 0;
    std::uintptr_t entity_list = 0;
    std::uintptr_t max_player_count = 0;
    std::uintptr_t game_mode = 0;

    // Decrypt RVAs
    std::uintptr_t client_info_enc = 0;
    std::uintptr_t client_info_key = 0;
    std::uintptr_t client_base_enc = 0;
    std::uintptr_t client_base_key = 0;
    std::uintptr_t bone_enc = 0;
    std::uintptr_t bone_key = 0;
    std::uintptr_t stub_client_info = 0;
    std::uintptr_t stub_client_base = 0;
    std::uintptr_t stub_bone_base = 0;

    // Camera offsets
    std::uintptr_t cam_pos = 0;
    std::uintptr_t cam_pos_enc = 0;
    std::uintptr_t cam_pos_key = 0;
    std::uintptr_t cam_angles_a = 0;
    std::uintptr_t cam_angles_b = 0;
    std::uintptr_t cam_angles_c = 0;

    // Local player
    std::uintptr_t local_index = 0;
    std::uintptr_t local_index_pos = 0;
    std::uintptr_t visible_client_bits = 0;
    std::uintptr_t recoil = 0;
    std::uintptr_t scoreboard = 0;
    std::uintptr_t scoreboard_size = 0;

    // Player struct
    std::uintptr_t player_size = 0;
    std::uintptr_t player_valid = 0;
    std::uintptr_t player_pos = 0;
    std::uintptr_t player_pos_data = 0;
    std::uintptr_t player_team = 0;
    std::uintptr_t player_stance = 0;
    std::uintptr_t player_weapon_index = 0;
    std::uintptr_t player_weapon_obj = 0;
    std::uintptr_t player_usercmd = 0;
    std::uintptr_t player_health = 0;
    std::uintptr_t player_armor = 0;

    // Bone
    std::uintptr_t bone_base_pos = 0;
    std::uintptr_t bone_size = 0;
    std::uintptr_t bone_offset = 0;
    int bone_head = 7;
    int bone_neck = 6;
    int bone_chest = 5;
    int bone_pelvis = 2;
} g_offsets;

bool LoadRuntimeOffsets() {
    auto result = OmniGhost::OffsetSource::LoadSnapshot(OmniGhost::OffsetSource::GameId::Warzone);
    if (!result.ok) {
        runtime.status = "Falha ao carregar offsets: " + result.detail;
        return false;
    }
    g_offset_snapshot = std::move(result.snapshot);

    // Root offsets
    g_offsets.ref_def = GetOffset("_root.ref_def");
    g_offsets.name_array = GetOffset("_root.name_array");
    g_offsets.camera_base = GetOffset("_root.camera_base");
    g_offsets.view_matrix = GetOffset("_root.view_matrix");
    g_offsets.entity_list = GetOffset("_root.entity_list");
    g_offsets.max_player_count = GetOffset("_root.max_player_count");
    g_offsets.game_mode = GetOffset("_root.game_mode");

    // Decrypt RVAs
    g_offsets.client_info_enc = GetOffset("decrypt_rva.client_info_enc");
    g_offsets.client_info_key = GetOffset("decrypt_rva.client_info_key");
    g_offsets.client_base_enc = GetOffset("decrypt_rva.client_base_enc");
    g_offsets.client_base_key = GetOffset("decrypt_rva.client_base_key");
    g_offsets.bone_enc = GetOffset("decrypt_rva.bone_enc");
    g_offsets.bone_key = GetOffset("decrypt_rva.bone_key");
    g_offsets.stub_client_info = GetOffset("decrypt_rva.stub_client_info");
    g_offsets.stub_client_base = GetOffset("decrypt_rva.stub_client_base");
    g_offsets.stub_bone_base = GetOffset("decrypt_rva.stub_bone_base");

    // Camera
    g_offsets.cam_pos = GetOffset("camera.pos");
    g_offsets.cam_pos_enc = GetOffset("camera.pos_enc");
    g_offsets.cam_pos_key = GetOffset("camera.pos_key");
    g_offsets.cam_angles_a = GetOffset("camera.angles_a");
    g_offsets.cam_angles_b = GetOffset("camera.angles_b");
    g_offsets.cam_angles_c = GetOffset("camera.angles_c");

    // Local
    g_offsets.local_index = GetOffset("local.index");
    g_offsets.local_index_pos = GetOffset("local.index_pos");
    g_offsets.visible_client_bits = GetOffset("local.visible_client_bits");
    g_offsets.recoil = GetOffset("local.recoil");
    g_offsets.scoreboard = GetOffset("local.scoreboard");
    g_offsets.scoreboard_size = GetOffset("local.scoreboard_size");

    // Player
    g_offsets.player_size = GetOffset("player.size");
    g_offsets.player_valid = GetOffset("player.valid");
    g_offsets.player_pos = GetOffset("player.pos");
    g_offsets.player_pos_data = GetOffset("player.pos_data");
    g_offsets.player_team = GetOffset("player.team");
    g_offsets.player_stance = GetOffset("player.stance");
    g_offsets.player_weapon_index = GetOffset("player.weapon_index");
    g_offsets.player_weapon_obj = GetOffset("player.weapon_obj");
    g_offsets.player_usercmd = GetOffset("player.usercmd");
    g_offsets.player_health = GetOffset("player.health");
    g_offsets.player_armor = GetOffset("player.armor");

    // Bone
    g_offsets.bone_base_pos = GetOffset("bone.base_pos");
    g_offsets.bone_size = GetOffset("bone.size");
    g_offsets.bone_offset = GetOffset("bone.offset");
    g_offsets.bone_head = GetOffset("bone.head", 7);
    g_offsets.bone_neck = GetOffset("bone.neck", 6);
    g_offsets.bone_chest = GetOffset("bone.chest", 5);
    g_offsets.bone_pelvis = GetOffset("bone.pelvis", 2);

    // Valida offsets críticos
    if (!g_offsets.ref_def || !g_offsets.entity_list || !g_offsets.camera_base ||
        !g_offsets.view_matrix || !g_offsets.player_size || !g_offsets.player_pos) {
        runtime.status = "Offsets críticos ausentes no JSON";
        return false;
    }

    runtime.status = "Offsets carregados do JSON (" + result.storage + ") build " + g_offset_snapshot.metadata.build;
    return true;
}

// Warzone usa XOR decryption para client info
inline std::uint64_t DecryptClientInfo(std::uint64_t enc, std::uint64_t key) {
    return enc ^ key;
}

inline std::uint64_t DecryptBone(std::uint64_t enc, std::uint64_t key) {
    return enc ^ key;
}

bool TryPlayer(std::uintptr_t entity, Player& player) {
    // Validação básica
    if (!entity) return false;

    // Lê player struct
    std::uint8_t valid = 0;
    if (!ReadValue(entity + g_offsets.player_valid, valid) || !valid)
        return false;

    // Posição
    std::uintptr_t posData = 0;
    if (!ReadValue(entity + g_offsets.player_pos_data, posData) || !posData)
        return false;

    Vec3 position{};
    if (!ReadValue(posData + g_offsets.player_pos, position) || !IsVector(position))
        return false;

    // Health/Armor
    float health = 0.f, armor = 0.f;
    if (!ReadValue(entity + g_offsets.player_health, health) ||
        !ReadValue(entity + g_offsets.player_armor, armor) ||
        health <= 0.f || health > 1000.f)
        return false;

    // Team
    std::int16_t team = 0;
    ReadValue(entity + g_offsets.player_team, team);

    // Weapon
    std::int16_t weaponIdx = 0;
    ReadValue(entity + g_offsets.player_weapon_index, weaponIdx);

    // Stance
    std::uint8_t stance = 0;
    ReadValue(entity + g_offsets.player_stance, stance);

    player = {};
    player.address = entity;
    player.pos[0] = position.x;
    player.pos[1] = position.y;
    player.pos[2] = position.z;
    player.head[0] = position.x;
    player.head[1] = position.y + 1.72f;
    player.head[2] = position.z;
    player.health = health;
    player.armor = armor;
    player.alive = health > 0.01f;
    player.team = team;
    player.stance = stance;
    player.weapon = weaponIdx;
    return true;
}

bool ReadEntityList(std::vector<std::uintptr_t>& entities) {
    if (!g_offsets.entity_list) return false;

    std::uintptr_t list = 0;
    if (!ReadPointer(runtime.game_base + g_offsets.entity_list, list) || !IsPointer(list))
        return false;

    std::int32_t maxPlayers = 150;
    ReadValue(runtime.game_base + g_offsets.max_player_count, maxPlayers);
    if (maxPlayers <= 0 || maxPlayers > 200) maxPlayers = 150;

    entities.clear();
    entities.reserve(maxPlayers);

    for (int i = 0; i < maxPlayers; ++i) {
        std::uintptr_t entity = 0;
        if (!ReadValue(list + static_cast<std::uintptr_t>(i) * 8, entity))
            continue;
        if (IsPointer(entity))
            entities.push_back(entity);
    }
    return !entities.empty();
}

bool ReadCollection(std::vector<std::uintptr_t>& entities) {
    return ReadEntityList(entities);
}

void ReadPlayers(const std::vector<std::uintptr_t>& raw) {
    runtime.players.clear();
    for (std::uintptr_t entity : raw) {
        Player player{};
        if (TryPlayer(entity, player)) {
            runtime.players.push_back(player);
        }
    }
    runtime.valid_player_count = static_cast<std::uint32_t>(runtime.players.size());
    runtime.transform_status = runtime.players.empty()
        ? "Sem jogadores validos" : "Posicoes validadas";
}

// Warzone camera decryption helper
bool DecryptCamera(float* outPos, float* outRot) {
    if (!g_offsets.camera_base) return false;

    std::uintptr_t camBase = 0;
    if (!ReadPointer(runtime.game_base + g_offsets.camera_base, camBase) || !camBase)
        return false;

    // Posição (pode ser encrypted)
    std::uint64_t posEnc = 0;
    std::uint64_t posKey = 0;
    if (!ReadValue(camBase + g_offsets.cam_pos_enc, posEnc) ||
        !ReadValue(camBase + g_offsets.cam_pos_key, posKey))
        return false;

    std::uint64_t posDec = posEnc ^ posKey;
    float pos[3];
    if (!ReadExact(reinterpret_cast<std::uintptr_t>(&posDec), pos, sizeof(pos)))
        return false;

    // Ângulos
    float angles[3]{};
    if (!ReadValue(camBase + g_offsets.cam_angles_a, angles[0]) ||
        !ReadValue(camBase + g_offsets.cam_angles_b, angles[1]) ||
        !ReadValue(camBase + g_offsets.cam_angles_c, angles[2]))
        return false;

    if (outPos) std::memcpy(outPos, pos, sizeof(pos));
    if (outRot) std::memcpy(outRot, angles, sizeof(angles));
    return true;
}

bool ReadCamera() {
    float pos[3]{}, rot[3]{};
    if (!DecryptCamera(pos, rot)) {
        runtime.camera_status = "Falha ao descriptografar camera";
        return false;
    }

    // View matrix
    float matrix[16]{};
    if (!ReadExact(runtime.game_base + g_offsets.view_matrix, matrix, sizeof(matrix))) {
        runtime.camera_status = "Falha ao ler view matrix";
        return false;
    }

    bool finite = true;
    for (float v : matrix) finite = finite && std::isfinite(v) && std::fabs(v) < 1000000.f;
    if (!finite) {
        runtime.camera_status = "View matrix invalida";
        return false;
    }

    std::memcpy(runtime.view_matrix, matrix, sizeof(matrix));
    runtime.camera_status = "Camera validada (descriptografada)";
    runtime.last_camera_sample_ms = GetTickCount64();
    return true;
}

bool HasOffsets() {
    return g_offsets.ref_def && g_offsets.entity_list && g_offsets.camera_base &&
           g_offsets.view_matrix && g_offsets.player_size && g_offsets.player_pos;
}

void Update(bool processProbe) {
    if (processProbe) {
        const auto diagnostics = mem.GetDiagnosticsSnapshot();
        runtime.device_ok = diagnostics.deviceOpen;
        runtime.process_ok = diagnostics.processInitialized && mem.GetPidFromName(kProcessName) != 0;
        runtime.modules_ok = runtime.game_base > kMinPointer;
    }
    runtime.offset_catalog_ok = HasOffsets();

    std::vector<std::uintptr_t> entities;
    runtime.entity_source_ok = runtime.modules_ok && ReadCollection(entities);
    if (runtime.entity_source_ok) ReadPlayers(entities); else runtime.players.clear();

    runtime.camera_ok = runtime.entity_source_ok && !runtime.players.empty() && ReadCamera();
    runtime.acquisition_ok = runtime.entity_source_ok && runtime.camera_ok;

    if (runtime.entity_source_ok) runtime.last_world_sample_ms = GetTickCount64();

    if (!runtime.device_ok) runtime.status = "DMA/FPGA indisponivel";
    else if (!runtime.process_ok) runtime.status = "cod.exe nao encontrado";
    else if (!runtime.modules_ok) runtime.status = "Modulo do jogo indisponivel";
    else if (!runtime.offset_catalog_ok) runtime.status = "Offsets Warzone build " + g_offset_snapshot.metadata.build + " invalidos";
    else if (!runtime.entity_source_ok) runtime.status = runtime.collection_status;
    else if (runtime.players.empty()) runtime.status = runtime.transform_status;
    else if (!runtime.camera_ok) runtime.status = runtime.camera_status;
    else runtime.status = "Warzone ESP ativa";
}

bool Attach() {
    Shutdown();
    runtime.status = "A ligar ao Warzone...";

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

} // namespace Warzone