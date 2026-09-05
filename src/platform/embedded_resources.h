#pragma once

#include "embedded_resource_catalog.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace OmniGhost {

class EmbeddedByteView {
public:
    constexpr EmbeddedByteView() noexcept = default;
    constexpr EmbeddedByteView(const std::uint8_t* data, std::size_t size) noexcept
        : data_(data), size_(size) {}
    constexpr const std::uint8_t* data() const noexcept { return data_; }
    constexpr std::size_t size() const noexcept { return size_; }
    constexpr bool empty() const noexcept { return size_ == 0; }
    constexpr const std::uint8_t* begin() const noexcept { return data_; }
    constexpr const std::uint8_t* end() const noexcept { return data_ + size_; }
    constexpr EmbeddedByteView subview(std::size_t offset, std::size_t count) const noexcept {
        return offset <= size_ && count <= size_ - offset
            ? EmbeddedByteView(data_ + offset, count) : EmbeddedByteView{};
    }
private:
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
};

enum class EmbeddedCompression : std::uint8_t { None = 0, PackBits = 1 };

struct EmbeddedResourceDiagnostics {
    bool found = false;
    bool headerValid = false;
    bool sizeValid = false;
    bool decompressed = false;
    bool integrityValid = false;
    std::size_t resourceSize = 0;
    std::string error;
};

// The only Win32 PE-resource access point in first-party code. Offset and
// private DLL bootstrap loaders also use this view instead of calling Win32 directly.
[[nodiscard]] std::optional<EmbeddedByteView> GetPeResourceView(int resourceId) noexcept;

[[nodiscard]] const EmbeddedResourceDescriptor* FindEmbeddedResource(
    EmbeddedResourceId id) noexcept;
[[nodiscard]] const EmbeddedResourceDescriptor* FindEmbeddedResource(
    std::string_view logicalName) noexcept;

// Pure, disk-free parser used by runtime and tests.
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

} // namespace OmniGhost
