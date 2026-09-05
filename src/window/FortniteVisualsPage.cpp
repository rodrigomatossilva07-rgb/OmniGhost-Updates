#include "widgets.h"
#include "../../Fortnite/fortnite_game.h"
#include "imgui.h"

void DrawFortniteVisuals()
{
    using namespace CyberWidgets;
    ImGui::PushID("fortnite_visuals");

    // Beta/Experimental indicator
    InlineMessage("Fortnite support is in Beta — LocalPlayer ESP only. No enemy enumeration.", TextTone::Warning, "fn_beta_warning");

    BeginCard("LocalPlayer ESP (fase 1)");
    ImGui::TextWrapped(
        "Apenas LocalPlayer. Sem enumeracao de inimigos. "
        "Testa no lobby: chain GEngine -> Viewport -> World -> LocalPlayer -> Camera.");
    ToggleSwitch("Ativar ESP", &Fortnite::config.esp_enabled);
    ToggleSwitch("Marcador Local Player", &Fortnite::config.show_local_marker);
    ToggleSwitch("Coordenadas no ecrã", &Fortnite::config.show_local_coords);
    ToggleSwitch("Painel debug overlay", &Fortnite::config.show_debug_panel);
    ToggleSwitch("Debug camera no overlay", &Fortnite::config.show_camera_debug);
    EndCard();

    BeginCard("Cores");
    ImGui::ColorEdit4("Local", Fortnite::config.col_local,
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    EndCard();

    BeginCard("Experimental (disabled)");
    ImGui::TextDisabled("Caixa / esqueleto / equipa / distancia — proximas fases.");
    ImGui::BeginDisabled();
    ToggleSwitch("Caixa", &Fortnite::config.box);
    ToggleSwitch("Esqueleto", &Fortnite::config.skeleton);
    ToggleSwitch("Nome", &Fortnite::config.name);
    ToggleSwitch("Distancia", &Fortnite::config.distance);
    ImGui::EndDisabled();
    EndCard();

    ImGui::PopID();
}
