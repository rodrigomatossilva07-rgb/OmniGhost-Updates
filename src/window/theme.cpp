#include "theme.h"
#include "../config/app_settings.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace CyberTheme {

    ColorPalette Colors;
    bool g_high_contrast = false;
    bool g_reduced_motion = false;
    ThemeMode g_theme_mode = ThemeMode::Dark;
    AccentPreset g_accent_preset = AccentPreset::Cyber;
    ImVec4 g_custom_accent = ImVec4(0.83f, 0.69f, 0.22f, 1.0f);

    ImVec4 Mix(const ImVec4& a, const ImVec4& b, float amount) {
        amount = std::clamp(amount, 0.0f, 1.0f);
        return ImVec4(
            a.x + (b.x - a.x) * amount,
            a.y + (b.y - a.y) * amount,
            a.z + (b.z - a.z) * amount,
            a.w + (b.w - a.w) * amount);
    }

    ImU32 U32(const ImVec4& color) {
        return ImGui::ColorConvertFloat4ToU32(color);
    }

    ImU32 WithAlpha(const ImVec4& color, float alpha) {
        ImVec4 result = color;
        result.w *= std::clamp(alpha, 0.0f, 1.0f);
        return U32(result);
    }

    namespace {
        constexpr float kBaseWindowWidth = 1180.0f;
        constexpr float kBaseWindowHeight = 720.0f;
        constexpr float kBaseWindowRounding = 14.0f;
        constexpr float kBaseHeaderHeight = 70.0f;
        constexpr float kBaseFooterHeight = 40.0f;
        constexpr float kBaseSidebarWidth = 210.0f;
        constexpr float kBaseContentInset = 12.0f;
        constexpr float kBaseContentPaddingX = 14.0f;
        constexpr float kBaseContentPaddingY = 12.0f;
        constexpr float kBaseGridGap = 12.0f;
        constexpr float kBaseCardPadding = 14.0f;
        constexpr float kBaseCardHeaderHeight = 42.0f;
        constexpr float kBaseCardRounding = 10.0f;
        constexpr float kBaseControlHeight = 36.0f;
        constexpr float kBaseControlRounding = 8.0f;
        constexpr float kBaseRowHeight = 34.0f;
        constexpr float kBaseToggleWidth = 48.0f;
        constexpr float kBaseToggleHeight = 25.0f;
        constexpr float kBaseSearchHeight = 36.0f;
        constexpr float kBaseLogoSize = 44.0f;
        constexpr float kBaseSidebarItemHeight = 40.0f;

        float g_ui_scale = 1.0f;

        const std::vector<AccentColor> kAccentPresets = {
            { ImVec4(0.83f, 0.69f, 0.22f, 1.0f), ImVec4(1.0f, 0.89f, 0.54f, 1.0f), ImVec4(0.83f, 0.69f, 0.22f, 0.20f), "Cyber", "cyber" },
            { ImVec4(0.94f, 0.74f, 0.25f, 1.0f), ImVec4(1.0f, 0.89f, 0.55f, 1.0f), ImVec4(0.94f, 0.74f, 0.25f, 0.20f), "Gold", "gold" },
            { ImVec4(0.67f, 0.45f, 0.96f, 1.0f), ImVec4(0.83f, 0.70f, 1.0f, 1.0f), ImVec4(0.67f, 0.45f, 0.96f, 0.20f), "Purple", "purple" },
            { ImVec4(0.24f, 0.87f, 0.47f, 1.0f), ImVec4(0.50f, 1.0f, 0.70f, 1.0f), ImVec4(0.24f, 0.87f, 0.47f, 0.20f), "Matrix", "matrix" },
            { ImVec4(0.94f, 0.35f, 0.37f, 1.0f), ImVec4(1.0f, 0.60f, 0.60f, 1.0f), ImVec4(0.94f, 0.35f, 0.37f, 0.20f), "Red", "red" },
            { ImVec4(0.32f, 0.62f, 0.96f, 1.0f), ImVec4(0.60f, 0.80f, 1.0f, 1.0f), ImVec4(0.32f, 0.62f, 0.96f, 0.20f), "Blue", "blue" },
            { ImVec4(0.00f, 0.80f, 0.80f, 1.0f), ImVec4(0.40f, 0.95f, 0.95f, 1.0f), ImVec4(0.00f, 0.80f, 0.80f, 0.20f), "Teal", "teal" },
            { ImVec4(1.0f, 0.55f, 0.10f, 1.0f), ImVec4(1.0f, 0.75f, 0.40f, 1.0f), ImVec4(1.0f, 0.55f, 0.10f, 0.20f), "Orange", "orange" },
            { ImVec4(1.0f, 0.40f, 0.70f, 1.0f), ImVec4(1.0f, 0.70f, 0.85f, 1.0f), ImVec4(1.0f, 0.40f, 0.70f, 0.20f), "Pink", "pink" }
        };

        ImVec4 GetAccentBase() {
            if (g_accent_preset == AccentPreset::Custom) {
                return g_custom_accent;
            }
            int idx = static_cast<int>(g_accent_preset);
            if (idx >= 0 && idx < static_cast<int>(kAccentPresets.size())) {
                return kAccentPresets[idx].base;
            }
            return kAccentPresets[0].base;
        }

        ImVec4 GetAccentHover() {
            if (g_accent_preset == AccentPreset::Custom) {
                return Mix(g_custom_accent, ImVec4(1.f, 1.f, 1.f, 1.f), 0.26f);
            }
            int idx = static_cast<int>(g_accent_preset);
            if (idx >= 0 && idx < static_cast<int>(kAccentPresets.size())) {
                return kAccentPresets[idx].hover;
            }
            return kAccentPresets[0].hover;
        }

        ImVec4 GetAccentGlow() {
            if (g_accent_preset == AccentPreset::Custom) {
                return ImVec4(g_custom_accent.x, g_custom_accent.y, g_custom_accent.z, 0.20f);
            }
            int idx = static_cast<int>(g_accent_preset);
            if (idx >= 0 && idx < static_cast<int>(kAccentPresets.size())) {
                return kAccentPresets[idx].glow;
            }
            return kAccentPresets[0].glow;
        }

        void ApplyDarkPalette(ImVec4 accent, ImVec4 accentHover, ImVec4 accentGlow) {
            Colors.Gold = accent;
            Colors.GoldHover = accentHover;
            Colors.GoldGlow = accentGlow;
            Colors.Text = ImVec4(236.f / 255.f, 236.f / 255.f, 232.f / 255.f, 1.00f);
            Colors.TextDisabled = ImVec4(161.f / 255.f, 161.f / 255.f, 155.f / 255.f, 1.00f);
            Colors.Border = ImVec4(225.f / 255.f, 187.f / 255.f, 55.f / 255.f, 0.10f);
            Colors.Success = ImVec4(53.f / 255.f, 210.f / 255.f, 127.f / 255.f, 1.00f);
            Colors.Warning = ImVec4(227.f / 255.f, 184.f / 255.f, 62.f / 255.f, 1.00f);
            Colors.Error = ImVec4(1.00f, 86.f / 255.f, 95.f / 255.f, 1.00f);
            Colors.Info = ImVec4(73.f / 255.f, 150.f / 255.f, 1.00f, 1.00f);
            Colors.Background = ImVec4(5.f / 255.f, 5.f / 255.f, 5.f / 255.f, 1.f);
            Colors.Surface = ImVec4(8.f / 255.f, 8.f / 255.f, 8.f / 255.f, 1.f);
            Colors.Card = ImVec4(11.f / 255.f, 11.f / 255.f, 12.f / 255.f, 1.f);
            Colors.CardHover = ImVec4(21.f / 255.f, 21.f / 255.f, 21.f / 255.f, 1.f);
            Colors.Panel = Colors.Surface;
            Colors.PanelHover = Colors.CardHover;
        }

        void ApplyLightPalette(ImVec4 accent, ImVec4 accentHover, ImVec4 accentGlow) {
            Colors.Gold = accent;
            Colors.GoldHover = accentHover;
            Colors.GoldGlow = accentGlow;
            Colors.Text = ImVec4(0.12f, 0.13f, 0.15f, 1.00f);
            Colors.TextDisabled = ImVec4(0.45f, 0.47f, 0.52f, 1.00f);
            Colors.Border = ImVec4(0.75f, 0.77f, 0.82f, 0.45f);
            Colors.Success = ImVec4(0.15f, 0.65f, 0.35f, 1.00f);
            Colors.Warning = ImVec4(0.85f, 0.55f, 0.10f, 1.00f);
            Colors.Error = ImVec4(0.85f, 0.25f, 0.25f, 1.00f);
            Colors.Info = ImVec4(0.20f, 0.50f, 0.85f, 1.00f);
            Colors.Background = ImVec4(0.96f, 0.97f, 0.98f, 1.f);
            Colors.Surface = ImVec4(1.f, 1.f, 1.f, 1.f);
            Colors.Card = ImVec4(0.92f, 0.93f, 0.95f, 1.f);
            Colors.CardHover = ImVec4(0.88f, 0.89f, 0.92f, 1.f);
            Colors.Panel = Colors.Surface;
            Colors.PanelHover = Colors.CardHover;
        }

        void ApplyHighContrastPalette(ImVec4 accent, ImVec4 accentHover, ImVec4 accentGlow) {
            Colors.Gold = accent;
            Colors.GoldHover = accentHover;
            Colors.GoldGlow = accentGlow;
            Colors.Text = ImVec4(1.f, 1.f, 1.f, 1.f);
            Colors.TextDisabled = ImVec4(0.7f, 0.7f, 0.7f, 1.f);
            Colors.Border = ImVec4(0.5f, 0.5f, 0.5f, 1.f);
            Colors.Success = ImVec4(0.2f, 1.f, 0.4f, 1.f);
            Colors.Warning = ImVec4(1.f, 0.8f, 0.f, 1.f);
            Colors.Error = ImVec4(1.f, 0.3f, 0.3f, 1.f);
            Colors.Info = ImVec4(0.2f, 0.7f, 1.f, 1.f);
            Colors.Background = ImVec4(0.f, 0.f, 0.f, 1.f);
            Colors.Surface = ImVec4(0.08f, 0.08f, 0.08f, 1.f);
            Colors.Card = ImVec4(0.12f, 0.12f, 0.12f, 1.f);
            Colors.CardHover = ImVec4(0.16f, 0.16f, 0.16f, 1.f);
            Colors.Panel = Colors.Surface;
            Colors.PanelHover = Colors.CardHover;
        }
    }

    void SetHighContrast(bool enabled) {
        g_high_contrast = enabled;
        if (ImGui::GetCurrentContext())
            ApplyTheme();
    }

    void SetReducedMotion(bool enabled) {
        g_reduced_motion = enabled;
        if (ImGui::GetCurrentContext())
            ApplyTheme();
    }

    bool IsHighContrast() {
        return g_high_contrast;
    }

    bool IsReducedMotion() {
        return g_reduced_motion;
    }

    float GetAnimationScale() {
        return g_reduced_motion ? 0.0f : 1.0f;
    }

    void SetUiScale(float scale) {
        scale = std::clamp(scale, 0.75f, 2.50f);
        if (std::fabs(scale - g_ui_scale) < 0.0001f)
            return;
        g_ui_scale = scale;

        Spacing::Xs = 4.0f * scale;
        Spacing::Sm = 8.0f * scale;
        Spacing::Md = 12.0f * scale;
        Spacing::Lg = 16.0f * scale;
        Spacing::Xl = 24.0f * scale;
        Spacing::Xxl = 32.0f * scale;

        Radius::Sm = 6.0f * scale;
        Radius::Md = 10.0f * scale;
        Radius::Lg = 14.0f * scale;

        Typography::Title = 20.0f * scale;
        Typography::Heading = 16.0f * scale;
        Typography::Body = 14.0f * scale;
        Typography::Caption = 11.5f * scale;
        Typography::Mono = 11.0f * scale;

        Shadow::SoftOffsetY = 5.0f * scale;
        Shadow::SoftRadius = 12.0f * scale;
        Shadow::SoftAlpha = 46;

        Metrics::WindowWidth = kBaseWindowWidth * scale;
        Metrics::WindowHeight = kBaseWindowHeight * scale;
        Metrics::WindowRounding = kBaseWindowRounding * scale;
        Metrics::HeaderHeight = kBaseHeaderHeight * scale;
        Metrics::FooterHeight = kBaseFooterHeight * scale;
        Metrics::SidebarWidth = kBaseSidebarWidth * scale;
        Metrics::ContentInset = kBaseContentInset * scale;
        Metrics::ContentPaddingX = kBaseContentPaddingX * scale;
        Metrics::ContentPaddingY = kBaseContentPaddingY * scale;
        Metrics::GridGap = kBaseGridGap * scale;
        Metrics::CardPadding = kBaseCardPadding * scale;
        Metrics::CardHeaderHeight = kBaseCardHeaderHeight * scale;
        Metrics::CardRounding = kBaseCardRounding * scale;
        Metrics::ControlHeight = kBaseControlHeight * scale;
        Metrics::ControlRounding = kBaseControlRounding * scale;
        Metrics::RowHeight = kBaseRowHeight * scale;
        Metrics::ToggleWidth = kBaseToggleWidth * scale;
        Metrics::ToggleHeight = kBaseToggleHeight * scale;
        Metrics::SearchHeight = kBaseSearchHeight * scale;
        Metrics::LogoSize = kBaseLogoSize * scale;
        Metrics::SidebarItemHeight = kBaseSidebarItemHeight * scale;
        Metrics::CardMinHeight = 0.0f; // content-sized
        Metrics::PageTransitionSeconds = g_reduced_motion ? 0.0f : 0.20f;

        if (ImGui::GetCurrentContext())
            ApplyTheme();
    }

    float UiScale() {
        return g_ui_scale;
    }

    float Px(float logicalPixels) {
        return logicalPixels * g_ui_scale;
    }

    void Initialize() {
        g_ui_scale = 1.0f;
        g_high_contrast = app_settings::config.high_contrast;
        g_reduced_motion = app_settings::config.reduce_motion;
        g_theme_mode = static_cast<ThemeMode>(app_settings::config.theme_mode);
        g_accent_preset = static_cast<AccentPreset>(app_settings::config.accent_preset);
        g_custom_accent = ImGui::ColorConvertU32ToFloat4(app_settings::config.color_primary);
        g_custom_accent.w = 1.0f;
        Colors = {};
        SetUiScale(app_settings::config.ui_scale);
        ApplyTheme();
    }

    ImU32 SafeShadowU32(int alpha) {
        alpha = std::clamp(alpha, 0, 255);
        ImVec4 shadow = Colors.Background;
        shadow.w = static_cast<float>(alpha) / 255.0f;
        return U32(shadow);
    }

    void ApplyTheme() {
        ImVec4 accent = GetAccentBase();
        ImVec4 accentHover = GetAccentHover();
        ImVec4 accentGlow = GetAccentGlow();

        if (g_high_contrast) {
            ApplyHighContrastPalette(accent, accentHover, accentGlow);
        } else if (g_theme_mode == ThemeMode::Light) {
            ApplyLightPalette(accent, accentHover, accentGlow);
        } else {
            ApplyDarkPalette(accent, accentHover, accentGlow);
        }

        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2(Spacing::Md, Spacing::Sm + Px(2.0f));
        style.FramePadding = ImVec2(Spacing::Sm + Px(1.0f), Spacing::Sm - Px(1.0f));
        style.CellPadding = ImVec2(Spacing::Sm, Spacing::Sm - Px(2.0f));
        style.ItemSpacing = ImVec2(Spacing::Sm, Spacing::Xs + Px(1.0f));
        style.ItemInnerSpacing = ImVec2(Spacing::Sm - Px(1.0f), Spacing::Xs + Px(1.0f));
        style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
        style.IndentSpacing = Px(18.0f);
        style.ScrollbarSize = Px(8.0f);
        style.GrabMinSize = Px(8.0f);

        style.WindowRounding = Metrics::WindowRounding;
        style.ChildRounding = Metrics::CardRounding;
        style.FrameRounding = Metrics::ControlRounding;
        style.PopupRounding = Metrics::CardRounding;
        style.ScrollbarRounding = Radius::Sm;
        style.GrabRounding = Radius::Sm;
        style.TabRounding = Metrics::ControlRounding;

        style.WindowBorderSize = g_high_contrast ? 2.0f : 0.0f;
        style.ChildBorderSize = g_high_contrast ? 1.0f : 0.0f;
        style.PopupBorderSize = g_high_contrast ? 2.0f : 1.0f;
        style.FrameBorderSize = g_high_contrast ? 1.0f : 0.0f;
        style.TabBorderSize = g_high_contrast ? 1.0f : 0.0f;

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_WindowBg] = Colors.Background;
        colors[ImGuiCol_WindowBg].w = 1.0f;
        colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
        colors[ImGuiCol_PopupBg] = Colors.Panel;
        colors[ImGuiCol_Border] = Colors.Border;
        colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
        colors[ImGuiCol_Text] = Colors.Text;
        colors[ImGuiCol_TextDisabled] = Colors.TextDisabled;
        colors[ImGuiCol_Button] = Colors.Card;
        colors[ImGuiCol_ButtonHovered] = Colors.CardHover;
        colors[ImGuiCol_ButtonActive] = Colors.Gold;
        colors[ImGuiCol_FrameBg] = Mix(Colors.Background, ImVec4(1, 1, 1, 1), g_theme_mode == ThemeMode::Light ? 0.08f : 0.045f);
        colors[ImGuiCol_FrameBgHovered] = Colors.PanelHover;
        colors[ImGuiCol_FrameBgActive] = Mix(Colors.Card, Colors.Gold, 0.10f);
        colors[ImGuiCol_TitleBg] = Colors.Background;
        colors[ImGuiCol_TitleBgActive] = Colors.Background;
        colors[ImGuiCol_TitleBgCollapsed] = Colors.Background;
        colors[ImGuiCol_MenuBarBg] = Colors.Background;
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.25f, 0.27f, 0.32f, 0.65f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.36f, 0.38f, 0.44f, 0.85f);
        colors[ImGuiCol_ScrollbarGrabActive] = Colors.Gold;
        colors[ImGuiCol_CheckMark] = Colors.Gold;
        colors[ImGuiCol_SliderGrab] = Colors.Gold;
        colors[ImGuiCol_SliderGrabActive] = Colors.GoldHover;
        colors[ImGuiCol_Header] = ImVec4(Colors.Gold.x * 0.16f, Colors.Gold.y * 0.16f, Colors.Gold.z * 0.16f, 0.85f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(Colors.Gold.x * 0.24f, Colors.Gold.y * 0.24f, Colors.Gold.z * 0.24f, 0.92f);
        colors[ImGuiCol_HeaderActive] = ImVec4(Colors.Gold.x * 0.30f, Colors.Gold.y * 0.30f, Colors.Gold.z * 0.30f, 1.0f);
        colors[ImGuiCol_Separator] = Colors.Border;
        colors[ImGuiCol_SeparatorHovered] = Colors.Gold;
        colors[ImGuiCol_SeparatorActive] = Colors.GoldHover;
        colors[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
        colors[ImGuiCol_ResizeGripHovered] = Colors.GoldGlow;
        colors[ImGuiCol_ResizeGripActive] = Colors.Gold;
        colors[ImGuiCol_Tab] = Colors.Surface;
        colors[ImGuiCol_TabHovered] = Colors.CardHover;
        colors[ImGuiCol_TabActive] = ImVec4(Colors.Gold.x * 0.17f, Colors.Gold.y * 0.17f, Colors.Gold.z * 0.17f, 1.0f);
        colors[ImGuiCol_TabUnfocused] = Colors.Surface;
        colors[ImGuiCol_TabUnfocusedActive] = Colors.Card;
        colors[ImGuiCol_TextSelectedBg] = ImVec4(Colors.Gold.x, Colors.Gold.y, Colors.Gold.z, 0.28f);
        colors[ImGuiCol_DragDropTarget] = Colors.Gold;
        colors[ImGuiCol_NavHighlight] = Colors.Gold;
        colors[ImGuiCol_NavWindowingHighlight] = Colors.GoldGlow;
        colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.f, 0.f, 0.f, 0.35f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.f, 0.f, 0.f, 0.60f);

        if (g_reduced_motion) {
            style.HoverDelayNormal = 0.0f;
            style.HoverDelayShort = 0.0f;
        }
    }

    void SetThemeMode(ThemeMode mode) {
        g_theme_mode = mode;
        app_settings::config.theme_mode = static_cast<int>(mode);
        if (ImGui::GetCurrentContext())
            ApplyTheme();
    }

    ThemeMode GetThemeMode() {
        return g_theme_mode;
    }

    void SetAccentPreset(AccentPreset preset) {
        g_accent_preset = preset;
        app_settings::config.accent_preset = static_cast<int>(preset);
        if (preset != AccentPreset::Custom) {
            ImVec4 accent = GetAccentBase();
            app_settings::config.color_primary = ImGui::ColorConvertFloat4ToU32(accent);
        }
        if (ImGui::GetCurrentContext())
            ApplyTheme();
    }

    AccentPreset GetAccentPreset() {
        return g_accent_preset;
    }

    void SetCustomAccent(const ImVec4& color) {
        g_custom_accent = color;
        g_custom_accent.w = 1.0f;
        g_accent_preset = AccentPreset::Custom;
        app_settings::config.accent_preset = static_cast<int>(AccentPreset::Custom);
        app_settings::config.color_primary = ImGui::ColorConvertFloat4ToU32(color);
        if (ImGui::GetCurrentContext())
            ApplyTheme();
    }

    const ImVec4& GetCustomAccent() {
        return g_custom_accent;
    }

    const std::vector<AccentColor>& GetAccentPresets() {
        return kAccentPresets;
    }

    std::string SerializeTheme() {
        ThemeExportData data;
        data.mode = g_theme_mode;
        data.accent = g_accent_preset;
        data.custom_accent = g_custom_accent;
        data.ui_scale = g_ui_scale;
        data.high_contrast = g_high_contrast;
        data.reduced_motion = g_reduced_motion;
        data.schema_version = 1;

        std::ostringstream out;
        out << "{\n";
        out << "  \"schema_version\": " << data.schema_version << ",\n";
        out << "  \"theme_mode\": " << static_cast<int>(data.mode) << ",\n";
        out << "  \"accent_preset\": " << static_cast<int>(data.accent) << ",\n";
        out << "  \"custom_accent\": { \"r\": " << data.custom_accent.x << ", \"g\": " << data.custom_accent.y << ", \"b\": " << data.custom_accent.z << ", \"a\": " << data.custom_accent.w << " },\n";
        out << "  \"ui_scale\": " << data.ui_scale << ",\n";
        out << "  \"high_contrast\": " << (data.high_contrast ? "true" : "false") << ",\n";
        out << "  \"reduced_motion\": " << (data.reduced_motion ? "true" : "false") << "\n";
        out << "}";
        return out.str();
    }

    bool DeserializeTheme(const std::string& json, std::string* error) {
        try {
            // Theme packs are data only.  Keep the parser deliberately small
            // and bounded so a Marketplace download can never become a code
            // execution or resource-exhaustion path.
            if (json.empty() || json.size() > 64u * 1024u) {
                if (error) *error = "Theme pack must be between 1 byte and 64 KiB";
                return false;
            }
            ThemeExportData data;
            bool sawSchema = false;
            bool sawMode = false;
            bool sawAccent = false;
            bool sawScale = false;
            std::istringstream in(json);
            std::string line;
            while (std::getline(in, line)) {
                if (line.find("schema_version") != std::string::npos) {
                    size_t pos = line.find(":");
                    if (pos != std::string::npos) { data.schema_version = std::stoi(line.substr(pos + 1)); sawSchema = true; }
                } else if (line.find("theme_mode") != std::string::npos) {
                    size_t pos = line.find(":");
                    if (pos != std::string::npos) { data.mode = static_cast<ThemeMode>(std::stoi(line.substr(pos + 1))); sawMode = true; }
                } else if (line.find("accent_preset") != std::string::npos) {
                    size_t pos = line.find(":");
                    if (pos != std::string::npos) { data.accent = static_cast<AccentPreset>(std::stoi(line.substr(pos + 1))); sawAccent = true; }
                } else if (line.find("custom_accent") != std::string::npos) {
                    size_t r = line.find("\"r\":");
                    size_t g = line.find("\"g\":");
                    size_t b = line.find("\"b\":");
                    size_t a = line.find("\"a\":");
                    if (r != std::string::npos) data.custom_accent.x = std::stof(line.substr(r + 4));
                    if (g != std::string::npos) data.custom_accent.y = std::stof(line.substr(g + 4));
                    if (b != std::string::npos) data.custom_accent.z = std::stof(line.substr(b + 4));
                    if (a != std::string::npos) data.custom_accent.w = std::stof(line.substr(a + 4));
                } else if (line.find("ui_scale") != std::string::npos) {
                    size_t pos = line.find(":");
                    if (pos != std::string::npos) { data.ui_scale = std::stof(line.substr(pos + 1)); sawScale = true; }
                } else if (line.find("high_contrast") != std::string::npos) {
                    data.high_contrast = line.find("true") != std::string::npos;
                } else if (line.find("reduced_motion") != std::string::npos) {
                    data.reduced_motion = line.find("true") != std::string::npos;
                }
            }

            const int mode = static_cast<int>(data.mode);
            const int accent = static_cast<int>(data.accent);
            const bool validAccent = (accent >= static_cast<int>(AccentPreset::Cyber) &&
                                      accent <= static_cast<int>(AccentPreset::Pink)) ||
                                     data.accent == AccentPreset::Custom;
            const auto finiteUnit = [](float value) {
                return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
            };
            if (!sawSchema || !sawMode || !sawAccent || !sawScale || data.schema_version != 1 || mode < static_cast<int>(ThemeMode::Dark) ||
                mode > static_cast<int>(ThemeMode::System) || !validAccent ||
                !finiteUnit(data.custom_accent.x) || !finiteUnit(data.custom_accent.y) ||
                !finiteUnit(data.custom_accent.z) || !finiteUnit(data.custom_accent.w) ||
                !std::isfinite(data.ui_scale) || data.ui_scale < 0.75f || data.ui_scale > 1.50f) {
                if (error) *error = "Theme pack contains unsupported or unsafe visual values";
                return false;
            }

            g_theme_mode = data.mode;
            g_accent_preset = data.accent;
            g_custom_accent = data.custom_accent;
            g_high_contrast = data.high_contrast;
            g_reduced_motion = data.reduced_motion;
            SetUiScale(data.ui_scale);
            ApplyTheme();

            app_settings::config.theme_mode = static_cast<int>(g_theme_mode);
            app_settings::config.accent_preset = static_cast<int>(g_accent_preset);
            app_settings::config.high_contrast = g_high_contrast;
            app_settings::config.reduce_motion = g_reduced_motion;
            app_settings::config.ui_scale = g_ui_scale;
            app_settings::config.color_primary = ImGui::ColorConvertFloat4ToU32(GetAccentBase());

            return true;
        } catch (const std::exception& e) {
            if (error) *error = std::string("Failed to parse theme: ") + e.what();
            return false;
        }
    }

    bool ExportTheme(const std::filesystem::path& path, std::string* error) {
        try {
            std::string json = SerializeTheme();
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) {
                if (error) *error = "Failed to create directory: " + ec.message();
                return false;
            }
            std::ofstream file(path);
            if (!file) {
                if (error) *error = "Failed to open file for writing";
                return false;
            }
            file << json;
            return true;
        } catch (const std::exception& e) {
            if (error) *error = std::string("Export failed: ") + e.what();
            return false;
        }
    }

    bool ImportTheme(const std::filesystem::path& path, std::string* error) {
        try {
            std::error_code ec;
            if (!std::filesystem::exists(path, ec)) {
                if (error) *error = "Theme file does not exist";
                return false;
            }
            if (!std::filesystem::is_regular_file(path, ec) || ec ||
                path.extension() != ".ogtheme") {
                if (error) *error = "Only regular .ogtheme files are accepted";
                return false;
            }
            const auto size = std::filesystem::file_size(path, ec);
            if (ec || size == 0 || size > 64u * 1024u) {
                if (error) *error = "Theme pack must be between 1 byte and 64 KiB";
                return false;
            }
            std::ifstream file(path);
            if (!file) {
                if (error) *error = "Failed to open theme file";
                return false;
            }
            std::stringstream buffer;
            buffer << file.rdbuf();
            return DeserializeTheme(buffer.str(), error);
        } catch (const std::exception& e) {
            if (error) *error = std::string("Import failed: ") + e.what();
            return false;
        }
    }

    void DrawSubtleNoise(
        ImDrawList* draw,
        const ImVec2& min,
        const ImVec2& max,
        float opacity,
        std::uint32_t seed,
        int density) {
        if (!draw || opacity <= 0.0f || density <= 0 || max.x <= min.x || max.y <= min.y)
            return;
        opacity = std::clamp(opacity, 0.0f, 0.12f);
        density = std::clamp(density, 32, 1400);
        std::uint32_t state = seed ? seed : 0x4F474E49u;
        auto next = [&]() {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        };
        const float width = max.x - min.x;
        const float height = max.y - min.y;
        const int alpha = static_cast<int>(255.0f * opacity);
        for (int i = 0; i < density; ++i) {
            const float x = min.x + (next() & 0xFFFFu) / 65535.0f * width;
            const float y = min.y + (next() & 0xFFFFu) / 65535.0f * height;
            const bool warm = (next() & 7u) == 0u;
            const ImU32 color = warm
                ? IM_COL32(212, 175, 55, std::max(1, alpha / 2))
                : IM_COL32(255, 255, 255, std::max(1, alpha / 3));
            draw->AddRectFilled(ImVec2(x, y), ImVec2(x + 1.0f, y + 1.0f), color);
        }
    }

    void DrawRadialAccent(
        ImDrawList* draw,
        const ImVec2& center,
        float radius,
        float opacity) {
        if (!draw || radius <= 1.0f || opacity <= 0.0f)
            return;
        opacity = std::clamp(opacity, 0.0f, 0.25f);
        constexpr int rings = 7;
        for (int i = rings; i >= 1; --i) {
            const float t = static_cast<float>(i) / rings;
            const float ringRadius = radius * t;
            const float a = opacity * (1.0f - t) * 0.85f + opacity * 0.035f;
            draw->AddCircleFilled(center, ringRadius, WithAlpha(Colors.Gold, a), 48);
        }
    }

} // namespace CyberTheme
