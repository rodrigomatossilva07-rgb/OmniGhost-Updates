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
#include <string>

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
    constexpr ImU32 kGunmetal = IM_COL32(27, 29, 29, 255);
    constexpr ImU32 kText = IM_COL32(216, 216, 210, 255);
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
    // Single visual reference for every layer in this preview.  Landmarks are
    // ratios of this body box, not independent screen coordinates.
    const float playerTop = top + 4.f * sc;
    const float playerBottom = top + 323.f * sc;
    const float playerHalfWidth = 62.f * sc;
    const float playerHeight = playerBottom - playerTop;
    const float playerLeft = cx - playerHalfWidth;
    const float playerRight = cx + playerHalfWidth;
    const auto pt = [&](float x, float y) { return ImVec2(cx + x * playerHalfWidth, playerTop + y * playerHeight); };
    const ImVec2 head = pt(0.f, .075f), neck = pt(0.f, .145f), chest = pt(0.f, .245f);
    const ImVec2 spine_mid = pt(0.f, .345f), spine_low = pt(0.f, .420f), pelvis = pt(0.f, .490f);
    const ImVec2 l_cl = pt(-.27f, .175f), r_cl = pt(.27f, .175f);
    const ImVec2 l_sh = pt(-.63f, .205f), r_sh = pt(.63f, .205f);
    const ImVec2 l_up = pt(-.82f, .285f), r_up = pt(.82f, .285f);
    const ImVec2 l_el = pt(-.96f, .390f), r_el = pt(.96f, .390f);
    const ImVec2 l_fore = pt(-1.04f, .465f), r_fore = pt(1.04f, .465f);
    const ImVec2 l_wr = pt(-1.06f, .530f), r_wr = pt(1.06f, .530f);
    const ImVec2 l_ha = pt(-1.04f, .550f), r_ha = pt(1.04f, .550f);
    const ImVec2 l_hi = pt(-.26f, .490f), r_hi = pt(.26f, .490f);
    const ImVec2 l_th = pt(-.31f, .630f), r_th = pt(.31f, .630f);
    const ImVec2 l_kn = pt(-.35f, .760f), r_kn = pt(.35f, .760f);
    const ImVec2 l_shin = pt(-.37f, .860f), r_shin = pt(.37f, .860f);
    const ImVec2 l_an = pt(-.39f, .950f), r_an = pt(.39f, .950f);
    const ImVec2 l_ft = pt(-.52f, 1.f), r_ft = pt(.52f, 1.f);

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

    const ImVec2 boxMin(playerLeft, playerTop);
    const ImVec2 boxMax(playerRight, playerBottom);
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
    if (esp::config.chinese_hat) {
        const float hatScale = std::clamp(esp::config.fun_effects_scale, .5f, 2.5f) * sc;
        const float base = 25.f * hatScale;
        const float tipY = playerTop - 23.f * hatScale;
        const float baseY = playerTop + 3.f * sc;
        for (int i = 0; i < 14; ++i) {
            const float a0 = i * 6.2831853f / 14.f;
            const float a1 = (i + 1) * 6.2831853f / 14.f;
            const ImU32 color = esp::config.fun_effects_rainbow
                ? IM_COL32((int)(std::sin(a0 + ImGui::GetTime() * .2f) * 127.f + 128.f),
                    (int)(std::sin(a0 + ImGui::GetTime() * .2f + 2.094f) * 127.f + 128.f),
                    (int)(std::sin(a0 + ImGui::GetTime() * .2f + 4.188f) * 127.f + 128.f), 245)
                : esp::config.color_fun_effects;
            const ImVec2 p0(cx + std::cos(a0) * base, baseY + std::sin(a0) * base * .24f);
            const ImVec2 p1(cx + std::cos(a1) * base, baseY + std::sin(a1) * base * .24f);
            dl->AddLine(ImVec2(cx, tipY), p0, color, 1.35f);
            dl->AddLine(p0, p1, color, 1.2f);
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
    // Preview-only silhouettes mirror the optional in-game adornments.  Their
    // anchors are the same head/shoulder landmarks used by the skeleton, so
    // they remain correctly placed as this panel scales.
    const float fxScale = std::clamp(esp::config.fun_effects_scale, .5f, 2.5f) * sc;
    const float hue = static_cast<float>(ImGui::GetTime()) * .18f;
    const auto fxColor = [&](float offset = 0.f) {
        if (!esp::config.fun_effects_rainbow)
            return esp::config.color_fun_effects;
        return IM_COL32((int)(std::sin(hue + offset) * 127.f + 128.f),
                        (int)(std::sin(hue + offset + 2.094f) * 127.f + 128.f),
                        (int)(std::sin(hue + offset + 4.188f) * 127.f + 128.f), 245);
    };
    if (esp::config.angel_wings) {
        const float w = 44.f * fxScale, h = 42.f * fxScale;
        for (int side : { -1, 1 }) {
            const ImVec2 root(cx + side * 18.f * sc, chest.y - 8.f * sc);
            const ImVec2 tip(root.x + side * w, root.y + h * .30f);
            dl->AddBezierCubic(root, ImVec2(root.x + side * w * .42f, root.y - h),
                ImVec2(tip.x, tip.y - h * .60f), tip, fxColor(side * .4f), 2.f);
            dl->AddLine(root, ImVec2(root.x + side * w * .72f, root.y + h), fxColor(side * .7f), 1.5f);
        }
    }
    if (esp::config.devil_horns) {
        const float w = 12.f * fxScale, h = 20.f * fxScale;
        for (int side : { -1, 1 }) {
            const ImVec2 base(head.x + side * 8.f * sc, head.y - 10.f * sc);
            const ImVec2 horn[] = { base, ImVec2(base.x + side * w, base.y - h),
                ImVec2(base.x + side * w * 1.15f, base.y + 2.f * sc) };
            dl->AddPolyline(horn, 3, fxColor(side * .5f), false, 2.f);
        }
    }
    if (esp::config.floating_crown) {
        const float w = 19.f * fxScale, y = head.y - 27.f * sc - std::sin(hue * 2.f) * 3.f * sc;
        const ImVec2 crown[] = { ImVec2(cx - w, y + 8.f * fxScale), ImVec2(cx - w, y),
            ImVec2(cx - w * .35f, y + 5.f * fxScale), ImVec2(cx, y - 5.f * fxScale),
            ImVec2(cx + w * .35f, y + 5.f * fxScale), ImVec2(cx + w, y), ImVec2(cx + w, y + 8.f * fxScale) };
        dl->AddPolyline(crown, 7, fxColor(), false, 2.f);
        dl->AddLine(crown[0], crown[6], fxColor(.4f), 2.f);
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

    const bool hasHeadAdornment = esp::config.chinese_hat || esp::config.floating_crown || esp::config.devil_horns;
    const float labelTop = boxMin.y - (hasHeadAdornment ? 52.f * sc : (esp::config.head_halo ? 31.f * sc : 18.f));
    if (esp::config.player_id) {
        const char* id = "ID: 42";
        const ImVec2 idSize = ImGui::CalcTextSize(id);
        dl->AddText(ImVec2(cx - idSize.x * 0.5f, labelTop - 15.f), kText, id);
    }
    if (esp::config.player_name || esp::config.distance) {
        const std::string label = (esp::config.player_name ? app_settings::T("Jogador", "Player") : "") +
            std::string(esp::config.player_name && esp::config.distance ? "  |  " : "") +
            (esp::config.distance ? "85m" : "");
        const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
        dl->AddText(ImVec2(cx - labelSize.x * .5f, labelTop - (esp::config.player_id ? 30.f : 15.f)),
            esp::config.player_name ? esp::config.color_name : esp::config.color_distance, label.c_str());
    }
    if (esp::config.weapon_name) {
        // Neutral firearm glyph: previewing the icon does not need a label or
        // a decorative badge that would not exist in the live ESP.
        const float y = boxMax.y + 11.f;
        const ImU32 wc = esp::config.color_weapon;
        dl->AddLine(ImVec2(cx - 12.f, y), ImVec2(cx + 12.f, y), wc, 2.f);
        dl->AddLine(ImVec2(cx - 5.f, y), ImVec2(cx - 8.f, y + 6.f), wc, 2.f);
        dl->AddLine(ImVec2(cx + 7.f, y), ImVec2(cx + 12.f, y - 3.f), wc, 1.5f);
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
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.chinese_hat"), &esp::config.chinese_hat);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.angel_wings"), &esp::config.angel_wings);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.devil_horns"), &esp::config.devil_horns);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.floating_crown"), &esp::config.floating_crown);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.hit_marker"), &esp::config.hit_marker);
    if (esp::config.angel_wings || esp::config.devil_horns || esp::config.floating_crown) {
        CyberWidgets::ToggleSwitch(Loc::Tr("vis.fun_rainbow"), &esp::config.fun_effects_rainbow);
        CyberWidgets::SliderFloat(Loc::Tr("vis.fun_size"), &esp::config.fun_effects_scale, .5f, 2.5f, "%.2f");
    }
    CyberWidgets::ToggleSwitch("Rastros", &esp::config.trails);
    if (esp::config.trails)
        CyberWidgets::ToggleSwitch("Rastros arco-iris", &esp::config.rainbow_trails);
    CyberWidgets::ToggleSwitch("Eye Line", &esp::config.look_direction);

    
    CyberWidgets::SectionTitle("ESPESSURA");
    CyberWidgets::SliderFloat("Esqueleto", &esp::config.skeleton_thickness, 0.5f, 6.f, "%.1f");
    CyberWidgets::SliderFloat("Linhas guia", &esp::config.snapline_thickness, 0.5f, 6.f, "%.1f");
    CyberWidgets::SliderFloat("Circulo cabeca", &esp::config.head_circle_thickness, 0.5f, 6.f, "%.1f");
    CyberWidgets::SliderFloat("Caixa", &esp::config.box_thickness, 0.5f, 6.f, "%.1f");
    CyberWidgets::SliderFloat("Eye Line", &esp::config.eye_line_thickness, 0.5f, 6.f, "%.1f");
    if (esp::config.trails)
        CyberWidgets::SliderFloat("Rastro", &esp::config.trail_thickness, 1.f, 8.f, "%.1f");

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
    colorWhenEnabled(Loc::Tr("vis.fun_color"), &esp::config.color_fun_effects,
                     espEnabled && !esp::config.fun_effects_rainbow &&
                     (esp::config.angel_wings || esp::config.devil_horns || esp::config.floating_crown));
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
        esp::config.color_fun_effects = defaults.color_fun_effects;
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
