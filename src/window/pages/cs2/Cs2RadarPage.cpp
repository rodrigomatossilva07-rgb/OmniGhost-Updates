#include "../../widgets.h"
#include "../../theme.h"
#include "cs2_config.h"
#include "cs2_game.h"
#include "cs2_radar.h"
#include "imgui.h"
#include <Windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cstdio>
#include <string>

namespace {

bool CompactToggle(const char* id, const char* label, bool* value, float width) {
    ImGui::PushID(id);
    ImGui::BeginChild("##compact_toggle", ImVec2(width, CyberTheme::Metrics::RowHeight), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const bool changed = CyberWidgets::ToggleSwitch(label, value);
    ImGui::EndChild();
    ImGui::PopID();
    return changed;
}

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
    const float controlWidth = (std::min)(left * 0.62f, 310.f);
    CompactToggle("radar2d", "Ativar radar 2D", &CS2::config.radar_2d, controlWidth);
    if (CS2::config.radar_2d) {
        if (ImGui::Button("Pequeno")) CS2::config.radar_2d_size = 160.f;
        ImGui::SameLine();
        if (ImGui::Button("Grande")) CS2::config.radar_2d_size = 280.f;
        CyberWidgets::SliderFloat("Tamanho", &CS2::config.radar_2d_size, 80.f, 320.f, "%.0f px");
        CyberWidgets::TextLine("Arrasta o radar no ecrã do jogo para reposicionar.",
                               CyberWidgets::TextTone::Secondary);
    }
    CyberWidgets::EndCard();

    CyberWidgets::CardGap(gap);
    CyberWidgets::BeginCard("WEB RADAR", left);
    const bool was = CS2::config.webradar_enabled;

    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("ESPECTADORES");
    CompactToggle("spectator_list", "Lista de quem te observa", &CS2::config.spectator_list, controlWidth);
    if (CS2::config.spectator_list) {
        const auto snap = CS2::AcquireRuntimeSnapshot();
        char spec_line[64];
        const int spectator_count = snap ? snap->spectator_count : 0;
        std::snprintf(spec_line, sizeof(spec_line), "A observar-te agora: %d", spectator_count);
        CyberWidgets::TextLine(spec_line, CyberWidgets::TextTone::Secondary);
        CyberWidgets::TextLine(
            "Janela no canto superior direito do overlay (in-game).",
            CyberWidgets::TextTone::Secondary);
        if (snap && snap->spectator_count > 0) {
            int shown = 0;
            for (const auto& sp : snap->spectators) {
                if (shown >= 8) break;
                char row[80];
                std::snprintf(row, sizeof(row), "  - %s", sp.name[0] ? sp.name : "Jogador");
                CyberWidgets::TextLine(row, CyberWidgets::TextTone::Secondary);
                ++shown;
            }
        }
    }

    CompactToggle("webradar", "Ativar Web Radar", &CS2::config.webradar_enabled, controlWidth);
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
        ImGui::SetNextItemWidth((std::min)(left * 0.50f, 260.f));
        if (ImGui::SliderInt("Porta", &port, 1024, 65535)) {
            CS2::config.webradar_port = port;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
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
        if (CompactToggle("cloudflare", "Tunel Cloudflare", &cf, controlWidth)) {
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
            const std::string tunnelStatus = CS2::WebRadar::CloudflareStatus();
            CyberWidgets::TextLine(tunnelStatus.empty() ? "A aguardar URL do cloudflared..." : tunnelStatus.c_str(),
                                   CyberWidgets::TextTone::Secondary);
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
