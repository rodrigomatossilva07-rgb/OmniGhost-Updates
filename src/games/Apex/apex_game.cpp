#include "apex_game.h"
#include "platform/offset_source.h"
#include "src/project/third_party/DMALibrary/Memory/Memory.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

extern Memory mem;

namespace Apex {

Config config{};
Runtime runtime{};
std::atomic_bool ready{false};

// Snapshot de offsets carregado do JSON
OmniGhost::EmbeddedOffsets::Snapshot g_offset_snapshot{};

namespace {
constexpr const char* kProcessName = "r5apex.exe";
constexpr std::uintptr_t kMinPointer = 0x10000;
constexpr std::uintptr_t kMaxPointer = 0x00007FFFFFFFFFFFull;
constexpr std::uint32_t kMaxEntities = 8192;

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
    // Globals
    std::uintptr_t client_dll_base = 0;
    std::uintptr_t engine_dll_base = 0;
    std::uintptr_t entity_list = 0;
    std::uintptr_t local_player = 0;
    std::uintptr_t view_render = 0;
    std::uintptr_t client_state = 0;
    std::uintptr_t view_matrix = 0;
    std::uintptr_t camera_origin = 0;
    std::uintptr_t view_angles = 0;
    std::uintptr_t name_list = 0;
    std::uintptr_t weapon_names = 0;
    std::uintptr_t input_system = 0;
    std::uintptr_t net_channel = 0;
    std::uintptr_t signon_state = 0;
    std::uintptr_t level_name = 0;

    // Base Entity
    std::uintptr_t m_iEFlags = 0;
    std::uintptr_t m_fFlags = 0;
    std::uintptr_t m_pMoveParent = 0;
    std::uintptr_t m_vecAngVelocity = 0;
    std::uintptr_t m_angAbsRotation = 0;
    std::uintptr_t m_vecAbsVelocity = 0;
    std::uintptr_t m_vecAbsOrigin = 0;
    std::uintptr_t m_localOrigin = 0;
    std::uintptr_t m_localAngles = 0;
    std::uintptr_t m_vecPrevAbsOrigin = 0;
    std::uintptr_t m_flGravity = 0;
    std::uintptr_t m_hGroundEntity = 0;
    std::uintptr_t m_iHealth = 0;
    std::uintptr_t m_flMaxspeed = 0;
    std::uintptr_t m_visibilityFlags = 0;
    std::uintptr_t m_fEffects = 0;
    std::uintptr_t m_iTeamNum = 0;
    std::uintptr_t m_deathVelocity = 0;
    std::uintptr_t m_vecVelocity = 0;
    std::uintptr_t m_flFriction = 0;
    std::uintptr_t m_hOwnerEntity = 0;
    std::uintptr_t m_nRenderMode = 0;
    std::uintptr_t m_MoveType = 0;
    std::uintptr_t m_Collision = 0;
    std::uintptr_t m_CollisionGroup = 0;
    std::uintptr_t m_nNextThinkTick = 0;
    std::uintptr_t m_iMaxHealth = 0;
    std::uintptr_t m_iSignifierName = 0;
    std::uintptr_t m_scriptNameIndex = 0;
    std::uintptr_t m_lifeState = 0;
    std::uintptr_t m_rgflCoordinateFrame = 0;
    std::uintptr_t m_nModelIndex = 0;
    std::uintptr_t m_vecMins = 0;
    std::uintptr_t m_vecMaxs = 0;
    std::uintptr_t m_usSolidFlags = 0;
    std::uintptr_t m_nSolidType = 0;
    std::uintptr_t m_triggerBloat = 0;
    std::uintptr_t m_collisionDetailLevel = 0;
    std::uintptr_t m_vecViewOffset_x = 0;
    std::uintptr_t m_vecViewOffset_y = 0;
    std::uintptr_t m_vecViewOffset_z = 0;
    std::uintptr_t m_shieldHealth = 0;
    std::uintptr_t m_shieldHealthMax = 0;
    std::uintptr_t m_networkedFlags = 0;
    std::uintptr_t m_meleeResponseFlags = 0;
    std::uintptr_t m_teamMemberIndex = 0;
    std::uintptr_t m_squadID = 0;
    std::uintptr_t m_minimapData = 0;
    std::uintptr_t m_nameVisibilityFlags = 0;
    std::uintptr_t m_bIsPlayerOverheating = 0;
    std::uintptr_t m_lastFiredTime = 0;
    std::uintptr_t m_lastFiredWeapon = 0;
    std::uintptr_t m_selectedWeapons = 0;
    std::uintptr_t m_latestPrimaryWeapons = 0;
    std::uintptr_t m_latestNonOffhandWeapons = 0;
    std::uintptr_t m_selectedOffhands = 0;
    std::uintptr_t m_weaponPermission = 0;
    std::uintptr_t m_weaponDisabledFlags = 0;
    std::uintptr_t m_phaseShiftType = 0;
    std::uintptr_t m_phaseShiftTimeStart = 0;
    std::uintptr_t m_phaseShiftTimeEnd = 0;
    std::uintptr_t m_targetInfoPingValue = 0;
    std::uintptr_t m_inventory = 0;
    std::uintptr_t m_Local = 0;
    std::uintptr_t m_bZooming = 0;
    std::uintptr_t m_zoomToggleOnStartTime = 0;
    std::uintptr_t m_zoomBaseFrac = 0;
    std::uintptr_t m_zoomBaseTime = 0;
    std::uintptr_t m_hActiveWeapon = 0;
    std::uintptr_t m_iClip1 = 0;
    std::uintptr_t m_iPrimaryReserveAmmo = 0;
    std::uintptr_t m_iItemDefinitionIndex = 0;
} g_offsets;

bool LoadRuntimeOffsets() {
    auto result = OmniGhost::OffsetSource::LoadSnapshot(OmniGhost::OffsetSource::GameId::Apex);
    if (!result.ok) {
        runtime.status = "Falha ao carregar offsets: " + result.detail;
        return false;
    }
    g_offset_snapshot = std::move(result.snapshot);

    // Globals
    g_offsets.entity_list = GetOffset("_root.EntityList");
    g_offsets.local_player = GetOffset("_root.LocalPlayer");
    g_offsets.view_render = GetOffset("_root.ViewRender");
    g_offsets.client_state = GetOffset("_root.ClientState");
    g_offsets.view_matrix = GetOffset("_root.view_matrix", GetOffset("camera.view_matrix"));
    g_offsets.camera_origin = GetOffset("_root.camera_origin", GetOffset("camera.camera_origin"));
    g_offsets.view_angles = GetOffset("_root.view_angles", GetOffset("camera.view_angles"));
    g_offsets.name_list = GetOffset("_root.NameList");
    g_offsets.weapon_names = GetOffset("_root.WeaponNames");
    g_offsets.input_system = GetOffset("_root.InputSystem");
    g_offsets.net_channel = GetOffset("_root.NetChannel");
    g_offsets.signon_state = GetOffset("_root.SignonState");
    g_offsets.level_name = GetOffset("_root.LevelName");
    g_offsets.client_dll_base = GetOffset("_root.CHLClient");
    g_offsets.engine_dll_base = GetOffset("_root.ClientState");

    // Base Entity fields
    g_offsets.m_iEFlags = GetOffset("base_entity.m_iEFlags");
    g_offsets.m_fFlags = GetOffset("base_entity.m_fFlags");
    g_offsets.m_pMoveParent = GetOffset("base_entity.m_pMoveParent");
    g_offsets.m_vecAngVelocity = GetOffset("base_entity.m_vecAngVelocity");
    g_offsets.m_angAbsRotation = GetOffset("base_entity.m_angAbsRotation");
    g_offsets.m_vecAbsVelocity = GetOffset("base_entity.m_vecAbsVelocity");
    g_offsets.m_vecAbsOrigin = GetOffset("base_entity.m_vecAbsOrigin");
    g_offsets.m_localOrigin = GetOffset("base_entity.m_localOrigin");
    g_offsets.m_localAngles = GetOffset("base_entity.m_localAngles");
    g_offsets.m_vecPrevAbsOrigin = GetOffset("base_entity.m_vecPrevAbsOrigin");
    g_offsets.m_flGravity = GetOffset("base_entity.m_flGravity");
    g_offsets.m_hGroundEntity = GetOffset("base_entity.m_hGroundEntity");
    g_offsets.m_iHealth = GetOffset("base_entity.m_iHealth");
    g_offsets.m_flMaxspeed = GetOffset("base_entity.m_flMaxspeed");
    g_offsets.m_visibilityFlags = GetOffset("base_entity.m_visibilityFlags");
    g_offsets.m_fEffects = GetOffset("base_entity.m_fEffects");
    g_offsets.m_iTeamNum = GetOffset("base_entity.m_iTeamNum");
    g_offsets.m_deathVelocity = GetOffset("base_entity.m_deathVelocity");
    g_offsets.m_vecVelocity = GetOffset("base_entity.m_vecVelocity");
    g_offsets.m_flFriction = GetOffset("base_entity.m_flFriction");
    g_offsets.m_hOwnerEntity = GetOffset("base_entity.m_hOwnerEntity");
    g_offsets.m_nRenderMode = GetOffset("base_entity.m_nRenderMode");
    g_offsets.m_MoveType = GetOffset("base_entity.m_MoveType");
    g_offsets.m_Collision = GetOffset("base_entity.m_Collision");
    g_offsets.m_CollisionGroup = GetOffset("base_entity.m_CollisionGroup");
    g_offsets.m_nNextThinkTick = GetOffset("base_entity.m_nNextThinkTick");
    g_offsets.m_iMaxHealth = GetOffset("base_entity.m_iMaxHealth");
    g_offsets.m_iSignifierName = GetOffset("base_entity.m_iSignifierName");
    g_offsets.m_scriptNameIndex = GetOffset("base_entity.m_scriptNameIndex");
    g_offsets.m_lifeState = GetOffset("base_entity.m_lifeState");
    g_offsets.m_rgflCoordinateFrame = GetOffset("base_entity.m_rgflCoordinateFrame");
    g_offsets.m_nModelIndex = GetOffset("base_entity.m_nModelIndex");
    g_offsets.m_vecMins = GetOffset("base_entity.m_vecMins");
    g_offsets.m_vecMaxs = GetOffset("base_entity.m_vecMaxs");
    g_offsets.m_usSolidFlags = GetOffset("base_entity.m_usSolidFlags");
    g_offsets.m_nSolidType = GetOffset("base_entity.m_nSolidType");
    g_offsets.m_triggerBloat = GetOffset("base_entity.m_triggerBloat");
    g_offsets.m_collisionDetailLevel = GetOffset("base_entity.m_collisionDetailLevel");
    g_offsets.m_vecViewOffset_x = GetOffset("base_entity.m_vecViewOffset_x");
    g_offsets.m_vecViewOffset_y = GetOffset("base_entity.m_vecViewOffset_y");
    g_offsets.m_vecViewOffset_z = GetOffset("base_entity.m_vecViewOffset_z");
    g_offsets.m_shieldHealth = GetOffset("base_entity.m_shieldHealth");
    g_offsets.m_shieldHealthMax = GetOffset("base_entity.m_shieldHealthMax");
    g_offsets.m_networkedFlags = GetOffset("base_entity.m_networkedFlags");
    g_offsets.m_meleeResponseFlags = GetOffset("base_entity.m_meleeResponseFlags");
    g_offsets.m_teamMemberIndex = GetOffset("base_entity.m_teamMemberIndex");
    g_offsets.m_squadID = GetOffset("base_entity.m_squadID");
    g_offsets.m_minimapData = GetOffset("base_entity.m_minimapData");
    g_offsets.m_nameVisibilityFlags = GetOffset("base_entity.m_nameVisibilityFlags");
    g_offsets.m_bIsPlayerOverheating = GetOffset("base_entity.m_bIsPlayerOverheating");
    g_offsets.m_lastFiredTime = GetOffset("base_entity.m_lastFiredTime");
    g_offsets.m_lastFiredWeapon = GetOffset("base_entity.m_lastFiredWeapon");
    g_offsets.m_selectedWeapons = GetOffset("base_entity.m_selectedWeapons");
    g_offsets.m_latestPrimaryWeapons = GetOffset("base_entity.m_latestPrimaryWeapons");
    g_offsets.m_latestNonOffhandWeapons = GetOffset("base_entity.m_latestNonOffhandWeapons");
    g_offsets.m_selectedOffhands = GetOffset("base_entity.m_selectedOffhands");
    g_offsets.m_weaponPermission = GetOffset("base_entity.m_weaponPermission");
    g_offsets.m_weaponDisabledFlags = GetOffset("base_entity.m_weaponDisabledFlags");
    g_offsets.m_phaseShiftType = GetOffset("base_entity.m_phaseShiftType");
    g_offsets.m_phaseShiftTimeStart = GetOffset("base_entity.m_phaseShiftTimeStart");
    g_offsets.m_phaseShiftTimeEnd = GetOffset("base_entity.m_phaseShiftTimeEnd");
    g_offsets.m_targetInfoPingValue = GetOffset("base_entity.m_targetInfoPingValue");
    g_offsets.m_inventory = GetOffset("base_entity.m_inventory");
    g_offsets.m_Local = GetOffset("base_entity.m_Local");
    g_offsets.m_bZooming = GetOffset("base_entity.m_bZooming");
    g_offsets.m_zoomToggleOnStartTime = GetOffset("base_entity.m_zoomToggleOnStartTime");
    g_offsets.m_zoomBaseFrac = GetOffset("base_entity.m_zoomBaseFrac");
    g_offsets.m_zoomBaseTime = GetOffset("base_entity.m_zoomBaseTime");
    g_offsets.m_hActiveWeapon = GetOffset("CBasePlayer.m_hActiveWeapon");
    g_offsets.m_iClip1 = GetOffset("CBasePlayer.m_iClip1");
    g_offsets.m_iPrimaryReserveAmmo = GetOffset("CBasePlayer.m_iPrimaryReserveAmmo");
    g_offsets.m_iItemDefinitionIndex = GetOffset("CBaseCombatWeapon.m_iItemDefinitionIndex");

    // Valida offsets críticos
    if (!g_offsets.entity_list || !g_offsets.local_player || !g_offsets.view_render ||
        !g_offsets.view_matrix || !g_offsets.m_vecAbsOrigin || !g_offsets.m_iHealth) {
        runtime.status = "Offsets críticos ausentes no JSON";
        return false;
    }

    runtime.status = "Offsets carregados do JSON (" + result.storage + ") build " + g_offset_snapshot.metadata.build;
    return true;
}

bool TryPlayer(std::uintptr_t entity, Player& player) {
    // Health
    int health = 0;
    if (!ReadValue(entity + g_offsets.m_iHealth, health) || health <= 0 || health > 1000)
        return false;

    // Life state
    std::uint8_t lifeState = 0;
    ReadValue(entity + g_offsets.m_lifeState, lifeState);
    if (lifeState != 0) return false; // Not alive

    // Team
    int team = 0;
    ReadValue(entity + g_offsets.m_iTeamNum, team);

    // Position
    Vec3 pos{};
    if (!ReadValue(entity + g_offsets.m_vecAbsOrigin, pos) || !IsVector(pos))
        return false;

    // Shield
    int shield = 0;
    ReadValue(entity + g_offsets.m_shieldHealth, shield);

    // Velocity
    Vec3 vel{};
    ReadValue(entity + g_offsets.m_vecVelocity, vel);

    // Name
    char name[64]{};
    if (g_offsets.m_iSignifierName) {
        std::uintptr_t namePtr = 0;
        if (ReadPointer(entity + g_offsets.m_iSignifierName, namePtr) && namePtr) {
            mem.Read(namePtr, name, sizeof(name) - 1);
        }
    }

    // Weapon
    char weapon[32]{};
    if (g_offsets.m_hActiveWeapon) {
        std::uintptr_t weaponHandle = 0;
        if (ReadValue(entity + g_offsets.m_hActiveWeapon, weaponHandle) && weaponHandle) {
            std::uintptr_t weaponPtr = 0;
            if (ReadPointer(weaponHandle, weaponPtr) && weaponPtr) {
                if (g_offsets.m_iItemDefinitionIndex) {
                    int defIdx = 0;
                    ReadValue(weaponPtr + g_offsets.m_iItemDefinitionIndex, defIdx);
                    // Map defIdx to weapon name if needed
                    std::snprintf(weapon, sizeof(weapon), "Weapon_%d", defIdx);
                }
            }
        }
    }

    player = {};
    player.address = entity;
    player.pos[0] = pos.x;
    player.pos[1] = pos.y;
    player.pos[2] = pos.z;
    player.head[0] = pos.x;
    player.head[1] = pos.y + 1.72f;
    player.head[2] = pos.z;
    player.health = static_cast<float>(health);
    player.shield = static_cast<float>(shield);
    player.alive = health > 0;
    player.team = team;
    player.life_state = lifeState;
    std::strncpy(player.name, name, sizeof(player.name) - 1);
    std::strncpy(player.weapon, weapon, sizeof(player.weapon) - 1);
    return true;
}

bool ReadEntityList(std::vector<std::uintptr_t>& entities) {
    if (!g_offsets.entity_list) return false;

    std::uintptr_t list = 0;
    if (!ReadPointer(runtime.client_dll_base + g_offsets.entity_list, list) || !IsPointer(list))
        return false;

    // Apex entity list: array of 0x10000 entries, each 0x8 bytes
    for (int i = 0; i < 10000; ++i) {
        std::uintptr_t entity = 0;
        if (!ReadValue(list + static_cast<std::uintptr_t>(i) * 8, entity))
            continue;
        if (IsPointer(entity))
            entities.push_back(entity);
        if (entities.size() >= 256) break;
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

bool ReadCamera() {
    // ViewRender -> view matrix
    std::uintptr_t viewRender = 0;
    if (!ReadPointer(runtime.engine_dll_base + g_offsets.view_render, viewRender) || !viewRender) {
        runtime.camera_status = "ViewRender nao encontrado";
        return false;
    }

    float matrix[16]{};
    if (!ReadExact(viewRender + g_offsets.view_matrix, matrix, sizeof(matrix))) {
        runtime.camera_status = "Falha ao ler view matrix";
        return false;
    }

    bool finite = true;
    for (float v : matrix) finite = finite && std::isfinite(v) && std::fabs(v) < 1000000.f;
    if (!finite) {
        runtime.camera_status = "View matrix invalida";
        return false;
    }

    // Camera origin
    float camPos[3]{};
    ReadExact(viewRender + g_offsets.camera_origin, camPos, sizeof(camPos));

    // View angles
    float viewAngles[3]{};
    ReadExact(viewRender + g_offsets.view_angles, viewAngles, sizeof(viewAngles));

    std::memcpy(runtime.view_matrix, matrix, sizeof(matrix));
    runtime.camera_status = "Camera validada (ViewRender)";
    runtime.last_camera_sample_ms = GetTickCount64();
    return true;
}

bool HasOffsets() {
    return g_offsets.entity_list && g_offsets.local_player && g_offsets.view_render &&
           g_offsets.view_matrix && g_offsets.m_vecAbsOrigin && g_offsets.m_iHealth;
}

void Update(bool processProbe) {
    if (processProbe) {
        const auto diagnostics = mem.GetDiagnosticsSnapshot();
        runtime.device_ok = diagnostics.deviceOpen;
        runtime.process_ok = diagnostics.processInitialized && mem.GetPidFromName(kProcessName) != 0;
        runtime.modules_ok = runtime.client_dll_base > kMinPointer && runtime.engine_dll_base > kMinPointer;
    }
    runtime.offset_catalog_ok = HasOffsets();

    std::vector<std::uintptr_t> entities;
    runtime.entity_source_ok = runtime.modules_ok && ReadCollection(entities);
    if (runtime.entity_source_ok) ReadPlayers(entities); else runtime.players.clear();

    runtime.camera_ok = runtime.entity_source_ok && !runtime.players.empty() && ReadCamera();
    runtime.acquisition_ok = runtime.entity_source_ok && runtime.camera_ok;

    if (runtime.entity_source_ok) runtime.last_world_sample_ms = GetTickCount64();

    if (!runtime.device_ok) runtime.status = "DMA/FPGA indisponivel";
    else if (!runtime.process_ok) runtime.status = "r5apex.exe nao encontrado";
    else if (!runtime.modules_ok) runtime.status = "Modulos client.dll/engine.dll indisponiveis";
    else if (!runtime.offset_catalog_ok) runtime.status = "Offsets Apex build " + g_offset_snapshot.metadata.build + " invalidos";
    else if (!runtime.entity_source_ok) runtime.status = runtime.collection_status;
    else if (runtime.players.empty()) runtime.status = runtime.transform_status;
    else if (!runtime.camera_ok) runtime.status = runtime.camera_status;
    else runtime.status = "Apex ESP ativa";
}

} // namespace

bool Attach() {
    Shutdown();
    runtime.status = "A ligar ao Apex...";

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
    runtime.client_dll_base = mem.GetBaseDaddy("client.dll");
    runtime.engine_dll_base = mem.GetBaseDaddy("engine.dll");
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

} // namespace Apex
