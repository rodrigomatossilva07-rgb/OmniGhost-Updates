#pragma once
#include <filesystem>
#include <cstddef>
#include <string>
#include <string_view>

namespace OmniGhost::Update {
bool Sha256File(const std::filesystem::path& file, std::string& hexDigest, std::string& error);
bool ConstantTimeEquals(const std::string& left, const std::string& right);

bool RandomTokenHex(std::size_t byteCount, std::wstring& token, std::string& error);
}
