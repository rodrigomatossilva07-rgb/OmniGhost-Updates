#include "transition_overlay.h"

#include "brand_assets.h"
#include "digital_rain.h"
#include "performance_mode.h"
#include "theme.h"
#include "../config/app_settings.h"
#include "../../ImGui/imgui.h"

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
    DigitalRain::SetQualityFromEffectLevel(static_cast<int>(app_settings::config.digital_rain_level));
    DigitalRain::Draw(background, ImVec2(0, 0), display, true,
        performance.effective, 0.0f, 0.075f * alpha, 0.40f,
        app_settings::AnimationScale(), true);

    const ImVec2 center(display.x * 0.5f, display.y * 0.5f - 18.0f * scale);
    const float pulse = app_settings::MotionEnabled()
        ? 1.0f + std::sin(options.elapsedSeconds * 2.15f) * 0.018f
        : 1.0f;
    const float logoSize = 62.0f * scale * pulse;
    if (ImTextureID logo = BrandAssets::GetLogoTexture()) {
        foreground->AddImage(logo,
            ImVec2(center.x - logoSize * 0.5f, center.y - 112.0f * scale),
            ImVec2(center.x + logoSize * 0.5f, center.y - 112.0f * scale + logoSize),
            ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, static_cast<int>(178.0f * alpha)));
    }

    const std::string title(options.title);
    const ImVec2 titleSize = ImGui::CalcTextSize(title.c_str());
    const ImU32 titleColor = options.success
        ? CyberTheme::WithAlpha(CyberTheme::Colors.Success, alpha)
        : CyberTheme::WithAlpha(CyberTheme::Colors.Text, alpha);
    foreground->AddText(ImVec2(center.x - titleSize.x * 0.5f, center.y - 25.0f * scale),
        titleColor, title.c_str());

    const float lineWidth = 112.0f * scale;
    const float lineY = center.y + 12.0f * scale;
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
        foreground->AddText(ImVec2(center.x - subtitleSize.x * 0.5f, center.y + 36.0f * scale),
            CyberTheme::WithAlpha(CyberTheme::Colors.TextDisabled, 0.82f * alpha), subtitle.c_str());
    }
}

} // namespace OmniGhost::UI
