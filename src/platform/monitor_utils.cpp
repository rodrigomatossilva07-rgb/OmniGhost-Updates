#include "monitor_utils.h"
#include <algorithm>
#include <shellscalingapi.h>

#pragma comment(lib, "Shcore.lib")

namespace OmniGhost::Platform {
namespace {

    static OverlayPosition g_overlay_position;
    static bool g_position_initialized = false;

    void InitializePosition() {
        if (g_position_initialized) return;
        g_overlay_position = OverlayPosition{};
        g_overlay_position.mode = OverlayPositionMode::Fullscreen;
        g_overlay_position.monitor_index = -1;
        g_overlay_position.scale = 1.0f;
        g_overlay_position.lock_to_monitor = true;
        g_position_initialized = true;
    }

    MONITORINFOEXW GetMonitorInfoEx(HMONITOR monitor) {
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        GetMonitorInfoW(monitor, &info);
        return info;
    }

    int GetMonitorDPI(HMONITOR monitor) {
        using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        static const auto getDpiForMonitor = reinterpret_cast<GetDpiForMonitorFn>(
            GetProcAddress(GetModuleHandleW(L"shcore.dll"), "GetDpiForMonitor"));
        
        UINT dpiX = 96, dpiY = 96;
        if (getDpiForMonitor) {
            getDpiForMonitor(monitor, 0, &dpiX, &dpiY); // MDT_EFFECTIVE_DPI = 0
        }
        return static_cast<int>(dpiX);
    }

    void UpdateMonitorInfo(MonitorInfo& info) {
        info.dpi = GetMonitorDPI(info.handle);
        info.scale = std::clamp(static_cast<float>(info.dpi) / 96.0f, 0.75f, 3.0f);
    }

} // namespace

std::vector<MonitorInfo> EnumerateMonitors() {
    std::vector<MonitorInfo> monitors;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        auto* monitors = reinterpret_cast<std::vector<MonitorInfo>*>(data);
        if (!monitors) return FALSE;

        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info)) return TRUE;

        MonitorInfo entry{};
        entry.handle = monitor;
        entry.rect = info.rcMonitor;
        entry.work = info.rcWork;
        entry.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        entry.deviceName = info.szDevice;
        
        // Get DPI
        using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        static const auto getDpiForMonitor = reinterpret_cast<GetDpiForMonitorFn>(
            GetProcAddress(GetModuleHandleW(L"shcore.dll"), "GetDpiForMonitor"));
        UINT dpiX = 96, dpiY = 96;
        if (getDpiForMonitor) {
            getDpiForMonitor(monitor, 0, &dpiX, &dpiY);
        }
        entry.dpi = static_cast<int>(dpiX);
        entry.scale = std::clamp(static_cast<float>(dpiX) / 96.0f, 0.75f, 3.0f);
        
        monitors->push_back(entry);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&monitors));

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
        fallback.dpi = 96;
        fallback.scale = 1.0f;
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

MonitorInfo GetMonitorFromPoint(POINT pt) {
    const HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    const auto monitors = EnumerateMonitors();
    for (const auto& m : monitors) {
        if (m.handle == monitor) return m;
    }
    return SelectMonitor(-1, nullptr);
}

MonitorInfo GetMonitorFromRect(const RECT& rect) {
    POINT center{ (rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2 };
    return GetMonitorFromPoint(center);
}

float DpiScaleForWindow(HWND hwnd) {
    if (!hwnd) return 1.0f;
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    const UINT dpi = getDpiForWindow ? getDpiForWindow(hwnd) : 96U;
    return std::clamp(static_cast<float>(dpi) / 96.0f, 0.75f, 3.0f);
}

float DpiScaleForMonitor(const MonitorInfo& monitor) {
    return monitor.scale;
}

OverlayPosition GetOverlayPosition() {
    InitializePosition();
    return g_overlay_position;
}

void SetOverlayPosition(const OverlayPosition& pos) {
    InitializePosition();
    g_overlay_position = pos;
}

RECT CalculateOverlayRect(const OverlayPosition& pos, const MonitorInfo& monitor) {
    RECT result = monitor.rect;
    
    switch (pos.mode) {
        case OverlayPositionMode::Fullscreen:
            result = monitor.rect;
            break;
            
        case OverlayPositionMode::Windowed:
            if (pos.custom_rect.right > pos.custom_rect.left && 
                pos.custom_rect.bottom > pos.custom_rect.top) {
                result = pos.custom_rect;
            } else {
                // Default to centered 80% of monitor
                int w = monitor.rect.right - monitor.rect.left;
                int h = monitor.rect.bottom - monitor.rect.top;
                int cw = static_cast<int>(w * 0.8f);
                int ch = static_cast<int>(h * 0.8f);
                result.left = monitor.rect.left + (w - cw) / 2;
                result.top = monitor.rect.top + (h - ch) / 2;
                result.right = result.left + cw;
                result.bottom = result.top + ch;
            }
            break;
            
        case OverlayPositionMode::Centered:
            {
                int w = monitor.rect.right - monitor.rect.left;
                int h = monitor.rect.bottom - monitor.rect.top;
                int cw = static_cast<int>(w * 0.9f);
                int ch = static_cast<int>(h * 0.9f);
                result.left = monitor.rect.left + (w - cw) / 2;
                result.top = monitor.rect.top + (h - ch) / 2;
                result.right = result.left + cw;
                result.bottom = result.top + ch;
            }
            break;
            
        case OverlayPositionMode::Custom:
            if (pos.custom_rect.right > pos.custom_rect.left && 
                pos.custom_rect.bottom > pos.custom_rect.top) {
                result = pos.custom_rect;
            }
            break;
    }
    
    return result;
}

bool IsPositionValid(const OverlayPosition& pos, const MonitorInfo& monitor) {
    if (pos.monitor_index >= 0) {
        const auto monitors = EnumerateMonitors();
        if (pos.monitor_index >= static_cast<int>(monitors.size())) return false;
    }
    
    if (pos.mode == OverlayPositionMode::Custom || pos.mode == OverlayPositionMode::Windowed) {
        if (pos.custom_rect.right <= pos.custom_rect.left || 
            pos.custom_rect.bottom <= pos.custom_rect.top) return false;
        
        // Check if rect is within monitor bounds (with some tolerance)
        const int tolerance = 100;
        if (pos.custom_rect.left < monitor.rect.left - tolerance ||
            pos.custom_rect.top < monitor.rect.top - tolerance ||
            pos.custom_rect.right > monitor.rect.right + tolerance ||
            pos.custom_rect.bottom > monitor.rect.bottom + tolerance) {
            return false;
        }
    }
    
    return true;
}

bool IsWindowOnMonitor(HWND hwnd, const MonitorInfo& monitor) {
    if (!hwnd) return false;
    RECT window_rect;
    GetWindowRect(hwnd, &window_rect);
    POINT center{ (window_rect.left + window_rect.right) / 2, (window_rect.top + window_rect.bottom) / 2 };
    return PtInRect(&monitor.rect, center);
}

void MoveWindowToMonitor(HWND hwnd, const MonitorInfo& monitor, const OverlayPosition& pos) {
    if (!hwnd) return;
    
    RECT target_rect = CalculateOverlayRect(pos, monitor);
    
    // Apply DPI scaling if needed
    float dpi_scale = DpiScaleForMonitor(monitor) * pos.scale; (void)dpi_scale;
    
    SetWindowPos(hwnd, HWND_TOPMOST,
        target_rect.left, target_rect.top,
        target_rect.right - target_rect.left,
        target_rect.bottom - target_rect.top,
        SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

std::vector<MonitorInfo> GetAllMonitorsSorted() {
    auto monitors = EnumerateMonitors();
    // Already sorted by EnumerateMonitors (primary first, then left-to-right, top-to-bottom)
    return monitors;
}

} // namespace OmniGhost::Platform