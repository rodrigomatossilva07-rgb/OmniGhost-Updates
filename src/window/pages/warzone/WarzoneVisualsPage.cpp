#include "../../widgets.h"
#include "../../theme.h"
#include "imgui.h"
#include "../../Warzone/warzone_game.h"

void DrawWarzoneVisuals() {
    CyberWidgets::BeginCard("ESP de Jogador");
    CyberWidgets::ToggleSwitch("Ativar ESP", &Warzone::config.esp_enabled);
    CyberWidgets::ToggleSwitch("Verificar equipa", &Warzone::config.team_check);
    CyberWidgets::ToggleSwitch("Ignorar derrubados", &Warzone::config.ignore_downed);
    CyberWidgets::ToggleSwitch("Ignorar IA", &Warzone::config.ignore_ai);
    ImGui::SliderInt("Distância máxima", &Warzone::config.max_distance, 50, 800);
    CyberWidgets::EndCard();

    CyberWidgets::BeginCardRow();
    CyberWidgets::BeginCard("Elementos");
    CyberWidgets::ToggleSwitch("Caixa 2D", &Warzone::config.box);
    CyberWidgets::ToggleSwitch("Caixa de canto", &Warzone::config.box_corner);
    CyberWidgets::ToggleSwitch("Esqueleto", &Warzone::config.skeleton);
    CyberWidgets::ToggleSwitch("Círculo na cabeça", &Warzone::config.head_dot);
    CyberWidgets::ToggleSwitch("Barra de vida", &Warzone::config.health_bar);
    CyberWidgets::ToggleSwitch("Nome", &Warzone::config.name);
    CyberWidgets::ToggleSwitch("Distância", &Warzone::config.distance);
    CyberWidgets::ToggleSwitch("Linhas guia", &Warzone::config.snaplines);
    CyberWidgets::EndCard();

    CyberWidgets::NextCardColumn();
    CyberWidgets::BeginCard("Cores / estado");
    ImGui::ColorEdit4("Inimigo", Warzone::config.col_enemy, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Alvo", Warzone::config.col_target, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::Text("Jogadores: %d · matriz: %s · lista: %s",
        Warzone::runtime.player_count,
        Warzone::runtime.matrix_ok ? "OK" : "—",
        Warzone::runtime.list_ok ? "OK" : "—");
    ImGui::TextWrapped("%s", Warzone::StatusLine());
    CyberWidgets::EndCard();
    CyberWidgets::EndCardRow();
}
