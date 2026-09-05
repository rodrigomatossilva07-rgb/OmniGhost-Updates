#pragma once

#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace OmniGhost::SessionLog {

enum class Severity {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical
};

enum class Subsystem {
    Core,
    Auth,
    Platform,
    DMA,
    VMM,
    LeechCore,
    Ftdi,
    Pnp,
    ProcInfo,
    Process,
    Runtime,
    UI,
    Updater,
    Radar,
    Input,
    Config,
    Adapter,
    Read,
    Watchdog
};

struct Field {
    std::string key;
    std::string value;
    bool sensitive{};
};

// Mirrors std::cout/std::cerr/std::clog to the per-user log directory while
// preserving the visible console output. Structured events are serialized into
// the same bounded logs.txt file; no logging sidecar is created.
bool Initialize();
void SetStartReason(std::string_view reason);
void Shutdown();
void Flush();

void Write(Severity severity,
           Subsystem subsystem,
           std::string_view message,
           std::initializer_list<Field> fields = {});

[[nodiscard]] std::filesystem::path CurrentLogPath();
[[nodiscard]] std::filesystem::path CurrentStructuredLogPath();
[[nodiscard]] std::string CurrentSessionId();
[[nodiscard]] bool FileWasTruncated();
[[nodiscard]] std::vector<std::string> RecentEvents(std::size_t maximum = 64);
[[nodiscard]] std::string LastErrorId();

// Removes common per-user path material and control characters before support
// data is exported. This is deliberately conservative; callers must still avoid
// passing secrets such as licence keys or authentication tokens.
[[nodiscard]] std::string RedactForSupport(std::string_view text);

[[nodiscard]] const char* SeverityName(Severity severity) noexcept;
[[nodiscard]] const char* SubsystemName(Subsystem subsystem) noexcept;

} // namespace OmniGhost::SessionLog
