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
    CyberWidgets::BeginCard("Warzone · Mira");
    CyberWidgets::StatusBadge("UI", true);
    CyberWidgets::StatusBadge("DMA", Warzone::runtime.module_base != 0);
    CyberWidgets::StatusBadge("Matriz", Warzone::runtime.matrix_ok);
    CyberWidgets::StatusBadge("Lista", Warzone::runtime.list_ok);
    ImGui::TextWrapped("%s", Warzone::StatusLine());
    if (Warzone::runtime.decrypt_needed)
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.f),
            "A lista de entidades precisa de desencriptação; ESP/mira ficam sem jogadores até estar disponível.");
    CyberWidgets::EndCard();

    CyberWidgets::BeginCardRow();
    CyberWidgets::BeginCard("Configurações globais");
    CyberWidgets::ToggleSwitch("Ativado", &Warzone::config.aim_enabled);
    CyberWidgets::ToggleSwitch("Humanização", &Warzone::config.aim_humanize);
    CyberWidgets::ToggleSwitch("Desenhar FOV", &Warzone::config.aim_draw_fov);
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
    CyberWidgets::BeginCard("Filtros");
    CyberWidgets::ToggleSwitch("Ignorar equipa", &Warzone::config.aim_ignore_team);
    CyberWidgets::ToggleSwitch("Ignorar derrubados", &Warzone::config.aim_ignore_downed);
    CyberWidgets::ToggleSwitch("Ignorar IA", &Warzone::config.aim_ignore_ai);
    CyberWidgets::ToggleSwitch("Verificar equipa no ESP", &Warzone::config.team_check);
    CyberWidgets::Separator();
    CyberWidgets::ToggleSwitch("Disparo automático", &Warzone::config.trigger_enabled);
    ImGui::TextUnformatted("Tecla de disparo");
    ImGui::SameLine(140.f); HotkeyCaptureButton("wz_trig", &Warzone::config.trigger_bind);
    CyberWidgets::InputInt("Atraso (ms)", &Warzone::config.trigger_delay_ms);
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
