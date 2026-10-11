#include "platform/dma_integration_test.h"
#include "platform/session_log.h"
#include "security/security_manager.h"
#include "platform/runtime_bootstrap.h"
#include "platform/app_paths.h"
#include "third_party/DMALibrary/Memory/Memory.h"
#include "src/games/Fivem/game/offsets.h"
#include "globals.h"
#include <iostream>
#include <string>

namespace OmniGhost::Platform {

int RunDmaIntegrationTest(int argc, wchar_t** argv) {
    SessionLog::Initialize();
    if (!Security::SecurityManager::Instance().VerifyHostIdentity()) return 10;

    std::wstring executable;
    unsigned long expectedPid = 0;
    unsigned long long expectedHwnd = 0;

    for (int i = 2; i + 1 < argc; i += 2) {
        const std::wstring key = argv[i];
        const std::wstring value = argv[i + 1];
        if (key == L"--target-exe") executable = value;
        else if (key == L"--target-pid") expectedPid = std::wcstoul(value.c_str(), nullptr, 10);
        else if (key == L"--target-hwnd") expectedHwnd = std::wcstoull(value.c_str(), nullptr, 10);
        else return 2;
    }

    const auto marker = executable.find(L"FiveM_b");
    const auto suffix = executable.find(L"_GTAProcess.exe");
    if (marker != 0 || suffix == std::wstring::npos || suffix <= 7 ||
        expectedPid == 0 || expectedHwnd == 0) return 2;

    const int build = std::wcstol(executable.c_str() + 7, nullptr, 10);
    if (build <= 0) return 2;

    DWORD windowPid = 0;
    GetWindowThreadProcessId(reinterpret_cast<HWND>(static_cast<uintptr_t>(expectedHwnd)), &windowPid);
    if (windowPid != expectedPid) return 3;

    if (FiveM::LoadOffsetsFromJson(nullptr) <= 0) return 4;
    const auto* offsets = FiveM::GetOffsetsForBuild(build);
    if (!offsets || !offsets->world_offset || !offsets->viewport_offset ||
        !offsets->ped_visibility_offset) return 5;

    std::string processName;
    processName.reserve(executable.size());
    for (const wchar_t character : executable) {
        if (character > 0x7f) return 2;
        processName.push_back(static_cast<char>(character));
    }

    if (RuntimeBootstrap::Prepare() == RuntimeBootstrap::Result::Failed) return 6;
    if (!Security::SecurityManager::Instance().VerifyProtectedFiles()) return 11;

    extern Memory mem;
    if (!mem.Init(processName, true, false)) return 6;
    if (mem.GetPidFromName(processName) != expectedPid) return 7;

    const uintptr_t base = mem.GetBaseDaddy(processName);
    uintptr_t world = 0, viewport = 0, localPed = 0;

    if (!base || !mem.Read(base + offsets->world_offset, &world, sizeof(world)) ||
        !mem.Read(base + offsets->viewport_offset, &viewport, sizeof(viewport)) ||
        world < 0x10000 || viewport < 0x10000 ||
        !mem.Read(world + 0x8, &localPed, sizeof(localPed)) || localPed < 0x10000) return 8;

    unsigned char visibleFlag = 0;
    if (!mem.Read(localPed + offsets->ped_visibility_offset, &visibleFlag, sizeof(visibleFlag))) return 9;

    std::clog << "[DMA integration] build=" << build << " pid=" << expectedPid
              << " world/viewport/localPed/visibilityFlag read OK, flag="
              << static_cast<unsigned>(visibleFlag) << '\n';
    return 0;
}

} // namespace OmniGhost::Platform