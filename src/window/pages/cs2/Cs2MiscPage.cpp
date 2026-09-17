#include "../../widgets.h"
#include "Cs2/cs2_game.h"
#include "Cs2/config/cs2_config.h"
#include "../../localization.h"
#include "imgui.h"

void DrawCs2Misc()
{
    using namespace CyberWidgets;
    ImGui::PushID("cs2_misc");
    BeginCard("DIVERSOS", 0.f);
    TextLine("Widgets e informação útil durante a partida.", TextTone::Secondary);
    ToggleSwitch(Loc::Tr("cs2.misc.spectators"), &CS2::config.spectator_list);
    ToggleSwitch(Loc::Tr("cs2.misc.bomb_timer"), &CS2::config.bomb_timer);
    ToggleSwitch(Loc::Tr("cs2.misc.hit_marker"), &CS2::config.hit_marker);
    ToggleSwitch("Indicador de portador da C4", &CS2::config.c4_carrier);
    ToggleSwitch("Estado de flash nos jogadores", &CS2::config.smoke_flash);
    ToggleSwitch("Rasto de granadas", &CS2::config.grenade_trail);
    if (CS2::config.bomb_timer) {
        TextLine("Com o menu aberto, arrasta as janelas de bomba e espectadores para guardar a posição.", TextTone::Secondary);
    }
    EndCard();

    ImGui::PopID();
}
