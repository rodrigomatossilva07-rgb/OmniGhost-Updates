#include "../../widgets.h"
#include "../../theme.h"
#include "config/app_settings.h"
#include "../../../../third_party/keyauth/x64/lib/QRCode/qrcode.hpp"
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
        CyberWidgets::ToggleSwitch(app_settings::T("Indicadores triangulares", "Triangle indicators"), &esp::config.triangle_radar);
        CyberWidgets::ToggleSwitch(app_settings::T("Marcadores no mapa", "Map markers"), &esp::config.blip_esp);
        CyberWidgets::ToggleSwitch(app_settings::T("Linha para o ponto marcado", "Line to waypoint"), &esp::config.waypoint_line);
        CyberWidgets::SliderFloat(app_settings::T("Alcance", "Range"), &esp::config.radar_range, 20.f, 500.f, "%.0f m");
        CyberWidgets::SliderFloat(app_settings::T("Tamanho", "Size"), &esp::config.radar_size, 80.f, 240.f, "%.0f px");
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
    CyberWidgets::BeginCard(app_settings::T("WEB RADAR", "WEB RADAR"), full);
    bool enabled = esp::config.web_radar_enabled;
    if (CyberWidgets::ToggleSwitch(app_settings::T("Ativar Web Radar", "Enable Web Radar"), &enabled)) {
        esp::config.web_radar_enabled = enabled;
        if (enabled) FiveM::WebRadar::Start(esp::config.web_radar_port);
        else FiveM::WebRadar::Stop();
    }
    if (enabled) {
        if (!FiveM::WebRadar::IsRunning()) FiveM::WebRadar::Start(esp::config.web_radar_port);
        bool cloud = esp::config.web_radar_cloudflare;
        if (cloud && !FiveM::WebRadar::CloudflareRunning() && FiveM::WebRadar::PublicUrl().empty())
            FiveM::WebRadar::StartCloudflare();
        if (CyberWidgets::ToggleSwitch(app_settings::T("Link público Cloudflare", "Cloudflare public link"), &cloud)) {
            esp::config.web_radar_cloudflare = cloud;
            if (cloud) FiveM::WebRadar::StartCloudflare(); else FiveM::WebRadar::StopCloudflare();
        }
        const auto d = FiveM::WebRadar::GetDiagnostics();
        CyberWidgets::KeyValueRow(app_settings::T("Fonte de dados", "Data source"),
            d.sourceFresh ? app_settings::T("Em direto", "Live") : app_settings::T("A aguardar", "Waiting"));
        CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary, "%u jogadores | %u veículos | %u ligações",
            d.players, d.vehicles, d.clients);
        if (CyberWidgets::GoldButton(app_settings::T("Abrir radar local", "Open local radar"), ImVec2(180.f, 34.f))) {
            const auto url = FiveM::WebRadar::LocalUrl();
            ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        if (cloud) {
            const auto publicUrl = FiveM::WebRadar::PublicUrl();
            if (!publicUrl.empty()) {
                if (CyberWidgets::CyberButton(app_settings::T("Abrir link público", "Open public link"), ImVec2(180.f, 34.f)))
                    ShellExecuteA(nullptr, "open", publicUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                ImGui::SameLine();
                if (CyberWidgets::CyberButton(app_settings::T("Copiar link", "Copy link"), ImVec2(140.f, 34.f)))
                    ImGui::SetClipboardText(publicUrl.c_str());
                DrawPublicLinkQr(publicUrl);
            }
            const auto status = FiveM::WebRadar::CloudflareStatus();
            if (!status.empty()) CyberWidgets::TextLine(status.c_str(), CyberWidgets::TextTone::Secondary);
        }
        CyberWidgets::TextLine(app_settings::T("O token privado fica no fragmento do link e nunca é incluído nos dados do mapa.",
            "The private token remains in the URL fragment and is never included in map data."), CyberWidgets::TextTone::Secondary);
    }
    CyberWidgets::EndCard();
#endif

    CyberWidgets::CardGap(gap);
    CyberWidgets::BeginCard(app_settings::T("ESTADO", "STATUS"), full);
    CyberWidgets::KeyValueRow(
        app_settings::T("Radar 2D", "2D Radar"),
        esp::config.radar_enabled
            ? app_settings::T("Ativo", "Active")
            : app_settings::T("Desativado", "Disabled"));
    CyberWidgets::EndCard();
    ImGui::EndGroup();
}
