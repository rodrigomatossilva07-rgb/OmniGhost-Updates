#include "../../widgets.h"
#include "../../menu_tab.h"
#include "../../../globals.h"
#include "config/app_settings.h"
#include "game/offsets.h"
#include "game/esp_manager.h"
#include "platform/offset_auto.h"
#include "esp/esp.h"
#include "aimbot/aim_type.h"
#include "Memory/Memory.h"
#include "imgui.h"
#include <cstdio>
#include "../../hardware_monitor.h"
#include "../../config_history.h"
#include "../../changelog.h"

#ifndef UI_PREVIEW
namespace {
bool LooksPtr(uintptr_t v) {
    return v > 0x10000ULL && v < 0x00007FFFFFFFFFFFULL;
}
}
#endif

void DrawFiveMStatus()
{
    using namespace CyberWidgets;
    ImGui::PushID("fivem_status");
#ifndef UI_PREVIEW
    using namespace FiveM::offset;
    const bool worldOk = LooksPtr(world);
    const bool viewOk = LooksPtr(viewport);
    const bool localOk = LooksPtr(localplayer);
    const bool offsetsOk = OmniGhost::OffsetAuto::HasCriticalOffsets(ActiveGame::FiveM)
        && worldOk && viewOk;
    const int pedCount = static_cast<int>(FiveM::ESP::validPeds.size());
    const bool espOn = esp::config.enabled;

    BeginCardRow();
    const float half = CardRowHalfWidth();
    BeginCard("SISTEMA", half);
    HealthRow("DMA", worldOk ? "Ligado" : "A ligar",
              worldOk ? HealthStatus::Ok : HealthStatus::Warning);
    HealthRow("FiveM", viewOk ? "Detetado" : "Não detetado",
              viewOk ? HealthStatus::Ok : HealthStatus::Warning);
    HealthRow("Jogador local", localOk ? "Detetado" : "A aguardar",
              localOk ? HealthStatus::Ok : HealthStatus::Warning);
    HealthRow("Offsets", offsetsOk ? "Verificados" : "Por validar",
              offsetsOk ? HealthStatus::Ok : HealthStatus::Warning);
    char players[32]{};
    std::snprintf(players, sizeof(players), "%d jogadores", pedCount);
    HealthRow("ESP", !espOn ? "Desativado" : players,
              !espOn ? HealthStatus::Warning : HealthStatus::Ok);
    EndCard();

    NextCardColumn();
    BeginCard("DISPOSITIVOS", half);
    HealthRow("Makcu", aim_type::config.makcu_connected ? "Ligado" : "Desligado",
              aim_type::config.makcu_connected ? HealthStatus::Ok : HealthStatus::Warning);
    HealthRow("KMBox", aim_type::config.kmbox_net_connected ? "Ligado" : "Desligado",
              aim_type::config.kmbox_net_connected ? HealthStatus::Ok : HealthStatus::Warning);
    HealthRow("Ferrum", aim_type::config.ferrum_connected ? "Ligado" : "Desligado",
              aim_type::config.ferrum_connected ? HealthStatus::Ok : HealthStatus::Warning);
    Separator();
    TextLine("Configurações técnicas ficam disponíveis em Definições > Avançado.",
             TextTone::Secondary);
    EndCard();
    EndCardRow();

    bool technical = app_settings::config.show_advanced;
#ifdef _DEBUG
    technical = true;
#endif
#ifdef OMNIGHOST_DIAGNOSTICS
    technical = true;
#endif
    if (technical) {
        CardGap();
        BeginCard("DETALHES TÉCNICOS", 0.f);
        TextLine("Informação de diagnóstico destinada a utilizadores avançados.", TextTone::Warning);
        char value[64]{};
        std::snprintf(value, sizeof(value), "0x%llX", static_cast<unsigned long long>(world)); KeyValueRow("world", value);
        std::snprintf(value, sizeof(value), "0x%llX", static_cast<unsigned long long>(viewport)); KeyValueRow("viewport", value);
        std::snprintf(value, sizeof(value), "0x%llX", static_cast<unsigned long long>(localplayer)); KeyValueRow("localplayer", value);
        std::snprintf(value, sizeof(value), "0x%llX", static_cast<unsigned long long>(base)); KeyValueRow("DMA base", value);
        char build[32]{};
        std::snprintf(build, sizeof(build), "b%d", buildVersion);
        KeyValueRow("Build do jogo", build);
        EndCard();
    }
#else
    BeginCard("SYS // 06   SISTEMA", 0.f);
    TextLine("Página de estado disponível no runtime Windows com DMA.", TextTone::Secondary);
    EndCard();
#endif

    if (app_settings::config.show_advanced) {
        CardGap();
        DrawHardwareStatusWidget();
        CardGap();
        ConfigHistory::DrawHistoryWidget();
        CardGap();
        Changelog::DrawChangelogCompact();
    }

    ImGui::PopID();
}
