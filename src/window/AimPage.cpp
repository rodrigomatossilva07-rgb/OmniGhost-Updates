#include "widgets.h"
#include "theme.h"
#include "localization.h"
#include "InputDevicesCard.h"
#include "aimbot/aim_type.h"
#ifdef UI_PREVIEW
#include "preview/preview_runtime.h"
#else
#include "aimbot/aimbot.h"
#endif
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
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
            *vk = 0;
            capturing = nullptr;
        }
    }
    ImGui::PopID();
    return false;
}

} // namespace

void DrawAim()
{
    CyberWidgets::SearchBar(Loc::Tr("aim.search"));
    CyberWidgets::CardGap(6.f);

    CyberWidgets::BeginCard(Loc::Tr("aim.general"));
    {
        const char* fov_styles[] = {
            Loc::Tr("aim.fov_circle"), Loc::Tr("aim.fov_square"), Loc::Tr("aim.fov_cross")
        };
        int fs = (int)aimbot::config.fov_style;
        if (CyberWidgets::Combo(Loc::TrID("aim.fov_style"), &fs, fov_styles, 3))
            aimbot::config.fov_style = (aimbot::FovStyle)fs;
        CyberWidgets::ToggleSwitch(Loc::TrID("aim.visible_check"), &aimbot::config.visible_check);
    }
    CyberWidgets::EndCard();

    CyberWidgets::BeginCard(Loc::Tr("aim.aimbot"));
    CyberWidgets::ToggleSwitch(Loc::TrID("aim.enable"), &aimbot::config.aimbot_enabled);
    CyberWidgets::ToggleSwitch(Loc::TrID("aim.show_fov"), &aimbot::config.show_fov);
    CyberWidgets::ToggleSwitch(Loc::TrID("aim.fov_rgb"), &aimbot::config.fov_rgb);
    CyberWidgets::ColorEditU32(Loc::TrID("aim.fov_color"), &aimbot::config.fov_color);

    ImGui::TextUnformatted(Loc::Tr("aim.bind"));
    ImGui::SameLine(160.f);
    HotkeyCaptureButton("aim1", &aimbot::config.aimbot_bind);
    // FiveM aiming is fire-independent: never allow LMB to become the aim bind.
    // Invalid/cleared primary binds fall back to RMB, matching the project default.
    if (aimbot::config.aimbot_bind <= 0 || aimbot::config.aimbot_bind == VK_LBUTTON)
        aimbot::config.aimbot_bind = VK_RBUTTON;

    ImGui::TextUnformatted(Loc::Tr("aim.bind2"));
    ImGui::SameLine(160.f);
    HotkeyCaptureButton("aim2", &aimbot::config.aimbot_bind2);
    // LMB must never acquire or reinforce aim. Also avoid duplicate binds.
    if (aimbot::config.aimbot_bind2 == VK_LBUTTON ||
        aimbot::config.aimbot_bind2 == aimbot::config.aimbot_bind)
        aimbot::config.aimbot_bind2 = 0;
    ImGui::TextDisabled("LMB/fire does not control FiveM aim; RMB is the default hold.");

    CyberWidgets::SliderFloat(Loc::TrID("aim.fov_size"), &aimbot::config.fov_size, 10.0f, 500.0f, "%.0f px");
    CyberWidgets::SliderFloat(Loc::TrID("aim.distance"), &aimbot::config.max_distance, 10.0f, 500.0f, "%.0f m");
    CyberWidgets::SliderFloat(Loc::TrID("aim.smooth_x"), &aimbot::config.smooth_x, 0.0f, 100.0f, "%.0f"); // 0=snap 100=none
    aimbot::config.smooth_y = aimbot::config.smooth_x;
    CyberWidgets::ToggleSwitch(Loc::TrID("aim.humanize"), &aimbot::config.humanize);
    CyberWidgets::EndCard();

    CyberWidgets::BeginCard(Loc::Tr("aim.trigger_title"));
    CyberWidgets::ToggleSwitch(Loc::TrID("aim.trigger"), &aimbot::config.trigger_enabled);
    CyberWidgets::ToggleSwitch(Loc::TrID("aim.trigger_head"), &aimbot::config.trigger_head_only);
    CyberWidgets::SliderFloat(Loc::TrID("aim.trigger_fov"), &aimbot::config.trigger_fov, 4.0f, 80.0f, "%.0f px");
    CyberWidgets::SliderFloat(Loc::TrID("aim.trigger_delay"), &aimbot::config.trigger_delay, 0.0f, 0.5f, "%.3f s");
    ImGui::TextDisabled("%s", Loc::Tr("aim.trigger_hint"));
    CyberWidgets::EndCard();

    // Hard-disable silent leftovers from old configs
    aimbot::config.silent_enabled = false;

    InputDevicesCard::Draw();
}
