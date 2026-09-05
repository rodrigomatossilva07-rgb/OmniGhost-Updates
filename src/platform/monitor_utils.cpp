#include "monitor_utils.h"

#include <algorithm>

namespace OmniGhost::Platform {
namespace {

BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto* monitors = reinterpret_cast<std::vector<MonitorInfo>*>(data);
    if (!monitors) return FALSE;

    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info))
        return TRUE;

    MonitorInfo entry{};
    entry.handle = monitor;
    entry.rect = info.rcMonitor;
    entry.work = info.rcWork;
    entry.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    entry.deviceName = info.szDevice;
    monitors->push_back(entry);
    return TRUE;
}

} // namespace

std::vector<MonitorInfo> EnumerateMonitors() {
    std::vector<MonitorInfo> monitors;
    EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitors));

    // Stable desktop order makes the persisted index predictable: primary first,
    // then left-to-right/top-to-bottom by virtual desktop coordinates.
    std::stable_sort(monitors.begin(), monitors.end(), [](const MonitorInfo& a, const MonitorInfo& b) {
        if (a.primary != b.primary) return a.primary > b.primary;
        if (a.rect.left != b.rect.left) return a.rect.left < b.rect.left;
        return a.rect.top < b.rect.top;
    });
    return monitors;
}

int MonitorIndexFromHandle(HMONITOR handle) {
    if (!handle) return -1;
    const auto monitors = EnumerateMonitors();
    for (std::size_t i = 0; i < monitors.size(); ++i) {
        if (monitors[i].handle == handle)
            return static_cast<int>(i);
    }
    return -1;
}

MonitorInfo SelectMonitor(int requestedIndex, HWND currentWindow) {
    const auto monitors = EnumerateMonitors();
    if (monitors.empty()) {
        MonitorInfo fallback{};
        fallback.rect = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        fallback.work = fallback.rect;
        fallback.primary = true;
        fallback.deviceName = L"Primary";
        return fallback;
    }

    if (requestedIndex >= 0 && requestedIndex < static_cast<int>(monitors.size()))
        return monitors[requestedIndex];

    if (currentWindow) {
        const HMONITOR current = MonitorFromWindow(currentWindow, MONITOR_DEFAULTTONEAREST);
        for (const auto& monitor : monitors) {
            if (monitor.handle == current)
                return monitor;
        }
    }

    for (const auto& monitor : monitors) {
        if (monitor.primary)
            return monitor;
    }
    return monitors.front();
}

float DpiScaleForWindow(HWND hwnd) {
    if (!hwnd) return 1.0f;
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    const UINT dpi = getDpiForWindow ? getDpiForWindow(hwnd) : 96U;
    return std::clamp(static_cast<float>(dpi) / 96.0f, 0.75f, 3.0f);
}

} // namespace OmniGhost::Platform
