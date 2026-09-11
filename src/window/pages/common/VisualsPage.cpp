#include "../../window.hpp"
#include "../../widgets.h"
#include "../../theme.h"
#include "../../fonts.h"
#include "../../localization.h"
#include "../../../launcher/launcher_assets.h"
#include "config/app_settings.h"
#ifdef UI_PREVIEW
#include "preview/preview_runtime.h"
#else
#include "esp/esp.h"
#endif
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <algorithm>

namespace {

ImU32 PreviewColor(ImU32 configured, bool visible)
{
    if (esp::config.rgb_mode) {
        const float t = static_cast<float>(ImGui::GetTime());
        return IM_COL32(
            static_cast<int>(std::sin(t * 2.0f) * 127.f + 128.f),
            static_cast<int>(std::sin(t * 2.0f + 2.094f) * 127.f + 128.f),
            static_cast<int>(std::sin(t * 2.0f + 4.188f) * 127.f + 128.f), 255);
    }
    if (!esp::config.visibility_colors)
        return configured;
    return visible ? esp::config.color_visible : esp::config.color_invisible;
}

ImU32 PreviewDarker(ImU32 color)
{
    ImVec4 value = ImGui::ColorConvertU32ToFloat4(color);
    value.x *= 0.48f;
    value.y *= 0.48f;
    value.z *= 0.48f;
    return ImGui::ColorConvertFloat4ToU32(value);
}

void DrawEspPreviewPanel(float panelWidth, float panelHeight, bool previewVisible)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 end(origin.x + panelWidth, origin.y + panelHeight);
    constexpr ImU32 kBlack = IM_COL32(5, 5, 5, 255);
    constexpr ImU32 kElevated = IM_COL32(13, 14, 14, 255);
    constexpr ImU32 kGunmetal = IM_COL32(27, 29, 29, 255);
    constexpr ImU32 kMuted = IM_COL32(116, 118, 116, 255);
    constexpr ImU32 kText = IM_COL32(216, 216, 210, 255);
    constexpr ImU32 kGold = IM_COL32(199, 165, 43, 255);
    constexpr ImU32 kChampagne = IM_COL32(227, 198, 90, 255);
    constexpr ImU32 kActive = IM_COL32(40, 244, 91, 255);

    // Layered, clipped HUD surface. All decoration is intentionally subtle so
    // the configured ESP colours remain the main visual information.
    dl->AddRectFilled(ImVec2(origin.x + 3.f, origin.y + 6.f),
        ImVec2(end.x + 3.f, end.y + 6.f), IM_COL32(0, 0, 0, 125), 10.f);
    dl->AddRectFilled(origin, end, kBlack, 10.f);
    dl->PushClipRect(ImVec2(origin.x + 1.f, origin.y + 1.f),
        ImVec2(end.x - 1.f, end.y - 1.f), true);
    dl->AddRectFilledMultiColor(ImVec2(origin.x + 1.f, origin.y + 1.f),
        ImVec2(end.x - 1.f, end.y - 1.f), IM_COL32(18, 16, 10, 125),
        kBlack, kBlack, IM_COL32(12, 12, 10, 220));

    // The preview deliberately contains only the target and the enabled ESP
    // layers. Decorative telemetry and calibration copy made this panel harder
    // to read than the real in-game result.
    const float sc = (std::min)(panelWidth / 275.f, (panelHeight - 30.f) / 360.f);
    const float cx = origin.x + panelWidth * 0.50f;
    const float top = origin.y + 16.f;
    const ImVec2 head(cx, top + 24.f * sc);
    const ImVec2 neck(cx, top + 48.f * sc);
    const ImVec2 chest(cx, top + 84.f * sc);
    const ImVec2 spine_mid(cx, top + 116.f * sc);
    const ImVec2 spine_low(cx, top + 142.f * sc);
    const ImVec2 pelvis(cx, top + 164.f * sc);
    const ImVec2 l_cl(cx - 18.f * sc, top + 58.f * sc);
    const ImVec2 r_cl(cx + 18.f * sc, top + 58.f * sc);
    const ImVec2 l_sh(cx - 46.f * sc, top + 70.f * sc);
    const ImVec2 r_sh(cx + 46.f * sc, top + 70.f * sc);
    const ImVec2 l_up(cx - 61.f * sc, top + 105.f * sc);
    const ImVec2 r_up(cx + 61.f * sc, top + 105.f * sc);
    const ImVec2 l_el(cx - 72.f * sc, top + 139.f * sc);
    const ImVec2 r_el(cx + 72.f * sc, top + 139.f * sc);
    const ImVec2 l_fore(cx - 79.f * sc, top + 166.f * sc);
    const ImVec2 r_fore(cx + 79.f * sc, top + 166.f * sc);
    const ImVec2 l_wr(cx - 82.f * sc, top + 184.f * sc);
    const ImVec2 r_wr(cx + 82.f * sc, top + 184.f * sc);
    const ImVec2 l_ha(cx - 83.f * sc, top + 191.f * sc);
    const ImVec2 r_ha(cx + 83.f * sc, top + 191.f * sc);
    const ImVec2 l_hi(cx - 18.f * sc, top + 164.f * sc);
    const ImVec2 r_hi(cx + 18.f * sc, top + 164.f * sc);
    const ImVec2 l_th(cx - 22.f * sc, top + 207.f * sc);
    const ImVec2 r_th(cx + 22.f * sc, top + 207.f * sc);
    const ImVec2 l_kn(cx - 25.f * sc, top + 246.f * sc);
    const ImVec2 r_kn(cx + 25.f * sc, top + 246.f * sc);
    const ImVec2 l_shin(cx - 26.f * sc, top + 278.f * sc);
    const ImVec2 r_shin(cx + 26.f * sc, top + 278.f * sc);
    const ImVec2 l_an(cx - 27.f * sc, top + 310.f * sc);
    const ImVec2 r_an(cx + 27.f * sc, top + 310.f * sc);
    const ImVec2 l_ft(cx - 35.f * sc, top + 319.f * sc);
    const ImVec2 r_ft(cx + 35.f * sc, top + 319.f * sc);

    const ImU32 sk = previewVisible ? kActive : PreviewColor(esp::config.color_skeleton, false);
    const ImU32 jt = previewVisible ? kActive : PreviewColor(esp::config.color_skeleton_points, false);


    // The provided operator image remains beneath the overlay. The preview is
    // still dynamic: every ESP element below observes its corresponding toggle.
    const LauncherAssets::Texture operatorImage = LauncherAssets::FiveMEspPreview();
    const float portraitTop = top - 12.f * sc;
    const float portraitBottom = end.y - 16.f;
    const float portraitHeight = portraitBottom - portraitTop;
    const float sourceAspect = operatorImage.width > 0 && operatorImage.height > 0
        ? static_cast<float>(operatorImage.width) / static_cast<float>(operatorImage.height)
        : 0.6667f;
    const float portraitWidth = (std::min)(portraitHeight * sourceAspect, panelWidth - 18.f);
    if (operatorImage.id && operatorImage.width > 0 && operatorImage.height > 0) {
        dl->AddImage(operatorImage.id,
            ImVec2(cx - portraitWidth * 0.5f, portraitTop),
            ImVec2(cx + portraitWidth * 0.5f, portraitBottom),
            ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), IM_COL32(255, 255, 255, 232));
    }

    // Fallback mannequin keeps the preview useful if an embedded asset fails
    // integrity validation; it is intentionally subdued behind the real image.
    const ImU32 limbShadow = IM_COL32(0, 0, 0, 210);
    const ImU32 limbFill = kGunmetal;
    if (!operatorImage.id) {
        dl->AddCircleFilled(ImVec2(head.x + 2.f, head.y + 3.f), 16.f * sc,
            IM_COL32(0, 0, 0, 180), 28);
        dl->AddCircleFilled(head, 15.f * sc, IM_COL32(30, 32, 32, 255), 28);
        dl->AddCircle(head, 15.f * sc, IM_COL32(199, 165, 43, 70), 28, 1.f);
    }
    const ImVec2 torsoShadow[] = {
        ImVec2(l_sh.x - 3.f, l_sh.y + 3.f), ImVec2(r_sh.x + 3.f, r_sh.y + 3.f),
        ImVec2(r_hi.x + 4.f, r_hi.y + 4.f), ImVec2(l_hi.x - 4.f, l_hi.y + 4.f)
    };
    if (!operatorImage.id)
        dl->AddConvexPolyFilled(torsoShadow, 4, IM_COL32(0, 0, 0, 190));
    const ImVec2 torso[] = { l_sh, r_sh, r_hi, l_hi };
    if (!operatorImage.id) {
        dl->AddConvexPolyFilled(torso, 4, IM_COL32(22, 24, 24, 255));
        dl->AddPolyline(torso, 4, IM_COL32(199, 165, 43, 72), true, 1.f);
    }
    const ImVec2 waist[] = {
        ImVec2(l_hi.x, l_hi.y - 2.f), ImVec2(r_hi.x, r_hi.y - 2.f),
        ImVec2(r_th.x + 2.f, r_th.y), ImVec2(l_th.x - 2.f, l_th.y)
    };
    if (!operatorImage.id)
        dl->AddConvexPolyFilled(waist, 4, IM_COL32(19, 21, 21, 255));
    for (const auto& segment : {
        std::pair<ImVec2, ImVec2>{l_sh, l_el}, {l_el, l_ha},
        {r_sh, r_el}, {r_el, r_ha}, {l_hi, l_kn}, {l_kn, l_an},
        {r_hi, r_kn}, {r_kn, r_an} }) {
        if (!operatorImage.id) {
            dl->AddLine(ImVec2(segment.first.x + 2.f, segment.first.y + 3.f),
                ImVec2(segment.second.x + 2.f, segment.second.y + 3.f), limbShadow, 15.f * sc);
            dl->AddLine(segment.first, segment.second, limbFill, 12.f * sc);
        }
    }
    if (!operatorImage.id) {
        dl->AddLine(l_an, l_ft, limbFill, 10.f * sc);
        dl->AddLine(r_an, r_ft, limbFill, 10.f * sc);
    }

    auto bone = [&](ImVec2 a, ImVec2 b) {
        if (esp::config.skeleton) {
            dl->AddLine(ImVec2(a.x + 1.f, a.y + 1.f), ImVec2(b.x + 1.f, b.y + 1.f),
                IM_COL32(0, 0, 0, 220), 3.5f);
            dl->AddLine(a, b, sk, 1.55f);
        }
    };
    auto joint = [&](ImVec2 p) {
        if (esp::config.joints) {
            dl->AddCircleFilled(p, 2.35f, jt, 10);
            dl->AddCircle(p, 2.35f, CyberTheme::SafeShadowU32(180), 10, 1.0f);
        }
    };

    bone(head, neck); bone(neck, chest); bone(chest, spine_mid);
    bone(spine_mid, spine_low); bone(spine_low, pelvis);
    bone(neck, l_cl); bone(l_cl, l_sh); bone(neck, r_cl); bone(r_cl, r_sh);
    bone(l_sh, l_up); bone(l_up, l_el); bone(l_el, l_fore); bone(l_fore, l_wr); bone(l_wr, l_ha);
    bone(r_sh, r_up); bone(r_up, r_el); bone(r_el, r_fore); bone(r_fore, r_wr); bone(r_wr, r_ha);
    bone(pelvis, l_hi); bone(pelvis, r_hi);
    bone(l_hi, l_th); bone(l_th, l_kn); bone(l_kn, l_shin); bone(l_shin, l_an); bone(l_an, l_ft);
    bone(r_hi, r_th); bone(r_th, r_kn); bone(r_kn, r_shin); bone(r_shin, r_an); bone(r_an, r_ft);

    for (ImVec2 p : { head, neck, chest, spine_mid, spine_low, pelvis,
        l_cl, l_sh, l_up, l_el, l_fore, l_wr, l_ha,
        r_cl, r_sh, r_up, r_el, r_fore, r_wr, r_ha,
        l_hi, l_th, l_kn, l_shin, l_an, l_ft,
        r_hi, r_th, r_kn, r_shin, r_an, r_ft })
        joint(p);

    const ImVec2 boxMin(cx - 72.f * sc, head.y - 22.f * sc);
    const ImVec2 boxMax(cx + 72.f * sc, l_ft.y + 12.f * sc);
    const ImU32 boxColor = PreviewColor(esp::config.color_box_2d, previewVisible);
    if (esp::config.box_2d) {
        dl->AddRect(ImVec2(boxMin.x - 1.f, boxMin.y - 1.f),
            ImVec2(boxMax.x + 1.f, boxMax.y + 1.f), CyberTheme::SafeShadowU32(210), 1.f, 0, 3.f);
        dl->AddRect(boxMin, boxMax, boxColor, 1.f, 0, 1.8f);
    }
    if (esp::config.corner_box) {
        const float part = 18.f * sc;
        const ImU32 color = PreviewColor(esp::config.color_corner_box, previewVisible);
        dl->AddLine(boxMin, ImVec2(boxMin.x + part, boxMin.y), color, 2.f);
        dl->AddLine(boxMin, ImVec2(boxMin.x, boxMin.y + part), color, 2.f);
        dl->AddLine(ImVec2(boxMax.x, boxMin.y), ImVec2(boxMax.x - part, boxMin.y), color, 2.f);
        dl->AddLine(ImVec2(boxMax.x, boxMin.y), ImVec2(boxMax.x, boxMin.y + part), color, 2.f);
        dl->AddLine(ImVec2(boxMin.x, boxMax.y), ImVec2(boxMin.x + part, boxMax.y), color, 2.f);
        dl->AddLine(ImVec2(boxMin.x, boxMax.y), ImVec2(boxMin.x, boxMax.y - part), color, 2.f);
        dl->AddLine(boxMax, ImVec2(boxMax.x - part, boxMax.y), color, 2.f);
        dl->AddLine(boxMax, ImVec2(boxMax.x, boxMax.y - part), color, 2.f);
    }
    if (esp::config.snaplines) {
        ImVec2 lineStart(cx, end.y - 10.f);
        if (esp::config.snapline_pos == 0) lineStart.y = origin.y + 44.f;
        else if (esp::config.snapline_pos == 1) lineStart.y = origin.y + panelHeight * 0.5f;
        dl->AddLine(lineStart, ImVec2(cx, boxMax.y),
            PreviewColor(esp::config.color_snaplines, previewVisible), 1.5f);
    }

    if (esp::config.head_circle) {
        const ImU32 headColor = PreviewColor(esp::config.color_head_circle, previewVisible);
        if (esp::config.circle_type == 1) {
            dl->AddCircleFilled(head, 11.f, headColor, 24);
        } else if (esp::config.circle_type == 2) {
            dl->AddLine(ImVec2(head.x - 12.f, head.y), ImVec2(head.x + 12.f, head.y), headColor, 2.f);
            dl->AddLine(ImVec2(head.x, head.y - 12.f), ImVec2(head.x, head.y + 12.f), headColor, 2.f);
        } else {
            dl->AddCircle(head, 11.f, headColor, 24, 2.0f);
        }
    }
    if (esp::config.head_halo) {
        ImVec2 halo[25]{};
        for (int i = 0; i <= 24; ++i) {
            const float angle = i * 6.28318530718f / 24.f;
            halo[i] = ImVec2(head.x + std::cos(angle) * 16.f * sc,
                             head.y - 15.f * sc + std::sin(angle) * 5.f * sc);
        }
        dl->AddPolyline(halo, 25, PreviewColor(esp::config.color_halo, previewVisible), false, 1.7f);
    }
    if (esp::config.look_direction) {
        const ImVec2 lookEnd(head.x + 48.f * sc, head.y - 3.f * sc);
        const ImU32 look = PreviewColor(esp::config.color_look_direction, previewVisible);
        dl->AddLine(head, lookEnd, look, 1.7f);
        dl->AddCircleFilled(lookEnd, 2.2f, look, 8);
    }

    auto meter = [&](float x, float topY, float bottomY, float pct, ImU32 color) {
        const float meterH = bottomY - topY;
        dl->AddRectFilled(ImVec2(x, topY), ImVec2(x + 4.f, bottomY),
            IM_COL32(38, 39, 38, 230), 2.f);
        dl->AddRectFilled(ImVec2(x, bottomY - meterH * pct), ImVec2(x + 4.f, bottomY),
            color, 2.f);
    };
    if (esp::config.health_bar)
        meter(boxMin.x - 15.f, head.y - 4.f, l_ft.y, 0.72f, kActive);
    if (esp::config.armor_bar)
        meter(boxMax.x + 11.f, head.y - 4.f, r_ft.y, 0.55f, IM_COL32(67, 171, 243, 255));

    if (esp::config.player_id) {
        const char* id = "ID: 42";
        const ImVec2 idSize = ImGui::CalcTextSize(id);
        dl->AddText(ImVec2(cx - idSize.x * 0.5f, boxMin.y - 22.f), kText, id);
    }
    if (esp::config.player_name) {
        const char* name = app_settings::T("Jogador", "Player");
        const ImVec2 nameSize = ImGui::CalcTextSize(name);
        dl->AddText(ImVec2(cx - nameSize.x * 0.5f,
                boxMin.y - (esp::config.player_id ? 37.f : 22.f)), kMuted, name);
    }
    if (esp::config.weapon_name || esp::config.distance) {
        const char* w = "Pistol";
        ImVec2 ws = ImGui::CalcTextSize(w);
        const float footerWidth = (esp::config.weapon_name ? ws.x + 28.f : 0.f) +
            (esp::config.distance ? 37.f : 0.f);
        const ImVec2 footerMin(cx - footerWidth * .5f - 12.f, boxMax.y + 7.f);
        const ImVec2 footerMax(cx + footerWidth * .5f + 12.f, boxMax.y + 31.f);
        dl->AddRectFilled(footerMin, footerMax, kElevated, 3.f);
        dl->AddRect(footerMin, footerMax, IM_COL32(199, 165, 43, 80), 3.f, 0, 1.f);
        float textX = footerMin.x + 10.f;
        if (esp::config.weapon_name) {
            dl->AddLine(ImVec2(textX, footerMin.y + 12.f), ImVec2(textX + 9.f, footerMin.y + 12.f), kGold, 1.3f);
            dl->AddLine(ImVec2(textX + 4.f, footerMin.y + 8.f), ImVec2(textX + 9.f, footerMin.y + 12.f), kGold, 1.3f);
            textX += 14.f;
            dl->AddText(ImVec2(textX, boxMax.y + 11.f), kChampagne, w);
            textX += ws.x + 6.f;
        }
        if (esp::config.distance)
            dl->AddText(ImVec2(textX, boxMax.y + 11.f), IM_COL32(226, 88, 183, 255),
                esp::config.weapon_name ? "| 85m" : "85m");
    }

    dl->PopClipRect();
    dl->AddRect(origin, end, IM_COL32(199, 165, 43, 68), 10.f, 0, 1.f);

    ImGui::Dummy(ImVec2(panelWidth, panelHeight));
}

} // namespace

void DrawVisualsLegacy()
{
    // Legacy effects remain compatible with stored profiles but stay out of the
    // primary workflow.
    esp::config.trails = false;
    esp::config.look_direction = false;
    const float full = CyberWidgets::CardContentWidth();
    const float gap = CyberTheme::Spacing::Sm;
    const float previewWidth = std::clamp(full * 0.38f, 280.f, 360.f);
    const float controlsWidth = full - previewWidth - gap;

    ImGui::BeginGroup();
    CyberWidgets::BeginCard("ESP DE JOGADORES", controlsWidth);
    CyberWidgets::ToggleSwitch("Ativar ESP", &esp::config.enabled);
    if (esp::config.enabled) {
        CyberWidgets::SectionTitle("ELEMENTOS");
        CyberWidgets::ToggleSwitch("Caixa", &esp::config.box_2d);
        CyberWidgets::ToggleSwitch("Esqueleto", &esp::config.skeleton);
        if (esp::config.skeleton)
            CyberWidgets::ToggleSwitch("Pontos das articulações", &esp::config.joints);
        CyberWidgets::ToggleSwitch("Ponto na cabeça", &esp::config.head_circle);
        if (esp::config.head_circle) {
            const char* circleTypes[] = { "Contorno", "Preenchido", "Cruz" };
            CyberWidgets::Combo("Estilo do ponto", &esp::config.circle_type, circleTypes, 3);
        }
        CyberWidgets::ToggleSwitch("Vida", &esp::config.health_bar);
        CyberWidgets::ToggleSwitch("Armadura", &esp::config.armor_bar);
        CyberWidgets::ToggleSwitch("Nome", &esp::config.player_name);
        CyberWidgets::ToggleSwitch("ID", &esp::config.player_id);
        CyberWidgets::ToggleSwitch("Distância", &esp::config.distance);
        CyberWidgets::ToggleSwitch("Arma", &esp::config.weapon_name);
        CyberWidgets::SliderFloat("Distância máxima", &esp::config.max_esp_distance,
                                  20.f, 500.f, "%.0f m");

        CyberWidgets::Separator();
        if (ImGui::CollapsingHeader("APARÊNCIA", ImGuiTreeNodeFlags_None)) {
            CyberWidgets::ToggleSwitch("Cores por visibilidade", &esp::config.visibility_colors);
            if (CyberWidgets::Button("PERSONALIZAR CORES", CyberWidgets::ButtonStyle::Secondary,
                                     ImVec2(190.f, 34.f)))
                CyberWidgets::OpenModal("##esp_colors");
            ImGui::SameLine();
            if (CyberWidgets::Button("REPOR", CyberWidgets::ButtonStyle::Ghost, ImVec2(86.f, 34.f))) {
                const esp::Config defaults{};
                esp::config.color_visible = defaults.color_visible;
                esp::config.color_invisible = defaults.color_invisible;
                esp::config.color_dead = defaults.color_dead;
                esp::config.color_knocked = defaults.color_knocked;
                esp::config.color_team = defaults.color_team;
                esp::config.color_skeleton = defaults.color_skeleton;
                esp::config.color_skeleton_points = defaults.color_skeleton_points;
                esp::config.color_head_circle = defaults.color_head_circle;
                esp::config.color_health = defaults.color_health;
                esp::config.color_armor = defaults.color_armor;
                esp::config.color_weapon = defaults.color_weapon;
                esp::config.color_box_2d = defaults.color_box_2d;
                esp::config.color_corner_box = defaults.color_corner_box;
                esp::config.color_snaplines = defaults.color_snaplines;
                esp::config.color_name = defaults.color_name;
                esp::config.color_id = defaults.color_id;
                esp::config.color_distance = defaults.color_distance;
                esp::config.color_npc = defaults.color_npc;
            }
        }

        if (ImGui::CollapsingHeader("AVANÇADO", ImGuiTreeNodeFlags_None)) {
            CyberWidgets::ToggleSwitch("Mostrar o jogador local", &esp::config.self_esp);
            CyberWidgets::ToggleSwitch("Apenas alvos visíveis", &esp::config.visible_check);
            CyberWidgets::ToggleSwitch("Ocultar membros da equipa", &esp::config.team_check);
            CyberWidgets::ToggleSwitch("Caixa de cantos", &esp::config.corner_box);
            CyberWidgets::ToggleSwitch("Linhas guia", &esp::config.snaplines);
            if (esp::config.snaplines) {
                const char* snapPositions[] = { "Topo", "Centro", "Fundo" };
                CyberWidgets::Combo("Origem das linhas", &esp::config.snapline_pos, snapPositions, 3);
            }
            CyberWidgets::ToggleSwitch("Mostrar abatidos", &esp::config.show_knocked);
            CyberWidgets::ToggleSwitch("Mostrar mortos", &esp::config.show_dead);
            CyberWidgets::ToggleSwitch("ESP de NPCs", &esp::config.npc_esp);
            CyberWidgets::ToggleSwitch("Auréola na cabeça", &esp::config.head_halo);
            CyberWidgets::Badge("ESP DE OBJETOS · EXPERIMENTAL", CyberWidgets::TextTone::Secondary);
        }
    } else {
        CyberWidgets::TextLine("ESP desativado — ativa para configurar elementos.", CyberWidgets::TextTone::Secondary);
    }
    CyberWidgets::EndCard();
    ImGui::EndGroup();

    ImGui::SameLine(0.f, gap);
    ImGui::BeginGroup();
    CyberWidgets::BeginCard("PRÉ-VISUALIZAÇÃO", previewWidth);
    DrawEspPreviewPanel((std::max)(160.f, previewWidth - 20.f), 374.f, true);
    CyberWidgets::EndCard();
    ImGui::EndGroup();

    if (CyberWidgets::BeginModal("##esp_colors", "PERSONALIZAR CORES", 500.f)) {
        CyberWidgets::SectionTitle("JOGADOR");
        CyberWidgets::ColorEditU32("Visível", &esp::config.color_visible);
        CyberWidgets::ColorEditU32("Oculto", &esp::config.color_invisible);
        CyberWidgets::ColorEditU32("Abatido", &esp::config.color_knocked);
        CyberWidgets::ColorEditU32("Morto", &esp::config.color_dead);
        CyberWidgets::ColorEditU32("Equipa", &esp::config.color_team);
        CyberWidgets::Separator();
        CyberWidgets::SectionTitle("ESP");
        CyberWidgets::ColorEditU32("Caixa", &esp::config.color_box_2d);
        CyberWidgets::ColorEditU32("Caixa de cantos", &esp::config.color_corner_box);
        CyberWidgets::ColorEditU32("Esqueleto", &esp::config.color_skeleton);
        CyberWidgets::ColorEditU32("Articulações", &esp::config.color_skeleton_points);
        CyberWidgets::ColorEditU32("Cabeça", &esp::config.color_head_circle);
        CyberWidgets::ColorEditU32("Linhas guia", &esp::config.color_snaplines);
        CyberWidgets::Separator();
        CyberWidgets::SectionTitle("INFORMAÇÃO");
        CyberWidgets::ColorEditU32("Vida", &esp::config.color_health);
        CyberWidgets::ColorEditU32("Armadura", &esp::config.color_armor);
        CyberWidgets::ColorEditU32("Nome", &esp::config.color_name);
        CyberWidgets::ColorEditU32("ID", &esp::config.color_id);
        CyberWidgets::ColorEditU32("Distância", &esp::config.color_distance);
        CyberWidgets::ColorEditU32("Arma", &esp::config.color_weapon);
        CyberWidgets::EndModal();
    }

}

void DrawVisuals()
{
    // Three independent columns mirror the reference layout: feature switches
    // and colours can scroll without ever moving the live preview out of view.
    esp::config.trails = false;
    esp::config.look_direction = false;

    const float full = ImGui::GetContentRegionAvail().x;
    const float columnHeight = (std::max)(CyberTheme::Px(410.0f), ImGui::GetContentRegionAvail().y);
    const float gap = CyberTheme::Metrics::GridGap;
    const float previewWidth = std::clamp(full * 0.31f, CyberTheme::Px(250.0f), CyberTheme::Px(310.0f));
    const float settingsWidth = (std::max)(CyberTheme::Px(220.0f), full - previewWidth - gap * 2.0f);
    const float featuresWidth = settingsWidth * 0.50f;
    const float colorsWidth = settingsWidth - featuresWidth;

    ImGui::BeginChild("##esp_features_scroll", ImVec2(featuresWidth, columnHeight), false,
        ImGuiWindowFlags_AlwaysVerticalScrollbar);
    CyberWidgets::BeginCard(Loc::Tr("vis.features"));
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.enable"), &esp::config.enabled);
    CyberWidgets::Separator();
    ImGui::BeginDisabled(!esp::config.enabled);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.self_esp"), &esp::config.self_esp);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.npc"), &esp::config.npc_esp);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.show_dead"), &esp::config.show_dead);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.show_knocked"), &esp::config.show_knocked);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.visible_check"), &esp::config.visible_check);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.hide_team"), &esp::config.team_check);
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle(Loc::Tr("vis.elements"));
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.skeleton"), &esp::config.skeleton);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.joints"), &esp::config.joints);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.head_circle"), &esp::config.head_circle);
    if (esp::config.head_circle) {
        const char* circleTypes[] = { Loc::Tr("vis.circle_normal"), Loc::Tr("vis.circle_filled"), Loc::Tr("vis.circle_cross") };
        CyberWidgets::Combo(Loc::Tr("vis.circle_type"), &esp::config.circle_type, circleTypes, 3);
    }
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.health_bar"), &esp::config.health_bar);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.armor_bar"), &esp::config.armor_bar);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.player_name"), &esp::config.player_name);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.player_id"), &esp::config.player_id);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.weapon_name"), &esp::config.weapon_name);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.distance"), &esp::config.distance);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.box_2d"), &esp::config.box_2d);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.corner_box"), &esp::config.corner_box);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.snaplines"), &esp::config.snaplines);
    if (esp::config.snaplines) {
        const char* snapPositions[] = { Loc::Tr("common.top"), Loc::Tr("common.center"), Loc::Tr("common.bottom") };
        CyberWidgets::Combo(Loc::Tr("vis.snap_pos"), &esp::config.snapline_pos, snapPositions, 3);
    }
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.head_halo"), &esp::config.head_halo);
    CyberWidgets::SliderFloat(Loc::Tr("vis.max_dist"), &esp::config.max_esp_distance,
                              20.f, 500.f, "%.0f m");
    ImGui::EndDisabled();
    CyberWidgets::EndCard();

    ImGui::EndChild();

    ImGui::SameLine(0.0f, gap);
    ImGui::BeginChild("##esp_colors_scroll", ImVec2(colorsWidth, columnHeight), false,
        ImGuiWindowFlags_AlwaysVerticalScrollbar);
    CyberWidgets::BeginCard(Loc::Tr("vis.customize"));
    const auto colorWhenEnabled = [](const char* label, ImU32* color, bool enabled) {
        ImGui::BeginDisabled(!enabled);
        CyberWidgets::ColorEditU32(label, color);
        ImGui::EndDisabled();
    };
    const bool espEnabled = esp::config.enabled;
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.visibility_colors"), &esp::config.visibility_colors);
    CyberWidgets::SectionTitle(Loc::Tr("fr.players"));
    const bool visibilityColorsEnabled = espEnabled && esp::config.visibility_colors && esp::config.visible_check;
    colorWhenEnabled(Loc::Tr("vis.visible"), &esp::config.color_visible, visibilityColorsEnabled);
    colorWhenEnabled(Loc::Tr("vis.hidden"), &esp::config.color_invisible, visibilityColorsEnabled);
    colorWhenEnabled(Loc::Tr("vis.knocked"), &esp::config.color_knocked, espEnabled && esp::config.show_knocked);
    colorWhenEnabled(Loc::Tr("vis.dead"), &esp::config.color_dead, espEnabled && esp::config.show_dead);
    colorWhenEnabled(Loc::Tr("vis.team"), &esp::config.color_team, espEnabled && !esp::config.team_check);
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle(Loc::Tr("vis.drawing"));
    colorWhenEnabled(Loc::Tr("vis.skeleton"), &esp::config.color_skeleton, espEnabled && esp::config.skeleton);
    colorWhenEnabled(Loc::Tr("vis.color_points"), &esp::config.color_skeleton_points,
                     espEnabled && esp::config.skeleton && esp::config.joints);
    colorWhenEnabled(Loc::Tr("vis.color_head"), &esp::config.color_head_circle,
                     espEnabled && esp::config.head_circle);
    colorWhenEnabled(Loc::Tr("vis.box_2d"), &esp::config.color_box_2d, espEnabled && esp::config.box_2d);
    colorWhenEnabled(Loc::Tr("vis.corner_box"), &esp::config.color_corner_box,
                     espEnabled && esp::config.corner_box);
    colorWhenEnabled(Loc::Tr("vis.snaplines"), &esp::config.color_snaplines,
                     espEnabled && esp::config.snaplines);
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle(Loc::Tr("vis.information"));
    colorWhenEnabled(Loc::Tr("vis.health"), &esp::config.color_health, espEnabled && esp::config.health_bar);
    colorWhenEnabled(Loc::Tr("vis.armor"), &esp::config.color_armor, espEnabled && esp::config.armor_bar);
    colorWhenEnabled(Loc::Tr("common.name"), &esp::config.color_name, espEnabled && esp::config.player_name);
    colorWhenEnabled(Loc::Tr("common.id"), &esp::config.color_id, espEnabled && esp::config.player_id);
    colorWhenEnabled(Loc::Tr("common.distance"), &esp::config.color_distance, espEnabled && esp::config.distance);
    colorWhenEnabled(Loc::Tr("vis.weapon_short"), &esp::config.color_weapon, espEnabled && esp::config.weapon_name);
    colorWhenEnabled("NPC", &esp::config.color_npc, espEnabled && esp::config.npc_esp);
    CyberWidgets::Separator();
    if (CyberWidgets::Button(Loc::Tr("vis.reset_colors"), CyberWidgets::ButtonStyle::Ghost,
                             ImVec2(CyberWidgets::CardContentWidth(), CyberTheme::Metrics::ControlHeight))) {
        const esp::Config defaults{};
        esp::config.color_visible = defaults.color_visible;
        esp::config.color_invisible = defaults.color_invisible;
        esp::config.color_dead = defaults.color_dead;
        esp::config.color_knocked = defaults.color_knocked;
        esp::config.color_team = defaults.color_team;
        esp::config.color_skeleton = defaults.color_skeleton;
        esp::config.color_skeleton_points = defaults.color_skeleton_points;
        esp::config.color_head_circle = defaults.color_head_circle;
        esp::config.color_health = defaults.color_health;
        esp::config.color_armor = defaults.color_armor;
        esp::config.color_weapon = defaults.color_weapon;
        esp::config.color_box_2d = defaults.color_box_2d;
        esp::config.color_corner_box = defaults.color_corner_box;
        esp::config.color_snaplines = defaults.color_snaplines;
        esp::config.color_name = defaults.color_name;
        esp::config.color_id = defaults.color_id;
        esp::config.color_distance = defaults.color_distance;
        esp::config.color_npc = defaults.color_npc;
    }
    CyberWidgets::EndCard();
    ImGui::EndChild();

    ImGui::SameLine(0.0f, gap);
    ImGui::BeginGroup();
    CyberWidgets::BeginCard(Loc::Tr("vis.preview"), previewWidth);
    const float panelHeight = (std::max)(CyberTheme::Px(300.0f),
        columnHeight - CyberTheme::Metrics::CardHeaderHeight -
        CyberTheme::Metrics::CardPadding * 2.0f - CyberTheme::Px(12.0f));
    DrawEspPreviewPanel((std::max)(CyberTheme::Px(180.0f),
        previewWidth - CyberTheme::Metrics::CardPadding * 2.0f), panelHeight, true);
    CyberWidgets::EndCard();
    ImGui::EndGroup();
}
