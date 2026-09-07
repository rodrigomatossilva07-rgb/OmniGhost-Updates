#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace OmniGhost::Platform {
[[nodiscard]] bool Sha256File(const std::filesystem::path& file,
                              std::string& hexDigest,
                              std::string& error);
[[nodiscard]] bool Sha256Text(std::string_view text,
                              std::string& hexDigest,
                              std::string& error);
[[nodiscard]] bool ConstantTimeEquals(std::string_view left, std::string_view right) noexcept;

// Convert raw 32-byte digest to lowercase hex string.
[[nodiscard]] inline std::string DigestHex(const std::array<std::uint8_t, 32>& digest) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out(64, '\0');
    for (std::size_t i = 0; i < 32; ++i) {
        out[i * 2]     = kHex[(digest[i] >> 4) & 0xF];
        out[i * 2 + 1] = kHex[digest[i] & 0xF];
    }
    return out;
}

} // namespace OmniGhost::Platform
