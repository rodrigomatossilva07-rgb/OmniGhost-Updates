#include "../../widgets.h"
#include "cs2_config.h"
#include "cs2_aim.h"
#include "aimbot/aim_type.h"
#include "../../localization.h"
#include "../../InputDevicesCard.h"
#include "imgui.h"
#include <Windows.h>
#include <cstdio>
#include <cstring>

namespace {

const char* VkName(int vk) {
    if (vk <= 0) return "—";
    switch (vk) {
    case 1:  return "Mouse Esquerdo";
    case 2:  return "Mouse Direito";
    case 4:  return "Mouse Meio";
    case 5:  return "Mouse 4";
    case 6:  return "Mouse 5";
    case 0x10: return "Shift";
    case 0x11: return "Ctrl";
    case 0x12: return "Alt";
    case 0x14: return "CapsLock";
    case 0x20: return "Space";
    case 0x09: return "Tab";
    default: break;
    }
    static char buf[32];
    if (vk >= 'A' && vk <= 'Z') { std::snprintf(buf, sizeof(buf), "%c", vk); return buf; }
    if (vk >= '0' && vk <= '9') { std::snprintf(buf, sizeof(buf), "%c", vk); return buf; }
    if (vk >= 0x70 && vk <= 0x7B) { std::snprintf(buf, sizeof(buf), "F%d", vk - 0x6F); return buf; }
    std::snprintf(buf, sizeof(buf), "VK 0x%02X", vk);
    return buf;
}

bool HotkeyCaptureButton(const char* id, int* vk) {
    static int* capturing = nullptr;
    static float blink = 0.f;
    static double ignore_until = 0.0;
    static uint8_t mask_at_start = 0;

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
        ImVec2(200.f, 34.f));

    if (clicked) {
        if (isCap) capturing = nullptr;
        else {
            capturing = vk;
            blink = 0.f;
            ignore_until = ImGui::GetTime() + 0.28;
            mask_at_start = aim_type::ButtonMask();
        }
    }
    if (isCap && ImGui::GetTime() >= ignore_until) {
        const uint8_t mask = aim_type::ButtonMask();
        const uint8_t rose = static_cast<uint8_t>(mask & ~mask_at_start);
        if (rose & 0x01) { *vk = 1; capturing = nullptr; }
        else if (rose & 0x02) { *vk = 2; capturing = nullptr; }
        else if (rose & 0x04) { *vk = 4; capturing = nullptr; }
        else if (rose & 0x08) { *vk = 5; capturing = nullptr; }
        else if (rose & 0x10) { *vk = 6; capturing = nullptr; }
        for (int vkScan = 1; vkScan < 256 && capturing; ++vkScan) {
            if (vkScan == 1 || vkScan == 2) continue;
            if (GetAsyncKeyState(vkScan) & 0x8000) { *vk = vkScan; capturing = nullptr; break; }
        }
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { *vk = 0; capturing = nullptr; }
    }
    ImGui::PopID();
    return false;
}

} // namespace

void DrawCs2Aim() {
    CyberWidgets::BeginCard(Loc::Tr("aim.general"));
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.team_check"), &CS2::config.team_check);
    CyberWidgets::EndCard();

    CyberWidgets::BeginCard(Loc::Tr("aim.aimbot"));
    CyberWidgets::ToggleSwitch(Loc::TrID("aim.enable"), &CS2::config.aim_enabled);
    CyberWidgets::ToggleSwitch(Loc::TrID("aim.show_fov"), &CS2::config.aim_draw_fov);
    CyberWidgets::ToggleSwitch(Loc::TrID("aim.fov_rgb"), &CS2::config.aim_fov_rgb);
    {
        const char* hitboxes[] = { "Cabeca", "Pescoco", "Torso", "Pelve", "Pernas" };
        CyberWidgets::Combo("Zona do alvo", &CS2::config.aim_bone, hitboxes, 5);
    }
    {
        const char* styles[] = { Loc::Tr("aim.fov_circle"), Loc::Tr("aim.fov_square"), Loc::Tr("aim.fov_cross") };
        CyberWidgets::Combo(Loc::TrID("aim.fov_style"), &CS2::config.aim_fov_style, styles, 3);
    }
    ImGui::ColorEdit4(Loc::Tr("aim.fov_color"), CS2::config.col_fov, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);

    ImGui::TextUnformatted(Loc::Tr("aim.bind"));
    ImGui::SameLine(160.f);
    HotkeyCaptureButton("cs2aim1", &CS2::config.aim_bind);
    if (CS2::config.aim_bind <= 0)
        CS2::config.aim_bind = 0x01;

    ImGui::TextUnformatted(Loc::Tr("aim.bind2"));
    ImGui::SameLine(160.f);
    HotkeyCaptureButton("cs2aim2", &CS2::config.aim_bind2);

    CyberWidgets::SliderFloat(Loc::TrID("aim.fov_size"), &CS2::config.aim_fov, 10.f, 400.f, "%.0f px");
    CyberWidgets::SliderFloat(Loc::TrID("aim.distance"), &CS2::config.aim_max_dist, 10.f, 500.f, "%.0f m");
    CyberWidgets::SliderFloat(Loc::TrID("aim.smooth_x"), &CS2::config.aim_smooth, 0.f, 100.f, "%.0f");
    ImGui::TextDisabled("0 = snap | 100 = no pull");
    CyberWidgets::ToggleSwitch(Loc::TrID("aim.humanize"), &CS2::config.aim_humanize);
    CyberWidgets::EndCard();

    CyberWidgets::BeginCard(Loc::Tr("aim.trigger_title"));
    CyberWidgets::ToggleSwitch(Loc::TrID("aim.trigger"), &CS2::config.trigger_enabled);
    CyberWidgets::ToggleSwitch(Loc::TrID("aim.trigger_head"), &CS2::config.trigger_head_only);
    ImGui::TextUnformatted(Loc::Tr("aim.bind"));
    ImGui::SameLine(160.f);
    HotkeyCaptureButton("cs2trig", &CS2::config.trigger_bind);
    CyberWidgets::InputInt(Loc::TrID("aim.trigger_delay"), &CS2::config.trigger_delay_ms);
    CyberWidgets::EndCard();

    InputDevicesCard::Draw();

}
