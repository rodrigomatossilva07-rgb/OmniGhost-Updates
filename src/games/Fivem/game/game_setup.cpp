#include "game_setup.h"
#include "offsets.h"
#include "../../DMALibrary/Memory/Memory.h"
#include "globals.h"
#include "../playerInfo/PedData.h"
#include <cmath>
#include <iostream>

extern PedCacheManager g_pedCacheManager;

namespace FiveM {

namespace {
bool LooksPtr(uintptr_t v) {
    return v > 0x10000ULL && v < 0x00007FFFFFFFFFFFULL;
}

bool LocalPlayerLooksAlive() {
    using namespace offset;
    if (!LooksPtr(localplayer))
        return false;
    float hp = 0.f;
    if (!mem.Read(localplayer + playerHealth, &hp, sizeof(hp)))
        return false;
    // GTA health is typically 100..200 when alive; accept a wider window for edge cases.
    return hp > 0.f && hp < 1000.f && std::isfinite(hp);
}

void ApplyBuildExtras(uintptr_t game_base, int build) {
    using namespace offset;
    base = game_base;
    const BuildOffsets* bo = GetOffsetsForBuild(build);
    if (!bo) {
        object_pool = network_player_mgr = blip_list = waypoint = aim_cped = 0;
        return;
    }
    // All module RVAs come from data/fivem_offsets.json via BuildOffsets.
    blip_list = bo->blip_list_offset ? game_base + bo->blip_list_offset : 0;
    aim_cped = bo->aim_cped_offset ? game_base + bo->aim_cped_offset : 0;
    network_player_mgr = bo->network_player_mgr_offset ? game_base + bo->network_player_mgr_offset : 0;
    object_pool = bo->object_pool_offset ? game_base + bo->object_pool_offset : 0;
    waypoint = bo->waypoint_offset ? game_base + bo->waypoint_offset : 0;
    bullet = 0;
    pedVisibilityOffset = bo->ped_visibility_offset;
    // Prefer JSON field offsets when present (also set from build_offset in Setup).
    if (bo->boneList_offset) boneList = bo->boneList_offset;
    if (bo->boneMatrix_offset) boneMatrix = bo->boneMatrix_offset;
    if (bo->playerInfo_offset) playerInfo = bo->playerInfo_offset;
    if (bo->playerHealth_offset) playerHealth = bo->playerHealth_offset;
    if (bo->playerPosition_offset) playerPosition = bo->playerPosition_offset;
}

void WriteStatusLog(const char* note) {
    using namespace offset;
    std::clog << "[OFFSETS] game=FiveM build=" << buildVersion
              << " verified=" << (IsBuildSupported() ? "YES" : "NO")
              << " note=" << (note ? note : "") << '\n';
#if defined(OMNIGHOST_VERBOSE_OFFSET_DIAGNOSTICS)
    std::clog << "[OFFSETS][VERBOSE] world=0x" << std::hex << world
              << " replay=0x" << replay << " viewport=0x" << viewport
              << " camera=0x" << camera << std::dec << '\n';
#endif
}
} // namespace

void Setup() {
    using namespace offset;

    // Process generations must never inherit pointers or optional offsets from
    // the previous FiveM session.
    world = replay = viewport = camera = localplayer = 0;
    base = 0;
    pedVisibilityOffset = blip_list = aim_cped = bullet = 0;
    network_player_mgr = object_pool = waypoint = 0;

    auto game_base = mem.GetBaseDaddy(OmniGhost::GameContext::Instance().GetValidExecutable());
    if (!game_base) {
        std::cout << "[FiveM] Error: game base is null" << std::endl;
        return;
    }

    LoadOffsetsFromJson(nullptr);
    buildVersion = GetBuildVersion();
    if (!IsBuildSupported()) {
        std::cerr << "[FiveM] Build b" << buildVersion
                  << " não suportada: offsets provisórios e offsets de outras builds foram recusados."
                  << std::endl;
        WriteStatusLog("unsupported_build");
        return;
    }
    std::cout << "[FiveM] Offsets validados para a build b" << buildVersion << std::endl;

    const BuildOffsets* build_offset = GetOffsetsForBuild(buildVersion);

    if (build_offset) {
        world = mem.Read<uintptr_t>(game_base + build_offset->world_offset);
        replay = mem.Read<uintptr_t>(game_base + build_offset->replay_offset);
        viewport = mem.Read<uintptr_t>(game_base + build_offset->viewport_offset);
        if (build_offset->camera_offset != 0)
            camera = mem.Read<uintptr_t>(game_base + build_offset->camera_offset);

        if (LooksPtr(world))
            localplayer = mem.Read<uintptr_t>(world + 0x8);

        playerInfo = build_offset->playerInfo_offset;
        playerHealth = build_offset->playerHealth_offset;
        playerPosition = build_offset->playerPosition_offset;
        boneMatrix = build_offset->boneMatrix_offset;
        if (build_offset->boneList_offset != 0)
            boneList = build_offset->boneList_offset;

        ApplyBuildExtras(game_base, buildVersion);
    }

    if (!LooksPtr(world) || !LooksPtr(viewport)) {
        std::cout << "[FiveM] Error: Failed to read valid pointers." << std::endl;
        std::cout << "  World: 0x" << std::hex << world << std::endl;
        std::cout << "  Viewport: 0x" << std::hex << viewport << std::endl;
        std::cout << "  Base: 0x" << std::hex << game_base << std::dec << std::endl;
        std::cout << "[FiveM] ESP will not work until offsets match this game build." << std::endl;
        std::cout << "[FiveM] Atualiza a tabela de offsets suportada e tenta novamente." << std::endl;
        WriteStatusLog("invalid_pointers");
    } else {
        if (!LooksPtr(localplayer))
            localplayer = mem.Read<uintptr_t>(world + 0x8);
        std::cout << "[FiveM] Pointers OK — World/Viewport/LocalPlayer valid (b"
                  << buildVersion << ")" << std::endl;
        if (LocalPlayerLooksAlive())
            std::cout << "[FiveM] LocalPlayer health probe OK" << std::endl;
        else
            std::cout << "[FiveM] LocalPlayer health probe soft-fail (lobby / loading?)" << std::endl;
        WriteStatusLog("ok");
    }
}

int GetCurrentBuildVersion() {
    return offset::buildVersion;
}

void InitializePedCache() {
    g_pedCacheManager.initialize();
}


bool ReinitDma() {
    std::cout << "[FiveM] Reinit DMA (process + world pointers)" << std::endl;
    mem.InvalidateProcess();
    std::string exe = OmniGhost::GameContext::Instance().GetValidExecutable();
    if (exe.empty())
        exe = "GTAProcess.exe";
    bool ok = mem.Init(exe, true, false);
    if (!ok) {
        ok = mem.Init("GTAProcess.exe", true, false);
    }
    if (!ok) {
        mem.ResetDevice();
        if (!mem.Init(std::string(), true, false)) {
            std::cout << "[FiveM] Reinit DMA: FPGA falhou" << std::endl;
            return false;
        }
        ok = mem.Init(exe, true, false) || mem.Init("GTAProcess.exe", true, false);
    }
    if (!ok) {
        std::cout << "[FiveM] Reinit DMA: attach falhou" << std::endl;
        return false;
    }
    Setup();
    g_pedCacheManager.manualCache();
    std::cout << "[FiveM] Reinit DMA OK" << std::endl;
    return true;
}

} // namespace FiveM
