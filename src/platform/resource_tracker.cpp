#include "resource_tracker.h"
#include "session_log.h"
#include <Windows.h>
#include <Psapi.h>
#include <TlHelp32.h>

#pragma comment(lib, "Psapi.lib")

namespace OmniGhost::Platform {

ResourceTracker::Snapshot ResourceTracker::TakeSnapshot() noexcept {
    Snapshot snap;
    snap.timestamp = std::chrono::steady_clock::now();

    // Get current process handle
    HANDLE hProcess = GetCurrentProcess();

    // Thread count
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te{};
        te.dwSize = sizeof(THREADENTRY32);
        if (Thread32First(snapshot, &te)) {
            do {
                if (te.th32OwnerProcessID == GetCurrentProcessId()) {
                    snap.threadCount++;
                }
            } while (Thread32Next(snapshot, &te));
        }
        CloseHandle(snapshot);
    }

    // Handle count
    DWORD handleCount = 0;
    if (GetProcessHandleCount(GetCurrentProcess(), &handleCount)) {
        snap.handleCount = handleCount;
    }

    // Memory usage
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        snap.memoryBytes = pmc.WorkingSetSize;
    }

    // GDI objects
    DWORD gdiCount = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    snap.gdiObjects = gdiCount;

    // User objects
    DWORD userCount = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    snap.userObjects = userCount;

    return snap;
}

void ResourceTracker::LogUsage(std::string_view context) noexcept {
    Snapshot snap = TakeSnapshot();
    SessionLog::Write(
        SessionLog::Severity::Debug,
        SessionLog::Subsystem::Core,
        "Resource usage: " + std::string(context),
        {
            {"threads", std::to_string(snap.threadCount), false},
            {"handles", std::to_string(snap.handleCount), false},
            {"memory_mb", std::to_string(snap.memoryBytes / (1024 * 1024)), false},
            {"gdi_objects", std::to_string(snap.gdiObjects), false},
            {"user_objects", std::to_string(snap.userObjects), false},
            {"context", std::string(context), false}
        });
    }

    // Also log delta if baseline exists
    if (auto delta = GetDelta()) {
        SessionLog::Write(
            SessionLog::Severity::Debug,
            SessionLog::Subsystem::Core,
            "Resource delta: " + std::string(context),
            {
                {"thread_delta", std::to_string(delta->threadDelta), false},
                {"handle_delta", std::to_string(delta->handleDelta), false},
                {"memory_delta_mb", std::to_string(delta->memoryDelta / (1024 * 1024)), false},
                {"gdi_delta", std::to_string(delta->gdiDelta), false},
                {"user_delta", std::to_string(delta->userDelta), false},
                {"has_leak", delta->hasLeak ? "true" : "false", false}
            );

            if (delta->hasLeak) {
                SessionLog::Write(
                    SessionLog::Severity::Warning,
                    SessionLog::Subsystem::Core,
                    "Potential resource leak detected",
                    {
                        {"thread_delta", std::to_string(delta->threadDelta), false},
                        {"handle_delta", std::to_string(delta->handleDelta), false},
                        {"memory_delta_mb", std::to_string(delta->memoryDelta / (1024 * 1024)), false}
                    });
            }
        }
    }
}

} // namespace OmniGhost::Platform