#include "../../widgets.h"
#include "../../menu_tab.h"
#include "../../../globals.h"
#include "../config/app_settings.h"
#include "game/offsets.h"
#include "game/esp_manager.h"
#include "game/offset_auto.h"
#include "esp/esp.h"
#include "Memory/Memory.h"
#include "imgui.h"
#include <cstdio>
#include "hardware_monitor.h"
#include "config_history.h"
#include "changelog.h"

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

    BeginCard("FiveM · Estado", 0.f);
#ifndef UI_PREVIEW
    using namespace FiveM::offset;
    const bool worldOk = LooksPtr(world);
    const bool viewOk = LooksPtr(viewport);
    const bool localOk = LooksPtr(localplayer);
    const bool offsetsOk = FiveM::OffsetAuto::PointersLookValid() && worldOk && viewOk;
    const int pedCount = static_cast<int>(FiveM::ESP::validPeds.size());
    const bool espOn = esp::config.enabled;
    const bool healthy = offsetsOk && (!espOn || (worldOk && viewOk));

    Badge(healthy ? "SAUDÁVEL" : "REQUER ATENÇÃO", healthy ? TextTone::Success : TextTone::Warning);
    TextLine(healthy
        ? "O runtime principal está pronto. Uma lista vazia de jogadores não é tratada como falha."
        : "Há uma dependência de runtime que precisa de atenção. Usa as linhas abaixo para localizar a etapa.",
        healthy ? TextTone::Secondary : TextTone::Warning);
    Separator();

    StatusBadge("Offsets", offsetsOk);
    StatusBadge("Mundo", worldOk);
    StatusBadge("Câmara / viewport", viewOk);
    StatusBadge("Jogador local", localOk);
    if (!espOn) Badge("ESP DESLIGADO", TextTone::Secondary);
    else if (!worldOk || !viewOk) Badge("ESP BLOQUEADO", TextTone::Error);
    else Badge(pedCount > 0 ? "ESP ATIVO" : "ESP PRONTO · 0 JOGADORES", pedCount > 0 ? TextTone::Success : TextTone::Warning);

    char players[48]{};
    std::snprintf(players, sizeof(players), "%d detetados", pedCount);
    KeyValueRow("Jogadores", players);
    char build[32]{};
    std::snprintf(build, sizeof(build), "b%d", buildVersion);
    KeyValueRow("Build do jogo", build);

    Separator();
    EndCard();

    bool technical = app_settings::config.show_advanced;
#ifdef _DEBUG
    technical = true;
#endif
#ifdef OMNIGHOST_DIAGNOSTICS
    technical = true;
#endif
    if (technical) {
        CardGap();
        BeginCard("Diagnóstico de desenvolvimento", 0.f);
        TextLine("Os endereços técnicos são ocultados na versão normal e só aparecem no modo avançado/diagnóstico.", TextTone::Warning);
        char value[64]{};
        std::snprintf(value, sizeof(value), "0x%llX", static_cast<unsigned long long>(world)); KeyValueRow("world", value);
        std::snprintf(value, sizeof(value), "0x%llX", static_cast<unsigned long long>(viewport)); KeyValueRow("viewport", value);
        std::snprintf(value, sizeof(value), "0x%llX", static_cast<unsigned long long>(localplayer)); KeyValueRow("localplayer", value);
        std::snprintf(value, sizeof(value), "0x%llX", static_cast<unsigned long long>(base)); KeyValueRow("DMA base", value);
        EndCard();
    }
#else
    TextLine("Página de estado disponível no runtime Windows com DMA.", TextTone::Secondary);
    EndCard();
#endif

    // Hardware Status Widget
    CardGap();
    DrawHardwareStatusWidget();
    
    // Config History Widget
    CardGap();
    ConfigHistory::DrawHistoryWidget();
    
    // Changelog Widget
    CardGap();
    Changelog::DrawChangelogCompact();

    ImGui::PopID();
}
