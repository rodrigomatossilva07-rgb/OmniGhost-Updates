#include "../../widgets.h"
#include "../../localization.h"
#include "../../Fortnite/fortnite_game.h"
#include "imgui.h"

void DrawFortniteAim()
{
    using namespace CyberWidgets;
    ImGui::PushID("fortnite_aim");

    // Beta/Experimental indicator
    InlineMessage("Fortnite Aimbot is in Beta — visual preview only. No input injection.", TextTone::Warning, "fn_aim_beta_warning");

    BeginCard("Aimbot (visual — sem input)");
    ImGui::TextWrapped(
        "Aimbot e Predict estao desligados no runtime ate a cadeia GWorld/UWorld "
        "estar confirmada no teu build. Os controlos abaixo sao so UI.");
    ImGui::BeginDisabled();
    ToggleSwitch("Ativar aimbot", &Fortnite::config.aim_enabled);
    ToggleSwitch("So visiveis", &Fortnite::config.aim_visible_only);
    ToggleSwitch("Predict", &Fortnite::config.predict);
    SliderFloat(Loc::Tr("aim.smooth"), &Fortnite::config.aim_smooth, 0.f, 100.f, "%.0f");
    SliderFloat("FOV", &Fortnite::config.aim_fov, 10.f, 180.f, "%.0f");
    SliderFloat("Velocidade da bala", &Fortnite::config.bullet_velocity, 10000.f, 120000.f, "%.0f");
    ImGui::EndDisabled();
    EndCard();

    ImGui::PopID();
}
