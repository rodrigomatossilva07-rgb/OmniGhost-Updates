#include "../../window.hpp"
#include "../../widgets.h"
#include "../../theme.h"
#include "../../fonts.h"
#include "../../localization.h"
#include "../../../gameplay/sound_esp.h"
#ifdef UI_PREVIEW
#include "preview/preview_runtime.h"
#else
#include "esp/esp.h"
#endif
#include <cmath>
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

    // Layered, clipped HUD surface. All decoration is intentionally subtle so
    // the configured ESP colours remain the main visual information.
    dl->AddRectFilled(ImVec2(origin.x + 2.f, origin.y + 5.f),
        ImVec2(end.x + 2.f, end.y + 5.f), IM_COL32(0, 0, 0, 150), 12.f);
    dl->AddRectFilled(origin, end, IM_COL32(3, 3, 5, 255), 12.f);
    dl->PushClipRect(ImVec2(origin.x + 1.f, origin.y + 1.f),
        ImVec2(end.x - 1.f, end.y - 1.f), true);
    dl->AddRectFilledMultiColor(ImVec2(origin.x + 1.f, origin.y + 1.f),
        ImVec2(end.x - 1.f, end.y - 1.f), IM_COL32(17, 15, 10, 150),
        IM_COL32(3, 3, 5, 255), IM_COL32(2, 2, 4, 255), IM_COL32(8, 7, 5, 170));

    const ImU32 gridMinor = IM_COL32(212, 175, 55, 10);
    const ImU32 gridMajor = IM_COL32(212, 175, 55, 20);
    for (float x = origin.x + 18.f; x < end.x; x += 18.f) {
        const int index = static_cast<int>((x - origin.x) / 18.f);
        dl->AddLine(ImVec2(x, origin.y + 42.f), ImVec2(x, end.y),
            (index % 4) == 0 ? gridMajor : gridMinor, 1.f);
    }
    for (float y = origin.y + 54.f; y < end.y; y += 18.f) {
        const int index = static_cast<int>((y - origin.y) / 18.f);
        dl->AddLine(ImVec2(origin.x, y), ImVec2(end.x, y),
            (index % 4) == 0 ? gridMajor : gridMinor, 1.f);
    }

    dl->AddRectFilled(origin, ImVec2(end.x, origin.y + 42.f), IM_COL32(7, 7, 9, 246),
        12.f, ImDrawFlags_RoundCornersTop);
    dl->AddLine(ImVec2(origin.x + 12.f, origin.y + 41.f),
        ImVec2(end.x - 12.f, origin.y + 41.f), IM_COL32(212, 175, 55, 92), 1.f);
    dl->AddRectFilled(ImVec2(origin.x + 12.f, origin.y + 12.f),
        ImVec2(origin.x + 15.f, origin.y + 30.f), IM_COL32(255, 226, 138, 255), 1.f);
    if (ImFont* title = CyberFonts::GetTitleFont())
        dl->AddText(title, 15.f, ImVec2(origin.x + 22.f, origin.y + 10.f),
            IM_COL32(244, 240, 226, 255), "ESP // PREVIEW");
    else
        dl->AddText(ImVec2(origin.x + 22.f, origin.y + 12.f),
            IM_COL32(244, 240, 226, 255), "ESP // PREVIEW");
    dl->AddText(ImVec2(origin.x + 22.f, origin.y + 27.f),
        IM_COL32(128, 126, 120, 255), "CONFIGURAÇÃO EM TEMPO REAL");

    const char* state = previewVisible ? "VISÍVEL" : "OBSTRUÍDO";
    const ImU32 stateColor = previewVisible ? esp::config.color_visible : esp::config.color_invisible;
    const ImVec2 stateSize = ImGui::CalcTextSize(state);
    const ImVec2 badgeMin(end.x - stateSize.x - 24.f, origin.y + 11.f);
    const ImVec2 badgeMax(end.x - 10.f, origin.y + 32.f);
    ImVec4 stateFill = ImGui::ColorConvertU32ToFloat4(stateColor);
    stateFill.w = 0.13f;
    dl->AddRectFilled(badgeMin, badgeMax, ImGui::ColorConvertFloat4ToU32(stateFill), 5.f);
    dl->AddRect(badgeMin, badgeMax, stateColor, 5.f, 0, 1.f);
    dl->AddCircleFilled(ImVec2(badgeMin.x + 8.f, badgeMin.y + 10.5f), 2.5f, stateColor, 10);
    dl->AddText(ImVec2(badgeMin.x + 14.f, badgeMin.y + 3.f), stateColor, state);

    const float sc = (std::min)(panelWidth / 275.f, (panelHeight - 112.f) / 360.f);
    const float cx = origin.x + panelWidth * 0.50f;
    const float top = origin.y + 78.f;
    const ImVec2 head(cx, top + 24.f * sc);
    const ImVec2 neck(cx, top + 48.f * sc);
    const ImVec2 chest(cx, top + 84.f * sc);
    const ImVec2 spine_mid(cx, top + 116.f * sc);
    const ImVec2 spine_low(cx, top + 142.f * sc);
    const ImVec2 pelvis(cx, top + 164.f * sc);
    const ImVec2 l_cl(cx - 18.f * sc, top + 58.f * sc);
    const ImVec2 r_cl(cx + 18.f * sc, top + 58.f * sc);
    const ImVec2 l_sh(cx - 38.f * sc, top + 70.f * sc);
    const ImVec2 r_sh(cx + 38.f * sc, top + 70.f * sc);
    const ImVec2 l_up(cx - 49.f * sc, top + 105.f * sc);
    const ImVec2 r_up(cx + 49.f * sc, top + 105.f * sc);
    const ImVec2 l_el(cx - 55.f * sc, top + 139.f * sc);
    const ImVec2 r_el(cx + 55.f * sc, top + 139.f * sc);
    const ImVec2 l_fore(cx - 59.f * sc, top + 166.f * sc);
    const ImVec2 r_fore(cx + 59.f * sc, top + 166.f * sc);
    const ImVec2 l_wr(cx - 61.f * sc, top + 184.f * sc);
    const ImVec2 r_wr(cx + 61.f * sc, top + 184.f * sc);
    const ImVec2 l_ha(cx - 62.f * sc, top + 191.f * sc);
    const ImVec2 r_ha(cx + 62.f * sc, top + 191.f * sc);
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

    const ImU32 sk = PreviewColor(esp::config.color_skeleton, previewVisible);
    const ImU32 jt = esp::config.color_skeleton_points
        ? PreviewColor(esp::config.color_skeleton_points, previewVisible)
        : IM_COL32(80, 220, 90, 255);


    // Scanner rings and a faint moving scan line add depth without obscuring
    // the configured elements.
    for (int ring = 1; ring <= 3; ++ring)
        dl->AddCircle(ImVec2(cx, top + 165.f * sc), 38.f * ring * sc,
            IM_COL32(212, 175, 55, 17 - ring * 3), 48, 1.f);
    const float scanPhase = std::fmod(static_cast<float>(ImGui::GetTime()) * 34.f,
        280.f * sc);
    dl->AddLine(ImVec2(cx - 78.f * sc, top + 34.f * sc + scanPhase),
        ImVec2(cx + 78.f * sc, top + 34.f * sc + scanPhase),
        IM_COL32(255, 226, 138, 24), 1.f);

    // Layered character silhouette: shadow, body mass, then the actual bone
    // overlay. This resembles an in-game target instead of a stick figure.
    const ImU32 limbShadow = IM_COL32(0, 0, 0, 210);
    const ImU32 limbFill = IM_COL32(35, 36, 42, 246);
    dl->AddCircleFilled(ImVec2(head.x + 2.f, head.y + 3.f), 16.f * sc,
        IM_COL32(0, 0, 0, 180), 28);
    dl->AddCircleFilled(head, 15.f * sc, IM_COL32(42, 43, 49, 255), 28);
    const ImVec2 torsoShadow[] = {
        ImVec2(l_sh.x - 3.f, l_sh.y + 3.f), ImVec2(r_sh.x + 3.f, r_sh.y + 3.f),
        ImVec2(r_hi.x + 4.f, r_hi.y + 4.f), ImVec2(l_hi.x - 4.f, l_hi.y + 4.f)
    };
    dl->AddConvexPolyFilled(torsoShadow, 4, IM_COL32(0, 0, 0, 190));
    const ImVec2 torso[] = { l_sh, r_sh, r_hi, l_hi };
    dl->AddConvexPolyFilled(torso, 4, IM_COL32(28, 29, 34, 255));
    const ImVec2 waist[] = {
        ImVec2(l_hi.x, l_hi.y - 2.f), ImVec2(r_hi.x, r_hi.y - 2.f),
        ImVec2(r_th.x + 2.f, r_th.y), ImVec2(l_th.x - 2.f, l_th.y)
    };
    dl->AddConvexPolyFilled(waist, 4, IM_COL32(24, 25, 29, 255));
    for (const auto& segment : {
        std::pair<ImVec2, ImVec2>{l_sh, l_el}, {l_el, l_ha},
        {r_sh, r_el}, {r_el, r_ha}, {l_hi, l_kn}, {l_kn, l_an},
        {r_hi, r_kn}, {r_kn, r_an} }) {
        dl->AddLine(ImVec2(segment.first.x + 2.f, segment.first.y + 3.f),
            ImVec2(segment.second.x + 2.f, segment.second.y + 3.f), limbShadow, 15.f * sc);
        dl->AddLine(segment.first, segment.second, limbFill, 12.f * sc);
    }
    dl->AddLine(l_an, l_ft, limbFill, 10.f * sc);
    dl->AddLine(r_an, r_ft, limbFill, 10.f * sc);

    auto bone = [&](ImVec2 a, ImVec2 b) {
        if (esp::config.skeleton) {
            dl->AddLine(ImVec2(a.x + 1.f, a.y + 1.f), ImVec2(b.x + 1.f, b.y + 1.f),
                IM_COL32(0, 0, 0, 220), 3.5f);
            dl->AddLine(a, b, sk, 1.8f);
        }
    };
    auto joint = [&](ImVec2 p) {
        if (esp::config.joints) {
            dl->AddCircleFilled(p, 2.8f, jt, 10);
            dl->AddCircle(p, 2.8f, CyberTheme::SafeShadowU32(180), 10, 1.0f);
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

    if (esp::config.head_circle)
        dl->AddCircle(head, 11.f,
            PreviewColor(esp::config.color_head_circle, previewVisible), 24, 2.0f);
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

    if (esp::config.health_bar) {
        const float barTop = head.y - 8.f;
        const float bot = l_ft.y;
        const float bx = boxMin.x - 11.f;
        const float h = bot - barTop;
        dl->AddRectFilled(ImVec2(bx - 5.f, barTop), ImVec2(bx + 1.f, bot), IM_COL32(0, 0, 0, 220), 2.f);
        const float pct = 0.72f;
        const ImU32 color = PreviewColor(esp::config.color_health, previewVisible);
        dl->AddRectFilledMultiColor(ImVec2(bx - 4.f, bot - h * pct), ImVec2(bx, bot),
            color, color, PreviewDarker(color), PreviewDarker(color));
        dl->AddText(ImVec2(bx - 7.f, barTop - 16.f), IM_COL32(150, 150, 154, 255), "HP");
    }

    if (esp::config.armor_bar) {
        const float barTop = head.y - 8.f;
        const float bot = r_ft.y;
        const float bx = boxMax.x + 10.f;
        const float h = bot - barTop;
        const float pct = 0.55f;
        dl->AddRectFilled(ImVec2(bx - 1.f, barTop), ImVec2(bx + 5.f, bot), IM_COL32(0, 0, 0, 220), 2.f);
        const ImU32 color = PreviewColor(esp::config.color_armor, previewVisible);
        dl->AddRectFilledMultiColor(ImVec2(bx, bot - h * pct), ImVec2(bx + 4.f, bot),
            color, color, PreviewDarker(color), PreviewDarker(color));
        dl->AddText(ImVec2(bx - 3.f, barTop - 16.f), IM_COL32(150, 150, 154, 255), "AP");
    }

    char line[64]{};
    if (esp::config.player_name || esp::config.player_id || esp::config.distance) {
        const char* name = esp::config.player_name ? "Jogador" : "";
        const char* id = esp::config.player_id ? " [42]" : "";
        const char* distance = esp::config.distance ? " | 85m" : "";
        std::snprintf(line, sizeof(line), "%s%s%s", name, id, distance);
        if (!esp::config.player_name && esp::config.player_id)
            std::snprintf(line, sizeof(line), "ID 42%s", distance);
        if (!esp::config.player_name && !esp::config.player_id)
            std::snprintf(line, sizeof(line), "85m");
        ImVec2 ts = ImGui::CalcTextSize(line);
        dl->AddRectFilled(ImVec2(cx - ts.x * 0.5f - 7.f, boxMin.y - 25.f),
            ImVec2(cx + ts.x * 0.5f + 7.f, boxMin.y - 5.f), IM_COL32(0, 0, 0, 180), 4.f);
        dl->AddText(ImVec2(cx - ts.x * 0.5f, boxMin.y - 23.f),
            PreviewColor(esp::config.player_name ? esp::config.color_name :
                (esp::config.player_id ? esp::config.color_id : esp::config.color_distance),
                previewVisible), line);
    }
    if (esp::config.weapon_name) {
        const char* w = "PISTOLA";
        ImVec2 ws = ImGui::CalcTextSize(w);
        dl->AddRectFilled(ImVec2(cx - ws.x * 0.5f - 8.f, boxMax.y + 5.f),
            ImVec2(cx + ws.x * 0.5f + 8.f, boxMax.y + 25.f), IM_COL32(0, 0, 0, 180), 4.f);
        dl->AddText(ImVec2(cx - ws.x * 0.5f, boxMax.y + 7.f),
            PreviewColor(esp::config.color_weapon, previewVisible), w);
    }

    dl->AddText(ImVec2(origin.x + 12.f, end.y - 22.f), IM_COL32(100, 98, 92, 255),
        "ALVO SIMULADO  •  60 FPS");
    dl->AddLine(ImVec2(end.x - 31.f, end.y - 16.f), ImVec2(end.x - 12.f, end.y - 16.f),
        IM_COL32(212, 175, 55, 155), 1.f);
    dl->PopClipRect();
    dl->AddRect(origin, end, IM_COL32(212, 175, 55, 76), 12.f, 0, 1.f);
    dl->AddLine(ImVec2(origin.x + 12.f, origin.y), ImVec2(origin.x + 66.f, origin.y),
        IM_COL32(255, 226, 138, 220), 2.f);

    ImGui::Dummy(ImVec2(panelWidth, panelHeight));
}

} // namespace

void DrawVisuals()
{
    // Product decision: trails / direction line removed from menu and runtime
    esp::config.trails = false;
    esp::config.look_direction = false;

    CyberWidgets::SearchBar(Loc::Tr("vis.search"));
    CyberWidgets::CardGap(6.f);

    const float full = CyberWidgets::CardContentWidth();
    const float gap = 10.f;
    const float column = (full - gap * 2.f) / 3.f;

    ImGui::BeginGroup();
    CyberWidgets::BeginCard("Configuração ESP", column);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.enable"), &esp::config.enabled);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.self_esp"), &esp::config.self_esp);
    CyberWidgets::ToggleSwitch("Cores por visibilidade", &esp::config.visibility_colors);
    CyberWidgets::ToggleSwitch("Verificação de visibilidade (só visíveis)", &esp::config.visible_check);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.team_check"), &esp::config.team_check);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.skeleton"), &esp::config.skeleton);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.joints"), &esp::config.joints);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.head_circle"), &esp::config.head_circle);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.health_bar"), &esp::config.health_bar);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.armor_bar"), &esp::config.armor_bar);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.weapon"), &esp::config.weapon_name);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.box_2d"), &esp::config.box_2d);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.corner"), &esp::config.corner_box);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.snaplines"), &esp::config.snaplines);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.distance"), &esp::config.distance);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.name"), &esp::config.player_name);
    CyberWidgets::ToggleSwitch("ID do jogador", &esp::config.player_id);
    CyberWidgets::ToggleSwitch("Mostrar abatidos", &esp::config.show_knocked);
    CyberWidgets::ToggleSwitch("Mostrar mortos", &esp::config.show_dead);
    CyberWidgets::ToggleSwitch("ESP de NPCs", &esp::config.npc_esp);
    CyberWidgets::ToggleSwitch("Auréola na cabeça", &esp::config.head_halo);
    CyberWidgets::SliderFloat(Loc::TrID("vis.max_dist"), &esp::config.max_esp_distance, 20.f, 500.f, "%.0f m");
    const char* snapPositions[] = { "Topo", "Centro", "Fundo" };
    CyberWidgets::Combo("Posição das linhas guia", &esp::config.snapline_pos, snapPositions, 3);
    const char* circleTypes[] = { "Normal", "Preenchido", "Cruz" };
    CyberWidgets::Combo("Tipo do círculo", &esp::config.circle_type, circleTypes, 3);
    CyberWidgets::EndCard();
    ImGui::EndGroup();

    ImGui::SameLine(0.f, gap);
    ImGui::BeginGroup();
    CyberWidgets::BeginCard("Personalização", column);
    CyberWidgets::ColorEditU32("Jogador visível", &esp::config.color_visible);
    CyberWidgets::ColorEditU32("Jogador invisível", &esp::config.color_invisible);
    CyberWidgets::ColorEditU32("Morto", &esp::config.color_dead);
    CyberWidgets::ColorEditU32("Abatido", &esp::config.color_knocked);
    CyberWidgets::ColorEditU32("Equipa", &esp::config.color_team);
    CyberWidgets::ColorEditU32("Esqueleto", &esp::config.color_skeleton);
    CyberWidgets::ColorEditU32("Pontos do esqueleto", &esp::config.color_skeleton_points);
    CyberWidgets::ColorEditU32("Círculo da cabeça", &esp::config.color_head_circle);
    CyberWidgets::ColorEditU32("Barra de vida", &esp::config.color_health);
    CyberWidgets::ColorEditU32("Barra de armadura", &esp::config.color_armor);
    CyberWidgets::ColorEditU32("Nome da arma", &esp::config.color_weapon);
    CyberWidgets::ColorEditU32("Caixa 2D", &esp::config.color_box_2d);
    CyberWidgets::ColorEditU32("Caixa de canto", &esp::config.color_corner_box);
    CyberWidgets::ColorEditU32("Linhas guia", &esp::config.color_snaplines);
    CyberWidgets::ColorEditU32("Nome do jogador", &esp::config.color_name);
    CyberWidgets::ColorEditU32("ID do jogador", &esp::config.color_id);
    CyberWidgets::ColorEditU32("Distância", &esp::config.color_distance);
    CyberWidgets::ColorEditU32("ESP de NPCs", &esp::config.color_npc);
    CyberWidgets::ColorEditU32("Trails", &esp::config.color_trail);
    CyberWidgets::ColorEditU32("Aureola", &esp::config.color_halo);
    CyberWidgets::ToggleSwitch("Modo RGB (arco-íris)", &esp::config.rgb_mode);
    CyberWidgets::EndCard();
    ImGui::EndGroup();

    ImGui::SameLine(0.f, gap);

    ImGui::BeginGroup();
    CyberWidgets::BeginCard("Pré-visualização do ESP", column);
    static bool previewVisible = true;
    CyberWidgets::ToggleSwitch("Simular jogador visível", &previewVisible);
    DrawEspPreviewPanel((std::max)(120.f, column - 24.f), 520.f, previewVisible);
    CyberWidgets::EndCard();
    ImGui::EndGroup();

    CyberWidgets::CardGap(10.f);
    static Gameplay::SoundESP::SoundESPConfig sound =
        Gameplay::SoundESP::GetLegitSoundESPConfig();
    CyberWidgets::BeginCard("Sound ESP", full);
    CyberWidgets::ToggleSwitch("Ativar Sound ESP", &sound.enabled);
    CyberWidgets::ToggleSwitch("Apenas enquanto mira", &sound.only_when_aiming);
    CyberWidgets::ToggleSwitch("Passos", &sound.visual.show_footsteps);
    CyberWidgets::ToggleSwitch("Tiros", &sound.visual.show_gunshots);
    CyberWidgets::ToggleSwitch("Explosões", &sound.visual.show_explosions);
    CyberWidgets::ToggleSwitch("Recarregamentos", &sound.visual.show_reloads);
    CyberWidgets::ToggleSwitch("Voz", &sound.visual.show_voice);
    CyberWidgets::ToggleSwitch("Indicadores no ecrã", &sound.directionals.show_on_screen);
    CyberWidgets::ToggleSwitch("Indicadores no radar", &sound.directionals.show_on_radar);
    CyberWidgets::SliderFloat("Distância máxima do som", &sound.max_distance,
                              10.f, 500.f, "%.0f m");
    CyberWidgets::SliderFloat("Tamanho do indicador", &sound.directionals.indicator_size,
                              10.f, 100.f, "%.0f px");
    CyberWidgets::EndCard();
    Gameplay::SoundESP::SoundESPManager::Instance().SetConfig(sound);
}
