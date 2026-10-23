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

void DrawFovCurveEditor() {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, 150.f);
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, end, IM_COL32(7, 8, 9, 235), 6.f);
    draw->AddRect(origin, end, IM_COL32(96, 78, 31, 220), 6.f);
    ImGui::InvisibleButton("##cs2_fov_curve", size);
    int nearest = -1;
    float nearestDistance = 1.e9f;
    ImVec2 points[5]{};
    for (int i = 0; i < 5; ++i) {
        const float x = std::clamp(CS2::config.aim_fov_curve_distance[i] / 300.f, 0.f, 1.f);
        const float y = std::clamp(CS2::config.aim_fov_curve_value[i] / 500.f, 0.f, 1.f);
        points[i] = ImVec2(origin.x + 8.f + x * (size.x - 16.f), end.y - 8.f - y * (size.y - 16.f));
        if (i > 0) draw->AddLine(points[i - 1], points[i], IM_COL32(229, 184, 55, 235), 2.f);
        draw->AddCircleFilled(points[i], 5.f, IM_COL32(246, 207, 83, 255));
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float dx = mouse.x - points[i].x, dy = mouse.y - points[i].y;
        const float distance = dx * dx + dy * dy;
        if (distance < nearestDistance) { nearestDistance = distance; nearest = i; }
    }
    static int dragging = -1;
    if (ImGui::IsItemActivated() && nearestDistance <= 225.f) dragging = nearest;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragging = -1;
    if (dragging >= 0 && ImGui::IsItemActive()) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float normalizedX = std::clamp((mouse.x - origin.x - 8.f) / (size.x - 16.f), 0.f, 1.f);
        const float normalizedY = std::clamp((end.y - 8.f - mouse.y) / (size.y - 16.f), 0.f, 1.f);
        if (dragging > 0) {
            const float lower = CS2::config.aim_fov_curve_distance[dragging - 1] + .1f;
            const float upper = dragging < 4 ? CS2::config.aim_fov_curve_distance[dragging + 1] - .1f : 300.f;
            CS2::config.aim_fov_curve_distance[dragging] = std::clamp(normalizedX * 300.f, lower, upper);
        }
        CS2::config.aim_fov_curve_value[dragging] = std::clamp(normalizedY * 500.f, 1.f, 500.f);
    }
    CyberWidgets::TextLine("Eixo X: distância (0-300 m) · eixo Y: FOV (1-500 px)", CyberWidgets::TextTone::Secondary);
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
        CyberWidgets::TextLine("Teclas alternativas (opcional)", CyberWidgets::TextTone::Secondary);
        HotkeyCaptureButton("##cs2_aim_bind2", &CS2::config.aim_bind2);
        ImGui::SameLine();
        HotkeyCaptureButton("##cs2_aim_bind3", &CS2::config.aim_bind3);

        if (CS2::config.aim_bind2 == CS2::config.aim_bind ||
            CS2::config.aim_bind2 == VK_LBUTTON)
            CS2::config.aim_bind2 = 0;
        if (CS2::config.aim_bind3 == CS2::config.aim_bind ||
            CS2::config.aim_bind3 == CS2::config.aim_bind2 ||
            CS2::config.aim_bind3 == VK_LBUTTON)
            CS2::config.aim_bind3 = 0;
        CyberWidgets::TextLine("Ativa enquanto mantiveres a tecla premida; ao largar, desativa.",
                               CyberWidgets::TextTone::Secondary);


        CyberWidgets::SliderFloat(Loc::Tr("aim.smooth"), &CS2::config.aim_smooth, 0.f, 100.f, "%.0f");
        CS2::config.aim_smooth = std::clamp(CS2::config.aim_smooth, 0.f, 100.f);


        CyberWidgets::SliderFloat("FOV Size", &CS2::config.aim_fov, 10.f, 500.f, "%.0f px");
        CS2::config.aim_fov = std::clamp(CS2::config.aim_fov, 1.f, 500.f);


        CyberWidgets::ToggleSwitch("Osso automatico", &CS2::config.aim_auto_bone);
        static const char* kPoints[] = {
            "Head", "Neck", "Chest", "Stomach", "Pelvis", "Left leg", "Right leg"
        };
        int point = CS2::config.aim_bone;
        if (point < 0) point = 0;
        if (point > 6) point = 6;
        if (CyberWidgets::Combo("Aim Point", &point, kPoints, 7))
            CS2::config.aim_bone = point;

        CyberWidgets::ToggleSwitch("Apenas bones visíveis", &CS2::config.aim_visible_bones_only);
        if (CS2::config.aim_visible_bones_only) {
            CyberWidgets::TextLine("Requer mapa de colisão carregado; dados desconhecidos são rejeitados.",
                                   CyberWidgets::TextTone::Secondary);
            for (int priority = 0; priority < 7; ++priority) {
                char label[32];
                std::snprintf(label, sizeof(label), "Prioridade %d", priority + 1);
                int selected = std::clamp(CS2::config.aim_bone_priority[priority], 0, 6);
                if (CyberWidgets::Combo(label, &selected, kPoints, 7)) {
                    for (int other = 0; other < 7; ++other) {
                        if (other != priority && CS2::config.aim_bone_priority[other] == selected)
                            std::swap(CS2::config.aim_bone_priority[other], CS2::config.aim_bone_priority[priority]);
                    }
                    CS2::config.aim_bone_priority[priority] = selected;
                }
            }
        }


        CyberWidgets::SliderFloat("Humanization", &CS2::config.aim_humanization, 0.f, 100.f, "%.0f");
        CS2::config.aim_humanization = std::clamp(CS2::config.aim_humanization, 0.f, 100.f);
        CS2::config.aim_humanize = CS2::config.aim_humanization > 0.5f;


        CyberWidgets::ToggleSwitch("Verificacao de visibilidade", &CS2::config.aim_visibility_check);
        if (CS2::config.aim_visibility_check)
            CyberWidgets::TextLine("So puxa alvos visiveis (spotted). Desliga para puxar atras de paredes.",
                                   CyberWidgets::TextTone::Secondary);

        bool allowBots = !CS2::config.aim_ignore_bots;
        if (CyberWidgets::ToggleSwitch("Permitir bots como alvo", &allowBots))
            CS2::config.aim_ignore_bots = !allowBots;
        CyberWidgets::ToggleSwitch("Ignorar jogadores da equipa", &CS2::config.aim_ignore_team);


        CyberWidgets::ToggleSwitch("Dynamic FOV", &CS2::config.aim_dynamic_fov);
        if (CS2::config.aim_dynamic_fov) {
            DrawFovCurveEditor();
            for (int i = 0; i < 5; ++i) {
                char distanceLabel[32], fovLabel[32];
                std::snprintf(distanceLabel, sizeof(distanceLabel), "Distância P%d", i + 1);
                std::snprintf(fovLabel, sizeof(fovLabel), "FOV P%d", i + 1);
                if (i > 0)
                    CyberWidgets::SliderFloat(distanceLabel, &CS2::config.aim_fov_curve_distance[i],
                        CS2::config.aim_fov_curve_distance[i - 1] + .1f,
                        i < 4 ? CS2::config.aim_fov_curve_distance[i + 1] - .1f : 300.f, "%.1f m");
                CyberWidgets::SliderFloat(fovLabel, &CS2::config.aim_fov_curve_value[i], 1.f, 500.f, "%.0f px");
            }
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
        CyberWidgets::ToggleSwitch(Loc::Tr("vis.visible_check"), &CS2::config.trigger_visibility_check);
        float delay_ms = static_cast<float>(CS2::config.trigger_delay_ms);
        if (CyberWidgets::SliderFloat(Loc::Tr("aim.trigger_reaction"), &delay_ms, 0.f, 500.f, "%.0f ms"))
            CS2::config.trigger_delay_ms = static_cast<int>(delay_ms);
    }
    CyberWidgets::EndCard();
}
