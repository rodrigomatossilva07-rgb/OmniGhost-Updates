#include "../../widgets.h"
#include "../../theme.h"
#include "cs2_config.h"
#include "../../localization.h"
#include "../../../launcher/launcher_assets.h"
#include "gameplay/esp_fx.h"
#include "gameplay/esp_core.h"
#include "imgui.h"
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <string>

namespace {

ImU32 Col4(const float c[4], float aMul = 1.f) {
    int a = (int)(c[3] * aMul * 255.f);
    if (a < 0) a = 0; if (a > 255) a = 255;
    return IM_COL32((int)(c[0] * 255), (int)(c[1] * 255), (int)(c[2] * 255), a);
}

void DrawCs2EspPreviewPanel(float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const ImVec2 e(o.x + width, o.y + height);
    dl->AddRectFilled(o, e, IM_COL32(8, 8, 12, 240), 10.f);
    dl->AddRect(o, e, IM_COL32(212, 175, 55, 40), 10.f);

    const float sc = (std::min)(width / 280.f, (height - 24.f) / 380.f);
    const float cx = o.x + width * 0.5f;
    const float top = o.y + 18.f;
    const float panelWidth = width;

    const LauncherAssets::Texture operatorImage = LauncherAssets::FiveMEspPreview();
    const float portraitTop = top - 12.f * sc;
    const float portraitBottom = e.y - 16.f;
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

    // One body box drives every ESP layer in this preview.  The landmarks are
    // normalized to it, so boxes, bars and bones remain aligned on resize.
    const float playerTop = top + 4.f * sc;
    const float playerBottom = top + 323.f * sc;
    const float playerHalfWidth = 62.f * sc;
    const float playerHeight = playerBottom - playerTop;
    const float left = cx - playerHalfWidth, right = cx + playerHalfWidth;
    const auto pt = [&](float x, float y) { return ImVec2(cx + x * playerHalfWidth, playerTop + y * playerHeight); };
    const ImVec2 head = pt(0.f, .075f), neck = pt(0.f, .145f), chest = pt(0.f, .245f);
    const ImVec2 spine = pt(0.f, .360f), pelvis = pt(0.f, .490f);
    const ImVec2 sh_l = pt(-.63f, .205f), sh_r = pt(.63f, .205f);
    const ImVec2 el_l = pt(-.96f, .390f), el_r = pt(.96f, .390f);
    const ImVec2 ha_l = pt(-1.04f, .550f), ha_r = pt(1.04f, .550f);
    const ImVec2 hip_l = pt(-.26f, .490f), hip_r = pt(.26f, .490f);
    const ImVec2 kn_l = pt(-.35f, .760f), kn_r = pt(.35f, .760f);
    const ImVec2 an_l = pt(-.39f, .950f), an_r = pt(.39f, .950f);

    auto line = [&](ImVec2 a, ImVec2 b, ImU32 col, float th) { dl->AddLine(a, b, col, th); };

    const float topB = playerTop, bot = playerBottom;

    // Full skeleton always when enabled
    if (CS2::config.skeleton) {
        const ImU32 sk = Col4(CS2::config.col_skeleton);
        const float skTh = std::clamp(CS2::config.skeleton_thickness, 0.5f, 6.f);
        line(head, neck, sk, skTh);
        line(neck, chest, sk, skTh);
        line(chest, spine, sk, skTh);
        line(spine, pelvis, sk, skTh);
        line(neck, sh_l, sk, skTh); line(sh_l, el_l, sk, skTh); line(el_l, ha_l, sk, skTh);
        line(neck, sh_r, sk, skTh); line(sh_r, el_r, sk, skTh); line(el_r, ha_r, sk, skTh);
        line(pelvis, hip_l, sk, skTh); line(hip_l, kn_l, sk, skTh); line(kn_l, an_l, sk, skTh);
        line(pelvis, hip_r, sk, skTh); line(hip_r, kn_r, sk, skTh); line(kn_r, an_r, sk, skTh);
        if (CS2::config.skeleton_joints) {
            const ImU32 jn = Col4(CS2::config.col_joints);
            for (const ImVec2& p : { head, neck, chest, spine, pelvis, sh_l, sh_r, el_l, el_r,
                                     ha_l, ha_r, hip_l, hip_r, kn_l, kn_r, an_l, an_r })
                dl->AddCircleFilled(p, 2.4f * sc, jn, 10);
        }
    }

    if (CS2::config.head_dot)
        dl->AddCircleFilled(head, 4.f * sc, Col4(CS2::config.col_head), 12);

    if (CS2::config.box && !CS2::config.box_corner) {
        dl->AddRect(ImVec2(left, topB), ImVec2(right, bot), Col4(CS2::config.col_box),
                    0.f, 0, std::clamp(CS2::config.box_thickness, 0.5f, 6.f));
    }
    if (CS2::config.box_corner) {
        OmniGhost::Gameplay::EspCore::DrawCornerBox(
            dl, ImVec2(left, topB), ImVec2(right, bot),
            Col4(CS2::config.col_box_corner),
            std::clamp(CS2::config.box_thickness, 0.5f, 6.f));
    }

    if (CS2::config.health_bar) {
        const float bar_w = 4.f;
        const float bx = left - 8.f;
        dl->AddRectFilled(ImVec2(bx, topB), ImVec2(bx + bar_w, bot), IM_COL32(20, 20, 20, 180));
        dl->AddRectFilled(ImVec2(bx, topB + (bot - topB) * 0.25f), ImVec2(bx + bar_w, bot), Col4(CS2::config.col_health));
    }
    if (CS2::config.armor_bar) {
        const float bar_w = 4.f;
        const float bx = right + 4.f;
        dl->AddRectFilled(ImVec2(bx, topB), ImVec2(bx + bar_w, bot), IM_COL32(20, 20, 20, 180));
        dl->AddRectFilled(ImVec2(bx, topB + (bot - topB) * 0.40f), ImVec2(bx + bar_w, bot), Col4(CS2::config.col_armor));
    }

    if (CS2::config.head_halo) {
        const float r = 14.f * sc;
        dl->AddCircle(ImVec2(head.x, head.y - 2.f * sc), r, Col4(CS2::config.col_halo), 24, 1.6f);
    }
    // These are lightweight preview equivalents of the 3D effects.  They use
    // the same head and shoulder anchors as the preview skeleton, preventing
    // effects from drifting as the card changes size.
    const float fxScale = std::clamp(CS2::config.fun_effects_scale, .5f, 2.5f) * sc;
    const float hue = static_cast<float>(ImGui::GetTime()) * .18f;
    const auto fxColor = [&](float offset = 0.f) {
        return CS2::config.fun_effects_rainbow
            ? OmniGhost::Gameplay::EspFx::Hsv(hue + offset, .88f, 1.f, .96f)
            : Col4(CS2::config.col_fun_effects);
    };
    if (CS2::config.angel_wings) {
        const float wingW = 43.f * fxScale, wingH = 42.f * fxScale;
        for (int side : { -1, 1 }) {
            const ImVec2 root(cx + side * 17.f * sc, chest.y - 7.f * sc);
            const ImVec2 tip(root.x + side * wingW, root.y + wingH * .28f);
            dl->AddBezierCubic(root, ImVec2(root.x + side * wingW * .42f, root.y - wingH),
                ImVec2(tip.x, tip.y - wingH * .60f), tip, fxColor(side * .4f), 2.f);
            dl->AddLine(root, ImVec2(root.x + side * wingW * .70f, root.y + wingH), fxColor(side * .7f), 1.5f);
        }
    }
    if (CS2::config.devil_horns) {
        const float hornW = 12.f * fxScale, hornH = 20.f * fxScale;
        for (int side : { -1, 1 }) {
            const ImVec2 base(head.x + side * 8.f * sc, head.y - 10.f * sc);
            const ImVec2 horn[] = { base, ImVec2(base.x + side * hornW, base.y - hornH),
                ImVec2(base.x + side * hornW * 1.15f, base.y + 2.f * sc) };
            dl->AddPolyline(horn, 3, fxColor(side * .5f), false, 2.f);
        }
    }
    if (CS2::config.floating_crown) {
        const float crownW = 19.f * fxScale;
        const float crownY = head.y - 27.f * sc - std::sin(hue * 2.f) * 3.f * sc;
        const ImVec2 crown[] = { ImVec2(cx - crownW, crownY + 8.f * fxScale), ImVec2(cx - crownW, crownY),
            ImVec2(cx - crownW * .35f, crownY + 5.f * fxScale), ImVec2(cx, crownY - 5.f * fxScale),
            ImVec2(cx + crownW * .35f, crownY + 5.f * fxScale), ImVec2(cx + crownW, crownY),
            ImVec2(cx + crownW, crownY + 8.f * fxScale) };
        dl->AddPolyline(crown, 7, fxColor(), false, 2.f);
        dl->AddLine(crown[0], crown[6], fxColor(.4f), 2.f);
    }
    if (CS2::config.chinese_hat) {
        // 2D preview stand-in for the 3D rotating hat
        const float hs = std::clamp(CS2::config.chinese_hat_scale, 0.4f, 3.f);
        const float base = 22.f * sc * hs;
        const float tipY = head.y - 28.f * sc * hs;
        const float baseY = head.y - 4.f * sc;
        for (int i = 0; i < 12; ++i) {
            const float t0 = (float)i / 12.f;
            const float t1 = (float)(i + 1) / 12.f;
            const ImU32 c0 = OmniGhost::Gameplay::EspFx::Hsv(t0 + (float)ImGui::GetTime() * 0.15f, 0.95f, 1.f, 0.95f);
            const float a0 = t0 * 6.2831853f, a1 = t1 * 6.2831853f;
            dl->AddLine(ImVec2(cx, tipY),
                        ImVec2(cx + std::cos(a0) * base, baseY + std::sin(a0) * base * 0.25f), c0, 1.4f);
            dl->AddLine(ImVec2(cx + std::cos(a0) * base, baseY + std::sin(a0) * base * 0.25f),
                        ImVec2(cx + std::cos(a1) * base, baseY + std::sin(a1) * base * 0.25f), c0, 1.3f);
        }
    }
    if (CS2::config.look_direction) {
        dl->AddLine(head, ImVec2(head.x + 40.f * sc, head.y - 8.f * sc), Col4(CS2::config.col_look),
                    std::clamp(CS2::config.eye_line_thickness, 0.5f, 6.f));
    }
    if (CS2::config.snaplines) {
        dl->AddLine(ImVec2(cx, e.y - 4.f), ImVec2(cx, bot), Col4(CS2::config.col_snaplines),
                    std::clamp(CS2::config.snapline_thickness, 0.5f, 6.f));
    }
    if (CS2::config.name || CS2::config.distance) {
        const std::string label = (CS2::config.name ? "Jogador" : "") +
            std::string(CS2::config.name && CS2::config.distance ? "  |  " : "") +
            (CS2::config.distance ? "24m" : "");
        const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
        const bool hasHeadAdornment = CS2::config.chinese_hat || CS2::config.floating_crown || CS2::config.devil_horns;
        const float labelY = topB - (hasHeadAdornment ? 52.f * sc : (CS2::config.head_halo ? 31.f * sc : 18.f));
        dl->AddText(ImVec2(cx - labelSize.x * .5f, labelY),
            CS2::config.name ? Col4(CS2::config.col_name) : Col4(CS2::config.col_distance), label.c_str());
    }
    if (CS2::config.weapon_icons)
    {
        const float weaponY = (std::min)(bot + 12.f, e.y - 10.f);
        const ImU32 weaponColor = Col4(CS2::config.col_weapon);
        dl->AddLine(ImVec2(cx - 12.f, weaponY), ImVec2(cx + 12.f, weaponY), weaponColor, 2.f);
        dl->AddLine(ImVec2(cx - 5.f, weaponY), ImVec2(cx - 8.f, weaponY + 6.f), weaponColor, 2.f);
        dl->AddLine(ImVec2(cx + 7.f, weaponY), ImVec2(cx + 12.f, weaponY - 3.f), weaponColor, 1.5f);
    }

    ImGui::Dummy(ImVec2(width, height));
}

} // namespace

void DrawCs2Visuals() {
    // Keep full body skeleton always on for gameplay + preview
    CS2::config.bone_draw_arms = true;
    CS2::config.bone_draw_legs = true;

    const float full = ImGui::GetContentRegionAvail().x;
    const float columnHeight = (std::max)(CyberTheme::Px(410.0f), ImGui::GetContentRegionAvail().y);
    const float gap = CyberTheme::Metrics::GridGap;
    const float previewWidth = std::clamp(full * 0.31f, CyberTheme::Px(250.0f), CyberTheme::Px(310.0f));
    const float settingsWidth = (std::max)(CyberTheme::Px(220.0f), full - previewWidth - gap * 2.0f);
    const float featuresWidth = settingsWidth * 0.50f;
    const float colorsWidth = settingsWidth - featuresWidth;

    ImGui::BeginChild("##cs2_esp_features_scroll", ImVec2(featuresWidth, columnHeight), false,
        ImGuiWindowFlags_AlwaysVerticalScrollbar);
    CyberWidgets::BeginCard("FUNÇÕES ESP");
    CyberWidgets::ToggleSwitch("Ativar ESP", &CS2::config.esp_enabled);
    CyberWidgets::Separator();
    CyberWidgets::ToggleSwitch("ESP do próprio jogador", &CS2::config.self_esp);
    CyberWidgets::ToggleSwitch("Mostrar bots", &CS2::config.show_bots);
    CyberWidgets::ToggleSwitch("Apenas alvos visíveis", &CS2::config.visible_check);
    CyberWidgets::ToggleSwitch("Ocultar equipa", &CS2::config.team_check);
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("ELEMENTOS");
    CyberWidgets::ToggleSwitch("Esqueleto", &CS2::config.skeleton);
    CyberWidgets::ToggleSwitch("Articulações", &CS2::config.skeleton_joints);
    CyberWidgets::ToggleSwitch("Ponto na cabeça", &CS2::config.head_dot);
    CyberWidgets::ToggleSwitch("Vida", &CS2::config.health_bar);
    CyberWidgets::ToggleSwitch("Armadura", &CS2::config.armor_bar);
    CyberWidgets::ToggleSwitch("Nome", &CS2::config.name);
    CyberWidgets::ToggleSwitch("Distância", &CS2::config.distance);
    CyberWidgets::ToggleSwitch("Arma", &CS2::config.weapon_icons);
    CyberWidgets::ToggleSwitch("Caixa", &CS2::config.box);
    CyberWidgets::ToggleSwitch("Caixa de cantos", &CS2::config.box_corner);
    CyberWidgets::ToggleSwitch("Linhas guia", &CS2::config.snaplines);
    CyberWidgets::ToggleSwitch("Auréola na cabeça", &CS2::config.head_halo);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.chinese_hat"), &CS2::config.chinese_hat);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.angel_wings"), &CS2::config.angel_wings);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.devil_horns"), &CS2::config.devil_horns);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.floating_crown"), &CS2::config.floating_crown);
    if (CS2::config.angel_wings || CS2::config.devil_horns || CS2::config.floating_crown) {
        CyberWidgets::ToggleSwitch(Loc::Tr("vis.fun_rainbow"), &CS2::config.fun_effects_rainbow);
        CyberWidgets::SliderFloat(Loc::Tr("vis.fun_size"), &CS2::config.fun_effects_scale, 0.5f, 2.5f, "%.2f");
    }
    CyberWidgets::ToggleSwitch("Rastros", &CS2::config.trails);
    if (CS2::config.trails)
        CyberWidgets::ToggleSwitch("Rastros arco-íris", &CS2::config.rainbow_trails);
    CyberWidgets::ToggleSwitch("Eye Line", &CS2::config.look_direction);
    CyberWidgets::SliderFloat("Distância máxima", &CS2::config.max_distance, 20.f, 500.f, "%.0f m");
    CyberWidgets::SectionTitle("ESPESSURA");
    CyberWidgets::SliderFloat("Esqueleto##th", &CS2::config.skeleton_thickness, 0.5f, 6.f, "%.1f");
    CyberWidgets::SliderFloat("Linhas guia##th", &CS2::config.snapline_thickness, 0.5f, 6.f, "%.1f");
    CyberWidgets::SliderFloat("Círculo cabeça", &CS2::config.head_circle_thickness, 0.5f, 6.f, "%.1f");
    CyberWidgets::SliderFloat("Caixa##th", &CS2::config.box_thickness, 0.5f, 6.f, "%.1f");
    CyberWidgets::SliderFloat("Eye Line##th", &CS2::config.eye_line_thickness, 0.5f, 6.f, "%.1f");
    if (CS2::config.trails)
        CyberWidgets::SliderFloat("Rastro##th", &CS2::config.trail_thickness, 1.f, 8.f, "%.1f");
    if (CS2::config.chinese_hat)
        CyberWidgets::SliderFloat("Tamanho do chapéu", &CS2::config.chinese_hat_scale, 0.4f, 2.5f, "%.2f");
    CyberWidgets::EndCard();
    ImGui::EndChild();

    ImGui::SameLine(0.0f, gap);
    ImGui::BeginChild("##cs2_esp_colors_scroll", ImVec2(colorsWidth, columnHeight), false,
        ImGuiWindowFlags_AlwaysVerticalScrollbar);
    CyberWidgets::BeginCard("PERSONALIZAÇÃO");
    CyberWidgets::ToggleSwitch("Cores por visibilidade", &CS2::config.visibility_colors);
    CyberWidgets::SectionTitle("JOGADORES");
    ImGui::ColorEdit4("Visível", CS2::config.col_visible, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Oculto", CS2::config.col_occluded, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Inimigo", CS2::config.col_enemy, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Equipa", CS2::config.col_team, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    CyberWidgets::SectionTitle("DESENHO ESP");
    ImGui::ColorEdit4("Esqueleto##c", CS2::config.col_skeleton, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Articulações##c", CS2::config.col_joints, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Ponto na cabeça##c", CS2::config.col_head, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Caixa##c", CS2::config.col_box, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Caixa de cantos##c", CS2::config.col_box_corner, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Linhas guia##c", CS2::config.col_snaplines, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4(Loc::TrID("vis.fun_color"), CS2::config.col_fun_effects, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    CyberWidgets::SectionTitle("INFORMAÇÃO");
    ImGui::ColorEdit4("Vida##c", CS2::config.col_health, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Armadura##c", CS2::config.col_armor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Nome##c", CS2::config.col_name, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Distância##c", CS2::config.col_distance, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Arma##c", CS2::config.col_weapon, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    CyberWidgets::EndCard();
    ImGui::EndChild();

    ImGui::SameLine(0.0f, gap);
    ImGui::BeginGroup();
    CyberWidgets::BeginCard("PRÉ-VISUALIZAÇÃO DO ESP", previewWidth);
    const float panelHeight = (std::max)(CyberTheme::Px(300.0f),
        columnHeight - CyberTheme::Metrics::CardHeaderHeight -
        CyberTheme::Metrics::CardPadding * 2.0f - CyberTheme::Px(12.0f));
    DrawCs2EspPreviewPanel(
        (std::max)(CyberTheme::Px(180.0f), previewWidth - CyberTheme::Metrics::CardPadding * 2.0f),
        panelHeight);
    CyberWidgets::EndCard();
    ImGui::EndGroup();
}
