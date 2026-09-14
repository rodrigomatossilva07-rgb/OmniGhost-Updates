#include "../../widgets.h"
#include "../../theme.h"
#include "../../localization.h"
#include "cs2_config.h"
#include "cs2_aim.h"
#include "imgui.h"
#include <Windows.h>
#include <cstdio>
#include <algorithm>

namespace {

const char* VkName(int vk) {
    if (vk <= 0) return "-";
    switch (vk) {
    case 1: return "Mouse Esquerdo";
    case 2: return "Mouse Direito";
    case 4: return "Mouse Meio";
    case 5: return "Mouse 4";
    case 6: return "Mouse 5";
    default: break;
    }
    static char buf[32];
    if (vk >= 'A' && vk <= 'Z') { std::snprintf(buf, sizeof(buf), "%c", vk); return buf; }
    if (vk >= 0x70 && vk <= 0x7B) { std::snprintf(buf, sizeof(buf), "F%d", vk - 0x6F); return buf; }
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
        const bool on = (static_cast<int>(blink * 5.f) % 2) == 0;
        std::snprintf(label, sizeof(label), on ? "( ... )" : "(  .  )");
    } else {
        std::snprintf(label, sizeof(label), "%s", VkName(*vk));
    }
    const bool clicked = CyberWidgets::Button(
        label, isCap ? CyberWidgets::ButtonStyle::Primary : CyberWidgets::ButtonStyle::Secondary,
        ImVec2(180.f, 32.f));
    if (clicked) {
        if (isCap) capturing = nullptr;
        else {
            capturing = vk;
            blink = 0.f;
            ignore_until = ImGui::GetTime() + 0.28;
        }
    }
    if (isCap && ImGui::GetTime() >= ignore_until) {
        for (int vkScan = 1; vkScan < 256 && capturing; ++vkScan) {
            if (GetAsyncKeyState(vkScan) & 0x8000) {
                *vk = vkScan;
                capturing = nullptr;
                break;
            }
        }
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
            *vk = 0;
            capturing = nullptr;
        }
    }
    ImGui::PopID();
    return false;
}

} // namespace

void DrawCs2Aim() {
    const float full = CyberWidgets::CardContentWidth();
    const float gap = CyberTheme::Spacing::Sm;
    const float left = full;

    ImGui::BeginGroup();
    CyberWidgets::BeginCard("ASSISTENCIA DE MIRA", left);
    CyberWidgets::ToggleSwitch("Ativar assistencia", &CS2::config.aim_enabled);
    CyberWidgets::ToggleSwitch("Crosshair para sniper", &CS2::config.sniper_crosshair);
    if (CS2::config.aim_enabled) {
        // These must drive the aim filters, not the independent ESP filters.
        CyberWidgets::ToggleSwitch("Verificacao de visibilidade", &CS2::config.aim_visibility_check);
        CyberWidgets::ToggleSwitch("Ignorar equipa", &CS2::config.aim_ignore_team);
        CyberWidgets::Separator();
        CyberWidgets::SectionTitle("ALVO");
        const char* hitboxes[] = { "Cabeca", "Pescoco", "Tronco", "Pelvis", "Pernas" };
        CyberWidgets::Combo("Zona do alvo", &CS2::config.aim_bone, hitboxes, 5);
        CyberWidgets::SliderFloat("FOV", &CS2::config.aim_fov, 10.f, 400.f, "%.0f px");
        CyberWidgets::SliderFloat("Distancia", &CS2::config.aim_max_dist, 10.f, 500.f, "%.0f m");
        CyberWidgets::SliderFloat("Suavidade", &CS2::config.aim_smooth, 0.f, 100.f, "%.0f");
        CyberWidgets::ToggleSwitch("Humanizar", &CS2::config.aim_humanize);
        CyberWidgets::Separator();
        CyberWidgets::SectionTitle("TECLA");
        ImGui::TextUnformatted("Tecla da mira");
        ImGui::SameLine(140.f);
        HotkeyCaptureButton("cs2aim1", &CS2::config.aim_bind);
        if (CS2::config.aim_bind <= 0)
            CS2::config.aim_bind = 0x02;
        CS2::config.aim_bind2 = 0;
        CyberWidgets::KeyValueRow("Estado", CS2_Aim::DebugStatus());
    } else {
        CyberWidgets::TextLine("Assistencia desativada — ativa para configurar alvo e teclas.",
                               CyberWidgets::TextTone::Secondary);
    }
    CyberWidgets::EndCard();

    CyberWidgets::CardGap(gap);
    CyberWidgets::BeginCard(Loc::Tr("aim.trigger_title"), left);
    CyberWidgets::ToggleSwitch(Loc::Tr("aim.trigger"), &CS2::config.trigger_enabled);
    if (CS2::config.trigger_enabled) {
        CyberWidgets::TextLine(Loc::Tr("aim.trigger_hint2"), CyberWidgets::TextTone::Secondary);
        CyberWidgets::ToggleSwitch(Loc::Tr("aim.trigger_always"), &CS2::config.trigger_always_on);
        if (!CS2::config.trigger_always_on) {
            ImGui::TextUnformatted(Loc::Tr("aim.trigger_key"));
            ImGui::SameLine(140.f);
            HotkeyCaptureButton("cs2trigger", &CS2::config.trigger_bind);
        }
        CyberWidgets::ToggleSwitch(Loc::Tr("aim.trigger_head"), &CS2::config.trigger_head_only);
        CyberWidgets::ToggleSwitch(Loc::Tr("vis.team_check"), &CS2::config.trigger_team_check);
        float delay_ms = static_cast<float>(CS2::config.trigger_delay_ms);
        if (CyberWidgets::SliderFloat(Loc::Tr("aim.trigger_reaction"), &delay_ms, 0.f, 500.f, "%.0f ms"))
            CS2::config.trigger_delay_ms = static_cast<int>(delay_ms);
    }
    CyberWidgets::EndCard();
    ImGui::EndGroup();
}
