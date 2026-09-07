#include "../../widgets.h"
#include "../../theme.h"
#include "imgui.h"

namespace {
bool g_group_teams = true;
bool g_hide_local = true;
bool g_hide_dead = true;
bool g_hide_ai = true;
}

void DrawWarzonePlayers() {
    CyberWidgets::BeginCard("Warzone · Lista de jogadores");
    CyberWidgets::Badge("BETA", CyberWidgets::TextTone::Warning);
    CyberWidgets::CardGap(CyberTheme::Spacing::Xs);
    CyberWidgets::TextLine("A tabela é preenchida quando o runtime disponibiliza um snapshot de jogadores válido.",
                           CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();

    CyberWidgets::BeginCard("Filtros");
    CyberWidgets::ToggleSwitch("Agrupar por times", &g_group_teams);
    CyberWidgets::ToggleSwitch("Esconder local", &g_hide_local);
    CyberWidgets::ToggleSwitch("Esconder mortos", &g_hide_dead);
    CyberWidgets::ToggleSwitch("Esconder IA / bots", &g_hide_ai);
    CyberWidgets::EndCard();

    CyberWidgets::BeginCard("Tabela");
    ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.45f));
    ImGui::PushStyleColor(ImGuiCol_TableBorderLight, CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.20f));
    if (ImGui::BeginTable("##warzone_players_preview", 6,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp,
        ImVec2(CyberWidgets::CardContentWidth(), 260.0f))) {
        ImGui::TableSetupColumn("Nome");
        ImGui::TableSetupColumn("Plataforma");
        ImGui::TableSetupColumn("Time");
        ImGui::TableSetupColumn("Estado");
        ImGui::TableSetupColumn("Distancia");
        ImGui::TableSetupColumn("Notas");
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("Nenhum dado ao vivo");
        ImGui::TableSetColumnIndex(1); ImGui::TextDisabled("--");
        ImGui::TableSetColumnIndex(2); ImGui::TextDisabled("--");
        ImGui::TableSetColumnIndex(3); ImGui::TextDisabled("A aguardar snapshot");
        ImGui::TableSetColumnIndex(4); ImGui::TextDisabled("--");
        ImGui::TableSetColumnIndex(5); ImGui::TextDisabled("BETA");
        ImGui::EndTable();
    }
    ImGui::PopStyleColor(2);
    CyberWidgets::EndCard();
}
