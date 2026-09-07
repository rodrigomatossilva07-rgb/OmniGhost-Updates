#pragma once
#include "../ImGui/imgui.h"

#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>

namespace CyberTheme {

    enum class ThemeMode : int {
        Dark = 0,
        Light = 1,
        System = 2
    };

    enum class AccentPreset : int {
        Cyber = 0,
        Gold = 1,
        Purple = 2,
        Matrix = 3,
        Red = 4,
        Blue = 5,
        Teal = 6,
        Orange = 7,
        Pink = 8,
        Custom = 99
    };

    struct AccentColor {
        ImVec4 base;
        ImVec4 hover;
        ImVec4 glow;
        const char* name;
        const char* id;
    };

    // Product-wide design tokens. All measurements are updated by SetUiScale()
    // from these logical 96-DPI base values so pages no longer invent their own
    // spacing/radius system.
    namespace Spacing {
        inline float Xs = 4.0f;
        inline float Sm = 8.0f;
        inline float Md = 12.0f;
        inline float Lg = 16.0f;
        inline float Xl = 24.0f;
        inline float Xxl = 32.0f;
    }

    namespace Radius {
        inline float Sm = 6.0f;
        inline float Md = 10.0f;
        inline float Lg = 14.0f;
    }

    namespace Typography {
        inline float Title = 27.0f;
        inline float Heading = 22.0f;
        inline float Body = 19.0f;
        inline float Caption = 15.0f;
        inline float Mono = 16.5f;
    }

    namespace Shadow {
        inline float SoftOffsetY = 5.0f;
        inline float SoftRadius = 12.0f;
        inline int SoftAlpha = 46;
    }

    namespace Metrics {
        inline float WindowWidth = 1180.0f;
        inline float WindowHeight = 720.0f;
        inline float WindowRounding = 14.0f;
        inline float HeaderHeight = 68.0f;
        inline float FooterHeight = 48.0f;
        inline float SidebarWidth = 188.0f;
        inline float ContentInset = 14.0f;
        inline float ContentPaddingX = 18.0f;
        inline float ContentPaddingY = 14.0f;
        inline float GridGap = 16.0f;
        inline float CardPadding = 18.0f;
        inline float CardHeaderHeight = 44.0f;
        inline float CardRounding = 11.0f;
        inline float ControlHeight = 42.0f;
        inline float ControlRounding = 9.0f;
        inline float RowHeight = 40.0f;
        inline float ToggleWidth = 48.0f;
        inline float ToggleHeight = 25.0f;
        inline float SearchHeight = 42.0f;
        inline float LogoSize = 42.0f;
        inline float SidebarItemHeight = 49.0f;
        inline float CardMinHeight = 156.0f;
        inline float PageTransitionSeconds = 0.20f;
    }

    struct ColorPalette {
        // Clear dark hierarchy: Background < Surface < Card < CardHover.
        ImVec4 Background;
        ImVec4 Surface;
        ImVec4 Card;
        ImVec4 CardHover;

        // Compatibility aliases used by older widgets. Panel maps to Surface
        // and PanelHover maps to CardHover when the palette is rebuilt.
        ImVec4 Panel;
        ImVec4 PanelHover;

        ImVec4 Gold;
        ImVec4 GoldHover;
        ImVec4 GoldGlow;
        ImVec4 Text;
        ImVec4 TextDisabled;
        ImVec4 Border;
        ImVec4 Success;
        ImVec4 Warning;
        ImVec4 Error;
        ImVec4 Info;
    };

    extern ColorPalette Colors;
    extern ThemeMode g_theme_mode;
    extern AccentPreset g_accent_preset;
    extern ImVec4 g_custom_accent;

    void Initialize();
    void ApplyTheme();
    void SetUiScale(float scale);
    float UiScale();
    float Px(float logicalPixels);

    ImU32 SafeShadowU32(int alpha);

    ImVec4 Mix(const ImVec4& a, const ImVec4& b, float amount);
    ImU32 U32(const ImVec4& color);
    ImU32 WithAlpha(const ImVec4& color, float alpha);

    void SetHighContrast(bool enabled);
    void SetReducedMotion(bool enabled);
    bool IsHighContrast();
    bool IsReducedMotion();
    float GetAnimationScale();

    void SetThemeMode(ThemeMode mode);
    ThemeMode GetThemeMode();
    void SetAccentPreset(AccentPreset preset);
    AccentPreset GetAccentPreset();
    void SetCustomAccent(const ImVec4& color);
    const ImVec4& GetCustomAccent();
    const std::vector<AccentColor>& GetAccentPresets();

    // Theme import/export
    struct ThemeExportData {
        ThemeMode mode = ThemeMode::Dark;
        AccentPreset accent = AccentPreset::Cyber;
        ImVec4 custom_accent = ImVec4(0.83f, 0.69f, 0.22f, 1.0f);
        float ui_scale = 1.0f;
        bool high_contrast = false;
        bool reduced_motion = false;
        int schema_version = 1;
    };

    bool ExportTheme(const std::filesystem::path& path, std::string* error = nullptr);
    bool ImportTheme(const std::filesystem::path& path, std::string* error = nullptr);
    std::string SerializeTheme();
    bool DeserializeTheme(const std::string& json, std::string* error = nullptr);

    // Low-cost ambient texture used instead of heavy glows. Seed is stable so
    // the pattern does not shimmer between frames.
    void DrawSubtleNoise(
        ImDrawList* draw,
        const ImVec2& min,
        const ImVec2& max,
        float opacity = 0.025f,
        std::uint32_t seed = 0x4F474E49u,
        int density = 420);

    void DrawRadialAccent(
        ImDrawList* draw,
        const ImVec2& center,
        float radius,
        float opacity = 0.10f);

} // namespace CyberTheme
