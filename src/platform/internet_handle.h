#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winhttp.h>
#include <wininet.h>

#include <memory>

namespace OmniGhost::Platform {

template <auto CloseFunction>
struct InternetHandleCloser {
    void operator()(void* handle) const noexcept {
        if (handle != nullptr)
            (void)CloseFunction(static_cast<HINTERNET>(handle));
    }
};

template <auto CloseFunction>
using BasicInternetHandle = std::unique_ptr<void, InternetHandleCloser<CloseFunction>>;

using UniqueWinHttpHandle = BasicInternetHandle<&WinHttpCloseHandle>;
using UniqueWinInetHandle = BasicInternetHandle<&InternetCloseHandle>;

[[nodiscard]] inline UniqueWinHttpHandle MakeWinHttpHandle(HINTERNET handle) noexcept {
    return UniqueWinHttpHandle(handle);
}

[[nodiscard]] inline UniqueWinInetHandle MakeWinInetHandle(HINTERNET handle) noexcept {
    return UniqueWinInetHandle(handle);
}

} // namespace OmniGhost::Platform
