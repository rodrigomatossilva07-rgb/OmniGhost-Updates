#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if __has_include("embedded_resource_catalog.h")
#include "embedded_resource_catalog.h"
#else


namespace OmniGhost {
enum class EmbeddedResourceId : std::uint16_t { Unavailable = 0 };
struct EmbeddedResourceDescriptor {
    EmbeddedResourceId id;
    const char* logicalName;
    std::uint32_t originalSize;
    std::uint32_t storedSize;
    bool mandatory;
};
inline constexpr std::array<EmbeddedResourceDescriptor, 0> kEmbeddedResourceCatalog{};
}
#endif

#include "memory_span.h"

namespace OmniGhost {


using EmbeddedByteView = OmniGhost::ByteSpan;
using MutableEmbeddedByteView = OmniGhost::MutableByteSpan;


[[nodiscard]] inline EmbeddedByteView MakeEmbeddedByteView(const std::vector<std::uint8_t>& data) noexcept {
    return OmniGhost::AsBytes(data);
}

[[nodiscard]] inline EmbeddedByteView MakeEmbeddedByteView(const std::uint8_t* data, std::size_t size) noexcept {
    return OmniGhost::AsBytes(data, size);
}

template <std::size_t N>
[[nodiscard]] inline EmbeddedByteView MakeEmbeddedByteView(const std::uint8_t (&data)[N]) noexcept {
    return OmniGhost::AsSpan(data);
}

enum class EmbeddedCompression : std::uint8_t { None = 0, PackBits = 1, Lzms = 2 };

struct EmbeddedResourceDiagnostics {
    bool found = false;
    bool headerValid = false;
    bool sizeValid = false;
    bool decompressed = false;
    bool integrityValid = false;
    std::size_t resourceSize = 0;
    std::string error;
};



[[nodiscard]] std::optional<EmbeddedByteView> GetPeResourceView(int resourceId) noexcept;

[[nodiscard]] const EmbeddedResourceDescriptor* FindEmbeddedResource(
    EmbeddedResourceId id) noexcept;
[[nodiscard]] const EmbeddedResourceDescriptor* FindEmbeddedResource(
    std::string_view logicalName) noexcept;


[[nodiscard]] bool ParseEmbeddedResourceBlob(
    EmbeddedByteView blob,
    std::vector<std::uint8_t>& output,
    EmbeddedResourceDiagnostics& diagnostics);

[[nodiscard]] std::optional<std::vector<std::uint8_t>> LoadEmbeddedResource(
    EmbeddedResourceId id,
    EmbeddedResourceDiagnostics* diagnostics = nullptr);
[[nodiscard]] std::optional<std::vector<std::uint8_t>> LoadEmbeddedResource(
    std::string_view logicalName,
    EmbeddedResourceDiagnostics* diagnostics = nullptr);

}
