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
};

std::vector<MonitorInfo> EnumerateMonitors();
MonitorInfo SelectMonitor(int requestedIndex, HWND currentWindow = nullptr);
int MonitorIndexFromHandle(HMONITOR handle);
float DpiScaleForWindow(HWND hwnd);

} // namespace OmniGhost::Platform
