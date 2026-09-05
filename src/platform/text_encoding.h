#pragma once

#include <string>
#include <string_view>

namespace OmniGhost::Platform {

[[nodiscard]] std::wstring Utf8ToWide(std::string_view text) noexcept;
[[nodiscard]] std::string WideToUtf8(std::wstring_view text) noexcept;

} // namespace OmniGhost::Platform
