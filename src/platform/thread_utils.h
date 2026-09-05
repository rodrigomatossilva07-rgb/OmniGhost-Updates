#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <cwchar>
#include <string_view>

namespace OmniGhost::Platform {

inline void SetCurrentThreadName(std::wstring_view name) noexcept {
    if (name.empty())
        return;

    using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    static const auto fn = reinterpret_cast<SetThreadDescriptionFn>(
        GetProcAddress(GetModuleHandleW(L"Kernel32.dll"), "SetThreadDescription"));
    if (!fn)
        return;

    // std::wstring_view is not required to be NUL-terminated.
    wchar_t buffer[64]{};
    constexpr size_t capacity = sizeof(buffer) / sizeof(buffer[0]);
    const size_t count = (std::min)(name.size(), capacity - 1);
    if (count == 0)
        return;

    std::wmemcpy(buffer, name.data(), count);
    buffer[count] = L'\0';
    (void)fn(GetCurrentThread(), buffer);
}

} // namespace OmniGhost::Platform
