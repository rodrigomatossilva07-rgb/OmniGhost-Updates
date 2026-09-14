#include "../../widgets.h"
#include "../../theme.h"
#include "../../localization.h"
#include "aimbot/aim_type.h"
#include "config/app_settings.h"
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

void DrawFovPreview(float previewWidth, float previewHeight) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 q(p.x + previewWidth, p.y + previewHeight);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, q, IM_COL32(5, 5, 5, 255), 8.f);
    dl->AddRect(p, q, CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.14f), 8.f);
    const ImVec2 c(p.x + previewWidth * .5f, p.y + previewHeight * .52f);
    const float radius = 18.f + (aimbot::config.fov_size / 500.f) * (std::min)(previewWidth, previewHeight) * .34f;
    const ImU32 color = aimbot::config.show_fov
        ? aimbot::config.fov_color
        : CyberTheme::WithAlpha(CyberTheme::Colors.TextDisabled, .25f);
    dl->AddLine(ImVec2(c.x - 8.f, c.y), ImVec2(c.x + 8.f, c.y),
                CyberTheme::WithAlpha(CyberTheme::Colors.Text, .55f), 1.f);
    dl->AddLine(ImVec2(c.x, c.y - 8.f), ImVec2(c.x, c.y + 8.f),
                CyberTheme::WithAlpha(CyberTheme::Colors.Text, .55f), 1.f);
    if (aimbot::config.fov_style == aimbot::FovStyle::Circle)
        dl->AddCircle(c, radius, color, 48, 1.4f);
    else if (aimbot::config.fov_style == aimbot::FovStyle::Square)
        dl->AddRect(ImVec2(c.x - radius, c.y - radius), ImVec2(c.x + radius, c.y + radius), color, 2.f, 0, 1.4f);
    else {
        dl->AddLine(ImVec2(c.x - radius, c.y), ImVec2(c.x - 12.f, c.y), color, 1.4f);
        dl->AddLine(ImVec2(c.x + 12.f, c.y), ImVec2(c.x + radius, c.y), color, 1.4f);
        dl->AddLine(ImVec2(c.x, c.y - radius), ImVec2(c.x, c.y - 12.f), color, 1.4f);
        dl->AddLine(ImVec2(c.x, c.y + 12.f), ImVec2(c.x, c.y + radius), color, 1.4f);
    }
    dl->AddText(ImVec2(p.x + 12.f, p.y + 10.f),
                CyberTheme::U32(CyberTheme::Colors.TextDisabled),
                aimbot::config.show_fov ? "FOV VISÍVEL" : "FOV OCULTO");
    ImGui::Dummy(ImVec2(previewWidth, previewHeight));
}

} // namespace

void DrawAim()
{
    const float full = CyberWidgets::CardContentWidth();
    const float gap = CyberTheme::Spacing::Sm;
    const float left = (full - gap) * .58f;
    const float right = full - left - gap;

    ImGui::BeginGroup();
    CyberWidgets::BeginCard(app_settings::T("ASSISTÊNCIA DE MIRA", "AIM ASSIST"), left);
    CyberWidgets::ToggleSwitch(app_settings::T("Ativar assistência", "Enable aim assist"), &aimbot::config.aimbot_enabled);
    if (aimbot::config.aimbot_enabled) {
        CyberWidgets::ToggleSwitch(Loc::Tr("aim.visible_check"), &aimbot::config.visible_check);
        CyberWidgets::Separator();
        CyberWidgets::SectionTitle(app_settings::T("ALVO", "TARGET"));
        const char* hitboxes[] = { Loc::Tr("aim.bone_head"), Loc::Tr("aim.bone_neck"), Loc::Tr("aim.bone_chest"), Loc::Tr("aim.bone_pelvis"), app_settings::T("Pernas", "Legs") };
        int hitbox = static_cast<int>(aimbot::config.hitbox);
        if (CyberWidgets::Combo(Loc::Tr("aim.hitbox"), &hitbox, hitboxes, 5))
            aimbot::config.hitbox = static_cast<aimbot::Hitbox>(hitbox);
        CyberWidgets::SliderFloat(Loc::Tr("vis.max_dist"), &aimbot::config.max_distance, 10.f, 500.f, "%.0f m");
        CyberWidgets::SliderFloat(Loc::Tr("aim.smooth"), &aimbot::config.smooth_x, 0.f, 100.f, "%.0f");
        aimbot::config.smooth_y = aimbot::config.smooth_x;
        CyberWidgets::ToggleSwitch(app_settings::T("Humanizar movimento", "Humanize movement"), &aimbot::config.humanize);
        CyberWidgets::Separator();
        CyberWidgets::SectionTitle(app_settings::T("ENTRADA", "INPUT"));
        ImGui::TextUnformatted(app_settings::T("Tecla principal", "Primary key"));
        ImGui::SameLine(160.f);
        HotkeyCaptureButton("aim1", &aimbot::config.aimbot_bind);
    }
    // FiveM aiming is fire-independent: never allow LMB to become the aim bind.
    // Invalid/cleared primary binds fall back to RMB, matching the project default.
    if (aimbot::config.aimbot_bind <= 0 || aimbot::config.aimbot_bind == VK_LBUTTON)
        aimbot::config.aimbot_bind = VK_RBUTTON;

    if (aimbot::config.aimbot_enabled) {
        ImGui::TextUnformatted(app_settings::T("Tecla secundária", "Secondary key"));
        ImGui::SameLine(160.f);
        HotkeyCaptureButton("aim2", &aimbot::config.aimbot_bind2);
    }
    // LMB must never acquire or reinforce aim. Also avoid duplicate binds.
    if (aimbot::config.aimbot_bind2 == VK_LBUTTON ||
        aimbot::config.aimbot_bind2 == aimbot::config.aimbot_bind)
        aimbot::config.aimbot_bind2 = 0;
    if (!aimbot::config.aimbot_enabled)
        CyberWidgets::TextLine(app_settings::T("Assistência desativada — ativa para configurar alvo e teclas.",
                                              "Aim assist disabled — enable it to configure targets and keys."), CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();

    CyberWidgets::CardGap(gap);
    CyberWidgets::BeginCard(Loc::Tr("aim.trigger_title"), left);
    CyberWidgets::ToggleSwitch(Loc::Tr("aim.trigger"), &aimbot::config.trigger_enabled);
    if (aimbot::config.trigger_enabled) {
        CyberWidgets::TextLine(Loc::Tr("aim.trigger_hint2"), CyberWidgets::TextTone::Secondary);
        CyberWidgets::ToggleSwitch(Loc::Tr("aim.trigger_always"), &aimbot::config.trigger_always_on);
        if (!aimbot::config.trigger_always_on) {
            ImGui::TextUnformatted(Loc::Tr("aim.trigger_key"));
            ImGui::SameLine(160.f);
            HotkeyCaptureButton("trigger", &aimbot::config.trigger_bind);
        }
        CyberWidgets::ToggleSwitch(Loc::Tr("aim.trigger_head"), &aimbot::config.trigger_head_only);
        CyberWidgets::SliderFloat(Loc::Tr("aim.trigger_fov"), &aimbot::config.trigger_fov, 4.f, 80.f, "%.0f px");
        CyberWidgets::SliderFloat(Loc::Tr("aim.trigger_reaction"), &aimbot::config.trigger_delay, 0.f, .5f, "%.3f s");
    }
    CyberWidgets::EndCard();
    ImGui::EndGroup();

    ImGui::SameLine(0.f, gap);
    ImGui::BeginGroup();
    CyberWidgets::BeginCard("FOV", right);
    CyberWidgets::ToggleSwitch(Loc::Tr("aim.show_fov"), &aimbot::config.show_fov);
    const char* fov_styles[] = { Loc::Tr("aim.fov_circle"), Loc::Tr("aim.fov_square"), app_settings::T("Dinâmico", "Dynamic") };
    int fs = static_cast<int>(aimbot::config.fov_style);
    if (CyberWidgets::Combo(app_settings::T("Forma", "Shape"), &fs, fov_styles, 3))
        aimbot::config.fov_style = static_cast<aimbot::FovStyle>(fs);
    CyberWidgets::SliderFloat(app_settings::T("Tamanho", "Size"), &aimbot::config.fov_size, 10.f, 500.f, "%.0f px");
    CyberWidgets::ColorEditU32(Loc::Tr("fr.color"), &aimbot::config.fov_color);
    DrawFovPreview((std::max)(140.f, right - 20.f), 160.f);
    CyberWidgets::EndCard();
    ImGui::EndGroup();

    // Hard-disable silent leftovers from old configs
    aimbot::config.silent_enabled = false;
    aimbot::config.fov_rgb = false;
}
