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
    if (build == 3258) {
        blip_list = game_base + 0x2023400;
        aim_cped = game_base + 0x202C8D0;
        network_player_mgr = game_base + 0x1E63C68;
        object_pool = game_base + 0x25BFDE8;
        waypoint = game_base + 0x2EE0288;
        bullet = 0;
        pedVisibilityOffset = 0x147C;
        // Module RVA for framecountlastvisible (global). Soft — used with ped flag.
        framecountlastvisible = game_base + 0x5719A3;
        boneList = 0x410;
        boneMatrix = 0x60;
        playerInfo = 0x10A8;
        playerHealth = 0x280;
        playerPosition = 0x90;
    }
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
    framecountlastvisible = pedVisibilityOffset = blip_list = aim_cped = bullet = 0;
    network_player_mgr = object_pool = waypoint = 0;

    auto game_base = mem.GetBaseDaddy(OmniGhost::GameContext::Instance().GetValidExecutable());
    if (!game_base) {
        std::cout << "[FiveM] Error: game base is null" << std::endl;
        return;
    }

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
