#pragma once

#include <Windows.h>

namespace OmniGhost::Platform {

struct WindowAudit {
    int totalProductWindows{};
    int currentProcessWindows{};
    int foreignProcessWindows{};
    HWND firstForeign{};
};

WindowAudit AuditOmniGhostWindows() noexcept;
bool ActivateExistingOmniGhostWindow() noexcept;
void LogWindowAudit(const char* stage, HWND expectedWindow = nullptr);

} // namespace OmniGhost::Platform
