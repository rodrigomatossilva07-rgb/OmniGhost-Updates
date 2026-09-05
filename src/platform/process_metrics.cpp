#include "process_metrics.h"
#include "unique_handle.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <TlHelp32.h>
#include <Psapi.h>

#include <sstream>

#pragma comment(lib, "psapi.lib")

namespace OmniGhost::Platform {

ProcessMetrics CaptureProcessMetrics() noexcept {
    ProcessMetrics metrics{};
    const HANDLE process = GetCurrentProcess();

    DWORD handles = 0;
    if (GetProcessHandleCount(process, &handles))
        metrics.handleCount = handles;

    metrics.gdiObjects = GetGuiResources(process, GR_GDIOBJECTS);
    metrics.userObjects = GetGuiResources(process, GR_USEROBJECTS);

    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(
            process,
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
            sizeof(memory))) {
        metrics.workingSetBytes = static_cast<std::uint64_t>(memory.WorkingSetSize);
        metrics.privateBytes = static_cast<std::uint64_t>(memory.PrivateUsage);
    }

    UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot) {
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot.get(), &entry)) {
            const DWORD pid = GetCurrentProcessId();
            do {
                if (entry.th32ProcessID == pid) {
                    metrics.threadCount = entry.cntThreads;
                    break;
                }
            } while (Process32NextW(snapshot.get(), &entry));
        }
    }

    return metrics;
}

std::string FormatProcessMetrics(const ProcessMetrics& metrics) {
    std::ostringstream out;
    out << "handles=" << metrics.handleCount
        << " threads=" << metrics.threadCount
        << " gdi=" << metrics.gdiObjects
        << " user=" << metrics.userObjects
        << " working_set=" << metrics.workingSetBytes
        << " private=" << metrics.privateBytes;
    return out.str();
}

} // namespace OmniGhost::Platform
