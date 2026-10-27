#include "../../widgets.h"
#include "../../theme.h"
#include "config/cs2_config.h"
#include "cs2_game.h"
#include "platform/offset_auto.h"
#include "Memory/Memory.h"
#include "globals.h"
#include "games/Cs2/radar/cs2_web_radar.h"
#include "../../../../third_party/keyauth/x64/lib/QRCode/qrcode.hpp"
#include "imgui.h"
#include <Windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <memory>

namespace {
void DrawCs2PublicLinkQr(const std::string& url) {
    static std::string encodedUrl;
    static std::unique_ptr<qrcodegen::QrCode> qr;
    if (url != encodedUrl) {
        encodedUrl = url;
        qr.reset();
        if (!url.empty()) {
            try {
                qr = std::make_unique<qrcodegen::QrCode>(
                    qrcodegen::QrCode::encodeText(url.c_str(), qrcodegen::QrCode::Ecc::MEDIUM));
            } catch (...) { encodedUrl.clear(); }
        }
    }
    if (!qr) return;
    constexpr int quietZone = 4;
    const int modules = qr->getSize();
    const float available = (std::min)(220.f, CyberWidgets::CardContentWidth() - 24.f);
    const float moduleSize = (std::max)(2.f, std::floor(available / float(modules + quietZone * 2)));
    const float side = moduleSize * float(modules + quietZone * 2);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
        (std::max)(0.f, (CyberWidgets::CardContentWidth() - side) * .5f));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + side, origin.y + side), IM_COL32_WHITE, 6.f);
    for (int y = 0; y < modules; ++y) for (int x = 0; x < modules; ++x) {
        if (!qr->getModule(x, y)) continue;
        const ImVec2 a(origin.x + (x + quietZone) * moduleSize,
                       origin.y + (y + quietZone) * moduleSize);
        dl->AddRectFilled(a, ImVec2(a.x + moduleSize, a.y + moduleSize), IM_COL32_BLACK);
    }
    ImGui::Dummy(ImVec2(side, side));
    CyberWidgets::TextLine("Lê o QR Code para abrir o radar público.", CyberWidgets::TextTone::Secondary);
}
}

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

    CyberWidgets::CardGap(gap);
    CyberWidgets::BeginCard("WEB RADAR", full);
    bool webEnabled = CS2::config.web_radar_enabled;
    if (CyberWidgets::ToggleSwitch("Ativar Web Radar", &webEnabled)) {
        CS2::config.web_radar_enabled = webEnabled;
        if (webEnabled) CS2::WebRadar::Start(CS2::config.web_radar_port);
        else CS2::WebRadar::Stop();
    }
    if (webEnabled) {
        if (!CS2::WebRadar::IsRunning()) CS2::WebRadar::Start(CS2::config.web_radar_port);
        CyberWidgets::TextLine("Máximo de jogadores", CyberWidgets::TextTone::Secondary);
        const float maxPlayersWidth = (std::min)(260.f, ImGui::GetContentRegionAvail().x * 0.42f);
        ImGui::SetNextItemWidth(maxPlayersWidth);
        ImGui::SliderInt("##web_radar_max_players", &CS2::config.web_radar_max_players, 8, 64, "%d jogadores");
        CyberWidgets::SliderFloat("Atualização", &CS2::config.web_radar_update_hz, 2.f, 30.f, "%.0f Hz");
        CS2::WebRadar::Configure({static_cast<std::uint32_t>(CS2::config.web_radar_max_players),
                                  CS2::config.web_radar_update_hz});
        bool cloud = CS2::config.web_radar_cloudflare;
        if (cloud && !CS2::WebRadar::CloudflareRunning() && CS2::WebRadar::PublicUrl().empty())
            CS2::WebRadar::StartCloudflare();
        if (CyberWidgets::ToggleSwitch("Link público Cloudflare", &cloud)) {
            CS2::config.web_radar_cloudflare = cloud;
            if (cloud) CS2::WebRadar::StartCloudflare(); else CS2::WebRadar::StopCloudflare();
        }
        const auto diagnostics = CS2::WebRadar::GetDiagnostics();
        CyberWidgets::KeyValueRow("Fonte de dados", diagnostics.sourceFresh ? "Em direto" : "A aguardar partida");
        CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary,
            "%u jogadores | %u ligações | %.1f / %.0f Hz | amostra %llu ms",
            diagnostics.players, diagnostics.clients, diagnostics.updateHz, diagnostics.configuredHz,
            static_cast<unsigned long long>(diagnostics.lastSampleAgeMs));
        if (CyberWidgets::GoldButton("Abrir radar local", ImVec2(180.f, 34.f))) {
            const auto url = CS2::WebRadar::LocalUrl();
            ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        if (cloud) {
            const auto publicUrl = CS2::WebRadar::PublicUrl();
            if (!publicUrl.empty()) {
                ImGui::SameLine();
                if (CyberWidgets::CyberButton("Abrir link público", ImVec2(180.f, 34.f)))
                    ShellExecuteA(nullptr, "open", publicUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                if (CyberWidgets::CyberButton("Copiar link público", ImVec2(180.f, 34.f)))
                    ImGui::SetClipboardText(publicUrl.c_str());
                DrawCs2PublicLinkQr(publicUrl);
            }
            const auto status = CS2::WebRadar::CloudflareStatus();
            if (!status.empty()) CyberWidgets::TextLine(status.c_str(), CyberWidgets::TextTone::Secondary);
        }
        CyberWidgets::TextLine("O token privado permanece no fragmento do endereço e nunca é enviado como parte do mapa.",
                               CyberWidgets::TextTone::Secondary);
    }
    CyberWidgets::EndCard();
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
