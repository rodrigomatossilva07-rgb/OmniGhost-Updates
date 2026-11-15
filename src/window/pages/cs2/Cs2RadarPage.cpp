#include "../../widgets.h"
#include "../../theme.h"
#include "config/cs2_config.h"
#include "cs2_game.h"
#include "platform/offset_auto.h"
#include "Memory/Memory.h"
#include "globals.h"
#include "games/Cs2/radar/cs2_web_radar.h"
#include "../../../project/third_party/keyauth/x64/lib/QRCode/qrcode.hpp"
#include "imgui.h"
#include <Windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <memory>

namespace {
bool IsUsableCs2Matrix(const float* matrix) {
    if (!matrix) return false;
    int meaningful = 0;
    for (int index = 0; index < 16; ++index) {
        const float value = matrix[index];
        if (!std::isfinite(value) || std::fabs(value) >= 1'000'000.f) return false;
        if (std::fabs(value) > .000001f) ++meaningful;
    }
    return meaningful >= 4;
}

bool DiagnosticWorldToScreen(const float world[3], const float matrix[16],
                             const ImVec2 displaySize, ImVec2& screen) {
    if (!world || !IsUsableCs2Matrix(matrix) || displaySize.x <= 1.f || displaySize.y <= 1.f)
        return false;
    const float clipX = world[0] * matrix[0] + world[1] * matrix[1] + world[2] * matrix[2] + matrix[3];
    const float clipY = world[0] * matrix[4] + world[1] * matrix[5] + world[2] * matrix[6] + matrix[7];
    const float clipW = world[0] * matrix[12] + world[1] * matrix[13] + world[2] * matrix[14] + matrix[15];
    if (!std::isfinite(clipX) || !std::isfinite(clipY) || !std::isfinite(clipW) || clipW < .01f)
        return false;
    screen.x = displaySize.x * .5f * (1.f + clipX / clipW);
    screen.y = displaySize.y * .5f * (1.f - clipY / clipW);
    return std::isfinite(screen.x) && std::isfinite(screen.y);
}

bool IsFiniteCs2Position(const float position[3]) {
    return position && std::isfinite(position[0]) && std::isfinite(position[1]) &&
        std::isfinite(position[2]);
}

void FormatAge(char* buffer, const size_t size, const uint64_t now, const uint64_t sample) {
    if (!sample || now < sample) {
        std::snprintf(buffer, size, "sem amostra");
        return;
    }
    std::snprintf(buffer, size, "%llu ms", static_cast<unsigned long long>(now - sample));
}

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
    CyberWidgets::TextLine("LÃª o QR Code para abrir o radar pÃºblico.", CyberWidgets::TextTone::Secondary);
}
}

void DrawCs2Radar() {
    const float full = CyberWidgets::CardContentWidth();
    const float gap = CyberTheme::Spacing::Sm;
    const float left = (full - gap) * 0.55f;
    const float right = full - left - gap;

    ImGui::BeginGroup();


    CyberWidgets::BeginCard("RADAR 2D", left);
    if (CyberWidgets::ToggleSwitch("Ativar Radar 2D", &CS2::config.radar_2d))
        CS2::SubmitAcquisitionConfig(CS2::config);
    if (CS2::config.radar_2d) {
        CyberWidgets::SliderFloat("Tamanho do Radar 2D", &CS2::config.radar_2d_size, 110.f, 360.f, "%.0f px");
        CyberWidgets::TextLine("Com o menu aberto, arrasta o radar para qualquer posiÃ§Ã£o.", CyberWidgets::TextTone::Secondary);
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



        CS2::SubmitAcquisitionConfig(CS2::config);
        if (webEnabled) CS2::WebRadar::Start(CS2::config.web_radar_port);
        else CS2::WebRadar::Stop();
    }
    if (webEnabled) {
        if (!CS2::WebRadar::IsRunning()) CS2::WebRadar::Start(CS2::config.web_radar_port);
        CyberWidgets::TextLine("MÃ¡ximo de jogadores", CyberWidgets::TextTone::Secondary);
        const float maxPlayersWidth = (std::min)(260.f, ImGui::GetContentRegionAvail().x * 0.42f);
        ImGui::SetNextItemWidth(maxPlayersWidth);
        ImGui::SliderInt("##web_radar_max_players", &CS2::config.web_radar_max_players, 8, 64, "%d jogadores");
        CyberWidgets::SliderFloat("AtualizaÃ§Ã£o", &CS2::config.web_radar_update_hz, 2.f, 30.f, "%.0f Hz");
        CS2::WebRadar::Configure({static_cast<std::uint32_t>(CS2::config.web_radar_max_players),
                                  CS2::config.web_radar_update_hz});
        bool cloud = CS2::config.web_radar_cloudflare;
        if (cloud && !CS2::WebRadar::CloudflareRunning() && CS2::WebRadar::PublicUrl().empty())
            CS2::WebRadar::StartCloudflare();
        if (CyberWidgets::ToggleSwitch("Link pÃºblico Cloudflare", &cloud)) {
            CS2::config.web_radar_cloudflare = cloud;
            if (cloud) CS2::WebRadar::StartCloudflare(); else CS2::WebRadar::StopCloudflare();
        }
        const auto diagnostics = CS2::WebRadar::GetDiagnostics();
        CyberWidgets::KeyValueRow("Fonte de dados", diagnostics.sourceFresh ? "Em direto" : "A aguardar partida");
        CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary,
            "%u jogadores | %u ligaÃ§Ãµes | %.1f / %.0f Hz | amostra %llu ms",
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
                if (CyberWidgets::CyberButton("Abrir link pÃºblico", ImVec2(180.f, 34.f)))
                    ShellExecuteA(nullptr, "open", publicUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                if (CyberWidgets::CyberButton("Copiar link pÃºblico", ImVec2(180.f, 34.f)))
                    ImGui::SetClipboardText(publicUrl.c_str());
                DrawCs2PublicLinkQr(publicUrl);
            }
            const auto status = CS2::WebRadar::CloudflareStatus();
            if (!status.empty()) CyberWidgets::TextLine(status.c_str(), CyberWidgets::TextTone::Secondary);
        }
        CyberWidgets::TextLine("O token privado permanece no fragmento do endereÃ§o e nunca Ã© enviado como parte do mapa.",
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
    const bool fastMatrixValid = camera && camera->timestamp_ms &&
        IsUsableCs2Matrix(camera->view_matrix);
    const bool cameraFresh = attached && fastMatrixValid && now >= camera->timestamp_ms &&
        now - camera->timestamp_ms <= 250;
    const bool snapshotMatrixValid = attached && world->snapshot_timestamp_ms &&
        now >= world->snapshot_timestamp_ms && now - world->snapshot_timestamp_ms <= 1000 &&
        IsUsableCs2Matrix(world->view_matrix);
    const float* renderMatrix = cameraFresh ? camera->view_matrix :
        snapshotMatrixValid ? world->view_matrix : nullptr;
    const uint64_t renderMatrixSample = cameraFresh ? camera->timestamp_ms :
        snapshotMatrixValid ? world->snapshot_timestamp_ms : 0;
    const bool offsetsOk = CS2::offsets.loaded &&
        compatibility.state == OmniGhost::OffsetAuto::CompatibilityState::Ready;

    int aliveRemote = 0;
    int positionValid = 0;
    int filterCandidates = 0;
    int projected = 0;
    int onScreen = 0;
    int bonesReady = 0;
    int visibilityKnown = 0;
    int rejectedBots = 0;
    int rejectedTeam = 0;
    int rejectedVisibility = 0;
    int rejectedDistance = 0;
    if (attached) {
        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        for (const CS2::Player& player : world->players) {
            if (!player.pawn || player.is_local || !player.alive) continue;
            ++aliveRemote;
            if (!IsFiniteCs2Position(player.pos)) continue;
            const uint64_t positionSample = player.position_sample_ms ?
                player.position_sample_ms : world->world_sample_ms;
            if (!positionSample || now < positionSample || now - positionSample > 1000) continue;
            ++positionValid;
            if (player.bones_ok) ++bonesReady;
            if (player.visibility_known) ++visibilityKnown;
            ImVec2 point{};
            if (renderMatrix && DiagnosticWorldToScreen(player.pos, renderMatrix, displaySize, point)) {
                ++projected;
                if (point.x >= 0.f && point.x <= displaySize.x &&
                    point.y >= 0.f && point.y <= displaySize.y) ++onScreen;
            }
            if (!CS2::config.show_bots && player.is_bot) { ++rejectedBots; continue; }
            if (CS2::config.team_check && player.team == world->local_team) {
                ++rejectedTeam;
                continue;
            }
            if (CS2::config.visible_check && !CS2::config.visibility_colors &&
                (!player.visibility_known || !player.visible)) {
                ++rejectedVisibility;
                continue;
            }
            if (!std::isfinite(player.distance) ||
                (CS2::config.max_distance > 0.f && player.distance > CS2::config.max_distance)) {
                ++rejectedDistance;
                continue;
            }
            ++filterCandidates;
        }
    }

    BeginCardRow();
    const float half = CardRowHalfWidth();
    BeginCard("SISTEMA CS2", half);
    HealthRow("AquisiÃ§Ã£o", worker ? "Ativa" : "Parada",
        worker ? HealthStatus::Ok : HealthStatus::Warning);
    const char* readState = attached ? "Dados recentes" :
        (dma.processInitialized ? "AquisiÃ§Ã£o sem resposta" : "Processo nÃ£o associado");
    HealthRow("Leitura do jogo", readState,
        attached ? HealthStatus::Ok : HealthStatus::Warning);
    HealthRow("Offsets", offsetsOk ? "Validados no jogo" :
        compatibility.state == OmniGhost::OffsetAuto::CompatibilityState::Outdated ?
        "IncompatÃ­veis" : "Por validar",
        offsetsOk ? HealthStatus::Ok :
        compatibility.state == OmniGhost::OffsetAuto::CompatibilityState::Outdated ?
        HealthStatus::Error : HealthStatus::Warning);
    const char* matrixState = cameraFresh ? "CÃ¢mara rÃ¡pida atual" :
        snapshotMatrixValid ? "Fallback do snapshot ativo" : "IndisponÃ­vel ou antiga";
    HealthRow("ViewMatrix", matrixState, renderMatrix ? HealthStatus::Ok : HealthStatus::Warning);
    HealthRow("EntityList", worldFresh && world->entity_list_addr ? "Resolvida" : "A aguardar",
        attached && world->entity_list_addr ? HealthStatus::Ok : HealthStatus::Warning);
    HealthRow("Jogador local", attached && world->local_pawn && world->local_controller ?
        "Pawn e controller vÃ¡lidos" : "Pawn/controller em falta",
        attached && world->local_pawn && world->local_controller ? HealthStatus::Ok : HealthStatus::Warning);
    EndCard();

    NextCardColumn();
    BeginCard("PARTIDA", half);
    HealthRow("Estado", attached && world->in_match ? "Em partida" : "Fora da partida",
        attached && world->in_match ? HealthStatus::Ok : HealthStatus::Warning);
    char value[96]{};
    std::snprintf(value, sizeof(value), "%d", attached ? world->player_count : 0);
    KeyValueRow("Jogadores remotos", value);
    std::snprintf(value, sizeof(value), "%d vivos Â· %d com posiÃ§Ã£o vÃ¡lida", aliveRemote, positionValid);
    KeyValueRow("Entidades ESP", value);
    KeyValueRow("Mapa", attached && world->map_name[0] ? world->map_name : "-");
    EndCard();
    EndCardRow();

    BeginCardRow();
    BeginCard("CADEIA DO ESP", half);
    HealthRow("ESP", CS2::config.esp_enabled ? "Ativado" : "Desativado nas opÃ§Ãµes",
        CS2::config.esp_enabled ? HealthStatus::Ok : HealthStatus::Warning);
    char age[64]{};
    FormatAge(age, sizeof(age), now, world ?
        (world->world_sample_ms ? world->world_sample_ms : world->snapshot_timestamp_ms) : 0);
    HealthRow("Snapshot do mundo", worldFresh ? age : "Ausente ou desatualizado",
        worldFresh ? HealthStatus::Ok : HealthStatus::Warning);
    FormatAge(age, sizeof(age), now, renderMatrixSample);
    HealthRow("Amostra da cÃ¢mara", renderMatrix ? age : "Sem matriz utilizÃ¡vel",
        renderMatrix ? HealthStatus::Ok : HealthStatus::Warning);

    char projection[96]{};
    if (!renderMatrix)
        std::snprintf(projection, sizeof(projection), "Bloqueado pela ViewMatrix");
    else if (positionValid == 0)
        std::snprintf(projection, sizeof(projection), "A aguardar posiÃ§Ã£o vÃ¡lida");
    else
        std::snprintf(projection, sizeof(projection), "%d/%d projetÃ¡veis Â· %d no ecrÃ£",
            projected, positionValid, onScreen);
    const bool projectionOk = renderMatrix && (positionValid == 0 || projected > 0);
    HealthRow("World-to-Screen", projection,
        projectionOk ? HealthStatus::Ok : HealthStatus::Warning);
    std::snprintf(value, sizeof(value), "%d/%d jogadores", bonesReady, positionValid);
    HealthRow("Ossos", value,
        positionValid == 0 || bonesReady > 0 ? HealthStatus::Ok : HealthStatus::Warning);
    EndCard();

    NextCardColumn();
    BeginCard("FILTROS E RESULTADO", half);
    std::snprintf(value, sizeof(value), "%d de %d elegÃ­veis", filterCandidates, aliveRemote);
    HealthRow("ApÃ³s filtros", value,
        aliveRemote == 0 || filterCandidates > 0 ? HealthStatus::Ok : HealthStatus::Warning);
    std::snprintf(value, sizeof(value), "equipa %d Â· bots %d Â· visib. %d Â· distÃ¢ncia %d",
        rejectedTeam, rejectedBots, rejectedVisibility, rejectedDistance);
    KeyValueRow("ExcluÃ­dos", value);
    KeyValueRow("Equipa", CS2::config.team_check ? "Ocultar aliados" : "Mostrar todos");
    KeyValueRow("Bots", CS2::config.show_bots ? "Mostrar" : "Ocultar");
    std::snprintf(value, sizeof(value), "%.0f m", CS2::config.max_distance);
    KeyValueRow("DistÃ¢ncia mÃ¡xima", value);
    const char* visibilityMode = CS2::config.visible_check ?
        (CS2::config.visibility_colors ? "Cores visÃ­vel/oculto" : "Apenas visÃ­veis") :
        (CS2::config.visibility_colors ? "Cores sem filtragem" : "Desativada");
    KeyValueRow("Visibilidade", visibilityMode);
    std::snprintf(value, sizeof(value), "%d/%d conhecidos", visibilityKnown, positionValid);
    KeyValueRow("Dados de visibilidade", value);
    EndCard();
    EndCardRow();
}
