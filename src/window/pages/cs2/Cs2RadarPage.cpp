#include "../../widgets.h"
#include "../../theme.h"
#include "config/cs2_config.h"
#include "cs2_game.h"
#include "radar/cs2_radar.h"
#include "platform/offset_auto.h"
#include "Memory/Memory.h"
#include "globals.h"
#include "imgui.h"
#include <Windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <string>

namespace {

void CopyToClipboard(const std::string& text) {



    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    HGLOBAL clipboardMemory = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (!clipboardMemory) { CloseClipboard(); return; }
    void* locked = GlobalLock(clipboardMemory);
    if (!locked) { GlobalFree(clipboardMemory); CloseClipboard(); return; }
    memcpy(locked, text.c_str(), text.size() + 1);
    GlobalUnlock(clipboardMemory);
    if (!SetClipboardData(CF_TEXT, clipboardMemory))
        GlobalFree(clipboardMemory);
    CloseClipboard();
}

}

void DrawCs2Radar() {
    const float full = CyberWidgets::CardContentWidth();
    const float gap = CyberTheme::Spacing::Sm;
    const float left = (full - gap) * 0.55f;
    const float right = full - left - gap;

    ImGui::BeginGroup();


    CyberWidgets::BeginCard("WEB RADAR", left);
    CyberWidgets::ToggleSwitch("Ativar Web Radar", &CS2::config.webradar_enabled);

    if (CS2::config.webradar_enabled) {
        int port = CS2::config.webradar_port;
        ImGui::SetNextItemWidth((std::min)(left * 0.50f, 260.f));
        if (ImGui::SliderInt("Porta", &port, 1024, 65535)) {
            CS2::config.webradar_port = port;
        }

        const bool online = CS2::WebRadar::IsRunning();
        CyberWidgets::Badge(online ? "ONLINE" : "OFFLINE",
            online ? CyberWidgets::TextTone::Success : CyberWidgets::TextTone::Warning);

        CyberWidgets::SectionTitle("LINK PUBLICO (CLOUDFLARE)");
        bool cf = CS2::config.webradar_cloudflare;
        if (CyberWidgets::ToggleSwitch("Ativar túnel Cloudflare", &cf)) {
            CS2::config.webradar_cloudflare = cf;
        }
        const std::string pub = CS2::WebRadar::PublicUrl();
        if (!pub.empty()) {
            CyberWidgets::TextLine(pub.c_str(), CyberWidgets::TextTone::Secondary);
            if (CyberWidgets::Button("Copiar link publico", CyberWidgets::ButtonStyle::Secondary, ImVec2(190.f, 32.f))) {
                CopyToClipboard(pub);
                CyberWidgets::Notify("Link publico copiado", CyberWidgets::ToastType::Success);
            }
            ImGui::SameLine();
            if (CyberWidgets::Button("Abrir publico", CyberWidgets::ButtonStyle::Ghost, ImVec2(130.f, 32.f))) {
                ShellExecuteA(nullptr, "open", pub.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        } else if (CS2::config.webradar_cloudflare) {
            const std::string tunnelStatus = CS2::WebRadar::CloudflareStatus();
            const bool actionableError =
                tunnelStatus.find("não foi possível") != std::string::npos ||
                tunnelStatus.find("não foi encontrado") != std::string::npos ||
                tunnelStatus.find("terminou antes") != std::string::npos;
            CyberWidgets::TextLine(
                actionableError ? tunnelStatus.c_str() : "A criar o link público seguro...",
                actionableError ? CyberWidgets::TextTone::Warning : CyberWidgets::TextTone::Secondary);
        } else {
            CyberWidgets::TextLine("Ativa o tunel para obter um link https publico.",
                                   CyberWidgets::TextTone::Secondary);
        }

        CyberWidgets::Separator();
        CyberWidgets::TextLine("O link público aparece apenas depois de o túnel ficar ligado.",
                               CyberWidgets::TextTone::Secondary);
    } else {
        CyberWidgets::TextLine("Web Radar desativado.", CyberWidgets::TextTone::Secondary);
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
    std::snprintf(line, sizeof(line), "%s", CS2::WebRadar::IsRunning() ? "HTTP a escutar" : "parado");
    CyberWidgets::KeyValueRow("Servidor", line);
    std::snprintf(line, sizeof(line), "%s", CS2::WebRadar::CloudflareRunning() ? "tunel ativo" : "off");
    CyberWidgets::KeyValueRow("Cloudflare", line);
    CyberWidgets::EndCard();
    ImGui::EndGroup();
}

void DrawCs2Status() {
    using namespace CyberWidgets;
    const auto world = CS2::AcquireRuntimeSnapshot();
    const auto camera = CS2::AcquireCameraSnapshot();
    const auto compatibility = OmniGhost::OffsetAuto::Compatibility(ActiveGame::CS2);
    const uint64_t now = GetTickCount64();
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

    CardGap();
    BeginCard("FRESCURA DOS DADOS");
    if (worldFresh) {
        std::snprintf(value, sizeof(value), "%llu ms",
            static_cast<unsigned long long>(now - world->snapshot_timestamp_ms));
        KeyValueRow("Snapshot do mundo", value);
        if (world->world_sample_ms && now >= world->world_sample_ms) {
            std::snprintf(value, sizeof(value), "%llu ms",
                static_cast<unsigned long long>(now - world->world_sample_ms));
            KeyValueRow("Amostra do mundo", value);
        }
        std::snprintf(value, sizeof(value), "%.1f Hz", world->acquisition_hz);
        KeyValueRow("Aquisição", value);
        std::snprintf(value, sizeof(value), "%llu",
            static_cast<unsigned long long>(world->read_fails));
        KeyValueRow("Falhas de leitura", value);
        std::snprintf(value, sizeof(value), "%d", CS2::DmaPressureLevel());
        KeyValueRow("Pressão DMA", value);
    } else {
        TextLine("Sem snapshot recente; entra no jogo e aguarda a aquisição.", TextTone::Secondary);
    }
    if (camera && camera->timestamp_ms && now >= camera->timestamp_ms) {
        std::snprintf(value, sizeof(value), "%llu ms",
            static_cast<unsigned long long>(now - camera->timestamp_ms));
        KeyValueRow("Amostra da câmara", value);
    }
    EndCard();

    if (!compatibility.detail.empty()) {
        CardGap();
        BeginCard("COMPATIBILIDADE");
        TextLine(compatibility.detail.c_str(),
            compatibility.state == OmniGhost::OffsetAuto::CompatibilityState::Outdated ?
            TextTone::Warning : TextTone::Secondary);
        EndCard();
    }
}
