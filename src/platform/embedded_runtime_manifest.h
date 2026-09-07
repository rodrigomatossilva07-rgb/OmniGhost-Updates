#pragma once
#ifndef OMNIGHOST_EMBEDDED_RUNTIME_MANIFEST_STUB
#define OMNIGHOST_EMBEDDED_RUNTIME_MANIFEST_STUB

#include <array>
#include <cstddef>
#include <cstdint>

namespace OmniGhost {
namespace EmbeddedRuntimeGenerated {

struct Entry {
    const wchar_t* relativePath = L"";
    const char* logicalName = "";
    std::size_t size = 0;
    std::uint32_t resourceId = 0;
    std::array<std::uint8_t, 32> sha256{};
    const std::uint8_t* data = nullptr;
};

// Empty table — populated by build-time generator when available.
// Zero-length array avoids iterating a dummy entry.
inline constexpr Entry kEntries[1] = {};
inline constexpr std::size_t kEntryCount = 0;
inline constexpr std::uint32_t kManifestVersion = 0;

} // namespace EmbeddedRuntimeGenerated

namespace EmbeddedRuntime {
inline constexpr std::uint32_t kManifestVersion = 0;
inline constexpr std::size_t kEmbeddedFileCount = 0;
}

} // namespace OmniGhost

#endif
