#pragma once
#include <string>
#include <vector>
#include <unordered_map>

namespace Gameplay::Resolution {

    // Aspect ratio presets
    enum class AspectRatio : int {
        Auto = 0,
        Ratio4_3 = 1,      // 1.333
        Ratio16_9 = 2,     // 1.777
        Ratio16_10 = 3,    // 1.6
        Ratio21_9 = 4,     // 2.333
        Ratio32_9 = 5,     // 3.555 (super ultrawide)
        Custom = 99
    };

    // Resolution info
    struct Resolution {
        int width = 1920;
        int height = 1080;
        int refresh_rate = 60;
        bool is_current = false;
        bool is_native = false;
        AspectRatio aspect = AspectRatio::Ratio16_9;
        
        float GetAspectRatio() const {
            return height > 0 ? (float)width / height : 1.777f;
        }
        
        std::string ToString() const {
            return std::to_string(width) + "x" + std::to_string(height) + 
                   (refresh_rate > 0 ? " @" + std::to_string(refresh_rate) + "Hz" : "");
        }
    };

    // Monitor configuration
    struct MonitorConfig {
        int index = 0;
        std::string name;
        Resolution native_resolution;
        std::vector<Resolution> supported_resolutions;
        float dpi_scale = 1.0f;
        bool is_primary = false;
        int physical_width_mm = 0;
        int physical_height_mm = 0;
        float max_refresh_rate = 60.0f;
        bool supports_gsync = false;
        bool supports_freesync = false;
        bool supports_hdr = false;
    };

    // World-to-screen configuration
    struct WorldToScreenConfig {
        // Viewport
        int viewport_width = 1920;
        int viewport_height = 1080;
        float fov_horizontal = 90.0f;      // Degrees
        float fov_vertical = 0.0f;         // 0 = auto from aspect
        float aspect_ratio = 1.777f;
        
        // Projection
        float near_plane = 0.1f;
        float far_plane = 10000.0f;
        
        // Offsets
        float x_offset = 0.0f;
        float y_offset = 0.0f;
        float scale_x = 1.0f;
        float scale_y = 1.0f;
        
        // Corrections
        bool correct_aspect = true;
        bool correct_fov = true;
        bool compensate_borderless = true;
        float borderless_offset_x = 0.0f;
        float borderless_offset_y = 0.0f;
        
        // Multi-monitor
        int target_monitor = -1;  // -1 = auto
        bool span_monitors = false;
        std::array<float, 4> monitor_offsets{}; // x, y for each monitor
    };

    // Resolution manager
    class ResolutionManager {
    public:
        static ResolutionManager& Instance();
        
        void Initialize();
        void Shutdown();
        
        // Monitor enumeration
        std::vector<MonitorConfig> EnumerateMonitors();
        MonitorConfig GetMonitor(int index) const;
        MonitorConfig GetPrimaryMonitor() const;
        int GetMonitorCount() const;
        
        // Resolution management
        std::vector<Resolution> GetSupportedResolutions(int monitor_index = -1) const;
        Resolution GetCurrentResolution(int monitor_index = -1) const;
        bool SetResolution(int width, int height, int refresh_rate = 60, int monitor_index = -1);
        bool SetResolution(const Resolution& res, int monitor_index = -1);
        
        // Aspect ratio
        AspectRatio DetectAspectRatio(int width, int height) const;
        float CalculateAspectRatio(int width, int height) const;
        std::pair<int, int> GetClosestResolution(AspectRatio target, int monitor_index = -1) const;
        
        // World-to-screen
        void SetWorldToScreenConfig(const WorldToScreenConfig& config);
        const WorldToScreenConfig& GetWorldToScreenConfig() const;
        
        bool WorldToScreen(const Vec3& world, Vec2& screen, const Matrix& view_proj) const;
        bool ScreenToWorld(const Vec2& screen, float world_z, Vec3& world, const Matrix& view_proj) const;
        
        // FOV calculations
        float CalculateHorizontalFOV(float vertical_fov, float aspect) const;
        float CalculateVerticalFOV(float horizontal_fov, float aspect) const;
        float GetHorizontalFOV() const;
        float GetVerticalFOV() const;
        
        // Scaling
        Vec2 ScaleToViewport(const Vec2& pos) const;
        Vec2 ScaleFromViewport(const Vec2& pos) const;
        
        // Multi-monitor
        bool IsPositionOnMonitor(const Vec2& pos, int monitor_index) const;
        int GetMonitorAtPosition(const Vec2& pos) const;
        Vec2 ConvertBetweenMonitors(const Vec2& pos, int from_monitor, int to_monitor) const;
        
        // DPI awareness
        float GetDPIScale(int monitor_index = -1) const;
        void SetDPIAwareness(bool per_monitor_v2 = true);
        
        // Callbacks
        void SetResolutionChangeCallback(std::function<void(int, int, int)> callback) {
            on_resolution_change_ = callback;
        }
        
    private:
        WorldToScreenConfig wts_config_;
        std::vector<MonitorConfig> monitors_;
        std::function<void(int, int, int)> on_resolution_change_;
        
        void RefreshMonitors();
        void UpdateDPIScales();
    };
    
    // Helper structures
    struct Vec2 {
        float x = 0, y = 0;
        Vec2() = default;
        Vec2(float x_, float y_) : x(x_), y(y_) {}
        Vec2 operator+(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
        Vec2 operator-(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
        Vec2 operator*(float s) const { return Vec2(x * s, y * s); }
        Vec2 operator/(float s) const { return Vec2(x / s, y / s); }
        float Length() const { return sqrtf(x*x + y*y); }
    };
    
    struct Vec3 {
        float x = 0, y = 0, z = 0;
        Vec3() = default;
        Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
        Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
        Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
        Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
        float Length() const { return sqrtf(x*x + y*y + z*z); }
        float Length2D() const { return sqrtf(x*x + y*y); }
    };
    
    struct Vec4 {
        float x, y, z, w;
    };
    
    struct Matrix {
        float m[16] = {};
        float& operator()(int row, int col) { return m[row * 4 + col]; }
        const float& operator()(int row, int col) const { return m[row * 4 + col]; }
    };
    
    // Resolution presets
    std::vector<Resolution> GetCommonResolutions(AspectRatio aspect = AspectRatio::Auto);
    Resolution GetResolutionFromString(const std::string& str); // "1920x1080@144"
    std::string AspectRatioToString(AspectRatio ar);
    AspectRatio StringToAspectRatio(const std::string& str);
    
    // FOV utilities
    float CalculateIdealFOV(float monitor_width_mm, float view_distance_mm);
    float CalculateMonitorDistance(float monitor_width_mm, float fov_degrees);
    
    // Multi-monitor utilities
    struct MonitorLayout {
        std::vector<MonitorConfig> monitors;
        int primary_index = 0;
        std::array<int, 4> arrangement{}; // Relative positions
    };
    
    MonitorLayout DetectMonitorLayout();
    Rect GetMonitorBounds(int index);
    Rect GetVirtualScreenBounds();
    
    struct Rect {
        int left = 0, top = 0, right = 0, bottom = 0;
        int Width() const { return right - left; }
        int Height() const { return bottom - top; }
        bool Contains(int x, int y) const {
            return x >= left && x < right && y >= top && y < bottom;
        }
    };

} // namespace Gameplay::Resolution