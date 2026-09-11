#include "../../widgets.h"
#include "cs2_game.h"
#include "cs2_config.h"
#include "imgui.h"
#include <cstdio>

void DrawCs2Misc()
{
    using namespace CyberWidgets;
    ImGui::PushID("cs2_misc");

    BeginCard("Estado CS2", 0.f);
    const bool offsetsOk = CS2::runtime.offsets_self_test_ok
        || (CS2::offsets.dwEntityList && CS2::offsets.dwViewMatrix);
    StatusBadge("Offsets", offsetsOk);
    StatusBadge("Execução", CS2::runtime.in_match);
    char players[64]{};
    std::snprintf(players, sizeof(players), "%d jogadores · %d inimigos", CS2::runtime.player_count, CS2::runtime.enemy_count);
    KeyValueRow("Entidades", players);
    char fps[32]{};
    std::snprintf(fps, sizeof(fps), "%.0f FPS", CS2::runtime.fps);
    KeyValueRow("Renderizador", fps);
    KeyValueRow("Mapa", CS2::runtime.map_name[0] ? CS2::runtime.map_name : "A aguardar partida");
    if (!offsetsOk)
        TextLine("Offsets precisam de atenção. Recarrega o ficheiro local.", TextTone::Warning);
    Separator();
    if (CyberButton("Recarregar offsets", ImVec2(170, 32)))
        CS2::LoadOffsetsFromJson(nullptr);
    ImGui::SameLine();
    if (GoldButton("Autoteste", ImVec2(120, 32)))
        CS2::SelfTestOffsets();
    EndCard();

    CardGap();
    BeginCard("Configuração local", 0.f);
    TextLine("Ferramentas rápidas para o perfil CS2. Os perfis comuns ficam na página Perfis.", TextTone::Secondary);
    if (CyberButton("Guardar config CS2", ImVec2(180, 32))) {
        CS2::SaveConfig("cs2_default");
        Notify("Config CS2 guardada", ToastType::Success);
    }
    ImGui::SameLine();
    if (CyberButton("Carregar config CS2", ImVec2(180, 32))) {
        CS2::LoadConfig("cs2_default");
        Notify("Config CS2 carregada", ToastType::Info);
    }
    ImGui::SameLine();
    if (DangerButton("Repor CS2", ImVec2(130, 32))) {
        CS2::config = CS2::Config{};
        Notify("Config CS2 reposta", ToastType::Warning);
    }
    TextLine("Guardado em %LocalAppData%\\OmniGhost\\CS2", TextTone::Secondary);
    EndCard();

    ImGui::PopID();
}
