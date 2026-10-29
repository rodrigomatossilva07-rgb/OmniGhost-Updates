#include "offsets.h"

#include "globals.h"
#include "src/project/third_party/DMALibrary/Memory/Memory.h"
#include "../../../platform/offset_source.h"

#include <cctype>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <cctype>
#include <Windows.h>

namespace FiveM {
namespace offset {

uintptr_t world = 0;
uintptr_t replay = 0;
uintptr_t viewport = 0;
uintptr_t camera = 0;
uintptr_t localplayer = 0;
uintptr_t boneList = 0;
uintptr_t boneMatrix = 0;
uintptr_t playerInfo = 0;
uintptr_t playerHealth = 0;
uintptr_t playerPosition = 0;
uintptr_t base = 0;
int buildVersion = 0;
uintptr_t pedVisibilityOffset = 0;
uintptr_t blip_list = 0;
uintptr_t aim_cped = 0;
uintptr_t bullet = 0;
uintptr_t network_player_mgr = 0;
uintptr_t object_pool = 0;
uintptr_t waypoint = 0;
uintptr_t playerInfo_netId = 0;
uintptr_t playerInfo_netId_alt = 0;
uintptr_t weaponManager = 0;
uintptr_t playerArmor = 0;
uintptr_t playerArmorAlt1 = 0;
uintptr_t playerArmorAlt2 = 0;
uintptr_t playerMaxHealth = 0;
uintptr_t pedVehicle = 0;
uintptr_t playerInfo_wanted = 0;
uintptr_t playerInfo_name = 0;
uintptr_t vehicleLock = 0;
uintptr_t vehicleDriver = 0;
uintptr_t vehicleDriverNew = 0;
uintptr_t vehicleEngineHp = 0;
uintptr_t vehicleBodyHp = 0;
uintptr_t vehicleFuel = 0;
uintptr_t vehicleGear = 0;
uintptr_t vehicleRPM = 0;
uintptr_t vehicleModelInfo = 0;
uintptr_t pedVelocity = 0;
uintptr_t pedConfigFlags = 0;
uintptr_t vehicleHandling = 0;
uintptr_t vehicleGravity = 0;
uintptr_t weaponMgr_currentWeapon = 0;
uintptr_t weaponInfo_hash = 0;
uintptr_t weaponInfo_recoil = 0;
uintptr_t weaponInfo_spread = 0;
uintptr_t weaponInfo_damage = 0;
uintptr_t weaponInfo_range = 0;
uintptr_t cam_position = 0;
uintptr_t cam_direction = 0;
uintptr_t world_localPlayer = 0;
uintptr_t boneFrag3258 = 0;
uintptr_t boneFrag3751 = 0;
uintptr_t boneSkeleton = 0;
uintptr_t boneSkeletonStride = 0;
uintptr_t boneSkeletonTranslation = 0;
uintptr_t boneArrayBase = 0;
uintptr_t boneSize = 0;
uintptr_t pedVelocityAlt = 0;
uintptr_t vehicleList = 0;
uintptr_t vehicleCount = 0;
uintptr_t vehicleEntryStride = 0;
uintptr_t vehiclePosition = 0;
uintptr_t vehicleMatrix = 0;
uintptr_t viewportMatrix = 0;
uintptr_t replayPedInterface = 0;
uintptr_t pedInterfaceList = 0;
uintptr_t replayVehicleInterface = 0;
uintptr_t replayVehicleInterfaceAlt = 0;
uintptr_t waypointBlipPosition = 0;
uintptr_t waypointBlipSprite = 0;
uintptr_t b3258_networkPlayerMgr = 0;
uintptr_t b3258_objectPool = 0;
uintptr_t b3258_pickupMgr = 0;
uintptr_t b3258_waypoint = 0;
uintptr_t b3258_globalPtr = 0;
uintptr_t b3258_skySettings = 0;

}

namespace {


static std::vector<BuildOffsets> g_buildOffsets;
static bool g_offsetsLoaded = false;

static void ResetLoadedFieldOffsets() noexcept {
    using namespace offset;
    boneList = 0;
    boneMatrix = playerInfo = playerHealth = playerPosition = 0;
    pedVisibilityOffset = 0;
    playerInfo_netId = playerInfo_netId_alt = 0;
    weaponManager = playerArmor = playerArmorAlt1 = playerArmorAlt2 = 0;
    playerMaxHealth = pedVehicle = playerInfo_wanted = playerInfo_name = 0;
    vehicleLock = vehicleDriver = vehicleDriverNew = vehicleEngineHp = 0;
    vehicleBodyHp = vehicleFuel = vehicleGear = vehicleRPM = vehicleModelInfo = 0;
    pedVelocity = pedConfigFlags = vehicleHandling = vehicleGravity = 0;
    weaponMgr_currentWeapon = weaponInfo_hash = weaponInfo_recoil = 0;
    weaponInfo_spread = weaponInfo_damage = weaponInfo_range = 0;
    cam_position = cam_direction = world_localPlayer = 0;
    boneFrag3258 = boneFrag3751 = boneSkeleton = boneSkeletonStride = 0;
    boneSkeletonTranslation = boneArrayBase = boneSize = pedVelocityAlt = 0;
    vehicleList = vehicleCount = vehicleEntryStride = vehiclePosition = vehicleMatrix = 0;
    viewportMatrix = replayPedInterface = pedInterfaceList = 0;
    replayVehicleInterface = replayVehicleInterfaceAlt = 0;
    waypointBlipPosition = waypointBlipSprite = 0;
    b3258_networkPlayerMgr = b3258_objectPool = b3258_pickupMgr = 0;
    b3258_waypoint = b3258_globalPtr = b3258_skySettings = 0;
}

static uintptr_t ParseHexU64(const std::string& s) {
    if (s.empty()) return 0;
    try {
        size_t idx = 0;
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
            return static_cast<uintptr_t>(std::stoull(s, &idx, 16));
        return static_cast<uintptr_t>(std::stoull(s, &idx, 0));
    } catch (...) {
        return 0;
    }
}

static bool ExtractJsonStringValue(const std::string& obj, const char* key, std::string& out) {
    out.clear();
    const std::string pat = std::string("\"") + key + "\"";
    size_t p = obj.find(pat);
    if (p == std::string::npos) return false;
    p = obj.find(':', p + pat.size());
    if (p == std::string::npos) return false;
    ++p;
    while (p < obj.size() && std::isspace(static_cast<unsigned char>(obj[p]))) ++p;
    if (p >= obj.size()) return false;
    if (obj[p] == '"') {
        size_t e = obj.find('"', p + 1);
        if (e == std::string::npos) return false;
        out = obj.substr(p + 1, e - p - 1);
        return true;
    }

    size_t e = p;
    while (e < obj.size() && (std::isxdigit(static_cast<unsigned char>(obj[e])) || obj[e]=='x'||obj[e]=='X'||obj[e]=='+'||obj[e]=='-'))
        ++e;
    out = obj.substr(p, e - p);
    return !out.empty();
}

static uintptr_t JsonHexField(const std::string& obj, const char* key) {
    std::string v;
    if (!ExtractJsonStringValue(obj, key, v)) return 0;
    return ParseHexU64(v);
}

static bool ReadEntireFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const auto n = in.tellg();
    if (n <= 0) return false;
    in.seekg(0, std::ios::beg);
    out.assign(static_cast<size_t>(n), '\0');
    in.read(out.data(), n);
    return static_cast<bool>(in) || in.eof();
}

static uintptr_t SnapshotValue(const OmniGhost::EmbeddedOffsets::Snapshot& snapshot,
                               int build, const char* field) {
    std::uint64_t value = 0;
    const std::string path = "builds." + std::to_string(build) + "." + field;
    return snapshot.TryGet(path, value) ? static_cast<uintptr_t>(value) : 0;
}

static bool LoadFromEmbeddedSnapshot() {
    const auto result = OmniGhost::OffsetSource::LoadSnapshot(
        OmniGhost::OffsetSource::GameId::FiveM);
    if (!result.ok)
        return false;

    g_buildOffsets.clear();
    std::uint64_t buildCount = 0;
    if (!result.snapshot.TryGet("build_index.count", buildCount) || buildCount == 0 || buildCount > 128)
        return false;
    for (std::uint64_t index = 0; index < buildCount; ++index) {
        std::uint64_t buildValue = 0;
        const std::string indexPath = "build_index." + std::to_string(index);
        if (!result.snapshot.TryGet(indexPath, buildValue) || buildValue == 0 || buildValue > 99999)
            return false;
        BuildOffsets offsets{};
        offsets.build = static_cast<int>(buildValue);
        offsets.world_offset = SnapshotValue(result.snapshot, offsets.build, "world");
        offsets.replay_offset = SnapshotValue(result.snapshot, offsets.build, "replay");
        offsets.viewport_offset = SnapshotValue(result.snapshot, offsets.build, "viewport");
        offsets.camera_offset = SnapshotValue(result.snapshot, offsets.build, "camera");
        offsets.playerInfo_offset = SnapshotValue(result.snapshot, offsets.build, "player_info");
        offsets.boneList_offset = SnapshotValue(result.snapshot, offsets.build, "bone_list");
        offsets.boneMatrix_offset = SnapshotValue(result.snapshot, offsets.build, "bone_matrix");
        offsets.playerHealth_offset = SnapshotValue(result.snapshot, offsets.build, "player_health");
        offsets.playerPosition_offset = SnapshotValue(result.snapshot, offsets.build, "player_position");
        offsets.object_pool_offset = SnapshotValue(result.snapshot, offsets.build, "object_pool");
        offsets.network_player_mgr_offset = SnapshotValue(result.snapshot, offsets.build, "network_player_mgr");
        offsets.blip_list_offset = SnapshotValue(result.snapshot, offsets.build, "blip_list");
        offsets.waypoint_offset = SnapshotValue(result.snapshot, offsets.build, "waypoint");
        offsets.aim_cped_offset = SnapshotValue(result.snapshot, offsets.build, "aim_cped");
        offsets.ped_pool_offset = SnapshotValue(result.snapshot, offsets.build, "ped_pool");
        offsets.vehicle_pool_offset = SnapshotValue(result.snapshot, offsets.build, "vehicle_pool");
        offsets.ped_visibility_offset = SnapshotValue(result.snapshot, offsets.build, "ped_visible_flag");
        if (offsets.world_offset && offsets.replay_offset && offsets.viewport_offset) {
            if (!offsets.boneList_offset)
                std::cerr << "[FiveM] Build b" << offsets.build
                          << " sem bone_list: skeleton ESP indisponivel nesta build.\n";
            g_buildOffsets.push_back(offsets);
        }
    }
    if (g_buildOffsets.empty())
        return false;
    auto shared = [&](const char* field, uintptr_t& output) {
        std::uint64_t value = 0;
        if (result.snapshot.TryGet(std::string("shared_fields.") + field, value))
            output = static_cast<uintptr_t>(value);
    };
    shared("playerInfo_netId", offset::playerInfo_netId);
    shared("playerInfo_netId_alt", offset::playerInfo_netId_alt);
    shared("weaponManager", offset::weaponManager);
    shared("playerArmor", offset::playerArmor);
    shared("playerArmorAlt1", offset::playerArmorAlt1);
    shared("playerArmorAlt2", offset::playerArmorAlt2);
    shared("playerMaxHealth", offset::playerMaxHealth);
    shared("pedVehicle", offset::pedVehicle);
    shared("playerInfo_wanted", offset::playerInfo_wanted);
    shared("playerInfo_name", offset::playerInfo_name);
    shared("vehicleLock", offset::vehicleLock);
    shared("vehicleDriver", offset::vehicleDriver);
    shared("vehicleDriverNew", offset::vehicleDriverNew);
    shared("vehicleEngineHp", offset::vehicleEngineHp);
    shared("vehicleBodyHp", offset::vehicleBodyHp);
    shared("vehicleFuel", offset::vehicleFuel);
    shared("vehicleGear", offset::vehicleGear);
    shared("vehicleRPM", offset::vehicleRPM);
    shared("vehicleModelInfo", offset::vehicleModelInfo);
    shared("pedVelocity", offset::pedVelocity);
    shared("pedConfigFlags", offset::pedConfigFlags);
    shared("vehicleHandling", offset::vehicleHandling);
    shared("vehicleGravity", offset::vehicleGravity);
    shared("weaponMgr_currentWeapon", offset::weaponMgr_currentWeapon);
    shared("weaponInfo_hash", offset::weaponInfo_hash);
    shared("weaponInfo_recoil", offset::weaponInfo_recoil);
    shared("weaponInfo_spread", offset::weaponInfo_spread);
    shared("weaponInfo_damage", offset::weaponInfo_damage);
    shared("weaponInfo_range", offset::weaponInfo_range);
    shared("cam_position", offset::cam_position);
    shared("cam_direction", offset::cam_direction);
    shared("world_localPlayer", offset::world_localPlayer);
    shared("boneFrag3258", offset::boneFrag3258);
    shared("boneFrag3751", offset::boneFrag3751);
    shared("boneSkeleton", offset::boneSkeleton);
    shared("boneSkeletonStride", offset::boneSkeletonStride);
    shared("boneSkeletonTranslation", offset::boneSkeletonTranslation);
    shared("boneArrayBase", offset::boneArrayBase);
    shared("boneSize", offset::boneSize);
    shared("pedVelocityAlt", offset::pedVelocityAlt);
    shared("vehicleList", offset::vehicleList);
    shared("vehicleCount", offset::vehicleCount);
    shared("vehicleEntryStride", offset::vehicleEntryStride);
    shared("vehiclePosition", offset::vehiclePosition);
    shared("vehicleMatrix", offset::vehicleMatrix);
    shared("viewportMatrix", offset::viewportMatrix);
    shared("replayPedInterface", offset::replayPedInterface);
    shared("pedInterfaceList", offset::pedInterfaceList);
    shared("replayVehicleInterface", offset::replayVehicleInterface);
    shared("replayVehicleInterfaceAlt", offset::replayVehicleInterfaceAlt);
    shared("waypointBlipPosition", offset::waypointBlipPosition);
    shared("waypointBlipSprite", offset::waypointBlipSprite);
    shared("b3258_networkPlayerMgr", offset::b3258_networkPlayerMgr);
    shared("b3258_objectPool", offset::b3258_objectPool);
    shared("b3258_pickupMgr", offset::b3258_pickupMgr);
    shared("b3258_waypoint", offset::b3258_waypoint);
    shared("b3258_globalPtr", offset::b3258_globalPtr);
    shared("b3258_skySettings", offset::b3258_skySettings);
    if (!offset::world_localPlayer || !offset::playerInfo_netId ||
        !offset::weaponManager || !offset::playerArmor ||
        !offset::pedVehicle || !offset::playerInfo_name ||
        !offset::pedVelocity || !offset::weaponMgr_currentWeapon ||
        !offset::weaponInfo_hash || !offset::viewportMatrix ||
        !offset::replayPedInterface || !offset::pedInterfaceList ||
        !offset::replayVehicleInterface || !offset::waypointBlipPosition ||
        !offset::waypointBlipSprite) {
        g_buildOffsets.clear();
        return false;
    }
    std::cout << "[FiveM] Offsets embedded: builds=" << g_buildOffsets.size() << std::endl;
    return true;
}

int LoadOffsetsFromJsonImpl(const char* explicit_path) {
    g_buildOffsets.clear();
    g_offsetsLoaded = false;
    ResetLoadedFieldOffsets();

    std::vector<std::string> candidates;
    if (explicit_path && explicit_path[0])
        candidates.emplace_back(explicit_path);
    candidates.emplace_back("data/fivem_offsets.json");
    try {

        char mod[MAX_PATH]{};
        if (GetModuleFileNameA(nullptr, mod, MAX_PATH)) {
            std::string dir(mod);
            const auto slash = dir.find_last_of("\\/");
            if (slash != std::string::npos) dir.resize(slash);
            candidates.push_back(dir + "\\data\\fivem_offsets.json");
        }
    } catch (...) {
        std::cerr << "[FiveM] Falha ao adicionar caminhos de offsets junto ao executavel; a continuar com os caminhos padrao.\n";
    }

    std::string data;
    std::string used;
    for (const auto& c : candidates) {
        if (ReadEntireFile(c, data)) {
            used = c;
            break;
        }
    }

    if (data.empty()) {
        if (!LoadFromEmbeddedSnapshot())
            std::cerr << "[FiveM] Snapshot de offsets indisponivel ou invalido; ativacao bloqueada.\n";
        g_offsetsLoaded = true;
        return static_cast<int>(g_buildOffsets.size());
    }


    const size_t buildsKey = data.find("\"builds\"");
    if (buildsKey == std::string::npos) {
        std::cerr << "[FiveM] fivem_offsets.json sem objeto \"builds\"; ativacao bloqueada.\n";
        g_offsetsLoaded = true;
        return static_cast<int>(g_buildOffsets.size());
    }
    size_t brace = data.find('{', buildsKey);
    if (brace == std::string::npos) {
        g_offsetsLoaded = true;
        return static_cast<int>(g_buildOffsets.size());
    }


    size_t i = brace + 1;
    int depth = 1;
    while (i < data.size() && depth > 0) {
        const char ch = data[i];
        if (ch == '{') { ++depth; ++i; continue; }
        if (ch == '}') { --depth; ++i; continue; }
        if (depth == 1 && ch == '"') {
            size_t k0 = i + 1;
            size_t k1 = data.find('"', k0);
            if (k1 == std::string::npos) break;
            const std::string buildKey = data.substr(k0, k1 - k0);
            size_t objStart = data.find('{', k1);
            if (objStart == std::string::npos) break;
            int d = 0;
            size_t j = objStart;
            for (; j < data.size(); ++j) {
                if (data[j] == '{') ++d;
                else if (data[j] == '}') {
                    --d;
                    if (d == 0) { ++j; break; }
                }
            }
            const std::string obj = data.substr(objStart, j - objStart);
            BuildOffsets bo{};
            try {
                bo.build = std::stoi(buildKey);
            } catch (...) {
                i = j;
                continue;
            }
            bo.world_offset = JsonHexField(obj, "world");
            bo.replay_offset = JsonHexField(obj, "replay");
            bo.viewport_offset = JsonHexField(obj, "viewport");
            bo.camera_offset = JsonHexField(obj, "camera");
            bo.playerInfo_offset = JsonHexField(obj, "player_info");
            bo.boneList_offset = JsonHexField(obj, "bone_list");
            bo.boneMatrix_offset = JsonHexField(obj, "bone_matrix");
            bo.playerHealth_offset = JsonHexField(obj, "player_health");
            bo.playerPosition_offset = JsonHexField(obj, "player_position");
            bo.object_pool_offset = JsonHexField(obj, "object_pool");
            bo.network_player_mgr_offset = JsonHexField(obj, "network_player_mgr");
            bo.blip_list_offset = JsonHexField(obj, "blip_list");
            bo.waypoint_offset = JsonHexField(obj, "waypoint");
            bo.aim_cped_offset = JsonHexField(obj, "aim_cped");
            bo.ped_pool_offset = JsonHexField(obj, "ped_pool");
            bo.vehicle_pool_offset = JsonHexField(obj, "vehicle_pool");
            bo.ped_visibility_offset = JsonHexField(obj, "ped_visible_flag");
            if (!bo.ped_visibility_offset)
                bo.ped_visibility_offset = JsonHexField(obj, "ped_visibility");
            if (bo.world_offset && bo.replay_offset && bo.viewport_offset) {
                if (!bo.boneList_offset)
                    std::cerr << "[FiveM] Build b" << bo.build
                              << " sem bone_list: skeleton ESP indisponivel nesta build.\n";
                g_buildOffsets.push_back(bo);
            }
            i = j;
            continue;
        }
        ++i;
    }

    if (g_buildOffsets.empty()) {
        std::cerr << "[FiveM] Nenhum build valido em " << used << "; ativacao bloqueada.\n";
    } else {
        auto shared = [&](const char* field, uintptr_t& output) {
            output = JsonHexField(data, field);
        };
        shared("playerInfo_netId", offset::playerInfo_netId);
        shared("playerInfo_netId_alt", offset::playerInfo_netId_alt);
        shared("weaponManager", offset::weaponManager);
        shared("playerArmor", offset::playerArmor);
        shared("playerArmorAlt1", offset::playerArmorAlt1);
        shared("playerArmorAlt2", offset::playerArmorAlt2);
        shared("playerMaxHealth", offset::playerMaxHealth);
        shared("pedVehicle", offset::pedVehicle);
        shared("playerInfo_wanted", offset::playerInfo_wanted);
        shared("playerInfo_name", offset::playerInfo_name);
        shared("vehicleLock", offset::vehicleLock);
        shared("vehicleDriver", offset::vehicleDriver);
        shared("vehicleDriverNew", offset::vehicleDriverNew);
        shared("vehicleEngineHp", offset::vehicleEngineHp);
        shared("vehicleBodyHp", offset::vehicleBodyHp);
        shared("vehicleFuel", offset::vehicleFuel);
        shared("vehicleGear", offset::vehicleGear);
        shared("vehicleRPM", offset::vehicleRPM);
        shared("vehicleModelInfo", offset::vehicleModelInfo);
        shared("pedVelocity", offset::pedVelocity);
        shared("pedConfigFlags", offset::pedConfigFlags);
        shared("vehicleHandling", offset::vehicleHandling);
        shared("vehicleGravity", offset::vehicleGravity);
        shared("weaponMgr_currentWeapon", offset::weaponMgr_currentWeapon);
        shared("weaponInfo_hash", offset::weaponInfo_hash);
        shared("weaponInfo_recoil", offset::weaponInfo_recoil);
        shared("weaponInfo_spread", offset::weaponInfo_spread);
        shared("weaponInfo_damage", offset::weaponInfo_damage);
        shared("weaponInfo_range", offset::weaponInfo_range);
        shared("cam_position", offset::cam_position);
        shared("cam_direction", offset::cam_direction);
        shared("world_localPlayer", offset::world_localPlayer);
        shared("boneFrag3258", offset::boneFrag3258);
        shared("boneFrag3751", offset::boneFrag3751);
        shared("boneSkeleton", offset::boneSkeleton);
        shared("boneSkeletonStride", offset::boneSkeletonStride);
        shared("boneSkeletonTranslation", offset::boneSkeletonTranslation);
        shared("boneArrayBase", offset::boneArrayBase);
        shared("boneSize", offset::boneSize);
        shared("pedVelocityAlt", offset::pedVelocityAlt);
        shared("vehicleList", offset::vehicleList);
        shared("vehicleCount", offset::vehicleCount);
        shared("vehicleEntryStride", offset::vehicleEntryStride);
        shared("vehiclePosition", offset::vehiclePosition);
        shared("vehicleMatrix", offset::vehicleMatrix);
        shared("viewportMatrix", offset::viewportMatrix);
        shared("replayPedInterface", offset::replayPedInterface);
        shared("pedInterfaceList", offset::pedInterfaceList);
        shared("replayVehicleInterface", offset::replayVehicleInterface);
        shared("replayVehicleInterfaceAlt", offset::replayVehicleInterfaceAlt);
        shared("waypointBlipPosition", offset::waypointBlipPosition);
        shared("waypointBlipSprite", offset::waypointBlipSprite);
        shared("b3258_networkPlayerMgr", offset::b3258_networkPlayerMgr);
        shared("b3258_objectPool", offset::b3258_objectPool);
        shared("b3258_pickupMgr", offset::b3258_pickupMgr);
        shared("b3258_waypoint", offset::b3258_waypoint);
        shared("b3258_globalPtr", offset::b3258_globalPtr);
        shared("b3258_skySettings", offset::b3258_skySettings);
        if (!offset::world_localPlayer || !offset::playerInfo_netId ||
            !offset::weaponManager || !offset::playerArmor ||
            !offset::pedVehicle || !offset::playerInfo_name ||
            !offset::pedVelocity || !offset::weaponMgr_currentWeapon ||
            !offset::weaponInfo_hash || !offset::viewportMatrix ||
            !offset::replayPedInterface || !offset::pedInterfaceList ||
            !offset::replayVehicleInterface || !offset::waypointBlipPosition ||
            !offset::waypointBlipSprite) {
            std::cerr << "[FiveM] shared_fields incompleto; ativacao bloqueada.\n";
            g_buildOffsets.clear();
        }
        std::cout << "[FiveM] Offsets JSON: " << used << " builds=" << g_buildOffsets.size() << std::endl;
    }
    g_offsetsLoaded = true;
    return static_cast<int>(g_buildOffsets.size());
}

static void EnsureOffsetsLoaded() {
    if (!g_offsetsLoaded)
        LoadOffsetsFromJsonImpl(nullptr);
}

int BuildFromProcessName()
{
    const std::string& executable = OmniGhost::GameContext::Instance().GetValidExecutable();
    std::size_t position = executable.find("_b");
    if (position == std::string::npos)
        position = executable.find("_B");
    if (position == std::string::npos)
        return 0;
    position += 2;
    if (position >= executable.size() ||
        !std::isdigit(static_cast<unsigned char>(executable[position])))
        return 0;

    int build = 0;
    while (position < executable.size() &&
           std::isdigit(static_cast<unsigned char>(executable[position]))) {
        build = build * 10 + (executable[position] - '0');
        if (build > 99999)
            return 0;
        ++position;
    }
    return build;
}

}

int LoadOffsetsFromJson(const char* explicit_path) {
    return LoadOffsetsFromJsonImpl(explicit_path);
}

int GetBuildVersion()
{
    EnsureOffsetsLoaded();
    const int build = BuildFromProcessName();
    if (build > 0)
        std::cout << "[FiveM] Build detetada no processo: b" << build << std::endl;
    else
        std::cerr << "[FiveM] Não foi possível determinar uma build validada pelo nome do processo." << std::endl;
    return build;
}

const BuildOffsets* GetOffsetsForBuild(int build)
{
    EnsureOffsetsLoaded();
    for (const BuildOffsets& offsets : g_buildOffsets) {
        if (offsets.build == build)
            return &offsets;
    }
    return nullptr;
}

bool IsBuildSupported()
{
    return GetOffsetsForBuild(offset::buildVersion) != nullptr;
}

bool SoftProbeLobbyOffsets() {
    using namespace offset;
    auto game_base = mem.GetBaseDaddy(OmniGhost::GameContext::Instance().GetValidExecutable());
    if (!game_base)
        game_base = mem.GetBaseDaddy("GTAProcess.exe");
    if (!game_base) {
        std::cout << "[FiveM] SoftProbe: module base=0\n";
        return false;
    }
    if (!world || !viewport) {
        std::cout << "[FiveM] SoftProbe: world/viewport missing (build offsets mismatch?)\n";
        return false;
    }
    const auto looksPointer = [](uintptr_t value) {
        return value >= 0x10000ULL && value < 0x00007FFFFFFFFFFFULL;
    };
    uintptr_t worldProbe = 0;
    uintptr_t viewportProbe = 0;
    const bool readable = looksPointer(world) && looksPointer(viewport) &&
        mem.Read(world, &worldProbe, sizeof(worldProbe)) &&
        mem.Read(viewport, &viewportProbe, sizeof(viewportProbe));
    const bool ok = readable;
    std::cout << "[FiveM] SoftProbe world=0x" << std::hex << world << std::dec
              << " read=" << (readable ? "OK" : "FAIL")
              << " => " << (ok ? "PASS" : "FAIL") << std::endl;
    return ok;
}

bool HasAnyCriticalOffsets() {
    EnsureOffsetsLoaded();
    for (const BuildOffsets& bo : g_buildOffsets) {
        if (bo.world_offset && bo.viewport_offset)
            return true;
    }
    return false;
}

}
