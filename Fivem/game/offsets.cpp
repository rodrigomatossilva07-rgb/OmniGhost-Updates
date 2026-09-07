#include "offsets.h"

#include "globals.h"
#include "../../DMALibrary/Memory/Memory.h"

#include <cctype>
#include <iostream>
#include <string>

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
uintptr_t framecountlastvisible = 0;
uintptr_t pedVisibilityOffset = 0;
uintptr_t blip_list = 0;
uintptr_t aim_cped = 0;
uintptr_t bullet = 0;
uintptr_t network_player_mgr = 0;
uintptr_t object_pool = 0;
uintptr_t waypoint = 0;

} // namespace offset

namespace {

// Complete production allow-list. Experimental offset sets are not compiled
// into the client: an unknown build fails closed instead of silently
// borrowing offsets from a different game version.
constexpr BuildOffsets kVerifiedBuildOffsets[] = {
    { 2802, 0x1F5B820, 0x1F5B820, 0x1FBC100, 0x0,       0x10A8, 0x0,    0x60,   0x280, 0x90 },
    { 2944, 0x257BEA0, 0x1F42068, 0x1FEAAC0, 0x0,       0x10A8, 0x0,    0x60,   0x280, 0x90 },
    { 3095, 0x2593320, 0x1FBD4F0, 0x201DBA0, 0x201ED50, 0x10A8, 0x410,  0x60,   0x280, 0x90 },
    { 3258, 0x25B14B0, 0x1FBD4F0, 0x201DBA0, 0x201E7D0, 0x10A8, 0xFF8,  0xFF8,  0x280, 0x90 },
    { 3751, 0x2603908, 0x1FC38A8, 0x206C060, 0x206CC40, 0x10A8, 0x12B8, 0x12B8, 0x280, 0x90 },
    { 3788, 0x26068E0, 0x1FC68A8, 0x206F060, 0x206FC40, 0x10A8, 0x12B8, 0x12B8, 0x280, 0x90 },
};

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

int GetBuildVersion()
{
    const int build = BuildFromProcessName();
    if (build > 0)
        std::cout << "[FiveM] Build detetada no processo: b" << build << std::endl;
    else
        std::cerr << "[FiveM] Não foi possível determinar uma build validada pelo nome do processo." << std::endl;
    return build;
}

const BuildOffsets* GetOffsetsForBuild(int build)
{
    for (const BuildOffsets& offsets : kVerifiedBuildOffsets) {
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
    if (!world) {
        std::cout << "[FiveM] SoftProbe: world=0 (build offsets mismatch?)\n";
        return false;
    }
    const bool world_ok = world >= 0x10000ULL && world < 0x00007FFFFFFFFFFFULL;
    uintptr_t probe = 0;
    const bool readable = world_ok && mem.Read(world, &probe, sizeof(probe));
    const bool ok = world_ok && readable;
    std::cout << "[FiveM] SoftProbe world=0x" << std::hex << world << std::dec
              << " read=" << (readable ? "OK" : "FAIL")
              << " => " << (ok ? "PASS" : "FAIL") << std::endl;
    return ok;
}

} // namespace FiveM
