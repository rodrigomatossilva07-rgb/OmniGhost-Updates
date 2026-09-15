#include "../../widgets.h"
#include "../../theme.h"
#include "imgui.h"
#include "../../Warzone/warzone_game.h"
#include "../../../config/config_manager.h"
#include <cstdio>
#include <cstring>
#include <string>

void DrawWarzoneMisc() {
    const float half = CyberWidgets::CardRowHalfWidth();

    CyberWidgets::BeginCard("DIVERSOS GERAL", 0.f);
    CyberWidgets::ToggleSwitch("Aviso de Airstrike e Cluster Strike", &Warzone::config.misc_airstrike_alert);
    CyberWidgets::ToggleSwitch("Alertar jogadores Top 250", &Warzone::config.misc_top250_alert);
    CyberWidgets::ToggleSwitch("Aviso se estiver sendo olhado", &Warzone::config.misc_watched_alert);
    CyberWidgets::ToggleSwitch("Mostrar tags de clã quando possível", &Warzone::config.misc_clan_tags);
    CyberWidgets::ToggleSwitch("Rastrear jogadores entre partidas", &Warzone::config.misc_track_between_matches);
    CyberWidgets::ToggleSwitch("Lista de espectadores", &Warzone::config.misc_spectator_list);
    CyberWidgets::TextLine("Opções visuais guardadas no perfil Warzone.", CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();

    // Live backend status (real runtime — not a UI stub)
    CyberWidgets::BeginCard("Estado do Warzone", 0.f);
    {
        const bool dma = Warzone::runtime.module_base != 0;
        const bool matrix = Warzone::runtime.matrix_ok;
        const bool list = Warzone::runtime.list_ok;
        const bool decrypt_pending = Warzone::runtime.decrypt_needed;
        char fpsText[32]{};
        const float uiFps = ImGui::GetIO().Framerate;
        std::snprintf(fpsText, sizeof(fpsText), "%.0f FPS", uiFps > 0.0f ? uiFps : 0.0f);
        CyberWidgets::KeyValueRow("FPS UI", fpsText);
        CyberWidgets::KeyValueRow("Jogadores", std::to_string(Warzone::runtime.player_count).c_str());
        CyberWidgets::StatusBadge("DMA / módulo", dma);
        CyberWidgets::StatusBadge("Matriz", matrix);
        CyberWidgets::StatusBadge("Desencriptação", !decrypt_pending && dma);
        CyberWidgets::StatusBadge("Lista verificada", list);
        ImGui::Spacing();
        ImGui::TextWrapped("%s", Warzone::StatusLine());
        if (Warzone::runtime.decrypt_detail[0]) {
            ImGui::Spacing();
            ImGui::TextDisabled("Desencriptação:");
            ImGui::TextWrapped("%s", Warzone::runtime.decrypt_detail);
        }
        ImGui::Spacing();
        ImGui::TextDisabled("base=0x%llX  frames=%llu",
            (unsigned long long)Warzone::runtime.module_base,
            (unsigned long long)Warzone::runtime.frames);
        if (Warzone::runtime.decrypt_detail[0] &&
            (std::strstr(Warzone::runtime.decrypt_detail, "NULL_ENC") ||
             std::strstr(Warzone::runtime.decrypt_detail, "desatualizados"))) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.f),
                "NULL_ENC = lobby/menu ou os offsets deixaram de corresponder.\n"
                "Entra numa partida; se persistir, esta build não é suportada.");
        }
    }
    CyberWidgets::EndCard();

    // Scenario presets
    CyberWidgets::BeginCard("Predefinições de cenário", 0.f);
    CyberWidgets::TextLine(
        "Conjuntos de configuração por contexto. Os nomes não indicam segurança/deteção e não substituem perfis guardados.",
        CyberWidgets::TextTone::Secondary);
    ImGui::Spacing();
    auto apply_scenario = [](const char* id) {
        std::snprintf(Warzone::config.scenario_preset, sizeof(Warzone::config.scenario_preset), "%s", id);
        if (std::strcmp(id, "stream") == 0) {
            config_manager::ApplyGameProfile(config_manager::GameProfile::Minimal);
            Warzone::config.esp_enabled = true;
            Warzone::config.box = true;
            Warzone::config.skeleton = false;
            Warzone::config.snaplines = false;
            Warzone::config.name = true;
            Warzone::config.distance = true;
            Warzone::config.health_bar = true;
            Warzone::config.max_distance = 180;
            Warzone::config.aim_enabled = true;
            Warzone::config.aim_smooth = 55.f;
            Warzone::config.aim_fov = 55.f;
            Warzone::config.aim_humanize = true;
            Warzone::config.trigger_enabled = false;
            Warzone::config.radar_only_mode = false;
        } else if (std::strcmp(id, "ranked") == 0) {
            config_manager::ApplyGameProfile(config_manager::GameProfile::Minimal);
            Warzone::config.esp_enabled = true;
            Warzone::config.skeleton = true;
            Warzone::config.max_distance = 280;
            Warzone::config.aim_smooth = 35.f;
            Warzone::config.aim_fov = 70.f;
            Warzone::config.aim_humanize = true;
            Warzone::config.trigger_enabled = true;
            Warzone::config.trigger_delay_ms = 55;
            Warzone::config.radar_only_mode = false;
        } else if (std::strcmp(id, "hotdrop") == 0) {
            config_manager::ApplyGameProfile(config_manager::GameProfile::Visual);
            Warzone::config.esp_enabled = true;
            Warzone::config.skeleton = true;
            Warzone::config.snaplines = true;
            Warzone::config.max_distance = 400;
            Warzone::config.aim_smooth = 12.f;
            Warzone::config.aim_fov = 120.f;
            Warzone::config.trigger_enabled = true;
            Warzone::config.trigger_delay_ms = 25;
            Warzone::config.radar_only_mode = false;
        } else if (std::strcmp(id, "radar_only") == 0) {
            Warzone::config.esp_enabled = false;
            Warzone::config.aim_enabled = false;
            Warzone::config.trigger_enabled = false;
            Warzone::config.radar_2d = true;
            Warzone::config.radar_only_mode = true;
        } else if (std::strcmp(id, "visuals") == 0) {
            config_manager::ApplyGameProfile(config_manager::GameProfile::Visual);
            Warzone::config.esp_enabled = true;
            Warzone::config.aim_enabled = false;
            Warzone::config.trigger_enabled = false;
            Warzone::config.skeleton = true;
            Warzone::config.box = true;
            Warzone::config.name = true;
            Warzone::config.health_bar = true;
            Warzone::config.radar_only_mode = false;
        }
    };

    if (CyberWidgets::CyberButton("Poucos elementos", ImVec2(150, 32))) apply_scenario("stream");
    ImGui::SameLine();
    if (CyberWidgets::CyberButton("Equilibrado", ImVec2(150, 32))) apply_scenario("ranked");
    ImGui::SameLine();
    if (CyberWidgets::CyberButton("Curta distância", ImVec2(150, 32))) apply_scenario("hotdrop");
    ImGui::SameLine();
    if (CyberWidgets::CyberButton("Apenas radar", ImVec2(150, 32))) apply_scenario("radar_only");
    ImGui::SameLine();
    if (CyberWidgets::CyberButton("Visual", ImVec2(150, 32))) apply_scenario("visuals");
    ImGui::Spacing();
    ImGui::TextDisabled("Predefinição ativa: %s", Warzone::config.scenario_preset);
    CyberWidgets::EndCard();

    CyberWidgets::BeginCardRow();
    CyberWidgets::BeginCard("Estado da sessão", half);
    CyberWidgets::KeyValueRow("Módulo", Warzone::runtime.module_base ? "OK" : "—");
    CyberWidgets::KeyValueRow("Matriz", Warzone::runtime.matrix_ok ? "OK" : "FALHOU");
    CyberWidgets::KeyValueRow("Desencriptação", Warzone::runtime.decrypt_needed ? "PENDENTE/FALHOU" : "OK");
    CyberWidgets::KeyValueRow("Falhas de associação", std::to_string(Warzone::runtime.bind_fail_count).c_str());
    CyberWidgets::TextLine(
        "Falhas USB, alterações CR3 e latência scatter p95 serão apresentadas pelo diagnóstico global.",
        CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();

    CyberWidgets::NextCardColumn();
    CyberWidgets::BeginCard("Densidade do ESP e radar", half);
    CyberWidgets::ToggleSwitch("Modo de desempenho", &Warzone::config.performance_mode);
    CyberWidgets::ToggleSwitch("Ocultar nomes automaticamente (densidade)", &Warzone::config.esp_auto_hide_names);
    CyberWidgets::InputInt("Limite de densidade", &Warzone::config.esp_density_limit);
    CyberWidgets::SliderFloat("Tamanho do radar", &Warzone::config.radar_size, 80.f, 320.f, "%.0f");
    CyberWidgets::ToggleSwitch("Radar 2D", &Warzone::config.radar_2d);
    CyberWidgets::TextLine(
        "Com muitos jogadores, os textos longos ocultam-se; caixa e vida ficam prioritárias.",
        CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();
    CyberWidgets::EndCardRow();
}
