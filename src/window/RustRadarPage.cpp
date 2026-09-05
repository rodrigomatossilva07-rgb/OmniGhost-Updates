#include "widgets.h"
#include "theme.h"
#include "../../Rust/rust_config.h"
#include "imgui.h"

void DrawRustRadar() {
    CyberWidgets::BeginCard("Radar 2D local");
    CyberWidgets::TextLine(
        "O radar de Rust é apresentado apenas no overlay local. Não são expostos dados na rede.",
        CyberWidgets::TextTone::Secondary);
    CyberWidgets::ToggleSwitch("Mostrar radar 2D", &Rust::config.radar_2d);
    CyberWidgets::SliderFloat("Tamanho", &Rust::config.radar_size, 80.f, 320.f, "%.0f");
    CyberWidgets::SliderFloat("Posição X", &Rust::config.radar_x, 0.f, 800.f, "%.0f");
    CyberWidgets::SliderFloat("Posição Y", &Rust::config.radar_y, 0.f, 800.f, "%.0f");
    CyberWidgets::EndCard();
}
