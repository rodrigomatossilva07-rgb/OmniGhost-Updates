#include "../../widgets.h"
#include "cs2_game.h"
#include "config/cs2_config.h"
#include "../../localization.h"
#include "imgui.h"
#include "gameplay/dma_telemetry_log.h"
#include "platform/session_log.h"
#include <cstdio>

void DrawCs2Misc()
{
    using namespace CyberWidgets;
    ImGui::PushID("cs2_misc");
    BeginCard("DIVERSOS", 0.f);
    TextLine("Widgets e informacao util durante a partida.", TextTone::Secondary);
    ToggleSwitch(Loc::Tr("cs2.misc.spectators"), &CS2::config.spectator_list);
    ToggleSwitch(Loc::Tr("cs2.misc.bomb_timer"), &CS2::config.bomb_timer);
    ToggleSwitch(Loc::Tr("cs2.misc.hit_marker"), &CS2::config.hit_marker);
    ToggleSwitch("Trajetoria de granadas", &CS2::config.grenade_trail);
    if (CS2::config.grenade_trail)
        TextLine("Mostra linha, impacto e tempo previsto para flash, HE, smoke, molotov e decoy.", TextTone::Secondary);
    ToggleSwitch("Ondas de disparo (Sound ESP)", &CS2::config.sound_esp);
    ToggleSwitch("Pulso de passos (Footstep ESP)", &CS2::config.footstep_esp);
    ToggleSwitch("Granadas em voo (Projectile ESP)", &CS2::config.projectile_esp);
    if (CS2::config.projectile_esp) {
        const auto snapshot = CS2::AcquireRuntimeSnapshot();
        if (snapshot) {
            CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary,
                "Diagnóstico: páginas %d | entidades %d | nomes %d | granadas %d | cenas %d | visíveis %d",
                snapshot->projectile_pages, snapshot->projectile_entities,
                snapshot->projectile_name_ptrs, snapshot->projectile_classified,
                snapshot->projectile_scenes, static_cast<int>(snapshot->projectiles.size()));
        }
    }
    Separator();
    TextLine("PLAYER FLAGS", TextTone::Primary);
    ToggleSwitch("Indicadores de jogadores", &CS2::config.player_flags);
    ImGui::BeginDisabled(!CS2::config.player_flags);
    ToggleSwitch("Blind", &CS2::config.flag_blind);
    ToggleSwitch("Scoped", &CS2::config.flag_scoped);
    ToggleSwitch("Defusing", &CS2::config.flag_defusing);
    ToggleSwitch("Kit", &CS2::config.flag_kit);
    ToggleSwitch("Dinheiro", &CS2::config.flag_money);
    ImGui::EndDisabled();
    if (CS2::config.bomb_timer) {
        TextLine("Com o menu aberto, arrasta as janelas de bomba e espectadores para guardar a posicao.", TextTone::Secondary);
    }
    EndCard();

    BeginCard("TELEMETRIA", 0.f);
    TextLine(
        "Grava no logs.txt leituras DMA, tempos de aquisicao, spikes e funcoes ativas do CS2. "
        "Usa isto durante a partida e envia o ficheiro para diagnosticar lags.",
        TextTone::Secondary);
    const bool prev = CS2::config.telemetry_enabled;
    ToggleSwitch("Telemetria", &CS2::config.telemetry_enabled);
    // Keep DMA telemetry engine in sync with this CS2-only toggle.
    OmniGhost::Gameplay::DmaTelemetry::SetEnabled(CS2::config.telemetry_enabled);
    if (CS2::config.telemetry_enabled && !prev) {
        char blob[384];
        std::snprintf(blob, sizeof(blob),
            "radar=%d\naim=%d\ntrigger=%d\nbomb=%d\nspectators=%d\nperf_mode=%d",
            (CS2::config.radar_2d || CS2::config.webradar_enabled) ? 1 : 0,
            CS2::config.aim_enabled ? 1 : 0,
            CS2::config.trigger_enabled ? 1 : 0,
            CS2::config.bomb_timer ? 1 : 0,
            CS2::config.spectator_list ? 1 : 0,
            CS2::config.performance_mode ? 1 : 0);
        OmniGhost::Gameplay::DmaTelemetry::LogConfigChanged("CS2", blob);
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Info,
            OmniGhost::SessionLog::Subsystem::Adapter,
            "[CS2] Telemetria ativada — PERF/SPIKE vao para logs.txt");
    }
    if (CS2::config.telemetry_enabled) {
        TextLine("Estado: a gravar (PERF ~1/s em partida, SPIKE se aquisicao >= 50 ms).", TextTone::Secondary);
        TextLine("Ficheiro: %%LOCALAPPDATA%%\\OmniGhost\\logs\\logs.txt", TextTone::Secondary);
    } else {
        TextLine("Estado: desligada.", TextTone::Secondary);
    }
    EndCard();

    ImGui::PopID();
}
