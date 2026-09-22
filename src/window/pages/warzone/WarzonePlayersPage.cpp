#include "../../widgets.h"
#include "../../theme.h"
#include "imgui.h"
#include "src/games/Warzone/warzone_game.h"

namespace {
bool g_group_teams = true;
bool g_hide_local = true;
bool g_hide_dead = true;
bool g_hide_ai = true;
}

void DrawWarzonePlayers() {
    CyberWidgets::BeginCard("LISTA DE JOGADORES");
    CyberWidgets::ToggleSwitch("Ativar lista de jogadores", &Warzone::config.player_list_enabled);
    CyberWidgets::CardGap(CyberTheme::Spacing::Xs);
    CyberWidgets::TextLine("A tabela é preenchida quando o runtime disponibiliza um snapshot de jogadores válido.",
                           CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();

    if (!Warzone::config.player_list_enabled) return;
    CyberWidgets::BeginCard("FILTROS");
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
        for (const auto& p : Warzone::runtime.players) {
            if ((g_hide_local && p.is_local) || (g_hide_dead && !p.alive) || (g_hide_ai && p.ai)) continue;
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(p.name[0] ? p.name : "Jogador");
            ImGui::TableSetColumnIndex(1); ImGui::TextDisabled("--"); ImGui::TableSetColumnIndex(2); ImGui::Text("%d",p.team);
            ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(p.downed ? "Derrubado" : p.alive ? "Vivo" : "Morto");
            ImGui::TableSetColumnIndex(4); ImGui::Text("%.0fm",p.distance); ImGui::TableSetColumnIndex(5); ImGui::TextUnformatted(p.ai ? "IA" : p.is_local ? "Local" : "-");
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleColor(2);
    CyberWidgets::EndCard();
}
