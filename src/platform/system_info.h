#pragma once

#include <cstdint>
#include <string>

namespace OmniGhost::Platform {

struct SystemInfo {
    std::uint32_t windowsMajor{};
    std::uint32_t windowsMinor{};
    std::uint32_t windowsBuild{};
    std::string processArchitecture;
    std::string nativeArchitecture;
};

[[nodiscard]] SystemInfo CaptureSystemInfo() noexcept;
[[nodiscard]] std::string FormatSystemInfo(const SystemInfo& info);

} // namespace OmniGhost::Platform
