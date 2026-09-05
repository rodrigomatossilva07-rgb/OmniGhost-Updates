#include "window_instance_guard.h"

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <iterator>

namespace OmniGhost::Platform {
namespace {
bool ProcessImageIsOmniGhost(DWORD pid) noexcept {
    if (pid == 0 || pid == GetCurrentProcessId()) return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    wchar_t path[32768]{};
    DWORD chars = static_cast<DWORD>(std::size(path));
    const BOOL ok = QueryFullProcessImageNameW(process, 0, path, &chars);
    CloseHandle(process);
    if (!ok || chars == 0) return false;
    const wchar_t* filename = path;
    for (DWORD i = 0; i < chars; ++i)
        if (path[i] == L'\\' || path[i] == L'/') filename = path + i + 1;
    return _wcsicmp(filename, L"OmniGhost.exe") == 0;
}
} // namespace

WindowAudit AuditOmniGhostWindows() noexcept {
    struct Context { WindowAudit audit{}; DWORD currentPid{}; }
        context{{}, GetCurrentProcessId()};
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        auto* ctx = reinterpret_cast<Context*>(parameter);
        if (!ctx || !IsWindow(window)) return TRUE;
        DWORD ownerPid = 0;
        GetWindowThreadProcessId(window, &ownerPid);
        if (ownerPid == 0) return TRUE;
        wchar_t className[128]{};
        const int classChars = GetClassNameW(window, className,
            static_cast<int>(std::size(className)));
        const bool classMatch = classChars > 0 &&
            _wcsicmp(className, L"OmniGhostOverlayClass") == 0;
        const bool processMatch = ownerPid != ctx->currentPid &&
            ProcessImageIsOmniGhost(ownerPid);
        if (!classMatch && !processMatch) return TRUE;
        ++ctx->audit.totalProductWindows;
        if (ownerPid == ctx->currentPid) ++ctx->audit.currentProcessWindows;
        else {
            ++ctx->audit.foreignProcessWindows;
            if (!ctx->audit.firstForeign) ctx->audit.firstForeign = window;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&context));
    return context.audit;
}

bool ActivateExistingOmniGhostWindow() noexcept {
    const WindowAudit audit = AuditOmniGhostWindows();
    HWND existing = audit.firstForeign;
    if (!existing) return false;
    ShowWindowAsync(existing, IsIconic(existing) ? SW_RESTORE : SW_SHOW);
    SetWindowPos(existing, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_ASYNCWINDOWPOS);
    SetForegroundWindow(existing);
    return true;
}

void LogWindowAudit(const char* stage, HWND expectedWindow) {
    const WindowAudit audit = AuditOmniGhostWindows();
    std::clog << "[WINDOW][Audit] stage=" << (stage ? stage : "unknown")
              << " pid=" << GetCurrentProcessId()
              << " expected_hwnd=0x" << std::hex
              << reinterpret_cast<std::uintptr_t>(expectedWindow) << std::dec
              << " product_windows=" << audit.totalProductWindows
              << " current_process=" << audit.currentProcessWindows
              << " foreign_process=" << audit.foreignProcessWindows << '\n';
}

} // namespace OmniGhost::Platform
