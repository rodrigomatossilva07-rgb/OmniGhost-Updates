#pragma once

#include <span>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string_view>

namespace OmniGhost {

// =============================================================================
// Modern Memory Views using std::span (C++20)
// =============================================================================
// Replaces custom ByteView/EmbeddedByteView with standard std::span
// Provides bounds-safe, non-owning views over contiguous memory

// Read-only byte span (equivalent to ByteView)
using ByteSpan = std::span<const std::uint8_t>;

// Writable byte span
using MutableByteSpan = std::span<std::uint8_t>;

// String span for text data
using StringSpan = std::span<const char>;

// Generic const span
template <typename T>
using ConstSpan = std::span<const T>;

// Generic mutable span
template <typename T>
using MutSpan = std::span<T>;

// =============================================================================
// Helper functions for creating spans
// =============================================================================

// Create ByteSpan from vector
[[nodiscard]] inline ByteSpan AsBytes(const std::vector<std::uint8_t>& vec) noexcept {
    return ByteSpan(vec.data(), vec.size());
}

// Create MutableByteSpan from vector
[[nodiscard]] inline MutableByteSpan AsMutableBytes(std::vector<std::uint8_t>& vec) noexcept {
    return MutableByteSpan(vec.data(), vec.size());
}

// Create ByteSpan from string_view
[[nodiscard]] inline ByteSpan AsBytes(std::string_view sv) noexcept {
    return ByteSpan(reinterpret_cast<const std::uint8_t*>(sv.data()), sv.size());
}

// Create ByteSpan from raw pointer + size (for legacy API compatibility)
[[nodiscard]] inline ByteSpan AsBytes(const std::uint8_t* data, std::size_t size) noexcept {
    return ByteSpan(data, size);
}

// Create MutableByteSpan from raw pointer + size
[[nodiscard]] inline MutableByteSpan AsMutableBytes(std::uint8_t* data, std::size_t size) noexcept {
    return MutableByteSpan(data, size);
}

// Create span from fixed array
template <typename T, std::size_t N>
[[nodiscard]] inline std::span<T, N> AsSpan(T (&arr)[N]) noexcept {
    return std::span<T, N>(arr);
}

template <typename T, std::size_t N>
[[nodiscard]] inline std::span<const T, N> AsSpan(const T (&arr)[N]) noexcept {
    return std::span<const T, N>(arr);
}

// =============================================================================
// Span utilities
// =============================================================================

// Safe subspan with bounds checking
template <typename T, std::size_t Extent>
[[nodiscard]] inline std::span<T, Extent> SafeSubspan(std::span<T, Extent> span,
                                                       std::size_t offset,
                                                       std::size_t count = std::dynamic_extent) noexcept {
    if (offset >= span.size()) return {};
    const auto maxCount = span.size() - offset;
    const auto actualCount = (count == std::dynamic_extent || count > maxCount) ? maxCount : count;
    return span.subspan(offset, actualCount);
}

// Check if span is valid (non-empty and data not null)
template <typename T, std::size_t Extent>
[[nodiscard]] inline bool IsValid(const std::span<T, Extent>& span) noexcept {
    return !span.empty() && span.data() != nullptr;
}

// Copy span to vector
template <typename T, std::size_t Extent>
[[nodiscard]] inline std::vector<T> ToVector(const std::span<T, Extent>& span) {
    return std::vector<T>(span.begin(), span.end());
}

// =============================================================================
// Compatibility aliases for existing code
// =============================================================================

// Deprecated: Use ByteSpan instead
using ByteView = ByteSpan;

// Deprecated: Use MutableByteSpan instead
using EmbeddedByteView = ByteSpan;

// =============================================================================
// Constants
// =============================================================================

inline constexpr std::size_t DynamicExtent = std::dynamic_extent;

} // namespace OmniGhost
