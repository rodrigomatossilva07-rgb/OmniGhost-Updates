#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

namespace OmniGhost::Platform {

struct ResourceSnapshot {
    std::uint32_t threadCount{};
    std::uint32_t handleCount{};
    std::uint64_t workingSetBytes{};
    std::uint64_t privateBytes{};
    std::chrono::steady_clock::time_point timestamp{};
};

class ResourceCounter final {
public:
    ResourceCounter() = default;
    ~ResourceCounter() = default;

    ResourceCounter(const ResourceCounter&) = delete;
    ResourceCounter& operator=(const ResourceCounter&) = delete;

    void StartSession(std::string_view sessionName) noexcept {
#if defined(OMNIGHOST_PRIVATE_STATIC_VMM)
        sessionName_ = sessionName;
        before_ = Capture();
        enabled_ = true;
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Info,
            OmniGhost::SessionLog::Subsystem::Core,
            "resource counter session started",
            {{"session", std::string(sessionName), false},
             {"threads_before", std::to_string(before_.threadCount), false},
             {"handles_before", std::to_string(before_.handleCount), false},
             {"working_set_before", std::to_string(before_.workingSetBytes), false},
             {"private_bytes_before", std::to_string(before_.privateBytes), false}});
#endif
    }

    void EndSession() noexcept {
#if defined(OMNIGHOST_PRIVATE_STATIC_VMM)
        if (!enabled_) return;
        after_ = Capture();
        enabled_ = false;

        const auto threadDelta = static_cast<int>(after_.threadCount) - static_cast<int>(before_.threadCount);
        const auto handleDelta = static_cast<int>(after_.handleCount) - static_cast<int>(before_.handleCount);
        const auto wsDelta = static_cast<long long>(after_.workingSetBytes) - static_cast<long long>(before_.workingSetBytes);
        const auto privDelta = static_cast<long long>(after_.privateBytes) - static_cast<long long>(before_.privateBytes);

        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Info,
            OmniGhost::SessionLog::Subsystem::Core,
            "resource counter session ended",
            {{"session", std::string(sessionName_), false},
             {"threads_before", std::to_string(before_.threadCount), false},
             {"threads_after", std::to_string(after_.threadCount), false},
             {"threads_delta", std::to_string(threadDelta), false},
             {"handles_before", std::to_string(before_.handleCount), false},
             {"handles_after", std::to_string(after_.handleCount), false},
             {"handles_delta", std::to_string(handleDelta), false},
             {"working_set_delta_bytes", std::to_string(wsDelta), false},
             {"private_bytes_delta", std::to_string(privDelta), false},
             {"leaked_threads", (threadDelta > 0 ? "YES" : "NO"), false},
             {"leaked_handles", (handleDelta > 0 ? "YES" : "NO"), false}});

        // Also log to console for immediate visibility
        if (threadDelta != 0 || handleDelta != 0) {
            std::clog << "[RESOURCE] session=" << sessionName_
                      << " threads=" << before_.threadCount << "->" << after_.threadCount << " (" << (threadDelta >= 0 ? "+" : "") << threadDelta << ")"
                      << " handles=" << before_.handleCount << "->" << after_.handleCount << " (" << (handleDelta >= 0 ? "+" : "") << handleDelta << ")"
                      << " WS=" << (wsDelta >= 0 ? "+" : "") << wsDelta
                      << " Priv=" << (privDelta >= 0 ? "+" : "") << privDelta << "\n";
        }
#endif
    }

    [[nodiscard]] bool Enabled() const noexcept { return enabled_; }

private:
    static ResourceSnapshot Capture() noexcept {
        ResourceSnapshot snapshot{};
        const HANDLE process = GetCurrentProcess();

        DWORD handles = 0;
        if (GetProcessHandleCount(process, &handles))
            snapshot.handleCount = handles;

        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        if (GetProcessMemoryInfo(
                process,
                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                sizeof(memory))) {
            snapshot.workingSetBytes = static_cast<std::uint64_t>(memory.WorkingSetSize);
            snapshot.privateBytes = static_cast<std::uint64_t>(memory.PrivateUsage);
        }

        HANDLE snapshotHandle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshotHandle != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            if (Process32FirstW(snapshotHandle, &entry)) {
                const DWORD pid = GetCurrentProcessId();
                do {
                    if (entry.th32ProcessID == pid) {
                        snapshot.threadCount = entry.cntThreads;
                        break;
                    }
                } while (Process32NextW(snapshotHandle, &entry));
            }
            CloseHandle(snapshotHandle);
        }

        snapshot.timestamp = std::chrono::steady_clock::now();
        return snapshot;
    }

    bool enabled_{false};
    std::string sessionName_;
    ResourceSnapshot before_{};
    ResourceSnapshot after_{};
};

// Global instance for convenient access
inline ResourceCounter g_resourceCounter;

} // namespace OmniGhost::Platform
