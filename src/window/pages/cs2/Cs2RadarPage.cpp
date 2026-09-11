#include "../../widgets.h"
#include "../../theme.h"
#include "cs2_config.h"
#include "cs2_game.h"
#include "cs2_radar.h"
#include "imgui.h"
#include <Windows.h>
#include <shellapi.h>
#include <cstdio>
#include <string>

namespace {

void CopyToClipboard(const std::string& text) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (mem) {
        memcpy(GlobalLock(mem), text.c_str(), text.size() + 1);
        GlobalUnlock(mem);
        SetClipboardData(CF_TEXT, mem);
    }
    CloseClipboard();
}

} // namespace

void DrawCs2Radar() {
    const float full = CyberWidgets::CardContentWidth();
    const float gap = CyberTheme::Spacing::Sm;
    const float left = (full - gap) * 0.55f;
    const float right = full - left - gap;

    ImGui::BeginGroup();
    CyberWidgets::BeginCard("RADAR 2D", left);
    CyberWidgets::ToggleSwitch("Ativar radar 2D", &CS2::config.radar_2d);
    if (CS2::config.radar_2d) {
        CyberWidgets::SliderFloat("Tamanho", &CS2::config.radar_2d_size, 80.f, 320.f, "%.0f px");
        CyberWidgets::TextLine("Arrasta o radar no ecrã do jogo para reposicionar.",
                               CyberWidgets::TextTone::Secondary);
    }
    CyberWidgets::EndCard();

    CyberWidgets::CardGap(gap);
    CyberWidgets::BeginCard("WEB RADAR", left);
    const bool was = CS2::config.webradar_enabled;
    CyberWidgets::ToggleSwitch("Ativar Web Radar", &CS2::config.webradar_enabled);
    if (CS2::config.webradar_enabled != was && CS2::config.webradar_enabled) {
        if (CS2::WebRadar::Start(CS2::config.webradar_port))
            CyberWidgets::Notify("Web Radar a escutar", CyberWidgets::ToastType::Success);
        else
            CyberWidgets::Notify("Falha ao abrir porto HTTP", CyberWidgets::ToastType::Error);
    }
    if (CS2::config.webradar_enabled != was && !CS2::config.webradar_enabled) {
        CS2::WebRadar::Stop();
    }

    if (CS2::config.webradar_enabled) {
        int port = CS2::config.webradar_port;
        if (ImGui::SliderInt("Porto HTTP", &port, 1024, 65535)) {
            CS2::config.webradar_port = port;
            if (CS2::WebRadar::IsRunning() && CS2::WebRadar::Port() != port) {
                CS2::WebRadar::Stop();
                CS2::WebRadar::Start(port);
            }
        }

        const bool online = CS2::WebRadar::IsRunning();
        CyberWidgets::Badge(online ? "ONLINE" : "OFFLINE",
            online ? CyberWidgets::TextTone::Success : CyberWidgets::TextTone::Warning);

        const std::string local = CS2::WebRadar::LocalUrl();
        CyberWidgets::SectionTitle("LINK LOCAL (LAN)");
        CyberWidgets::TextLine(local.c_str(), CyberWidgets::TextTone::Secondary);
        if (CyberWidgets::Button("Copiar link local", CyberWidgets::ButtonStyle::Secondary, ImVec2(180.f, 32.f))) {
            CopyToClipboard(local);
            CyberWidgets::Notify("Link local copiado", CyberWidgets::ToastType::Success);
        }
        ImGui::SameLine();
        if (CyberWidgets::Button("Abrir local", CyberWidgets::ButtonStyle::Ghost, ImVec2(120.f, 32.f))) {
            std::string open = local;
            auto hash = open.find('#');
            if (hash != std::string::npos) open = open.substr(0, hash); // ShellExecute may not like hash
            open.push_back('#');
            open += CS2::WebRadar::AccessToken();
            ShellExecuteA(nullptr, "open", open.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }

        CyberWidgets::Separator();
        CyberWidgets::SectionTitle("LINK PUBLICO (CLOUDFLARE)");
        bool cf = CS2::config.webradar_cloudflare;
        if (CyberWidgets::ToggleSwitch("Tunel Cloudflare", &cf)) {
            CS2::config.webradar_cloudflare = cf;
            if (cf) {
                if (CS2::WebRadar::StartCloudflare())
                    CyberWidgets::Notify("Tunel Cloudflare a iniciar", CyberWidgets::ToastType::Info);
                else
                    CyberWidgets::Notify("cloudflared nao encontrado / falhou", CyberWidgets::ToastType::Error);
            } else {
                CS2::WebRadar::StopCloudflare();
            }
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
            CyberWidgets::TextLine("A aguardar URL do cloudflared...", CyberWidgets::TextTone::Secondary);
        } else {
            CyberWidgets::TextLine("Ativa o tunel para obter um link https publico.",
                                   CyberWidgets::TextTone::Secondary);
        }

        CyberWidgets::Separator();
        CyberWidgets::TextLine(
            "Frontend: Cs2/radar_webapp  ·  API: /api/live  ·  Token no # da URL",
            CyberWidgets::TextTone::Secondary);
    } else {
        CyberWidgets::TextLine("Web Radar desativado.", CyberWidgets::TextTone::Secondary);
    }
    CyberWidgets::EndCard();
    ImGui::EndGroup();

    ImGui::SameLine(0.f, gap);
    ImGui::BeginGroup();
    CyberWidgets::BeginCard("ESTADO", right);
    char line[128];
    std::snprintf(line, sizeof(line), "Jogadores no snapshot: %d", CS2::runtime.player_count);
    CyberWidgets::KeyValueRow("Dados", line);
    const char* mapLabel = CS2::runtime.map_name[0] ? CS2::runtime.map_name : "-";
    std::snprintf(line, sizeof(line), "%s", mapLabel);
    CyberWidgets::KeyValueRow("Mapa", line);
    std::snprintf(line, sizeof(line), "%s", CS2::WebRadar::IsRunning() ? "HTTP a escutar" : "parado");
    CyberWidgets::KeyValueRow("Servidor", line);
    std::snprintf(line, sizeof(line), "%s", CS2::WebRadar::CloudflareRunning() ? "tunel ativo" : "off");
    CyberWidgets::KeyValueRow("Cloudflare", line);
    CyberWidgets::EndCard();
    ImGui::EndGroup();
}
