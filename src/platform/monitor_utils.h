#pragma once

#include <Windows.h>

#include <string>
#include <vector>

namespace OmniGhost::Platform {

struct MonitorInfo {
    HMONITOR handle = nullptr;
    RECT rect{};
    RECT work{};
    bool primary = false;
    std::wstring deviceName;
    int dpi = 96;
    float scale = 1.0f;
};

enum class OverlayPositionMode {
    Fullscreen,        // Full monitor
    Windowed,          // Custom window size/position
    Centered,          // Centered on monitor
    Custom             // User-defined rect
};

struct OverlayPosition {
    OverlayPositionMode mode = OverlayPositionMode::Fullscreen;
    int monitor_index = -1;  // -1 = auto/current
    RECT custom_rect{};      // For Custom mode
    float scale = 1.0f;      // Additional UI scale multiplier
    bool lock_to_monitor = true; // Prevent moving between monitors
};

std::vector<MonitorInfo> EnumerateMonitors();
MonitorInfo SelectMonitor(int requestedIndex, HWND currentWindow = nullptr);
MonitorInfo GetMonitorFromPoint(POINT pt);
MonitorInfo GetMonitorFromRect(const RECT& rect);
int MonitorIndexFromHandle(HMONITOR handle);
float DpiScaleForWindow(HWND hwnd);
float DpiScaleForMonitor(const MonitorInfo& monitor);

// Advanced positioning
OverlayPosition GetOverlayPosition();
void SetOverlayPosition(const OverlayPosition& pos);
RECT CalculateOverlayRect(const OverlayPosition& pos, const MonitorInfo& monitor);
bool IsPositionValid(const OverlayPosition& pos, const MonitorInfo& monitor);

// Multi-monitor utilities
bool IsWindowOnMonitor(HWND hwnd, const MonitorInfo& monitor);
void MoveWindowToMonitor(HWND hwnd, const MonitorInfo& monitor, const OverlayPosition& pos);
std::vector<MonitorInfo> GetAllMonitorsSorted(); // Left-to-right, top-to-bottom

} // namespace OmniGhost::Platform
