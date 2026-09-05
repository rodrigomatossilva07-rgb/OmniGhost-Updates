#include "widgets.h"
#include "theme.h"
#include "fonts.h"
#include "brand_assets.h"

#include <cmath>

namespace CyberWidgets {

    void DrawHeader(const ImVec2& wp, const ImVec2& ws, float fps, bool dma_ok,
                    const char* build, int ping_ms, int players)
    {
        // Status metrics live only in the footer.
        (void)fps;
        (void)dma_ok;
        (void)build;
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

        ImFont* title = CyberFonts::GetTitleFont();
        ImFont* body = CyberFonts::GetBodyFont();
        const float text_x = logo_b.x + 14.0f;

        if (title)
            dl->AddText(title, 20.0f, ImVec2(text_x, a.y + 12.0f),
                CyberTheme::U32(CyberTheme::Colors.Text), "OMNIGHOST");
        else
            dl->AddText(ImVec2(text_x, a.y + 12.0f),
                CyberTheme::U32(CyberTheme::Colors.Text), "OMNIGHOST");

        if (body)
            dl->AddText(body, 11.5f, ImVec2(text_x + 1.0f, a.y + 35.0f),
                IM_COL32(160, 165, 180, 220), "CONTROL CENTER");
        else
            dl->AddText(ImVec2(text_x + 1.0f, a.y + 35.0f),
                IM_COL32(160, 165, 180, 220), "CONTROL CENTER");

        if (body) {
            dl->AddText(body, 10.5f, ImVec2(b.x - 118.0f, a.y + (h - 11.0f) * 0.5f),
                CyberTheme::WithAlpha(CyberTheme::Colors.TextDisabled, 0.55f),
                "PREMIUM DMA");
        }
    }

} // namespace CyberWidgets
