#include "text_encoding.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <limits>

namespace OmniGhost::Platform {
namespace {

template <typename Size>
bool FitsWin32Length(Size size) noexcept {
    return size <= static_cast<Size>((std::numeric_limits<int>::max)());
}

} // namespace

std::wstring Utf8ToWide(std::string_view text) noexcept {
    if (text.empty())
        return {};
    if (!FitsWin32Length(text.size()))
        return {};

    const int inputLength = static_cast<int>(text.size());
    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        inputLength,
        nullptr,
        0);
    if (required <= 0)
        return {};

    std::wstring result(static_cast<size_t>(required), L'\0');
    const int converted = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        inputLength,
        result.data(),
        required);
    if (converted != required)
        return {};

    return result;
}

std::string WideToUtf8(std::wstring_view text) noexcept {
    if (text.empty())
        return {};
    if (!FitsWin32Length(text.size()))
        return {};

    const int inputLength = static_cast<int>(text.size());
    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text.data(),
        inputLength,
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0)
        return {};

    std::string result(static_cast<size_t>(required), '\0');
    const int converted = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text.data(),
        inputLength,
        result.data(),
        required,
        nullptr,
        nullptr);
    if (converted != required)
        return {};

    return result;
}

} // namespace OmniGhost::Platform
