#include "../../widgets.h"
#include "../../theme.h"
#ifdef UI_PREVIEW
#include "preview/preview_runtime.h"
#else
#include "esp/esp.h"
#endif

namespace {

void DrawRadarPreview(float previewWidth, float previewHeight)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 q(p.x + previewWidth, p.y + previewHeight);
    const ImVec2 c(p.x + previewWidth * .5f, p.y + previewHeight * .52f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, q, IM_COL32(5, 5, 5, 255), 8.f);
    dl->AddRect(p, q, CyberTheme::WithAlpha(CyberTheme::Colors.Gold, .18f), 8.f);
    for (int i = 1; i <= 3; ++i)
        dl->AddCircle(c, i * 34.f, CyberTheme::WithAlpha(CyberTheme::Colors.Gold, .08f), 48, 1.f);
    dl->AddLine(ImVec2(c.x - 104.f, c.y), ImVec2(c.x + 104.f, c.y),
                CyberTheme::WithAlpha(CyberTheme::Colors.Gold, .10f));
    dl->AddLine(ImVec2(c.x, c.y - 104.f), ImVec2(c.x, c.y + 104.f),
                CyberTheme::WithAlpha(CyberTheme::Colors.Gold, .10f));
    dl->AddTriangleFilled(ImVec2(c.x, c.y - 8.f), ImVec2(c.x - 6.f, c.y + 7.f),
                          ImVec2(c.x + 6.f, c.y + 7.f), CyberTheme::U32(CyberTheme::Colors.Gold));
    if (esp::config.radar_enabled) {
        dl->AddCircleFilled(ImVec2(c.x + 54.f, c.y - 42.f), 4.f, IM_COL32(32, 240, 90, 255), 12);
        dl->AddCircleFilled(ImVec2(c.x - 67.f, c.y + 24.f), 4.f, IM_COL32(255, 86, 95, 255), 12);
        dl->AddCircleFilled(ImVec2(c.x + 25.f, c.y + 72.f), 4.f, IM_COL32(255, 86, 95, 255), 12);
    }
    dl->AddText(ImVec2(p.x + 12.f, p.y + 10.f),
                CyberTheme::U32(CyberTheme::Colors.TextDisabled), "RADAR LOCAL · PRÉ-VISUALIZAÇÃO");
    ImGui::Dummy(ImVec2(previewWidth, previewHeight));
}

} // namespace

void DrawRadar()
{
    const float full = CyberWidgets::CardContentWidth();
    const float gap = CyberTheme::Spacing::Sm;
    const float left = (full - gap) * .58f;
    const float right = full - left - gap;

    ImGui::BeginGroup();
    CyberWidgets::BeginCard("RADAR", left);
    CyberWidgets::ToggleSwitch("Ativar radar", &esp::config.radar_enabled);
    if (esp::config.radar_enabled) {
        CyberWidgets::ToggleSwitch("Radar quadrado", &esp::config.square_radar);
        CyberWidgets::ToggleSwitch("Indicadores triangulares", &esp::config.triangle_radar);
        CyberWidgets::ToggleSwitch("Marcadores no mapa", &esp::config.blip_esp);
        CyberWidgets::ToggleSwitch("Linha para o ponto marcado", &esp::config.waypoint_line);
        CyberWidgets::SliderFloat("Alcance", &esp::config.radar_range, 20.f, 500.f, "%.0f m");
        CyberWidgets::SliderFloat("Tamanho", &esp::config.radar_size, 80.f, 240.f, "%.0f px");
        if (ImGui::CollapsingHeader("POSIÇÃO AVANÇADA", ImGuiTreeNodeFlags_None)) {
            CyberWidgets::SliderFloat("Posição horizontal", &esp::config.radar_pos_x, .05f, .95f, "%.2f");
            CyberWidgets::SliderFloat("Posição vertical", &esp::config.radar_pos_y, .05f, .95f, "%.2f");
        }
    } else {
        CyberWidgets::TextLine("Radar desativado.", CyberWidgets::TextTone::Secondary);
    }
    CyberWidgets::EndCard();
    ImGui::EndGroup();

    ImGui::SameLine(0.f, gap);
    ImGui::BeginGroup();
    CyberWidgets::BeginCard("PRÉ-VISUALIZAÇÃO", right);
    CyberWidgets::Badge(esp::config.radar_enabled ? "ONLINE" : "OFFLINE",
        esp::config.radar_enabled ? CyberWidgets::TextTone::Success : CyberWidgets::TextTone::Secondary);
    DrawRadarPreview((std::max)(160.f, right - 20.f), 240.f);
    CyberWidgets::EndCard();
    ImGui::EndGroup();
}
