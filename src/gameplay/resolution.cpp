#include "resolution.h"
#include <algorithm>
#include <cmath>
#include <windows.h>
#include <shellscalingapi.h>

#pragma comment(lib, "Shcore.lib")

namespace Gameplay::Resolution {

    ResolutionManager& ResolutionManager::Instance() {
        static ResolutionManager instance;
        return instance;
    }

    void ResolutionManager::Initialize() {
        SetDPIAwareness(true);
        RefreshMonitors();
        UpdateDPIScales();
    }

    void ResolutionManager::Shutdown() {
        // Cleanup
    }

    void ResolutionManager::RefreshMonitors() {
        monitors_.clear();
        
        // Get monitor info using Windows API
        std::vector<MONITORINFOEXW> monitor_infos;
        
        EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam) -> BOOL {
            auto* infos = reinterpret_cast<std::vector<MONITORINFOEXW>*>(lParam);
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(hMonitor, &info)) {
                infos->push_back(info);
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&monitor_infos));
        
        for (size_t i = 0; i < monitor_infos.size(); ++i) {
            const auto& info = monitor_infos[i];
            MonitorConfig monitor;
            monitor.index = (int)i;
            monitor.name = std::string(info.szDevice);
            monitor.is_primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
            
            monitor.native_resolution.width = info.rcMonitor.right - info.rcMonitor.left;
            monitor.native_resolution.height = info.rcMonitor.bottom - info.rcMonitor.top;
            monitor.native_resolution.is_native = true;
            monitor.native_resolution.aspect = DetectAspectRatio(
                monitor.native_resolution.width, monitor.native_resolution.height);
            
            // Get DPI
            using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
            static GetDpiForMonitorFn getDpiForMonitor = reinterpret_cast<GetDpiForMonitorFn>(
                GetProcAddress(GetModuleHandleW(L"shcore.dll"), "GetDpiForMonitor"));
            
            UINT dpiX = 96, dpiY = 96;
            HMONITOR hMonitor = MonitorFromRect(&monitor_infos[i].rcMonitor, MONITOR_DEFAULTTONEAREST);
            if (getDpiForMonitor) {
                getDpiForMonitor(hMonitor, 0, &dpiX, &dpiY); // MDT_EFFECTIVE_DPI = 0
            }
            monitor.dpi_scale = std::max(1.0f, (float)dpiX / 96.0f);
            
            // Get supported resolutions
            DEVMODEW dm{};
            dm.dmSize = sizeof(dm);
            for (int mode = 0; EnumDisplaySettingsW(info.szDevice, mode, &dm); ++mode) {
                Resolution res;
                res.width = dm.dmPelsWidth;
                res.height = dm.dmPelsHeight;
                res.refresh_rate = dm.dmDisplayFrequency;
                res.aspect = DetectAspectRatio(res.width, res.height);
                if (res.width == monitor.native_resolution.width && 
                    res.height == monitor.native_resolution.height) {
                    res.is_current = true;
                }
                monitor.supported_resolutions.push_back(res);
            }
            
            monitors_.push_back(monitor);
        }
    }

    std::vector<MonitorConfig> ResolutionManager::EnumerateMonitors() {
        return monitors_;
    }

    MonitorConfig ResolutionManager::GetMonitor(int index) const {
        if (index >= 0 && index < (int)monitors_.size()) {
            return monitors_[index];
        }
        return MonitorConfig{};
    }

    MonitorConfig ResolutionManager::GetPrimaryMonitor() const {
        for (const auto& m : monitors_) {
            if (m.is_primary) return m;
        }
        return monitors_.empty() ? MonitorConfig{} : monitors_[0];
    }

    int ResolutionManager::GetMonitorCount() const {
        return (int)monitors_.size();
    }

    std::vector<Resolution> ResolutionManager::GetSupportedResolutions(int monitor_index) const {
        if (monitor_index >= 0 && monitor_index < (int)monitors_.size()) {
            return monitors_[monitor_index].supported_resolutions;
        }
        // Return all unique resolutions across all monitors
        std::vector<Resolution> all;
        for (const auto& m : monitors_) {
            all.insert(all.end(), m.supported_resolutions.begin(), m.supported_resolutions.end());
        }
        // Deduplicate
        std::sort(all.begin(), all.end(), [](const Resolution& a, const Resolution& b) {
            if (a.width != b.width) return a.width < b.width;
            if (a.height != b.height) return a.height < b.height;
            return a.refresh_rate < b.refresh_rate;
        });
        all.erase(std::unique(all.begin(), all.end(), 
            [](const Resolution& a, const Resolution& b) {
                return a.width == b.width && a.height == b.height && a.refresh_rate == b.refresh_rate;
            }), all.end());
        return all;
    }

    Resolution ResolutionManager::GetCurrentResolution(int monitor_index) const {
        if (monitor_index >= 0 && monitor_index < (int)monitors_.size()) {
            for (const auto& res : monitors_[monitor_index].supported_resolutions) {
                if (res.is_current) return res;
            }
            return monitors_[monitor_index].native_resolution;
        }
        return Resolution{};
    }

    bool ResolutionManager::SetResolution(int width, int height, int refresh_rate, int monitor_index) {
        // Would use ChangeDisplaySettingsEx
        return false;
    }

    bool ResolutionManager::SetResolution(const Resolution& res, int monitor_index) {
        return SetResolution(res.width, res.height, res.refresh_rate, monitor_index);
    }

    AspectRatio ResolutionManager::DetectAspectRatio(int width, int height) const {
        if (height == 0) return AspectRatio::Ratio16_9;
        float ratio = (float)width / height;
        
        if (std::abs(ratio - 4.0f/3.0f) < 0.02f) return AspectRatio::Ratio4_3;
        if (std::abs(ratio - 16.0f/9.0f) < 0.02f) return AspectRatio::Ratio16_9;
        if (std::abs(ratio - 16.0f/10.0f) < 0.02f) return AspectRatio::Ratio16_10;
        if (std::abs(ratio - 21.0f/9.0f) < 0.02f) return AspectRatio::Ratio21_9;
        if (std::abs(ratio - 32.0f/9.0f) < 0.02f) return AspectRatio::Ratio32_9;
        
        return AspectRatio::Custom;
    }

    float ResolutionManager::CalculateAspectRatio(int width, int height) const {
        return height > 0 ? (float)width / height : 1.777f;
    }

    std::pair<int, int> ResolutionManager::GetClosestResolution(AspectRatio target, int monitor_index) const {
        auto resolutions = GetSupportedResolutions(monitor_index);
        float target_ratio = 0.0f;
        
        switch (target) {
            case AspectRatio::Ratio4_3: target_ratio = 4.0f/3.0f; break;
            case AspectRatio::Ratio16_9: target_ratio = 16.0f/9.0f; break;
            case AspectRatio::Ratio16_10: target_ratio = 16.0f/10.0f; break;
            case AspectRatio::Ratio21_9: target_ratio = 21.0f/9.0f; break;
            case AspectRatio::Ratio32_9: target_ratio = 32.0f/9.0f; break;
            default: return {1920, 1080};
        }
        
        float best_diff = 1e9f;
        std::pair<int, int> best = {1920, 1080};
        
        for (const auto& res : resolutions) {
            float ratio = (float)res.width / res.height;
            float diff = std::abs(ratio - target_ratio);
            if (diff < best_diff) {
                best_diff = diff;
                best = {res.width, res.height};
            }
        }
        return best;
    }

    void ResolutionManager::SetWorldToScreenConfig(const WorldToScreenConfig& config) {
        wts_config_ = config;
    }

    const WorldToScreenConfig& ResolutionManager::GetWorldToScreenConfig() const {
        return wts_config_;
    }

    bool ResolutionManager::WorldToScreen(const Vec3& world, Vec2& screen, const Matrix& view_proj) const {
        // Transform world to clip space
        Vec4 clip;
        clip.x = world.x * view_proj.m[0] + world.y * view_proj.m[4] + world.z * view_proj.m[8] + view_proj.m[12];
        clip.y = world.x * view_proj.m[1] + world.y * view_proj.m[5] + world.z * view_proj.m[9] + view_proj.m[13];
        clip.z = world.x * view_proj.m[2] + world.y * view_proj.m[6] + world.z * view_proj.m[10] + view_proj.m[14];
        clip.w = world.x * view_proj.m[3] + world.y * view_proj.m[7] + world.z * view_proj.m[11] + view_proj.m[15];
        
        if (clip.w < 0.1f) return false;
        
        // Normalize device coordinates
        float ndc_x = clip.x / clip.w;
        float ndc_y = clip.y / clip.w;
        
        // Apply viewport transform
        float viewport_w = wts_config_.viewport_width * wts_config_.scale_x;
        float viewport_h = wts_config_.viewport_height * wts_config_.scale_y;
        
        screen.x = (ndc_x * 0.5f + 0.5f) * viewport_w + wts_config_.x_offset;
        screen.y = (1.0f - ndc_y * 0.5f) * viewport_h + wts_config_.y_offset;
        
        // Apply aspect correction
        if (wts_config_.correct_aspect) {
            float aspect = wts_config_.aspect_ratio;
            screen.x *= aspect / (wts_config_.viewport_width / (float)wts_config_.viewport_height);
        }
        
        // Borderless compensation
        if (wts_config_.compensate_borderless) {
            screen.x += wts_config_.borderless_offset_x;
            screen.y += wts_config_.borderless_offset_y;
        }
        
        return true;
    }

    bool ResolutionManager::ScreenToWorld(const Vec2& screen, float world_z, Vec3& world, const Matrix& view_proj) const {
        // Inverse projection - simplified
        return false;
    }

    float ResolutionManager::CalculateHorizontalFOV(float vertical_fov, float aspect) const {
        return 2.0f * atanf(tanf(vertical_fov * 0.5f * 3.14159f / 180.0f) * aspect) * 180.0f / 3.14159f;
    }

    float ResolutionManager::CalculateVerticalFOV(float horizontal_fov, float aspect) const {
        return 2.0f * atanf(tanf(horizontal_fov * 0.5f * 3.14159f / 180.0f) / aspect) * 180.0f / 3.14159f;
    }

    float ResolutionManager::GetHorizontalFOV() const {
        return wts_config_.fov_horizontal;
    }

    float ResolutionManager::GetVerticalFOV() const {
        if (wts_config_.fov_vertical > 0) return wts_config_.fov_vertical;
        return CalculateVerticalFOV(wts_config_.fov_horizontal, wts_config_.aspect_ratio);
    }

    Vec2 ResolutionManager::ScaleToViewport(const Vec2& pos) const {
        return Vec2(
            pos.x * wts_config_.scale_x + wts_config_.x_offset,
            pos.y * wts_config_.scale_y + wts_config_.y_offset
        );
    }

    Vec2 ResolutionManager::ScaleFromViewport(const Vec2& pos) const {
        return Vec2(
            (pos.x - wts_config_.x_offset) / wts_config_.scale_x,
            (pos.y - wts_config_.y_offset) / wts_config_.scale_y
        );
    }

    bool ResolutionManager::IsPositionOnMonitor(const Vec2& pos, int monitor_index) const {
        if (monitor_index < 0 || monitor_index >= (int)monitors_.size()) return false;
        const auto& m = monitors_[monitor_index];
        return pos.x >= 0 && pos.x < m.native_resolution.width &&
               pos.y >= 0 && pos.y < m.native_resolution.height;
    }

    int ResolutionManager::GetMonitorAtPosition(const Vec2& pos) const {
        for (size_t i = 0; i < monitors_.size(); ++i) {
            if (IsPositionOnMonitor(pos, (int)i)) return (int)i;
        }
        return -1;
    }

    Vec2 ResolutionManager::ConvertBetweenMonitors(const Vec2& pos, int from_monitor, int to_monitor) const {
        if (from_monitor < 0 || from_monitor >= (int)monitors_.size() ||
            to_monitor < 0 || to_monitor >= (int)monitors_.size()) {
            return pos;
        }
        
        const auto& from = monitors_[from_monitor];
        const auto& to = monitors_[to_monitor];
        
        // Normalize position in from monitor
        Vec2 norm(pos.x / from.native_resolution.width, pos.y / from.native_resolution.height);
        
        // Apply to monitor
        return Vec2(norm.x * to.native_resolution.width, norm.y * to.native_resolution.height);
    }

    float ResolutionManager::GetDPIScale(int monitor_index) const {
        if (monitor_index >= 0 && monitor_index < (int)monitors_.size()) {
            return monitors_[monitor_index].dpi_scale;
        }
        return 1.0f;
    }

    void ResolutionManager::SetDPIAwareness(bool per_monitor_v2) {
        if (per_monitor_v2) {
            SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        } else {
            SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
        }
    }

    void ResolutionManager::RefreshMonitors() {
        // Re-enumerate
    }

    void ResolutionManager::UpdateDPIScales() {
        for (auto& m : monitors_) {
            HMONITOR hMonitor = MonitorFromRect(
                &RECT{m.native_resolution.width, m.native_resolution.height, 0, 0}, 
                MONITOR_DEFAULTTONEAREST);
            using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
            static GetDpiForMonitorFn getDpiForMonitor = reinterpret_cast<GetDpiForMonitorFn>(
                GetProcAddress(GetModuleHandleW(L"shcore.dll"), "GetDpiForMonitor"));
            UINT dpiX = 96, dpiY = 96;
            if (getDpiForMonitor) {
                getDpiForMonitor(MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY), 0, &dpiX, &dpiY);
            }
            m.dpi_scale = std::max(1.0f, (float)dpiX / 96.0f);
        }
    }

    // Preset resolutions
    std::vector<Resolution> GetCommonResolutions(AspectRatio aspect) {
        std::vector<Resolution> resolutions;
        
        switch (aspect) {
            case AspectRatio::Ratio4_3:
                resolutions = {{640,480}, {800,600}, {1024,768}, {1280,960}, {1400,1050}, {1600,1200}};
                break;
            case AspectRatio::Ratio16_9:
                resolutions = {{640,360}, {854,480}, {960,540}, {1024,576}, {1280,720}, 
                              {1366,768}, {1600,900}, {1920,1080}, {2560,1440}, {3840,2160}, {7680,4320}};
                break;
            case AspectRatio::Ratio16_10:
                resolutions = {{1280,800}, {1440,900}, {1680,1050}, {1920,1200}, {2560,1600}};
                break;
            case AspectRatio::Ratio21_9:
                resolutions = {{2560,1080}, {3440,1440}, {5120,2160}};
                break;
            case AspectRatio::Ratio32_9:
                resolutions = {{3840,1080}, {5120,1440}};
                break;
            default:
                resolutions = {{1920,1080}, {2560,1440}, {3840,2160}};
        }
        
        for (auto& r : resolutions) {
            r.aspect = DetectAspectRatioStatic(r.width, r.height);
        }
        return resolutions;
    }

    AspectRatio DetectAspectRatioStatic(int width, int height) {
        if (height == 0) return AspectRatio::Ratio16_9;
        float ratio = (float)width / height;
        
        if (std::abs(ratio - 4.0f/3.0f) < 0.02f) return AspectRatio::Ratio4_3;
        if (std::abs(ratio - 16.0f/9.0f) < 0.02f) return AspectRatio::Ratio16_9;
        if (std::abs(ratio - 16.0f/10.0f) < 0.02f) return AspectRatio::Ratio16_10;
        if (std::abs(ratio - 21.0f/9.0f) < 0.02f) return AspectRatio::Ratio21_9;
        if (std::abs(ratio - 32.0f/9.0f) < 0.02f) return AspectRatio::Ratio32_9;
        return AspectRatio::Custom;
    }

    Resolution GetResolutionFromString(const std::string& str) {
        Resolution res;
        size_t x_pos = str.find('x');
        size_t at_pos = str.find('@');
        
        if (x_pos != std::string::npos) {
            res.width = std::stoi(str.substr(0, x_pos));
            std::string rest = str.substr(x_pos + 1);
            
            size_t at = rest.find('@');
            if (at != std::string::npos) {
                res.height = std::stoi(rest.substr(0, at));
                res.refresh_rate = std::stoi(rest.substr(at + 1));
            } else {
                res.height = std::stoi(rest);
            }
        }
        res.aspect = DetectAspectRatioStatic(res.width, res.height);
        return res;
    }

    std::string AspectRatioToString(AspectRatio ar) {
        switch (ar) {
            case AspectRatio::Ratio4_3: return "4:3";
            case AspectRatio::Ratio16_9: return "16:9";
            case AspectRatio::Ratio16_10: return "16:10";
            case AspectRatio::Ratio21_9: return "21:9";
            case AspectRatio::Ratio32_9: return "32:9";
            default: return "Custom";
        }
    }

    AspectRatio StringToAspectRatio(const std::string& str) {
        if (str == "4:3") return AspectRatio::Ratio4_3;
        if (str == "16:9") return AspectRatio::Ratio16_9;
        if (str == "16:10") return AspectRatio::Ratio16_10;
        if (str == "21:9") return AspectRatio::Ratio21_9;
        if (str == "32:9") return AspectRatio::Ratio32_9;
        return AspectRatio::Auto;
    }

    float CalculateIdealFOV(float monitor_width_mm, float view_distance_mm) {
        return 2.0f * atanf(monitor_width_mm / (2.0f * view_distance_mm)) * 180.0f / 3.14159f;
    }

    float CalculateMonitorDistance(float monitor_width_mm, float fov_degrees) {
        float fov_rad = fov_degrees * 3.14159f / 180.0f;
        return monitor_width_mm / (2.0f * tanf(fov_rad * 0.5f));
    }

    Rect GetMonitorBounds(int index) {
        auto& rm = ResolutionManager::Instance();
        if (index >= 0 && index < rm.GetMonitorCount()) {
            auto m = rm.GetMonitor(index);
            return Rect{0, 0, m.native_resolution.width, m.native_resolution.height};
        }
        return Rect{0, 0, 1920, 1080};
    }

    Rect GetVirtualScreenBounds() {
        Rect bounds{INT_MAX, INT_MAX, INT_MIN, INT_MIN};
        auto& rm = ResolutionManager::Instance();
        
        for (int i = 0; i < rm.GetMonitorCount(); ++i) {
            auto m = rm.GetMonitor(i);
            bounds.left = std::min(bounds.left, 0);
            bounds.top = std::min(bounds.top, 0);
            bounds.right = std::max(bounds.right, m.native_resolution.width);
            bounds.bottom = std::max(bounds.bottom, m.native_resolution.height);
        }
        return bounds;
    }

    MonitorLayout DetectMonitorLayout() {
        MonitorLayout layout;
        auto& rm = ResolutionManager::Instance();
        
        for (int i = 0; i < rm.GetMonitorCount(); ++i) {
            layout.monitors.push_back(rm.GetMonitor(i));
        }
        return layout;
    }

} // namespace Gameplay::Resolution