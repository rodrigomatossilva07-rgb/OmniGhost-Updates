#include "widgets.h"
#include "../../Valorant/valorant_config.h"
#include "imgui.h"

void DrawValorantVisuals()
{
    using namespace CyberWidgets;
    ImGui::PushID("valorant_visuals");

    BeginCard("ESP de jogadores");
    ToggleSwitch("Ativar ESP", &Valorant::config.esp_enabled);
    ToggleSwitch("ESP próprio", &Valorant::config.self_esp);
    ToggleSwitch("Verificar equipa", &Valorant::config.team_check);
    ToggleSwitch("Cores por visibilidade", &Valorant::config.visibility_colors);
    ToggleSwitch("Caixa", &Valorant::config.box);
    ToggleSwitch("Caixa de canto", &Valorant::config.box_corner);
    ToggleSwitch("Esqueleto", &Valorant::config.skeleton);
    ToggleSwitch("Articulações", &Valorant::config.skeleton_joints);
    ToggleSwitch("Barra de vida", &Valorant::config.health_bar);
    ToggleSwitch("Barra de escudo", &Valorant::config.armor_bar);
    ToggleSwitch("Nome", &Valorant::config.name);
    ToggleSwitch("Distância", &Valorant::config.distance);
    ToggleSwitch("Arma", &Valorant::config.weapon_name);
    ToggleSwitch("Ponto na cabeça", &Valorant::config.head_dot);
    ToggleSwitch("Linhas guia", &Valorant::config.snaplines);
    SliderFloat("Distância máxima", &Valorant::config.max_distance, 20.f, 500.f, "%.0f m");
    EndCard();

    BeginCard("Esqueleto e desempenho");
    ToggleSwitch("LOD do esqueleto", &Valorant::config.skeleton_lod);
    SliderFloat("Distância LOD", &Valorant::config.skeleton_lod_distance, 20.f, 250.f, "%.0f m");
    ToggleSwitch("Desenhar braços", &Valorant::config.bone_draw_arms);
    ToggleSwitch("Desenhar pernas", &Valorant::config.bone_draw_legs);
    ToggleSwitch("Modo de desempenho", &Valorant::config.performance_mode);
    EndCard();

    BeginCard("Cores");
    ImGui::ColorEdit4("Inimigo", Valorant::config.col_enemy, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Equipa", Valorant::config.col_team, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Esqueleto", Valorant::config.col_skeleton, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Caixa", Valorant::config.col_box, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Nome", Valorant::config.col_name, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Arma", Valorant::config.col_weapon, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Articulações", Valorant::config.col_joints, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Vida", Valorant::config.col_health, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Escudo", Valorant::config.col_armor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Linhas guia", Valorant::config.col_snaplines, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    EndCard();

    ImGui::PopID();
}
