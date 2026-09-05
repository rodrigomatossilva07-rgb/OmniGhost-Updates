#include "widgets.h"

namespace {
struct RadarUiState {
    bool enabled = false;
    bool rotate = true;
    bool show_team = true;
    bool show_names = true;
    bool show_height = true;
    bool border = true;
    float range = 120.0f;
    float scale = 100.0f;
    float marker_size = 6.0f;
    int shape = 0;
};
RadarUiState g_radar;
}

void DrawWarzoneRadar() {
    CyberWidgets::BeginCard("Warzone · Radar");
    CyberWidgets::TextLine("Configuracoes do radar organizadas sem painel de preview, para manter a pagina mais limpa.",
                           CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();

    CyberWidgets::BeginCard("Configuracoes gerais");
    CyberWidgets::ToggleSwitch("Radar ativado", &g_radar.enabled);
    CyberWidgets::ToggleSwitch("Rotacionar com direcao", &g_radar.rotate);
    CyberWidgets::ToggleSwitch("Mostrar time", &g_radar.show_team);
    CyberWidgets::ToggleSwitch("Mostrar nomes", &g_radar.show_names);
    CyberWidgets::ToggleSwitch("Indicador de altura", &g_radar.show_height);
    CyberWidgets::ToggleSwitch("Borda do widget", &g_radar.border);
    CyberWidgets::SliderFloat("Alcance", &g_radar.range, 20.0f, 500.0f, "%.0f m");
    CyberWidgets::SliderFloat("Escala", &g_radar.scale, 50.0f, 160.0f, "%.0f%%");
    CyberWidgets::SliderFloat("Tamanho do marcador", &g_radar.marker_size, 3.0f, 12.0f, "%.0f px");
    {
        const char* shapes[] = { "Circular", "Quadrado" };
        CyberWidgets::Combo("Formato", &g_radar.shape, shapes, 2);
    }
    CyberWidgets::EndCard();
}
