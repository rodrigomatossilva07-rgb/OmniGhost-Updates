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
    CyberWidgets::BeginCard("ESP DE MUNDO");
    CyberWidgets::TextLine("Modo visual: filtros e apresentação ficam prontos para a futura leitura de loot.",
                           CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();

    CyberWidgets::BeginCardRow();
    const float half = CyberWidgets::CardRowHalfWidth();
    CyberWidgets::BeginCard("OPÇÕES PRINCIPAIS", half);
    CyberWidgets::ToggleSwitch("Ativar ESP do mundo", &g_world.enabled);
    CyberWidgets::ToggleSwitch("Armas, munição e placas", &g_world.pickups);
    CyberWidgets::ToggleSwitch("Itens colocados / lançados", &g_world.thrown);
    CyberWidgets::ToggleSwitch("Dinheiro, contratos e killstreaks", &g_world.contracts);
    CyberWidgets::ToggleSwitch("Veiculos", &g_world.vehicles);
    CyberWidgets::ToggleSwitch("Estacoes / pontos de interesse", &g_world.stations);
    CyberWidgets::SliderFloat("Distancia maxima", &g_world.max_distance, 20.0f, 500.0f, "%.0f m");
    CyberWidgets::EndCard();

    CyberWidgets::NextCardColumn();
    CyberWidgets::BeginCard("APRESENTAÇÃO", half);
    CyberWidgets::ToggleSwitch("Nome / tipo do item", &g_world.show_name);
    CyberWidgets::ToggleSwitch("Mostrar distancia", &g_world.show_distance);
    CyberWidgets::ToggleSwitch("Ícone ou texto", &g_world.show_icon);
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

    CyberWidgets::BeginCard("FILTROS DE LOOT");
    CyberWidgets::BeginSurfaceList("warzone_world_items", 190.0f);
    CyberWidgets::TextLine("ARMAS · MUNIÇÃO · PLACAS · DINHEIRO", CyberWidgets::TextTone::Accent);
    CyberWidgets::TextLine("Caixas / containers de loot", CyberWidgets::TextTone::Primary);
    CyberWidgets::TextLine("Killstreaks · contratos · veículos", CyberWidgets::TextTone::Primary);
    CyberWidgets::TextLine("Veículos", CyberWidgets::TextTone::Primary);
    CyberWidgets::TextLine("Pré-visualização local: só entra no motor do jogo quando existir uma integração separada e permitida.", CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndSurfaceList();
    CyberWidgets::EndCard();
}
