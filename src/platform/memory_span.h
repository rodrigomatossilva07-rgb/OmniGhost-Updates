#pragma once

#include <span>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string_view>

namespace OmniGhost {








using ByteSpan = std::span<const std::uint8_t>;


using MutableByteSpan = std::span<std::uint8_t>;


using StringSpan = std::span<const char>;


template <typename T>
using ConstSpan = std::span<const T>;


template <typename T>
using MutSpan = std::span<T>;






[[nodiscard]] inline ByteSpan AsBytes(const std::vector<std::uint8_t>& vec) noexcept {
    return ByteSpan(vec.data(), vec.size());
}


[[nodiscard]] inline MutableByteSpan AsMutableBytes(std::vector<std::uint8_t>& vec) noexcept {
    return MutableByteSpan(vec.data(), vec.size());
}


[[nodiscard]] inline ByteSpan AsBytes(std::string_view sv) noexcept {
    return ByteSpan(reinterpret_cast<const std::uint8_t*>(sv.data()), sv.size());
}


[[nodiscard]] inline ByteSpan AsBytes(const std::uint8_t* data, std::size_t size) noexcept {
    return ByteSpan(data, size);
}


[[nodiscard]] inline MutableByteSpan AsMutableBytes(std::uint8_t* data, std::size_t size) noexcept {
    return MutableByteSpan(data, size);
}


template <typename T, std::size_t N>
[[nodiscard]] inline std::span<T, N> AsSpan(T (&arr)[N]) noexcept {
    return std::span<T, N>(arr);
}

template <typename T, std::size_t N>
[[nodiscard]] inline std::span<const T, N> AsSpan(const T (&arr)[N]) noexcept {
    return std::span<const T, N>(arr);
}






template <typename T, std::size_t Extent>
[[nodiscard]] inline std::span<T, Extent> SafeSubspan(std::span<T, Extent> span,
                                                       std::size_t offset,
                                                       std::size_t count = std::dynamic_extent) noexcept {
    if (offset >= span.size()) return {};
    const auto maxCount = span.size() - offset;
    const auto actualCount = (count == std::dynamic_extent || count > maxCount) ? maxCount : count;
    return span.subspan(offset, actualCount);
}


template <typename T, std::size_t Extent>
[[nodiscard]] inline bool IsValid(const std::span<T, Extent>& span) noexcept {
    return !span.empty() && span.data() != nullptr;
}


template <typename T, std::size_t Extent>
[[nodiscard]] inline std::vector<T> ToVector(const std::span<T, Extent>& span) {
    return std::vector<T>(span.begin(), span.end());
}






using ByteView = ByteSpan;


using EmbeddedByteView = ByteSpan;





inline constexpr std::size_t DynamicExtent = std::dynamic_extent;

}
