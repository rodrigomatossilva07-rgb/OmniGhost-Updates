#include "../../widgets.h"
#include "src/games/Valorant/valorant_game.h"
#include "imgui.h"

void DrawValorantStatus()
{
    using namespace CyberWidgets;
    ImGui::PushID("valorant_status");

    BeginCard("Estado do runtime");
    StatusBadge("Processo / DMA", Valorant::runtime.attached);
    StatusBadge("Offsets", Valorant::offsets.loaded);
    StatusBadge("Em jogo", Valorant::runtime.in_game);
    KeyValueRow("Processo", Valorant::runtime.process_name.c_str());
    KeyValueRow("Estado", Valorant::StatusText());
    TextLineF(TextTone::Secondary, "Jogadores: %d", static_cast<int>(Valorant::runtime.players.size()));
    if (GoldButton("Recarregar offsets", ImVec2(-1.f, 32.f)))
        Valorant::LoadOffsetsJson(nullptr);
    EndCard();

    BeginCard("Configuração local");
    ToggleSwitch("Mostrar FPS", &Valorant::config.show_fps);
    ToggleSwitch("Mostrar estado Makcu", &Valorant::config.show_makcu_status);
    InputInt("Máximo de atores", &Valorant::config.max_actors);
    if (Valorant::config.max_actors < 1) Valorant::config.max_actors = 1;
    if (Valorant::config.max_actors > 512) Valorant::config.max_actors = 512;
    if (DangerButton("Reposição segura (tudo desligado)", ImVec2(-1.f, 32.f)))
        Valorant::config = Valorant::Config{};
    EndCard();

    ImGui::PopID();
}
