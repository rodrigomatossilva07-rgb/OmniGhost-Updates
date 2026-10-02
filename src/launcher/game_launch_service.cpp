#include "game_launch_service.h"
#include "game_adapter_registry.h"

#include "../globals.h"
#include "../platform/offset_auto.h"
#include "../../DMALibrary/Memory/Memory.h"
#include "src/games/Fivem/game/game.h"
#include "src/games/Fivem/aimbot/aim_type.h"
#include "src/games/Cs2/cs2_game.h"
#include "src/games/Warzone/warzone_game.h"
#include "src/games/Fortnite/fortnite_game.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <iostream>
#include <initializer_list>

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
    const auto processes = mem.GetProcessNames();
    if (processes.empty()) {
        std::cerr << "[FiveM] Falha a listar processos via DMA.\n";
        return {};
    }
    for (const auto& name : processes) {
        const size_t separator = name.find_last_of("\\/");
        const std::string executable = separator == std::string::npos
            ? name : name.substr(separator + 1);
        std::string lower = executable;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (lower.find("gtaprocess") != std::string::npos) {
            std::cout << "[FiveM] Processo remoto (scan): " << executable << '\n';
            return executable;
        }
    }
    return {};
}

ProcessProbeResult ProbeProcess(::Launcher::GameId selected) {
    try {
    // This is a preflight only: open the DMA device without binding it to a
    // game process, then look for the selected executable.  It prevents a
    // wrong menu from changing the global DMA process context.
    // Trust the live handle rather than a diagnostics snapshot that may lag a
    // reset/reinitialization by one sample.
    if (!mem.vHandle && !mem.Init(std::string(), true, false))
        return {ProcessProbeState::DeviceUnavailable, {}};
    const auto tryPidNames = [&](std::initializer_list<const char*> names) -> ProcessProbeResult {
        for (const char* name : names)
            if (mem.GetPidFromName(name) != 0)
                return {ProcessProbeState::Found, name};
        return {ProcessProbeState::InventoryUnavailable, {}};
    };
    ProcessProbeResult direct{};
    switch (selected) {
    case ::Launcher::GameId::CS2: direct = tryPidNames({"cs2.exe"}); break;
    case ::Launcher::GameId::Warzone: direct = tryPidNames({"cod.exe", "ModernWarfare.exe"}); break;
    case ::Launcher::GameId::Fortnite:
        direct = tryPidNames({"FortniteClient-Win64-Shipping.exe",
            "FortniteClient-Win64-Shipping_EAC_EOS.exe", "Fortnite.exe"});
        break;
    case ::Launcher::GameId::FiveM:
        direct = tryPidNames({"FiveM_GTAProcess.exe", "FiveM_b3258_GTAProcess.exe",
            "FiveM_b3407_GTAProcess.exe", "FiveM_b3570_GTAProcess.exe", "GTAProcess.exe"});
        break;
    default: break;
    }
    if (direct.state == ProcessProbeState::Found)
        return direct;
    const auto processes = mem.GetProcessNames();
    if (processes.empty())
        return {ProcessProbeState::InventoryUnavailable, {}};
    for (const auto& process : processes) {
        const size_t separator = process.find_last_of("\\/");
        const std::string processBase = separator == std::string::npos
            ? process : process.substr(separator + 1);
        const auto matches = [&](const char* name) { return _stricmp(processBase.c_str(), name) == 0; };
        bool found = false;
        switch (selected) {
        case ::Launcher::GameId::CS2: found = matches("cs2.exe"); break;
        case ::Launcher::GameId::Warzone:
            found = matches("cod.exe") || matches("ModernWarfare.exe"); break;
        case ::Launcher::GameId::Fortnite:
            found = matches("FortniteClient-Win64-Shipping.exe") ||
                matches("FortniteClient-Win64-Shipping_EAC_EOS.exe") || matches("Fortnite.exe"); break;
        case ::Launcher::GameId::FiveM: {
            std::string lower = processBase;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            found = lower.find("gtaprocess") != std::string::npos;
            break;
        }
        default: break;
        }
        if (found)
            return {ProcessProbeState::Found, processBase};
    }
    return {ProcessProbeState::NotFound, {}};
    } catch (const std::exception& ex) {
        std::cerr << "[LAUNCHER] Process preflight failed: " << ex.what() << '\n';
    } catch (...) {
        std::cerr << "[LAUNCHER] Process preflight failed unexpectedly.\n";
    }
    return {mem.GetDiagnosticsSnapshot().deviceOpen
        ? ProcessProbeState::InventoryUnavailable : ProcessProbeState::DeviceUnavailable, {}};
}

bool IsProcessPresent(::Launcher::GameId selected) {
    return ProbeProcess(selected).state == ProcessProbeState::Found;
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
