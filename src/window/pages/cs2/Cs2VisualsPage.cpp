#include "../../widgets.h"
#include "config/cs2_config.h"

void DrawCs2Visuals() {
    CyberWidgets::BeginCard("VISUAIS CS2");
    CyberWidgets::TextLine("ESP de jogadores — etapa 1", CyberWidgets::TextTone::Secondary);
    CyberWidgets::ToggleSwitch("Ativar ESP", &CS2::config.esp_enabled);
    CyberWidgets::ToggleSwitch("Caixa 2D", &CS2::config.box);
    CyberWidgets::SliderFloat("Espessura da caixa", &CS2::config.box_thickness, 0.5f, 4.f, "%.1f");
    CyberWidgets::EndCard();
}
