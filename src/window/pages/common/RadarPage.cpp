#include "../../widgets.h"
#include "../../theme.h"
#include "config/app_settings.h"
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
    dl->AddRect(ImVec2(c.x - 104.f, c.y - 104.f), ImVec2(c.x + 104.f, c.y + 104.f),
                CyberTheme::WithAlpha(CyberTheme::Colors.Gold, .24f), 7.f, 0, 1.5f);
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

}

void DrawRadar()
{
    const float full = CyberWidgets::CardContentWidth();
    const float gap = CyberTheme::Spacing::Sm;
    const float left = (full - gap) * .58f;
    const float right = full - left - gap;

    ImGui::BeginGroup();
    CyberWidgets::BeginCard("RADAR", left);
    if (CyberWidgets::ToggleSwitch(app_settings::T("Ativar radar", "Enable radar"), &esp::config.radar_enabled) && esp::config.radar_enabled)
        esp::config.square_radar = true;
    if (esp::config.radar_enabled) {
        CyberWidgets::ToggleSwitch(app_settings::T("Radar quadrado", "Square radar"), &esp::config.square_radar);
        CyberWidgets::ToggleSwitch(app_settings::T("Indicadores triangulares", "Triangle indicators"), &esp::config.triangle_radar);
        CyberWidgets::ToggleSwitch(app_settings::T("Marcadores no mapa", "Map markers"), &esp::config.blip_esp);
        CyberWidgets::ToggleSwitch(app_settings::T("Linha para o ponto marcado", "Line to waypoint"), &esp::config.waypoint_line);
        CyberWidgets::SliderFloat(app_settings::T("Alcance", "Range"), &esp::config.radar_range, 20.f, 500.f, "%.0f m");
        CyberWidgets::SliderFloat(app_settings::T("Tamanho", "Size"), &esp::config.radar_size, 80.f, 240.f, "%.0f px");
        CyberWidgets::TextLine(app_settings::T("Com o menu aberto, arrasta o radar diretamente no ecrã.",
                                              "With the menu open, drag the radar directly on screen."),
            CyberWidgets::TextTone::Secondary);
        CyberWidgets::SliderFloat(app_settings::T("Posição horizontal", "Horizontal position"), &esp::config.radar_pos_x, .05f, .95f, "%.2f");
        CyberWidgets::SliderFloat(app_settings::T("Posição vertical", "Vertical position"), &esp::config.radar_pos_y, .05f, .95f, "%.2f");
    } else {
        CyberWidgets::TextLine(app_settings::T("Radar desativado.", "Radar disabled."), CyberWidgets::TextTone::Secondary);
    }
    CyberWidgets::EndCard();

    CyberWidgets::CardGap(gap);
    CyberWidgets::BeginCard(app_settings::T("WEB RADAR", "WEB RADAR"), left);
    CyberWidgets::Badge(app_settings::T("EM PREPARAÇÃO", "IN PREPARATION"),
        CyberWidgets::TextTone::Warning);
    CyberWidgets::TextLine(
        app_settings::T(
            "A interface do Web Radar está preparada, mas o servidor e a publicação remota ainda não estão ativos.",
            "The Web Radar interface is prepared, but its server and remote publishing are not active yet."),
        CyberWidgets::TextTone::Secondary);

    bool webRadarPreviewEnabled = false;
    ImGui::BeginDisabled();
    CyberWidgets::ToggleSwitch(
        app_settings::T("Ativar Web Radar", "Enable Web Radar"),
        &webRadarPreviewEnabled);
    CyberWidgets::KeyValueRow(
        app_settings::T("Ligação", "Connection"),
        app_settings::T("Ainda não configurada", "Not configured yet"));
    CyberWidgets::KeyValueRow(
        app_settings::T("Link público", "Public link"),
        "https://---.trycloudflare.com");
    CyberWidgets::Button(
        app_settings::T("Copiar link", "Copy link"),
        CyberWidgets::ButtonStyle::Secondary,
        ImVec2(132.f, 32.f));
    ImGui::SameLine();
    CyberWidgets::Button(
        app_settings::T("Abrir link", "Open link"),
        CyberWidgets::ButtonStyle::Ghost,
        ImVec2(132.f, 32.f));
    ImGui::EndDisabled();

    CyberWidgets::TextLine(
        app_settings::T(
            "Estes controlos são apenas uma pré-visualização e não iniciam qualquer serviço.",
            "These controls are only a preview and do not start any service."),
        CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();
    ImGui::EndGroup();

    ImGui::SameLine(0.f, gap);
    ImGui::BeginGroup();
    CyberWidgets::BeginCard(app_settings::T("PRÉ-VISUALIZAÇÃO", "PREVIEW"), right);
    CyberWidgets::Badge(esp::config.radar_enabled ? "ONLINE" : "OFFLINE",
        esp::config.radar_enabled ? CyberWidgets::TextTone::Success : CyberWidgets::TextTone::Secondary);
    DrawRadarPreview((std::max)(160.f, right - 20.f), 240.f);
    CyberWidgets::EndCard();

    CyberWidgets::CardGap(gap);
    CyberWidgets::BeginCard(app_settings::T("ESTADO", "STATUS"), right);
    CyberWidgets::KeyValueRow(
        app_settings::T("Radar 2D", "2D Radar"),
        esp::config.radar_enabled
            ? app_settings::T("Ativo", "Active")
            : app_settings::T("Desativado", "Disabled"));
    CyberWidgets::KeyValueRow(
        app_settings::T("Web Radar", "Web Radar"),
        app_settings::T("Em preparação", "In preparation"));
    CyberWidgets::KeyValueRow(
        app_settings::T("Acesso remoto", "Remote access"),
        app_settings::T("Não iniciado", "Not started"));
    CyberWidgets::EndCard();
    ImGui::EndGroup();
}
