#include "application_transitions.h"

#include "../config/app_settings.h"
#include "../platform/shutdown_coordinator.h"
#include "../window/brand_assets.h"
#include "../window/digital_rain.h"
#include "../window/fonts.h"
#include "../window/performance_mode.h"
#include "../window/theme.h"
#include "../window/transition_overlay.h"
#include "../window/window.hpp"
#include "third_party/ImGui/imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <future>
#include <iostream>

namespace OmniGhost::UI {

void DrawDmaPreparation(std::chrono::steady_clock::time_point started) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImDrawList* background = ImGui::GetBackgroundDrawList();
    background->AddRectFilled(ImVec2(0, 0), display, IM_COL32(3, 3, 4, 255));
    const float animation = app_settings::AnimationScale();
    DigitalRain::Draw(background, ImVec2(0, 0), display, true,
        PerformanceMode::Current().effective, 0.0f,
        app_settings::DigitalRainOpacity() * 0.48f,
        app_settings::DigitalRainDensity() * 0.82f, animation, false, true);

    const float elapsed = std::chrono::duration<float>(
        std::chrono::steady_clock::now() - started).count();
    const float fade = animation <= 0.f ? 1.f : std::min(1.f, elapsed / 0.20f);
    const ImU32 gold = CyberTheme::WithAlpha(CyberTheme::Colors.GoldHover, 0.88f * fade);
    const ImU32 text = CyberTheme::WithAlpha(CyberTheme::Colors.Text, fade);
    const ImVec2 center(display.x * 0.5f, display.y * 0.44f);
    CyberTheme::DrawRadialAccent(background, center, CyberTheme::Px(220.f), 0.055f * fade);

    const ImVec2 panelMin(center.x - CyberTheme::Px(252.f), center.y - CyberTheme::Px(150.f));
    const ImVec2 panelMax(center.x + CyberTheme::Px(252.f), center.y + CyberTheme::Px(132.f));
    background->AddRectFilled(panelMin, panelMax,
        CyberTheme::WithAlpha(CyberTheme::Colors.Card, 0.965f * fade), CyberTheme::Px(14.f));
    background->AddRect(panelMin, panelMax,
        CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.24f * fade), CyberTheme::Px(14.f),
        0, CyberTheme::Px(1.f));
    background->AddRectFilled(
        ImVec2(panelMin.x, panelMin.y + CyberTheme::Px(34.f)),
        ImVec2(panelMin.x + CyberTheme::Px(3.f), panelMax.y - CyberTheme::Px(34.f)),
        CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.84f * fade), CyberTheme::Px(2.f));
    background->AddText(ImVec2(panelMin.x + CyberTheme::Px(22.f), panelMin.y + CyberTheme::Px(17.f)),
        CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.72f * fade),
        "OMNIGHOST // GAME SESSION");

    if (ImTextureID logo = BrandAssets::GetLogoTexture()) {
        const float pulse = animation <= 0.f ? 1.f : 1.f + std::sin(elapsed * 1.8f) * 0.025f;
        const float size = CyberTheme::Px(54.f) * pulse;
        background->AddImage(logo,
            ImVec2(center.x - size * 0.5f, center.y - CyberTheme::Px(82.f) - size * 0.5f),
            ImVec2(center.x + size * 0.5f, center.y - CyberTheme::Px(82.f) + size * 0.5f),
            ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, static_cast<int>(215.f * fade)));
    }
    const char* title = "A preparar sessão DMA";
    ImFont* font = CyberFonts::GetTitleFont();
    const float fontSize = font ? CyberTheme::Px(20.f) : ImGui::GetFontSize();
    const ImVec2 titleSize = font
        ? font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, title)
        : ImGui::CalcTextSize(title);
    background->AddText(font ? font : ImGui::GetFont(), fontSize,
        ImVec2(center.x - titleSize.x * 0.5f, center.y), text, title);

    static constexpr const char* stages[] = {
        "A validar o jogo e as permissões",
        "A preparar a ligação DMA",
        "A iniciar os módulos da sessão",
        "A concluir o ambiente seguro"
    };
    const int stage = std::min(3, static_cast<int>(elapsed / 0.72f));
    const char* stageText = stages[stage];
    const ImVec2 stageSize = ImGui::CalcTextSize(stageText);
    background->AddText(ImVec2(center.x - stageSize.x * 0.5f, center.y + CyberTheme::Px(24.f)),
        CyberTheme::WithAlpha(CyberTheme::Colors.TextDisabled, 0.86f * fade), stageText);

    const float width = CyberTheme::Px(118.f);
    const float y = center.y + CyberTheme::Px(62.f);
    background->AddLine(ImVec2(center.x - width * 0.5f, y),
        ImVec2(center.x + width * 0.5f, y),
        CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.15f * fade), CyberTheme::Px(1.f));
    const float phase = animation <= 0.f ? 0.5f : std::fmod(elapsed * 0.72f, 1.f);
    const float segment = width * 0.34f;
    const float startX = center.x - width * 0.5f + (width + segment) * phase - segment;
    background->PushClipRect(ImVec2(center.x - width * 0.5f, y - CyberTheme::Px(2.f)),
        ImVec2(center.x + width * 0.5f, y + CyberTheme::Px(2.f)), true);
    background->AddLine(ImVec2(startX, y), ImVec2(startX + segment, y), gold, CyberTheme::Px(1.7f));
    background->PopClipRect();

    const float dotsY = y + CyberTheme::Px(24.f);
    for (int index = 0; index < 4; ++index) {
        const float x = center.x + (static_cast<float>(index) - 1.5f) * CyberTheme::Px(24.f);
        background->AddCircleFilled(ImVec2(x, dotsY), CyberTheme::Px(index == stage ? 3.2f : 2.1f),
            CyberTheme::WithAlpha(index <= stage ? CyberTheme::Colors.GoldHover
                                                  : CyberTheme::Colors.TextDisabled,
                                  index == stage ? 0.94f * fade : 0.30f * fade), 16);
    }
}

void RenderTimedTransition(Overlay& application, const char* title,
                           const char* subtitle, float durationSeconds,
                           bool success) {
    const auto started = std::chrono::steady_clock::now();
    while (application.shouldRun) {
        const float elapsed = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - started).count();
        if (elapsed >= durationSeconds)
            break;
        application.StartRender();
        if (!application.shouldRun)
            break;
        const float fadeIn = app_settings::MotionEnabled()
            ? std::clamp(elapsed / 0.16f, 0.0f, 1.0f) : 1.0f;
        DrawTransitionOverlay({title, subtitle, elapsed, fadeIn, success});
        application.EndRender();
    }
}

void EndGameSessionWithTransition(Overlay& application,
    Platform::ShutdownCoordinator& coordinator, std::uint64_t generation,
    bool requestedByUser) {
    const auto started = std::chrono::steady_clock::now();
    auto teardown = std::async(std::launch::async, [&coordinator, generation, requestedByUser] {
        return coordinator.EndSession(generation,
            requestedByUser ? "user-return-to-launcher" : "game-process-ended");
    });
    const float minimum = app_settings::MotionEnabled()
        ? (requestedByUser ? 0.34f : 0.18f) : 0.0f;
    while (application.shouldRun) {
        const float elapsed = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - started).count();
        const bool finished = teardown.wait_for(std::chrono::milliseconds(0)) ==
            std::future_status::ready;
        if (finished && elapsed >= minimum)
            break;
        application.StartRender();
        if (!application.shouldRun)
            break;
        DrawTransitionOverlay({"A terminar sessão", {}, elapsed, 1.0f, false});
        application.EndRender();
    }





    try {
        (void)teardown.get();
    } catch (const std::exception& ex) {
        std::cerr << "[SESSION] EndSession falhou durante o teardown (ignorado): "
                  << ex.what() << std::endl;
    } catch (...) {
        std::cerr << "[SESSION] EndSession falhou durante o teardown (ignorado): "
                     "exceção desconhecida." << std::endl;
    }
}

}
