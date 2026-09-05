#include "theme.h"
#include "../config/app_settings.h"
#include <algorithm>
#include <cmath>

namespace CyberTheme {

    ColorPalette Colors;
    bool g_high_contrast = false;
    bool g_reduced_motion = false;

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
        constexpr float kBaseHeaderHeight = 68.0f;
        constexpr float kBaseFooterHeight = 48.0f;
        constexpr float kBaseSidebarWidth = 188.0f;
        constexpr float kBaseContentInset = 14.0f;
        constexpr float kBaseContentPaddingX = 18.0f;
        constexpr float kBaseContentPaddingY = 14.0f;
        constexpr float kBaseGridGap = 16.0f;
        constexpr float kBaseCardPadding = 18.0f;
        constexpr float kBaseCardHeaderHeight = 44.0f;
        constexpr float kBaseCardRounding = 11.0f;
        constexpr float kBaseControlHeight = 42.0f;
        constexpr float kBaseControlRounding = 9.0f;
        constexpr float kBaseRowHeight = 40.0f;
        constexpr float kBaseToggleWidth = 48.0f;
        constexpr float kBaseToggleHeight = 25.0f;
        constexpr float kBaseSearchHeight = 42.0f;
        constexpr float kBaseLogoSize = 42.0f;
        constexpr float kBaseSidebarItemHeight = 49.0f;

        float g_ui_scale = 1.0f;
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

        Typography::Title = 27.0f * scale;
        Typography::Heading = 22.0f * scale;
        Typography::Body = 19.0f * scale;
        Typography::Caption = 15.0f * scale;
        Typography::Mono = 16.5f * scale;

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
        Metrics::CardMinHeight = 156.0f * scale;
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
        g_high_contrast = app_settings::config.reduce_motion; // Note: we'll add high_contrast to app_settings
        g_reduced_motion = app_settings::config.reduce_motion;
        Colors = {};
        Colors.Gold = ImVec4(212.f / 255.f, 175.f / 255.f, 55.f / 255.f, 1.00f);
        Colors.GoldHover = ImVec4(1.000f, 226.f / 255.f, 138.f / 255.f, 1.00f);
        Colors.GoldGlow = ImVec4(212.f / 255.f, 175.f / 255.f, 55.f / 255.f, 0.20f);
        Colors.Text = ImVec4(0.949f, 0.949f, 0.957f, 1.00f);
        Colors.TextDisabled = ImVec4(0.56f, 0.57f, 0.62f, 1.00f);
        Colors.Border = ImVec4(0.40f, 0.41f, 0.46f, 0.34f);
        Colors.Success = ImVec4(0.286f, 0.820f, 0.537f, 1.00f);
        Colors.Warning = ImVec4(0.945f, 0.671f, 0.275f, 1.00f);
        Colors.Error = ImVec4(0.941f, 0.349f, 0.365f, 1.00f);
        Colors.Info = ImVec4(0.353f, 0.671f, 0.965f, 1.00f);

        Colors.Background = ImVec4(0.f, 0.f, 0.f, 1.f);
        Colors.Surface = ImVec4(0.f, 0.f, 0.f, 1.f);
        Colors.Card = ImVec4(0.f, 0.f, 0.f, 1.f);
        Colors.CardHover = ImVec4(13.f / 255.f, 13.f / 255.f, 13.f / 255.f, 1.f);
        Colors.Panel = Colors.Surface;
        Colors.PanelHover = Colors.CardHover;
        SetUiScale(1.0f);
        ApplyTheme();
    }

    ImU32 SafeShadowU32(int alpha) {
        alpha = std::clamp(alpha, 0, 255);
        ImVec4 shadow = Colors.Background;
        shadow.w = static_cast<float>(alpha) / 255.0f;
        return U32(shadow);
    }

void ApplyTheme() {
        // Accent is a real global setting rather than a decorative preview-only
        // value. Rebuild the interactive palette from the persisted accent each
        // time the ImGui theme is applied.
        ImVec4 accent = ImGui::ColorConvertU32ToFloat4(app_settings::config.color_primary);
        accent.w = 1.0f;

        // High contrast mode overrides
        if (g_high_contrast) {
            Colors.Background = ImVec4(0.f, 0.f, 0.f, 1.f);
            Colors.Surface = ImVec4(0.08f, 0.08f, 0.08f, 1.f);
            Colors.Card = ImVec4(0.12f, 0.12f, 0.12f, 1.f);
            Colors.CardHover = ImVec4(0.16f, 0.16f, 0.16f, 1.f);
            Colors.Text = ImVec4(1.f, 1.f, 1.f, 1.f);
            Colors.TextDisabled = ImVec4(0.7f, 0.7f, 0.7f, 1.f);
            Colors.Border = ImVec4(0.5f, 0.5f, 0.5f, 1.f);
            Colors.Gold = ImVec4(1.f, 0.9f, 0.f, 1.f);
            Colors.GoldHover = ImVec4(1.f, 1.f, 0.2f, 1.f);
            Colors.GoldGlow = ImVec4(1.f, 0.9f, 0.f, 0.5f);
            Colors.Success = ImVec4(0.2f, 1.f, 0.4f, 1.f);
            Colors.Warning = ImVec4(1.f, 0.8f, 0.f, 1.f);
            Colors.Error = ImVec4(1.f, 0.3f, 0.3f, 1.f);
            Colors.Info = ImVec4(0.2f, 0.7f, 1.f, 1.f);
        } else {
            Colors.Gold = accent;
            Colors.GoldHover = Mix(accent, ImVec4(1.f, 1.f, 1.f, 1.f), 0.26f);
            Colors.GoldGlow = ImVec4(accent.x, accent.y, accent.z, 0.20f);
            Colors.Text = ImVec4(0.949f, 0.949f, 0.957f, 1.00f);
            Colors.TextDisabled = ImVec4(0.56f, 0.57f, 0.62f, 1.00f);
            Colors.Border = ImVec4(0.40f, 0.41f, 0.46f, 0.34f);
            Colors.Success = ImVec4(0.286f, 0.820f, 0.537f, 1.00f);
            Colors.Warning = ImVec4(0.945f, 0.671f, 0.275f, 1.00f);
            Colors.Error = ImVec4(0.941f, 0.349f, 0.365f, 1.00f);
            Colors.Info = ImVec4(0.353f, 0.671f, 0.965f, 1.00f);
            Colors.Background = ImVec4(0.f, 0.f, 0.f, 1.f);
            Colors.Surface = ImVec4(0.f, 0.f, 0.f, 1.f);
            Colors.Card = ImVec4(0.f, 0.f, 0.f, 1.f);
            Colors.CardHover = ImVec4(13.f / 255.f, 13.f / 255.f, 13.f / 255.f, 1.f);
            Colors.Panel = Colors.Surface;
            Colors.PanelHover = Colors.CardHover;
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
        colors[ImGuiCol_FrameBg] = Mix(Colors.Background, ImVec4(1, 1, 1, 1), 0.045f);
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

        // Reduced motion: disable transitions
        if (g_reduced_motion) {
            style.HoverDelayNormal = 0.0f;
            style.HoverDelayShort = 0.0f;
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
