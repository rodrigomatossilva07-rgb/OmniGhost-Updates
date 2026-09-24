#include "offsets.h"

#include "globals.h"
#include "../../DMALibrary/Memory/Memory.h"

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
uintptr_t boneMatrix = 0x60;
uintptr_t playerInfo = 0;
uintptr_t playerHealth = 0x280;
uintptr_t playerPosition = 0x90;
uintptr_t base = 0;
int buildVersion = 0;
uintptr_t pedVisibilityOffset = 0;
uintptr_t blip_list = 0;
uintptr_t aim_cped = 0;
uintptr_t bullet = 0;
uintptr_t network_player_mgr = 0;
uintptr_t object_pool = 0;
uintptr_t waypoint = 0;

} // namespace offset

namespace {

// Source of truth: data/fivem_offsets.json (also embedded at Publish via Build-EmbeddedOffsets).
// Hardcoded seed is only used if JSON cannot be loaded (dev misconfig).
static std::vector<BuildOffsets> g_buildOffsets;
static bool g_offsetsLoaded = false;

static const BuildOffsets kSeedBuildOffsets[] = {
    // build, world, replay, viewport, camera, playerInfo, boneList, boneMatrix, health, pos,
    // object_pool, net_mgr, blip, waypoint, aim_cped, ped_pool, veh_pool, ped_visible_flag
    { 2802, 0x1F5B820, 0x1F5B820, 0x1FBC100, 0x0,       0x10A8, 0x0,    0x60,   0x280, 0x90, 0,0,0,0,0,0,0,0x147C },
    { 2944, 0x257BEA0, 0x1F42068, 0x1FEAAC0, 0x0,       0x10A8, 0x0,    0x60,   0x280, 0x90, 0,0,0,0,0,0,0,0x147C },
    { 3095, 0x2593320, 0x1FBD4F0, 0x201DBA0, 0x201ED50, 0x10A8, 0x410,  0x60,   0x280, 0x90, 0,0,0,0,0,0,0,0x147C },
    { 3258, 0x25B14B0, 0x1FBD4F0, 0x201DBA0, 0x201E7D0, 0x10A8, 0x410,  0x60,   0x280, 0x90,
      0x25BFDE8, 0x1E63C68, 0x2023400, 0x2EE0288, 0x202C8D0, 0x25B1758, 0x2F23F78, 0x147C },
    { 3751, 0x2603908, 0x1FC38A8, 0x206C060, 0x206CC40, 0x10A8, 0x12B8, 0x12B8, 0x280, 0x90, 0,0,0,0,0,0,0x147C },
    { 3788, 0x26068E0, 0x1FC68A8, 0x206F060, 0x206FC40, 0x10A8, 0x12B8, 0x12B8, 0x280, 0x90, 0,0,0,0,0,0,0,0x147C },
};

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
    // bare number
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

static void SeedFromHardcoded() {
    g_buildOffsets.clear();
    for (const auto& b : kSeedBuildOffsets)
        g_buildOffsets.push_back(b);
}

int LoadOffsetsFromJsonImpl(const char* explicit_path) {
    g_buildOffsets.clear();
    g_offsetsLoaded = false;

    std::vector<std::string> candidates;
    if (explicit_path && explicit_path[0])
        candidates.emplace_back(explicit_path);
    candidates.emplace_back("data/fivem_offsets.json");
    candidates.emplace_back("fivem_offsets.json");
    try {
        // Beside executable
        char mod[MAX_PATH]{};
        if (GetModuleFileNameA(nullptr, mod, MAX_PATH)) {
            std::string dir(mod);
            const auto slash = dir.find_last_of("\\/");
            if (slash != std::string::npos) dir.resize(slash);
            candidates.push_back(dir + "\\data\\fivem_offsets.json");
            candidates.push_back(dir + "\\fivem_offsets.json");
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
        std::cerr << "[FiveM] fivem_offsets.json nao encontrado — a usar tabela seed embutida\n";
        SeedFromHardcoded();
        g_offsetsLoaded = true;
        return static_cast<int>(g_buildOffsets.size());
    }

    // Parse "builds" : { "3258": { ... }, ... }
    const size_t buildsKey = data.find("\"builds\"");
    if (buildsKey == std::string::npos) {
        std::cerr << "[FiveM] fivem_offsets.json sem objeto \"builds\" — seed\n";
        SeedFromHardcoded();
        g_offsetsLoaded = true;
        return static_cast<int>(g_buildOffsets.size());
    }
    size_t brace = data.find('{', buildsKey);
    if (brace == std::string::npos) {
        SeedFromHardcoded();
        g_offsetsLoaded = true;
        return static_cast<int>(g_buildOffsets.size());
    }

    // Walk top-level keys inside builds object (depth-aware)
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
            if (bo.world_offset && bo.viewport_offset)
                g_buildOffsets.push_back(bo);
            i = j;
            continue;
        }
        ++i;
    }

    if (g_buildOffsets.empty()) {
        std::cerr << "[FiveM] Nenhum build valido em " << used << " — seed\n";
        SeedFromHardcoded();
    } else {
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

} // namespace

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

} // namespace FiveM
