#include "transition_overlay.h"

#include "brand_assets.h"
#include "digital_rain.h"
#include "performance_mode.h"
#include "theme.h"
#include "../config/app_settings.h"
#include "src/project/third_party/ImGui/imgui.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace OmniGhost::UI {

void DrawTransitionOverlay(const TransitionOverlayOptions& options) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImDrawList* background = ImGui::GetBackgroundDrawList();
    ImDrawList* foreground = ImGui::GetForegroundDrawList();
    const float alpha = std::clamp(options.alpha, 0.0f, 1.0f);
    const float scale = CyberTheme::Px(1.0f);

    background->AddRectFilled(ImVec2(0, 0), display,
        IM_COL32(3, 3, 4, static_cast<int>(255.0f * alpha)));
    const PerformanceMode::State performance = PerformanceMode::Current();
    const float rainDensity = app_settings::DigitalRainDensity();
    if (rainDensity > 0.0f) {
        const float rainOpacity = std::max(app_settings::DigitalRainOpacity(), 0.64f);
        DigitalRain::Draw(background, ImVec2(0, 0), display, true,
            performance.effective, 0.0f,
            rainOpacity * 0.68f * alpha,
            rainDensity * 0.82f,
            app_settings::AnimationScale(), false, true);
        DigitalRain::DrawGoldMotif(background, ImVec2(0, 0), display,
            performance.effective, app_settings::AnimationScale(),
            (performance.effective ? 0.72f : 0.88f) * alpha);
    }

    const ImVec2 center(display.x * 0.5f, display.y * 0.5f - 4.0f * scale);
    const float panelHalfWidth = (std::min)(220.0f * scale, display.x * 0.43f);
    const ImVec2 panelMin(center.x - panelHalfWidth, center.y - 132.0f * scale);
    const ImVec2 panelMax(center.x + panelHalfWidth, center.y + 104.0f * scale);
    foreground->AddRectFilled(panelMin, panelMax,
        CyberTheme::WithAlpha(CyberTheme::Colors.Card, 0.978f * alpha), 16.0f * scale);
    foreground->AddRect(panelMin, panelMax,
        CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.20f * alpha), 16.0f * scale,
        0, std::max(1.0f, scale));
    const char* brand = "OMNIGHOST";
    const ImVec2 brandSize = ImGui::CalcTextSize(brand);
    foreground->AddText(ImVec2(center.x - brandSize.x * 0.5f, panelMin.y + 18.0f * scale),
        CyberTheme::WithAlpha(CyberTheme::Colors.GoldHover, 0.78f * alpha), brand);
    const float pulse = app_settings::MotionEnabled()
        ? 1.0f + std::sin(options.elapsedSeconds * 2.15f) * 0.018f
        : 1.0f;
    const float logoSize = 43.0f * scale * pulse;
    const ImVec2 logoCenter(center.x, center.y - 69.0f * scale);
    foreground->AddCircle(logoCenter, 32.0f * scale,
        CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.15f * alpha), 48, scale);
    if (ImTextureID logo = BrandAssets::GetLogoTexture()) {
        foreground->AddImage(logo,
            ImVec2(logoCenter.x - logoSize * 0.5f, logoCenter.y - logoSize * 0.5f),
            ImVec2(logoCenter.x + logoSize * 0.5f, logoCenter.y + logoSize * 0.5f),
            ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, static_cast<int>(178.0f * alpha)));
    }

    if (app_settings::MotionEnabled() && !options.success) {
        const float orbit = options.elapsedSeconds * 1.55f;
        const ImVec2 orbitCenter = logoCenter;
        foreground->AddCircle(orbitCenter, 33.0f * scale,
            CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.14f * alpha), 48, scale);
        foreground->AddCircleFilled(
            ImVec2(orbitCenter.x + std::cos(orbit) * 33.0f * scale,
                   orbitCenter.y + std::sin(orbit) * 33.0f * scale),
            2.6f * scale, CyberTheme::WithAlpha(CyberTheme::Colors.GoldHover, 0.90f * alpha), 16);
    }

    const std::string title(options.title);
    const ImVec2 titleSize = ImGui::CalcTextSize(title.c_str());
    const ImU32 titleColor = options.success
        ? CyberTheme::WithAlpha(CyberTheme::Colors.Success, alpha)
        : CyberTheme::WithAlpha(CyberTheme::Colors.Text, alpha);
    foreground->AddText(ImVec2(center.x - titleSize.x * 0.5f, center.y - 18.0f * scale),
        titleColor, title.c_str());

    const float lineWidth = 112.0f * scale;
    const float lineY = center.y + 17.0f * scale;
    foreground->AddRectFilled(
        ImVec2(center.x - lineWidth * 0.5f, lineY),
        ImVec2(center.x + lineWidth * 0.5f, lineY + scale),
        CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.42f * alpha));
    const float phase = app_settings::MotionEnabled()
        ? std::fmod(options.elapsedSeconds * 0.85f, 1.0f)
        : 0.5f;
    const float segment = 34.0f * scale;
    const float start = center.x - lineWidth * 0.5f + (lineWidth + segment) * phase - segment;
    foreground->PushClipRect(ImVec2(center.x - lineWidth * 0.5f, lineY - scale),
        ImVec2(center.x + lineWidth * 0.5f, lineY + 3.0f * scale), true);
    foreground->AddRectFilled(ImVec2(start, lineY), ImVec2(start + segment, lineY + 2.0f * scale),
        CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.92f * alpha));
    foreground->PopClipRect();

    if (!options.subtitle.empty()) {
        const std::string subtitle(options.subtitle);
        const ImVec2 subtitleSize = ImGui::CalcTextSize(subtitle.c_str());
        foreground->AddText(ImVec2(center.x - subtitleSize.x * 0.5f, center.y + 43.0f * scale),
            CyberTheme::WithAlpha(CyberTheme::Colors.TextDisabled, 0.82f * alpha), subtitle.c_str());
    }
}

}
