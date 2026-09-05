#pragma once

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
} // namespace OmniGhost::Platform
