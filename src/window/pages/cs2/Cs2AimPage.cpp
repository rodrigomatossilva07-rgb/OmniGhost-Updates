#include "../../widgets.h"
#include "../../theme.h"
#include "../../localization.h"
#include "config/cs2_config.h"
#include "aimbot/cs2_aim.h"
#include "aimbot/aim_type.h"
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
    bool changed = false;
    if (isCap && ImGui::GetTime() >= ignore_until) {
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
            *vk = 0;
            capturing = nullptr;
            changed = true;
        }
        for (int vkScan = 1; vkScan < 256 && capturing; ++vkScan) {
            if (GetAsyncKeyState(vkScan) & 0x8000) {
                *vk = vkScan;
                capturing = nullptr;
                changed = true;
                break;
            }
        }
    }
    ImGui::PopID();
    return changed;
}

}

void DrawCs2Aim() {
    const float gap = CyberTheme::Spacing::Md;
    const float full = CyberWidgets::CardContentWidth();
    const float left = full;

    CyberWidgets::BeginCard("ASSISTENCIA DE MIRA", left);
    CyberWidgets::ToggleSwitch("Ativar mira", &CS2::config.aim_enabled);
    if (CS2::config.aim_enabled) {
        CyberWidgets::TextLine("Tecla de ativacao", CyberWidgets::TextTone::Secondary);
        HotkeyCaptureButton("##cs2_aim_bind", &CS2::config.aim_bind);
        CyberWidgets::TextLine("Ativa enquanto mantiveres a tecla premida; ao largar, desativa.",
                               CyberWidgets::TextTone::Secondary);


        CyberWidgets::SliderFloat(Loc::Tr("aim.smooth"), &CS2::config.aim_smooth, 0.f, 100.f, "%.0f");
        CS2::config.aim_smooth = std::clamp(CS2::config.aim_smooth, 0.f, 100.f);


        CyberWidgets::SliderFloat("FOV Size", &CS2::config.aim_fov, 10.f, 500.f, "%.0f px");
        CS2::config.aim_fov = std::clamp(CS2::config.aim_fov, 1.f, 500.f);


        CyberWidgets::ToggleSwitch("Osso automatico", &CS2::config.aim_auto_bone);
        static const char* kPoints[] = { "Head", "Neck", "Chest", "Stomach" };
        int point = CS2::config.aim_bone;
        if (point < 0) point = 0;
        if (point > 3) point = 3;
        if (CyberWidgets::Combo("Aim Point", &point, kPoints, 4))
            CS2::config.aim_bone = point;


        CyberWidgets::SliderFloat("Humanization", &CS2::config.aim_humanization, 0.f, 100.f, "%.0f");
        CS2::config.aim_humanization = std::clamp(CS2::config.aim_humanization, 0.f, 100.f);
        CS2::config.aim_humanize = CS2::config.aim_humanization > 0.5f;


        CyberWidgets::ToggleSwitch("Verificacao de visibilidade", &CS2::config.aim_visibility_check);
        if (CS2::config.aim_visibility_check)
            CyberWidgets::TextLine("So puxa alvos visiveis (spotted). Desliga para puxar atras de paredes.",
                                   CyberWidgets::TextTone::Secondary);


        CyberWidgets::ToggleSwitch("Dynamic FOV", &CS2::config.aim_dynamic_fov);
        if (CS2::config.aim_dynamic_fov) {
            CyberWidgets::SliderFloat("Min FOV", &CS2::config.aim_fov_min, 5.f, 200.f, "%.0f px");
            CS2::config.aim_fov_min = std::clamp(CS2::config.aim_fov_min, 1.f, 200.f);
        }

        CyberWidgets::Separator();
        CyberWidgets::KeyValueRow("Diagnostico", CS2_Aim::DebugStatus());
        CyberWidgets::KeyValueRow("Entrada", aim_type::StatusText());
    } else {
        CyberWidgets::TextLine("Ativa a mira para configurar smooth, ponto e humanizacao.",
                               CyberWidgets::TextTone::Secondary);
    }
    CyberWidgets::EndCard();

    CyberWidgets::CardGap(gap);
    CyberWidgets::BeginCard(Loc::Tr("aim.trigger_title"), left);
    CyberWidgets::ToggleSwitch(Loc::Tr("aim.trigger"), &CS2::config.trigger_enabled);
    if (CS2::config.trigger_enabled) {
        CyberWidgets::TextLine(Loc::Tr("aim.trigger_hint2"), CyberWidgets::TextTone::Secondary);
        CyberWidgets::ToggleSwitch(Loc::Tr("aim.trigger_always"), &CS2::config.trigger_always_on);
        CyberWidgets::ToggleSwitch(Loc::Tr("aim.trigger_head"), &CS2::config.trigger_head_only);
        CyberWidgets::ToggleSwitch(Loc::Tr("vis.team_check"), &CS2::config.trigger_team_check);
        float delay_ms = static_cast<float>(CS2::config.trigger_delay_ms);
        if (CyberWidgets::SliderFloat(Loc::Tr("aim.trigger_reaction"), &delay_ms, 0.f, 500.f, "%.0f ms"))
            CS2::config.trigger_delay_ms = static_cast<int>(delay_ms);
    }
    CyberWidgets::EndCard();
}
