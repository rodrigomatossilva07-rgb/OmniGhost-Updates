#include "../../widgets.h"
#include "../../menu_tab.h"
#include "../../../globals.h"
#include "config/app_settings.h"
#include "game/offsets.h"
#include "game/esp_manager.h"
#include "platform/offset_auto.h"
#include "esp/esp.h"
#include "esp/vehicle_esp.h"
#include "games/Fivem/radar/fivem_web_radar.h"
#include "games/Fivem/collision/fivem_collision_world.h"
#include "gameplay/dma_telemetry_log.h"
#include "aimbot/aim_type.h"
#include "Memory/Memory.h"
#include "imgui.h"
#include <algorithm>
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
    const int pedCount = static_cast<int>(FiveM::ESP::GetValidPedsSnapshot().validPeds.size());
    const int rawPedCount = FiveM::ESP::LastRawPedCount();
    const int filteredPedCount = FiveM::ESP::LastFilteredPedCount();
    const int cachedVehicleCount = static_cast<int>(vehicle_esp::GetSnapshot().size());
    const bool espOn = esp::config.enabled;
    const auto& telemetry = OmniGhost::Gameplay::DmaTelemetry::FiveM();
    const auto radar = FiveM::WebRadar::GetDiagnostics();
    const auto collision = FiveM::Collision::World::Instance().GetDiagnostics();
    const float worldAge = telemetry.snapshot.world_age_ms.load(std::memory_order_relaxed);
    const float snapshotHz = telemetry.snapshot.snapshot_hz.load(std::memory_order_relaxed);
    const int pressure = telemetry.pressure.pressure_level.load(std::memory_order_relaxed);
    const auto readFails = telemetry.counts.read_fails.load(std::memory_order_relaxed);
    const bool coreWorldOk = pedCount > 0 && worldAge >= 0.f && worldAge < 150.f;

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
    EndCard();
    EndCardRow();

    CardGap();
    BeginCardRow();
    BeginCard("CORE WORLD", half);
    HealthRow("Aquisição", coreWorldOk ? "Dados recentes" : (worldOk ? "A aguardar entidades" : "World indisponível"),
              coreWorldOk ? HealthStatus::Ok : HealthStatus::Warning);
    TextLineF(TextTone::Secondary, "Pool: %d lidos | %d válidos | %d filtrados",
              rawPedCount, pedCount, filteredPedCount);
    TextLineF(TextTone::Secondary, "Veículos válidos: %d", cachedVehicleCount);
    TextLineF(TextTone::Secondary, "Snapshot: %.1f Hz | idade %.1f ms", snapshotHz, worldAge);
    TextLineF(pressure > 1 ? TextTone::Warning : TextTone::Secondary,
              "Pressão DMA: %d | falhas: %llu", pressure,
              static_cast<unsigned long long>(readFails));
    HealthRow("Raycast do mapa", collision.initialized ? "Disponível" : "Indisponível",
              collision.initialized ? HealthStatus::Ok : HealthStatus::Warning);
    TextLineF(TextTone::Secondary, "Colisão: %u regiões em RAM | %u em fila | %.1f / %.0f MiB",
              collision.cachedRegions, collision.pendingRegions,
              static_cast<double>(collision.cacheBytes) / (1024.0 * 1024.0),
              static_cast<double>(collision.cacheLimitBytes) / (1024.0 * 1024.0));
    TextLineF(collision.failedLoads ? TextTone::Warning : TextTone::Secondary,
              "Regiões: %llu carregadas | %llu falharam | %llu consultas pendentes",
              static_cast<unsigned long long>(collision.successfulLoads),
              static_cast<unsigned long long>(collision.failedLoads),
              static_cast<unsigned long long>(collision.missingQueries));
    if (!collision.initialized || collision.failedLoads)
        TextLineF(TextTone::Warning, "Motivo: %s", collision.status.c_str());
    EndCard();

    NextCardColumn();
    BeginCard("WEB RADAR", half);
    HealthRow("Servidor local", radar.running ? "Ligado" : "Desligado",
              radar.running ? HealthStatus::Ok : HealthStatus::Warning);
    HealthRow("Fonte", radar.sourceFresh ? "Em direto" : "Sem amostra recente",
              radar.sourceFresh ? HealthStatus::Ok : HealthStatus::Warning);
    HealthRow("Cloudflare", FiveM::WebRadar::CloudflareRunning() ? "Ligado" : "Desligado",
              FiveM::WebRadar::CloudflareRunning() ? HealthStatus::Ok : HealthStatus::Warning);
    TextLineF(TextTone::Secondary, "%u jogadores | %u veículos | %u clientes",
              radar.players, radar.vehicles, radar.clients);
    TextLineF(TextTone::Secondary, "Envio: %.1f / %.0f Hz | idade %llu ms",
              radar.updateHz, radar.configuredHz,
              static_cast<unsigned long long>(radar.lastSampleAgeMs));
    if (!radar.running)
        TextLine("Motivo: Web Radar desativado nas definições do Radar.", TextTone::Secondary);
    else if (!worldOk)
        TextLine("Motivo: ponteiro World do FiveM indisponível.", TextTone::Warning);
    else if (!localOk)
        TextLine("Motivo: jogador local ainda não identificado.", TextTone::Warning);
    else if (!radar.sourceFresh)
        TextLine("Motivo: cache do Core World vazia ou com mais de 1,5 s.", TextTone::Warning);
    EndCard();
    EndCardRow();
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
