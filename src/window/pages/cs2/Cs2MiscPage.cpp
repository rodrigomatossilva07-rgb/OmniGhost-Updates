#include "../../widgets.h"
#include "cs2_game.h"
#include "cs2_config.h"
#include "config/config_manager.h"
#include "imgui.h"
#include <cstdio>

void DrawCs2Misc()
{
    using namespace CyberWidgets;
    ImGui::PushID("cs2_misc");
    const auto snapshot = CS2::AcquireRuntimeSnapshot();
    const CS2::Runtime empty{};
    const CS2::Runtime& runtime = snapshot ? *snapshot : empty;

    BeginCard("Estado CS2", 0.f);
    const bool offsetsOk = runtime.offsets_self_test_ok
        || (CS2::offsets.dwEntityList && CS2::offsets.dwViewMatrix);
    StatusBadge("Offsets", offsetsOk);
    StatusBadge("Execução", runtime.in_match);
    char players[64]{};
    std::snprintf(players, sizeof(players), "%d jogadores · %d inimigos", runtime.player_count, runtime.enemy_count);
    KeyValueRow("Entidades", players);
    char fps[32]{};
    std::snprintf(fps, sizeof(fps), "%.0f FPS", runtime.fps);
    KeyValueRow("Renderizador", fps);
    KeyValueRow("Mapa", runtime.map_name[0] ? runtime.map_name : "A aguardar partida");
    if (!offsetsOk)
        TextLine("Offsets precisam de atenção. Recarrega o ficheiro local.", TextTone::Warning);
    Separator();
    if (CyberButton("Recarregar offsets", ImVec2(170, 32))) {
        const bool restart = CS2::AcquisitionRunning();
        if (restart) CS2::StopAcquisition();
        CS2::LoadOffsetsFromJson(nullptr);
        if (restart) CS2::EnsureAcquisitionStarted();
    }
    ImGui::SameLine();
    if (GoldButton("Autoteste", ImVec2(120, 32))) {
        const bool restart = CS2::AcquisitionRunning();
        if (restart) CS2::StopAcquisition();
        CS2::SelfTestOffsets();
        if (restart) CS2::EnsureAcquisitionStarted();
    }
    EndCard();

    CardGap();
    BeginCard("OVERLAY DIVERSOS", 0.f);
    TextLine("Informação de partida que aparece por cima do jogo.", TextTone::Secondary);
    ToggleSwitch("Lista de espectadores", &CS2::config.spectator_list);
    ToggleSwitch("Temporizador da bomba", &CS2::config.bomb_timer);
    ToggleSwitch("Marcador de impacto (0,5 s)", &CS2::config.hit_marker);
    if (CS2::config.bomb_timer) {
        TextLine("Em CT indica se ainda existe tempo para desarmar, considerando o kit.",
                 TextTone::Secondary);
    }
    EndCard();

    CardGap();
    BeginCard("Configuração local", 0.f);
    TextLine("Ferramentas rápidas para o perfil CS2 ativo. Os perfis completos ficam na página Perfis.", TextTone::Secondary);
    if (CyberButton("Guardar config CS2", ImVec2(180, 32))) {
        CS2::SaveConfig(config_manager::ActiveConfigName());
        Notify("Config CS2 guardada", ToastType::Success);
    }
    ImGui::SameLine();
    if (CyberButton("Carregar config CS2", ImVec2(180, 32))) {
        CS2::LoadConfig(config_manager::ActiveConfigName());
        Notify("Config CS2 carregada", ToastType::Info);
    }
    ImGui::SameLine();
    if (DangerButton("Repor CS2", ImVec2(130, 32))) {
        CS2::config = CS2::Config{};
        Notify("Config CS2 reposta", ToastType::Warning);
    }
    TextLine("Usa o mesmo sistema completo e seguro da página Perfis.", TextTone::Secondary);
    EndCard();

    ImGui::PopID();
}
