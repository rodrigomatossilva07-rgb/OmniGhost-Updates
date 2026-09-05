#pragma once

#include <cstdint>
#include <string>

namespace OmniGhost::Platform {

struct ProcessMetrics {
    std::uint32_t handleCount{};
    std::uint32_t threadCount{};
    std::uint32_t gdiObjects{};
    std::uint32_t userObjects{};
    std::uint64_t workingSetBytes{};
    std::uint64_t privateBytes{};
};

[[nodiscard]] ProcessMetrics CaptureProcessMetrics() noexcept;
[[nodiscard]] std::string FormatProcessMetrics(const ProcessMetrics& metrics);

} // namespace OmniGhost::Platform
