#include "../../widgets.h"
#include "../../theme.h"
#include "config/cs2_config.h"
#include "cs2_game.h"
#include "platform/offset_auto.h"
#include "Memory/Memory.h"
#include "globals.h"
#include "imgui.h"
#include <algorithm>
#include <cstdio>
#include <cmath>

void DrawCs2Radar() {
    const float full = CyberWidgets::CardContentWidth();
    const float gap = CyberTheme::Spacing::Sm;
    const float left = (full - gap) * 0.55f;
    const float right = full - left - gap;

    ImGui::BeginGroup();


    CyberWidgets::BeginCard("RADAR 2D", left);
    CyberWidgets::ToggleSwitch("Ativar Radar 2D", &CS2::config.radar_2d);
    if (CS2::config.radar_2d) {
        CyberWidgets::SliderFloat("Tamanho do Radar 2D", &CS2::config.radar_2d_size, 110.f, 360.f, "%.0f px");
        CyberWidgets::TextLine("Com o menu aberto, arrasta o radar para qualquer posição.", CyberWidgets::TextTone::Secondary);
    } else {
        CyberWidgets::TextLine("Radar 2D desativado.", CyberWidgets::TextTone::Secondary);
    }
    CyberWidgets::EndCard();
    ImGui::EndGroup();

    ImGui::SameLine(0.f, gap);
    ImGui::BeginGroup();
    CyberWidgets::BeginCard("ESTADO", right);
    const auto snapshot = CS2::AcquireRuntimeSnapshot();
    char line[128];
    std::snprintf(line, sizeof(line), "Jogadores no snapshot: %d",
                  snapshot ? snapshot->player_count : 0);
    CyberWidgets::KeyValueRow("Dados", line);
    const char* mapLabel = snapshot && snapshot->map_name[0] ? snapshot->map_name : "-";
    std::snprintf(line, sizeof(line), "%s", mapLabel);
    CyberWidgets::KeyValueRow("Mapa", line);
    CyberWidgets::KeyValueRow("Radar 2D", CS2::config.radar_2d ? "ativo" : "desativado");
    CyberWidgets::EndCard();
    ImGui::EndGroup();
}

void DrawCs2Status() {
    using namespace CyberWidgets;
    const auto world = CS2::AcquireRuntimeSnapshot();
    const auto camera = CS2::AcquireCameraSnapshot();
    const auto compatibility = OmniGhost::OffsetAuto::Compatibility(ActiveGame::CS2);
    const uint64_t now = CS2::SampleClockMs();
    const bool worker = CS2::AcquisitionRunning();
    const auto dma = mem.GetDiagnosticsSnapshot();
    const bool worldFresh = worker && world && world->snapshot_timestamp_ms &&
        now >= world->snapshot_timestamp_ms && now - world->snapshot_timestamp_ms < 1000;
    const bool attached = worldFresh && world->client_base != 0;
    bool matrixValid = false;
    if (camera && camera->timestamp_ms) {
        int nonzero = 0;
        matrixValid = true;
        for (float value : camera->view_matrix) {
            if (!std::isfinite(value)) { matrixValid = false; break; }
            if (std::fabs(value) > 0.00001f) ++nonzero;
        }
        matrixValid = matrixValid && nonzero >= 4;
    }
    const bool cameraFresh = attached && matrixValid &&
        now >= camera->timestamp_ms && now - camera->timestamp_ms < 250;
    const bool offsetsOk = CS2::offsets.loaded &&
        compatibility.state == OmniGhost::OffsetAuto::CompatibilityState::Ready;

    BeginCardRow();
    const float half = CardRowHalfWidth();
    BeginCard("SISTEMA CS2", half);
    HealthRow("Aquisição", worker ? "Ativa" : "Parada",
        worker ? HealthStatus::Ok : HealthStatus::Warning);
    const char* readState = attached ? "Dados recentes" :
        (dma.processInitialized ? "Aquisição sem resposta" : "Processo não associado");
    HealthRow("Leitura do jogo", readState,
        attached ? HealthStatus::Ok : HealthStatus::Warning);
    HealthRow("Offsets", offsetsOk ? "Validados no jogo" :
        compatibility.state == OmniGhost::OffsetAuto::CompatibilityState::Outdated ?
        "Incompatíveis" : "Por validar",
        offsetsOk ? HealthStatus::Ok :
        compatibility.state == OmniGhost::OffsetAuto::CompatibilityState::Outdated ?
        HealthStatus::Error : HealthStatus::Warning);
    HealthRow("ViewMatrix", cameraFresh ? "Atual" : "Indisponível ou antiga",
        cameraFresh ? HealthStatus::Ok : HealthStatus::Warning);
    HealthRow("EntityList", worldFresh && world->entity_list_addr ? "Resolvida" : "A aguardar",
        attached && world->entity_list_addr ? HealthStatus::Ok : HealthStatus::Warning);
    EndCard();

    NextCardColumn();
    BeginCard("PARTIDA", half);
    HealthRow("Estado", attached && world->in_match ? "Em partida" : "Fora da partida",
        attached && world->in_match ? HealthStatus::Ok : HealthStatus::Warning);
    HealthRow("Jogador local", attached && world->local_pawn ? "Detetado" : "A aguardar",
        attached && world->local_pawn ? HealthStatus::Ok : HealthStatus::Warning);
    char value[96]{};
    std::snprintf(value, sizeof(value), "%d", attached ? world->player_count : 0);
    KeyValueRow("Jogadores remotos", value);
    KeyValueRow("Mapa", attached && world->map_name[0] ? world->map_name : "-");
    EndCard();
    EndCardRow();





































}
