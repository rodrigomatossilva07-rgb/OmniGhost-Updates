#include "../../widgets.h"
#include "imgui.h"

namespace {
struct WorldUiState {
    bool enabled = false;
    bool pickups = true;
    bool thrown = true;
    bool contracts = true;
    bool vehicles = true;
    bool stations = true;
    bool show_name = true;
    bool show_distance = true;
    bool show_icon = true;
    float max_distance = 180.0f;
    float icon_size = 16.0f;
    int box_type = 0;
};
WorldUiState g_world;
}

void DrawWarzoneWorld() {
    CyberWidgets::BeginCard("Warzone · ESP do mundo");
    CyberWidgets::TextLine("Organizacao visual inspirada na referencia, mantendo o estilo do OmniGhost. Sem leitura de objetos do jogo.",
                           CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();

    CyberWidgets::BeginCardRow();
    const float half = CyberWidgets::CardRowHalfWidth();
    CyberWidgets::BeginCard("Opcoes principais", half);
    CyberWidgets::ToggleSwitch("Ativar ESP do mundo", &g_world.enabled);
    CyberWidgets::ToggleSwitch("Pickups", &g_world.pickups);
    CyberWidgets::ToggleSwitch("Itens colocados / lancados", &g_world.thrown);
    CyberWidgets::ToggleSwitch("Contratos / objetivos", &g_world.contracts);
    CyberWidgets::ToggleSwitch("Veiculos", &g_world.vehicles);
    CyberWidgets::ToggleSwitch("Estacoes / pontos de interesse", &g_world.stations);
    CyberWidgets::SliderFloat("Distancia maxima", &g_world.max_distance, 20.0f, 500.0f, "%.0f m");
    CyberWidgets::EndCard();

    CyberWidgets::NextCardColumn();
    CyberWidgets::BeginCard("Apresentacao", half);
    CyberWidgets::ToggleSwitch("Mostrar nome", &g_world.show_name);
    CyberWidgets::ToggleSwitch("Mostrar distancia", &g_world.show_distance);
    CyberWidgets::ToggleSwitch("Mostrar icone", &g_world.show_icon);
    CyberWidgets::SliderFloat("Tamanho do icone", &g_world.icon_size, 8.0f, 32.0f, "%.0f px");
    {
        const char* boxes[] = { "Cantos", "Caixa", "Nenhum" };
        CyberWidgets::Combo("Tipo de marcador", &g_world.box_type, boxes, 3);
    }
    CyberWidgets::Separator();
    CyberWidgets::TextLine("Categorias e filtros ficam guardados apenas durante esta sessao da interface.",
                           CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();
    CyberWidgets::EndCardRow();

    CyberWidgets::BeginCard("Itens disponiveis");
    CyberWidgets::BeginSurfaceList("warzone_world_items", 190.0f);
    CyberWidgets::TextLine("ARMAS", CyberWidgets::TextTone::Accent);
    CyberWidgets::TextLine("Placas / armadura", CyberWidgets::TextTone::Primary);
    CyberWidgets::TextLine("Caixas / suprimentos", CyberWidgets::TextTone::Primary);
    CyberWidgets::TextLine("Contratos", CyberWidgets::TextTone::Primary);
    CyberWidgets::TextLine("Veículos", CyberWidgets::TextTone::Primary);
    CyberWidgets::TextLine("Pré-visualização local: só entra no motor do jogo quando existir uma integração separada e permitida.", CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndSurfaceList();
    CyberWidgets::EndCard();
}
