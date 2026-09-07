#include "../../widgets.h"
#include "../../InputDevicesCard.h"
#include "../../Valorant/valorant_config.h"
#include "imgui.h"

void DrawValorantAim()
{
    using namespace CyberWidgets;
    ImGui::PushID("valorant_aim");

    BeginCard("Sistema de mira");
    ToggleSwitch("Ativar mira", &Valorant::config.aim_enabled);
    ToggleSwitch("Mostrar FOV", &Valorant::config.aim_draw_fov);
    ToggleSwitch("FOV RGB", &Valorant::config.aim_fov_rgb);
    ToggleSwitch("Humanização", &Valorant::config.aim_humanize);
    ToggleSwitch("Previsão", &Valorant::config.aim_prediction);
    ToggleSwitch("Destacar alvo", &Valorant::config.highlight_aim_target);
    const char* bones[] = { "Cabeça", "Pescoço", "Peito", "Pelve" };
    Combo("Zona do alvo", &Valorant::config.aim_bone, bones, 4);
    const char* styles[] = { "Círculo", "Quadrado", "Cruz" };
    Combo("Estilo FOV", &Valorant::config.aim_fov_style, styles, 3);
    SliderFloat("FOV", &Valorant::config.aim_fov, 10.f, 400.f, "%.0f px");
    SliderFloat("Suavização", &Valorant::config.aim_smooth, 0.f, 100.f, "%.0f");
    SliderFloat("Distância máxima", &Valorant::config.aim_max_dist, 10.f, 500.f, "%.0f m");
    SliderFloat("Zona morta", &Valorant::config.aim_deadzone, 0.f, 30.f, "%.1f px");
    SliderFloat("Persistência", &Valorant::config.sticky_ms, 0.f, 500.f, "%.0f ms");
    ImGui::ColorEdit4("Cor FOV", Valorant::config.col_fov, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    InputInt("Tecla principal (VK)", &Valorant::config.aim_bind);
    InputInt("Tecla secundária (VK)", &Valorant::config.aim_bind2);
    EndCard();

    BeginCard("Disparo automático");
    ToggleSwitch("Ativar disparo automático", &Valorant::config.trigger_enabled);
    ToggleSwitch("Verificar equipa", &Valorant::config.trigger_team_check);
    ToggleSwitch("Apenas cabeça", &Valorant::config.trigger_head_only);
    InputInt("Tecla (VK)", &Valorant::config.trigger_bind);
    InputInt("Atraso (ms)", &Valorant::config.trigger_delay_ms);
    if (Valorant::config.trigger_delay_ms < 0) Valorant::config.trigger_delay_ms = 0;
    if (Valorant::config.trigger_delay_ms > 1000) Valorant::config.trigger_delay_ms = 1000;
    EndCard();

    InputDevicesCard::Draw();
    ImGui::PopID();
}
