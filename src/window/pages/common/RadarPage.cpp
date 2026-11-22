#include "../../widgets.h"
#include "../../theme.h"
#include "../../localization.h"
#include "../../platform/strprotect/strprotect.h"
#include "config/app_settings.h"
#include "../../../project/third_party/keyauth/x64/lib/QRCode/qrcode.hpp"
#include <cmath>
#include <memory>
#include <string>
#ifndef UI_PREVIEW
#include "games/Fivem/radar/fivem_web_radar.h"
#include <Windows.h>
#include <shellapi.h>
#endif
#ifdef UI_PREVIEW
#include "preview/preview_runtime.h"
#else
#include "esp/esp.h"
#endif

namespace {

void DrawPublicLinkQr(const std::string& url)
{
    static std::string encodedUrl;
    static std::unique_ptr<qrcodegen::QrCode> qr;

    if (url != encodedUrl) {
        encodedUrl = url;
        qr.reset();
        if (!url.empty()) {
            try {
                qr = std::make_unique<qrcodegen::QrCode>(
                    qrcodegen::QrCode::encodeText(url.c_str(), qrcodegen::QrCode::Ecc::MEDIUM));
            } catch (...) {
                encodedUrl.clear();
            }
        }
    }
    if (!qr)
        return;

    constexpr int quietZone = 4;
    const int modules = qr->getSize();
    const float available = (std::min)(220.f, CyberWidgets::CardContentWidth() - 24.f);
    const float moduleSize = (std::max)(2.f, std::floor(available / float(modules + quietZone * 2)));
    const float side = moduleSize * float(modules + quietZone * 2);
    const float offsetX = (CyberWidgets::CardContentWidth() - side) * .5f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.f, offsetX));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + side, origin.y + side), IM_COL32_WHITE, 6.f);
    for (int y = 0; y < modules; ++y) {
        for (int x = 0; x < modules; ++x) {
            if (!qr->getModule(x, y))
                continue;
            const ImVec2 a(origin.x + (x + quietZone) * moduleSize,
                           origin.y + (y + quietZone) * moduleSize);
            dl->AddRectFilled(a, ImVec2(a.x + moduleSize, a.y + moduleSize), IM_COL32_BLACK);
        }
    }
    ImGui::Dummy(ImVec2(side, side));
    CyberWidgets::TextLine(app_settings::T("Lê o QR Code para abrir o link público.",
                                          "Scan the QR code to open the public link."),
                           CyberWidgets::TextTone::Secondary);
}

}

void DrawRadar()
{
    const float full = CyberWidgets::CardContentWidth();
    const float gap = CyberTheme::Spacing::Sm;

    ImGui::BeginGroup();
    CyberWidgets::BeginCard("RADAR", full);
    if (CyberWidgets::ToggleSwitch(app_settings::T("Ativar radar", "Enable radar"), &esp::config.radar_enabled) && esp::config.radar_enabled)
        esp::config.square_radar = true;
    if (esp::config.radar_enabled) {
        CyberWidgets::ToggleSwitch(app_settings::T("Radar quadrado", "Square radar"), &esp::config.square_radar);
        CyberWidgets::ToggleSwitch(app_settings::T("Rodar com o jogador", "Rotate with player"), &esp::config.radar_rotate);
        CyberWidgets::ToggleSwitch(app_settings::T("Mostrar jogadores", "Show players"), &esp::config.radar_show_players);
        CyberWidgets::ToggleSwitch(app_settings::T("Mostrar veículos", "Show vehicles"), &esp::config.radar_show_vehicles);
        CyberWidgets::ToggleSwitch(app_settings::T("Nomes", "Names"), &esp::config.radar_show_names);
        CyberWidgets::ToggleSwitch(app_settings::T("Distâncias", "Distances"), &esp::config.radar_show_distances);
        CyberWidgets::ToggleSwitch(app_settings::T("Diferença de altura", "Height difference"), &esp::config.radar_show_height);
        CyberWidgets::ToggleSwitch(app_settings::T("Círculos de distância", "Distance rings"), &esp::config.radar_show_rings);
        CyberWidgets::ToggleSwitch(app_settings::T("Agrupar marcadores próximos", "Group nearby markers"), &esp::config.radar_group_markers);
        CyberWidgets::ToggleSwitch(app_settings::T("Trilhos curtos", "Short trails"), &esp::config.radar_trails);
        CyberWidgets::ToggleSwitch(app_settings::T("Modo compacto", "Compact mode"), &esp::config.radar_compact);
        CyberWidgets::ToggleSwitch(app_settings::T("Indicadores triangulares", "Triangle indicators"), &esp::config.triangle_radar);
        CyberWidgets::ToggleSwitch(app_settings::T("Marcadores no mapa", "Map markers"), &esp::config.blip_esp);
        CyberWidgets::ToggleSwitch(app_settings::T("Linha para o ponto marcado", "Line to waypoint"), &esp::config.waypoint_line);
        CyberWidgets::SliderFloat(app_settings::T("Alcance", "Range"), &esp::config.radar_range, 100.f, 2000.f, "%.0f m");
        CyberWidgets::SliderFloat(app_settings::T("Tamanho", "Size"), &esp::config.radar_size, 90.f, 320.f, "%.0f px");
        CyberWidgets::SliderFloat(app_settings::T("Tamanho dos marcadores", "Marker size"), &esp::config.radar_marker_size, 2.f, 12.f, "%.1f px");
        CyberWidgets::SliderFloat(app_settings::T("Suavização", "Smoothing"), &esp::config.radar_smoothing, 1.f, 30.f, "%.0f");
        CyberWidgets::TextLine(app_settings::T("Com o menu aberto, arrasta o radar diretamente no ecrã.",
                                              "With the menu open, drag the radar directly on screen."),
            CyberWidgets::TextTone::Secondary);
        CyberWidgets::SliderFloat(app_settings::T("Posição horizontal", "Horizontal position"), &esp::config.radar_pos_x, .05f, .95f, "%.2f");
        CyberWidgets::SliderFloat(app_settings::T("Posição vertical", "Vertical position"), &esp::config.radar_pos_y, .05f, .95f, "%.2f");
    } else {
        CyberWidgets::TextLine(app_settings::T("Radar desativado.", "Radar disabled."), CyberWidgets::TextTone::Secondary);
    }
    CyberWidgets::EndCard();

#ifndef UI_PREVIEW
    CyberWidgets::CardGap(gap);
    CyberWidgets::BeginCard(Loc::Tr("vis.web_radar_title"), full);
    bool enabled = esp::config.web_radar_enabled;
    if (CyberWidgets::ToggleSwitch(Loc::Tr("vis.web_radar_enable"), &enabled)) {
        esp::config.web_radar_enabled = enabled;
        if (enabled) FiveM::WebRadar::Start(esp::config.web_radar_port);
        else FiveM::WebRadar::Stop();
    }
    if (enabled) {
        if (!FiveM::WebRadar::IsRunning()) FiveM::WebRadar::Start(esp::config.web_radar_port);
        CyberWidgets::Separator();
        CyberWidgets::SectionTitle(Loc::Tr("vis.acquisition"));
        const char* radarModes[] = {
            Loc::Tr("vis.profile_balanced"),
            Loc::Tr("vis.profile_coverage"),
            Loc::Tr("vis.profile_performance")
        };
        if (CyberWidgets::Combo(Loc::Tr("vis.profile_label"),
                &esp::config.web_radar_mode, radarModes, 3)) {
            if (esp::config.web_radar_mode == 1) {
                esp::config.web_radar_max_players = 110;
                esp::config.web_radar_max_vehicles = 64;
                esp::config.web_radar_update_hz = 10.f;
            } else if (esp::config.web_radar_mode == 2) {
                esp::config.web_radar_max_players = 30;
                esp::config.web_radar_max_vehicles = 32;
                esp::config.web_radar_update_hz = 8.f;
            } else {
                esp::config.web_radar_max_players = 64;
                esp::config.web_radar_max_vehicles = 48;
                esp::config.web_radar_update_hz = 10.f;
            }
        }
        CyberWidgets::SliderFloat(Loc::Tr("vis.radar_distance"),
            &esp::config.web_radar_distance, 100.f, 5000.f, "%.0f m");
        const float compactSliderWidth = (std::min)(260.f, ImGui::GetContentRegionAvail().x * .42f);
        CyberWidgets::TextLine(Loc::Tr("vis.max_players"),
            CyberWidgets::TextTone::Secondary);
        ImGui::SetNextItemWidth(compactSliderWidth);
        ImGui::SliderInt("##fivem_web_radar_max_players", &esp::config.web_radar_max_players,
            8, 110, "%d jogadores");
        CyberWidgets::TextLine(Loc::Tr("vis.max_vehicles"),
            CyberWidgets::TextTone::Secondary);
        ImGui::SetNextItemWidth(compactSliderWidth);
        ImGui::SliderInt("##fivem_web_radar_max_vehicles", &esp::config.web_radar_max_vehicles,
            8, 64, "%d veículos");
        CyberWidgets::SliderFloat(Loc::Tr("vis.update_rate"),
            &esp::config.web_radar_update_hz, 2.f, 30.f, "%.0f Hz");
        FiveM::WebRadar::Configure({
            esp::config.web_radar_distance,
            static_cast<std::uint32_t>(esp::config.web_radar_max_players),
            static_cast<std::uint32_t>(esp::config.web_radar_max_vehicles),
            esp::config.web_radar_update_hz,
            esp::config.web_radar_mode
        });
        CyberWidgets::TextLine(Loc::Tr("vis.token_notice"),
            CyberWidgets::TextTone::Secondary);
        bool cloud = esp::config.web_radar_cloudflare;
        if (cloud && !FiveM::WebRadar::CloudflareRunning() && FiveM::WebRadar::PublicUrl().empty())
            FiveM::WebRadar::StartCloudflare();
        if (CyberWidgets::ToggleSwitch(Loc::Tr("vis.open_public_link"), &cloud)) {
            esp::config.web_radar_cloudflare = cloud;
            if (cloud) FiveM::WebRadar::StartCloudflare(); else FiveM::WebRadar::StopCloudflare();
        }
        const auto d = FiveM::WebRadar::GetDiagnostics();
        CyberWidgets::KeyValueRow(Loc::Tr("vis.data_source"),
            d.sourceFresh ? Loc::Tr("vis.live") : Loc::Tr("vis.waiting"));
        CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary, "%u jogadores | %u veículos | %u ligações",
            d.players, d.vehicles, d.clients);
        CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary,
            "%.1f / %.0f Hz | amostra %llu ms | %.0f m",
            d.updateHz, d.configuredHz,
            static_cast<unsigned long long>(d.lastSampleAgeMs), d.maxDistance);
        if (CyberWidgets::GoldButton(Loc::Tr("vis.open_local_radar"), ImVec2(180.f, 34.f))) {
            const auto url = FiveM::WebRadar::LocalUrl();
            ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        if (cloud) {
            const auto publicUrl = FiveM::WebRadar::PublicUrl();
            if (!publicUrl.empty()) {
                if (CyberWidgets::CyberButton(Loc::Tr("vis.open_public_link"), ImVec2(180.f, 34.f)))
                    ShellExecuteA(nullptr, "open", publicUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                ImGui::SameLine();
                if (CyberWidgets::CyberButton(Loc::Tr("vis.copy_link"), ImVec2(140.f, 34.f)))
                    ImGui::SetClipboardText(publicUrl.c_str());
                DrawPublicLinkQr(publicUrl);
            }
            const auto status = FiveM::WebRadar::CloudflareStatus();
            if (!status.empty()) CyberWidgets::TextLine(status.c_str(), CyberWidgets::TextTone::Secondary);
        }
        CyberWidgets::TextLine(Loc::Tr("vis.token_notice"), CyberWidgets::TextTone::Secondary);
    }
    CyberWidgets::EndCard();
#endif

    ImGui::EndGroup();
}

