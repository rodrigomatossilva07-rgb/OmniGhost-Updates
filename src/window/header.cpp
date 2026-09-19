#include "widgets.h"
#include "theme.h"
#include "fonts.h"
#include "brand_assets.h"
#include "../config/config_manager.h"

#include <cmath>
#include <string>

namespace CyberWidgets {

    void DrawHeader(const ImVec2& wp, const ImVec2& ws, float fps, bool dma_ok,
                    const char* build, int ping_ms, int players)
    {
        // Detailed telemetry lives in the footer; the header carries only the
        // global state needed for orientation.
        (void)fps;
        (void)ping_ms;
        (void)players;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float h = CyberTheme::Metrics::HeaderHeight;
        const ImVec2 a(wp.x, wp.y);
        const ImVec2 b(wp.x + ws.x, wp.y + h);

        dl->AddRectFilled(a, b, CyberTheme::U32(CyberTheme::Colors.Background),
            CyberTheme::Metrics::WindowRounding, ImDrawFlags_RoundCornersTop);

        dl->AddRectFilledMultiColor(
            ImVec2(a.x, a.y), ImVec2(a.x + 320.0f, b.y),
            CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.08f),
            CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.0f),
            CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.0f),
            CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.03f));

        dl->AddLine(ImVec2(a.x, a.y + 1.0f), ImVec2(b.x, a.y + 1.0f),
            CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.10f), 1.0f);
        dl->AddLine(ImVec2(a.x, b.y - 1.0f), ImVec2(b.x, b.y - 1.0f),
            CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.38f), 1.0f);

        const float logo = CyberTheme::Metrics::LogoSize > 0.0f
            ? CyberTheme::Metrics::LogoSize : 36.0f;
        const ImVec2 logo_a(a.x + 16.0f, a.y + (h - logo) * 0.5f);
        const ImVec2 logo_b(logo_a.x + logo, logo_a.y + logo);

        const float pulse = 0.65f + 0.35f * std::sin(static_cast<float>(ImGui::GetTime()) * 1.4f);
        dl->AddCircleFilled(
            ImVec2(logo_a.x + logo * 0.5f, logo_a.y + logo * 0.5f),
            logo * 0.58f,
            IM_COL32(212, 175, 55, static_cast<int>(12 + 10 * pulse)), 28);

        if (ImTextureID tex = BrandAssets::GetLogoTexture()) {
            dl->AddImage(tex, logo_a, logo_b);
        }
        else {
            dl->AddRectFilled(logo_a, logo_b, CyberTheme::U32(CyberTheme::Colors.Panel), 8.0f);
            dl->AddCircle(
                ImVec2(logo_a.x + logo * 0.5f, logo_a.y + logo * 0.5f),
                logo * 0.28f, CyberTheme::U32(CyberTheme::Colors.Gold), 20, 1.5f);
        }
        dl->AddRect(logo_a, logo_b,
            CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.38f), 8.0f, 0, 1.1f);

        // The home header intentionally stays icon-led.  Keeping the brand
        // mark without the old "Omni // Control Center" copy gives the page
        // more room and avoids repeating the application name.

        const char* systemState = dma_ok ? "DMA ONLINE" : "DMA OFFLINE";
        const ImU32 systemColor = dma_ok
            ? CyberTheme::U32(CyberTheme::Colors.Success)
            : CyberTheme::U32(CyberTheme::Colors.Error);
        const float right = b.x - 18.f;
        const std::string profile = std::string("PERFIL  ") + config_manager::CurrentGameProfileName();
        const ImVec2 profileSize = ImGui::CalcTextSize(profile.c_str());
        dl->AddText(ImVec2(right - profileSize.x, a.y + 30.f),
                    CyberTheme::U32(CyberTheme::Colors.TextDisabled), profile.c_str());
        const char* game = build ? build : "FiveM";
        const ImVec2 gameSize = ImGui::CalcTextSize(game);
        dl->AddText(ImVec2(right - gameSize.x, a.y + 12.f),
                    CyberTheme::U32(CyberTheme::Colors.Text), game);
        const ImVec2 stateSize = ImGui::CalcTextSize(systemState);
        const float stateX = right - gameSize.x - stateSize.x - 30.f;
        dl->AddCircleFilled(ImVec2(stateX, a.y + 19.f), 3.f, systemColor, 10);
        dl->AddText(ImVec2(stateX + 9.f, a.y + 12.f), systemColor, systemState);
        
    }

} // namespace CyberWidgets
