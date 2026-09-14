#include "../../widgets.h"
#include "../../theme.h"
#include "../../InputDevicesCard.h"
#include "imgui.h"
#include "../../Warzone/warzone_game.h"
#include "../../Warzone/warzone_aim.h"
#include "aimbot/aim_type.h"
#include <Windows.h>
#include <cstdio>

namespace {
const char* VkName(int vk) {
    if (vk <= 0) return "NENHUM";
    switch (vk) {
    case 1: return "Mouse Esquerdo"; case 2: return "Mouse Direito";
    case 4: return "Mouse Meio"; case 5: return "Mouse 4"; case 6: return "Mouse 5";
    default: break;
    }
    static char buf[32];
    std::snprintf(buf, sizeof(buf), "VK 0x%02X", vk);
    return buf;
}
bool HotkeyCaptureButton(const char* id, int* vk) {
    static int* capturing = nullptr;
    static float blink = 0.f;
    static double ignore_until = 0.0;
    ImGui::PushID(id);
    const bool isCap = (capturing == vk);
    char label[64];
    if (isCap) {
        blink += ImGui::GetIO().DeltaTime;
        std::snprintf(label, sizeof(label), (int(blink * 5.f) % 2) ? "( ... )" : "(  .  )");
    } else std::snprintf(label, sizeof(label), "%s", VkName(*vk));
    const bool clicked = CyberWidgets::Button(
        label, isCap ? CyberWidgets::ButtonStyle::Primary : CyberWidgets::ButtonStyle::Secondary,
        ImVec2(160.f, 30.f));
    if (clicked) {
        if (isCap) capturing = nullptr;
        else { capturing = vk; blink = 0.f; ignore_until = ImGui::GetTime() + 0.28; }
    }
    if (isCap && ImGui::GetTime() >= ignore_until) {
        for (int s = 1; s < 256 && capturing; ++s) {
            if (GetAsyncKeyState(s) & 0x8000) { *vk = s; capturing = nullptr; break; }
        }
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { *vk = 0; capturing = nullptr; }
    }
    ImGui::PopID();
    return false;
}
}

void DrawWarzoneAim() {
    static bool smoothTransitions = true, visibility = true, hardLock = false, deadzoneVisual = false, predictionDot = false;
    static float aimInterval = 5.f, cameraReaction = 0.f, bezier = 0.f, predictionDotSize = 1.f;
    CyberWidgets::BeginCard("MIRA GLOBAL", 0.f);
    CyberWidgets::TextLine("Uma configuração única para todas as armas.", CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();
    CyberWidgets::BeginCardRow();
    CyberWidgets::BeginCard("CONFIGURAÇÕES GLOBAIS");
    CyberWidgets::ToggleSwitch("Ativado", &Warzone::config.aim_enabled);
    CyberWidgets::ToggleSwitch("Sempre ativo", &Warzone::config.aim_always_on);
    CyberWidgets::ToggleSwitch("Transições suaves entre bones", &smoothTransitions);
    CyberWidgets::ToggleSwitch("Humanização", &Warzone::config.aim_humanize);
    CyberWidgets::SliderFloat("Intervalo de mira", &aimInterval, 1.f, 25.f, "%.0f ms");
    CyberWidgets::SliderFloat("Tempo de reação da câmera", &cameraReaction, 0.f, 250.f, "%.0f ms");
    CyberWidgets::SliderFloat("Curva Bézier", &bezier, 0.f, 100.f, "%.0f%%");
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("MIRA");
    CyberWidgets::SliderFloat("FOV", &Warzone::config.aim_fov, 10.0f, 300.0f, "%.0f px");
    CyberWidgets::SliderFloat("Suavização", &Warzone::config.aim_smooth, 0.0f, 100.0f, "%.0f");
    CyberWidgets::SliderFloat("Zona morta", &Warzone::config.aim_deadzone, 0.0f, 20.0f, "%.1f px");
    CyberWidgets::SliderFloat("Distância máxima", &Warzone::config.aim_max_dist, 20.f, 500.f, "%.0f m");
    {
        const char* hitboxes[] = { "Cabeça", "Peito" };
        CyberWidgets::Combo("Zona do alvo", &Warzone::config.aim_bone, hitboxes, 2);
    }
    ImGui::TextUnformatted("Tecla de ativação");
    ImGui::SameLine(120.f); HotkeyCaptureButton("wz_aim1", &Warzone::config.aim_bind);
    ImGui::SameLine(); HotkeyCaptureButton("wz_aim2", &Warzone::config.aim_bind2);
    CyberWidgets::EndCard();

    CyberWidgets::NextCardColumn();
    CyberWidgets::BeginCard("FILTROS");
    CyberWidgets::ToggleSwitch("Verificar visibilidade", &visibility);
    CyberWidgets::ToggleSwitch("Ignorar equipa", &Warzone::config.aim_ignore_team);
    CyberWidgets::ToggleSwitch("Ignorar derrubados", &Warzone::config.aim_ignore_downed);
    CyberWidgets::ToggleSwitch("Ignorar IA", &Warzone::config.aim_ignore_ai);
    CyberWidgets::ToggleSwitch("Verificar equipa no ESP", &Warzone::config.team_check);
    CyberWidgets::ToggleSwitch("Hard lock", &hardLock);
    CyberWidgets::EndCard();
    CyberWidgets::EndCardRow();

    CyberWidgets::BeginCardRow();
    CyberWidgets::BeginCard("TRIGGER BOT");
    CyberWidgets::ToggleSwitch("Ativado", &Warzone::config.trigger_enabled);
    CyberWidgets::ToggleSwitch("Ignorar equipa", &Warzone::config.trigger_team_check);
    static bool verifyDowned = true, verifySpectators = false;
    CyberWidgets::ToggleSwitch("Verificar derrubados", &verifyDowned);
    CyberWidgets::ToggleSwitch("Verificar espectadores ativos", &verifySpectators);
    CyberWidgets::EndCard();
    CyberWidgets::NextCardColumn();
    CyberWidgets::BeginCard("TECLAS");
    CyberWidgets::TextLine("Define uma tecla para ativar o trigger bot.", CyberWidgets::TextTone::Secondary);
    ImGui::TextUnformatted("Tecla de disparo");
    ImGui::SameLine(150.f); HotkeyCaptureButton("wz_trig", &Warzone::config.trigger_bind);
    CyberWidgets::EndCard();
    CyberWidgets::EndCardRow();

    CyberWidgets::BeginCardRow();
    CyberWidgets::BeginCard("CONFIGURAÇÃO GLOBAL");
    float triggerDelay = (float)Warzone::config.trigger_delay_ms;
    if (CyberWidgets::SliderFloat("Delay de tiro", &triggerDelay, 0.f, 500.f, "%.0f ms"))
        Warzone::config.trigger_delay_ms = (int)triggerDelay;
    CyberWidgets::EndCard();
    CyberWidgets::NextCardColumn();
    CyberWidgets::BeginCard("ESTADO GLOBAL");
    static bool triggerAlways = false, scopedOnly = false;
    CyberWidgets::ToggleSwitch("Sempre ativo", &triggerAlways);
    CyberWidgets::ToggleSwitch("Ativar apenas com mira", &scopedOnly);
    CyberWidgets::TextLine("Configuração global para todas as armas.", CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();
    CyberWidgets::EndCardRow();

    CyberWidgets::BeginCardRow();
    CyberWidgets::BeginCard("VISUAIS");
    CyberWidgets::ToggleSwitch("Desenhar círculo de FOV", &Warzone::config.aim_draw_fov);
    CyberWidgets::ToggleSwitch("Desenhar círculo de deadzone", &deadzoneVisual);
    CyberWidgets::ToggleSwitch("Desenhar ponto de predição", &predictionDot);
    CyberWidgets::SliderFloat("Tamanho do ponto", &predictionDotSize, .5f, 8.f, "%.1f px");
    CyberWidgets::EndCard();
    CyberWidgets::NextCardColumn();
    CyberWidgets::BeginCard("SENSIBILIDADES");
    CyberWidgets::TextLine("Parâmetros visuais preparados para futura integração de sensibilidade.", CyberWidgets::TextTone::Secondary);
    CyberWidgets::SliderFloat("Sensibilidade do mouse", &Warzone::config.aim_smooth, .1f, 100.f, "%.2f");
    CyberWidgets::SliderFloat("Multiplicador ADS", &Warzone::config.aim_deadzone, .1f, 5.f, "%.2fx");
    CyberWidgets::EndCard();
    CyberWidgets::EndCardRow();

    InputDevicesCard::Draw();

    CyberWidgets::BeginCard("Estado");
    CyberWidgets::KeyValueRow("Entrada", aim_type::StatusText());
    CyberWidgets::KeyValueRow("Mira", Warzone_Aim::DebugStatus());
    char runtime[96]{};
    std::snprintf(runtime, sizeof(runtime), "%d jogadores · DMA %s", Warzone::runtime.player_count,
        Warzone::runtime.module_base ? "anexado" : "em espera");
    CyberWidgets::KeyValueRow("Execução", runtime);
    CyberWidgets::EndCard();
}
