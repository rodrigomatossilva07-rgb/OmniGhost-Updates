#include "game_launch_service.h"
#include "game_adapter_registry.h"

#include "../globals.h"
#include "../platform/offset_auto.h"
#include "../../DMALibrary/Memory/Memory.h"
#include "../../Fivem/game/game.h"
#include "../../Fivem/aimbot/aim_type.h"
#include "../../Cs2/cs2_game.h"
#include "../../Rust/rust_game.h"
#include "../../Warzone/warzone_game.h"
#include "../../Valorant/valorant_game.h"
#include "../../Fortnite/fortnite_game.h"

#include <algorithm>
#include <cctype>
#include <iostream>

namespace OmniGhost::GameLaunch {

std::string FindFiveMProcessViaDma() {
    static const char* known[] = {
        "FiveM_b3258_GTAProcess.exe", "FiveM_b3407_GTAProcess.exe",
        "FiveM_b3323_GTAProcess.exe", "FiveM_b3095_GTAProcess.exe",
        "FiveM_b2944_GTAProcess.exe", "FiveM_b2802_GTAProcess.exe",
        "FiveM_b2699_GTAProcess.exe", "FiveM_b2612_GTAProcess.exe",
        "FiveM_b2545_GTAProcess.exe", "FiveM_b2372_GTAProcess.exe",
        "FiveM_b2189_GTAProcess.exe", "FiveM_GTAProcess.exe",
        "FiveM_b3570_GTAProcess.exe"
    };
    for (const char* name : known) {
        if (mem.GetPidFromName(name) != 0) {
            std::cout << "[FiveM] Processo remoto encontrado: " << name << '\n';
            return name;
        }
    }
    PVMMDLL_PROCESS_INFORMATION info = nullptr;
    DWORD total = 0;
    if (!VMMDLL_ProcessGetInformationAll(mem.vHandle, &info, &total) || !info) {
        std::cerr << "[FiveM] Falha a listar processos via DMA.\n";
        return {};
    }
    VmmOwned<VMMDLL_PROCESS_INFORMATION> processInfo(info);
    for (DWORD i = 0; i < total; ++i) {
        const char* name = info[i].szNameLong;
        if (!name || !name[0]) continue;
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (lower.find("gtaprocess") != std::string::npos) {
            std::cout << "[FiveM] Processo remoto (scan): " << name << '\n';
            return name;
        }
    }
    return {};
}

bool IsProcessPresent(::Launcher::GameId selected) {
    if (!mem.GetDiagnosticsSnapshot().deviceOpen) return false;
    switch (selected) {
    case ::Launcher::GameId::CS2:
        return mem.GetPidFromName("cs2.exe") != 0 || mem.GetPidFromName("CS2.exe") != 0;
    case ::Launcher::GameId::Rust:
        return mem.GetPidFromName("RustClient.exe") != 0 || mem.GetPidFromName("Rust.exe") != 0;
    case ::Launcher::GameId::Warzone: return mem.GetPidFromName("cod.exe") != 0;
    case ::Launcher::GameId::Valorant:
        return mem.GetPidFromName("VALORANT-Win64-Shipping.exe") != 0 ||
               mem.GetPidFromName("VALORANT.exe") != 0;
    case ::Launcher::GameId::Fortnite:
        return mem.GetPidFromName("FortniteClient-Win64-Shipping.exe") != 0 ||
               mem.GetPidFromName("FortniteClient-Win64-Shipping_EAC_EOS.exe") != 0 ||
               mem.GetPidFromName("Fortnite.exe") != 0;
    case ::Launcher::GameId::FiveM: return !FindFiveMProcessViaDma().empty();
    default: return false;
    }
}

bool Initialize(::Launcher::GameId selected) {
    mem.ClearCancel();
    const auto result = OmniGhost::Launcher::StartGameAdapter(selected);
    if (!result.succeeded) {
        std::cerr << "[ADAPTER] code=" << static_cast<int>(result.error.code)
                  << " message=" << OmniGhost::Launcher::AdapterErrorMessage(result.error.code) << '\n';
    }
    return result.succeeded;
}

} // namespace OmniGhost::GameLaunch
