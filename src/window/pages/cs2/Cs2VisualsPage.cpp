#include "../../widgets.h"
#include "../../theme.h"
#include "cs2_config.h"
#include "../../localization.h"
#include "../../../launcher/launcher_assets.h"
#include "imgui.h"
#include <cstdio>
#include <algorithm>
#include <cmath>

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

    // Operator portrait (same asset pipeline as FiveM preview)
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

    const ImVec2 head(cx, top + 22.f * sc);
    const ImVec2 neck(cx, top + 46.f * sc);
    const ImVec2 chest(cx, top + 78.f * sc);
    const ImVec2 spine(cx, top + 110.f * sc);
    const ImVec2 pelvis(cx, top + 148.f * sc);
    const ImVec2 sh_l(cx - 42.f * sc, top + 66.f * sc);
    const ImVec2 sh_r(cx + 42.f * sc, top + 66.f * sc);
    const ImVec2 el_l(cx - 68.f * sc, top + 118.f * sc);
    const ImVec2 el_r(cx + 68.f * sc, top + 118.f * sc);
    const ImVec2 ha_l(cx - 82.f * sc, top + 178.f * sc);
    const ImVec2 ha_r(cx + 82.f * sc, top + 178.f * sc);
    const ImVec2 hip_l(cx - 16.f * sc, top + 148.f * sc);
    const ImVec2 hip_r(cx + 16.f * sc, top + 148.f * sc);
    const ImVec2 kn_l(cx - 22.f * sc, top + 220.f * sc);
    const ImVec2 kn_r(cx + 22.f * sc, top + 220.f * sc);
    const ImVec2 an_l(cx - 24.f * sc, top + 285.f * sc);
    const ImVec2 an_r(cx + 24.f * sc, top + 285.f * sc);

    auto line = [&](ImVec2 a, ImVec2 b, ImU32 col, float th) {
        dl->AddLine(a, b, col, th);
    };

    const float skTh = std::clamp(CS2::config.skeleton_thickness, 0.5f, 6.f);
    if (CS2::config.skeleton) {
        const ImU32 sk = Col4(CS2::config.col_skeleton);
        line(head, neck, sk, skTh);
        line(neck, chest, sk, skTh);
        line(chest, spine, sk, skTh);
        line(spine, pelvis, sk, skTh);
        if (CS2::config.bone_draw_arms) {
            line(neck, sh_l, sk, skTh); line(sh_l, el_l, sk, skTh); line(el_l, ha_l, sk, skTh);
            line(neck, sh_r, sk, skTh); line(sh_r, el_r, sk, skTh); line(el_r, ha_r, sk, skTh);
        }
        if (CS2::config.bone_draw_legs) {
            line(pelvis, hip_l, sk, skTh); line(hip_l, kn_l, sk, skTh); line(kn_l, an_l, sk, skTh);
            line(pelvis, hip_r, sk, skTh); line(hip_r, kn_r, sk, skTh); line(kn_r, an_r, sk, skTh);
        }
        if (CS2::config.skeleton_joints) {
            const ImU32 jn = Col4(CS2::config.col_joints);
            for (const ImVec2& p : { head, neck, chest, spine, pelvis, sh_l, sh_r, el_l, el_r,
                                     ha_l, ha_r, hip_l, hip_r, kn_l, kn_r, an_l, an_r })
                dl->AddCircleFilled(p, 2.4f * sc, jn, 10);
        }
    }
    if (CS2::config.head_dot)
        dl->AddCircleFilled(head, 4.f * sc, Col4(CS2::config.col_head), 12);
    if (CS2::config.box) {
        const float left = cx - 70.f * sc, right = cx + 70.f * sc;
        const float topB = top + 8.f * sc, bot = top + 310.f * sc;
        dl->AddRect(ImVec2(left, topB), ImVec2(right, bot), Col4(CS2::config.col_box),
                    0.f, 0, std::clamp(CS2::config.box_thickness, 0.5f, 6.f));
    }
    if (CS2::config.name)
        dl->AddText(ImVec2(cx - 24.f, top - 2.f), Col4(CS2::config.col_name), "Jogador");
    if (CS2::config.distance)
        dl->AddText(ImVec2(cx - 12.f, top + 312.f * sc), Col4(CS2::config.col_distance), "24m");

    ImGui::Dummy(ImVec2(width, height));
}

} // namespace

void DrawCs2Visuals() {
    // Three independent columns like FiveM: features | colors | fixed preview
    const float full = ImGui::GetContentRegionAvail().x;
    const float columnHeight = (std::max)(CyberTheme::Px(410.0f), ImGui::GetContentRegionAvail().y);
    const float gap = CyberTheme::Metrics::GridGap;
    const float previewWidth = std::clamp(full * 0.31f, CyberTheme::Px(250.0f), CyberTheme::Px(310.0f));
    const float settingsWidth = (std::max)(CyberTheme::Px(220.0f), full - previewWidth - gap * 2.0f);
    const float featuresWidth = settingsWidth * 0.50f;
    const float colorsWidth = settingsWidth - featuresWidth;

    // ── Column 1: FUNÇÕES ESP (scrollable) ──────────────────────────
    ImGui::BeginChild("##cs2_esp_features_scroll", ImVec2(featuresWidth, columnHeight), false,
        ImGuiWindowFlags_AlwaysVerticalScrollbar);
    CyberWidgets::BeginCard("FUNÇÕES ESP");
    CyberWidgets::ToggleSwitch("Ativar ESP", &CS2::config.esp_enabled);
    CyberWidgets::Separator();
    ImGui::BeginDisabled(!CS2::config.esp_enabled);
    CyberWidgets::ToggleSwitch("ESP do próprio jogador", &CS2::config.self_esp);
    CyberWidgets::ToggleSwitch("Mostrar bots", &CS2::config.show_bots);
    CyberWidgets::ToggleSwitch("Apenas alvos visíveis", &CS2::config.visible_check);
    CyberWidgets::ToggleSwitch("Ocultar equipa", &CS2::config.team_check);
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("ELEMENTOS");
    CyberWidgets::ToggleSwitch("Esqueleto", &CS2::config.skeleton);
    if (CS2::config.skeleton) {
        CyberWidgets::ToggleSwitch("Articulações", &CS2::config.skeleton_joints);
        CyberWidgets::ToggleSwitch("Braços", &CS2::config.bone_draw_arms);
        CyberWidgets::ToggleSwitch("Pernas", &CS2::config.bone_draw_legs);
    }
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
    CyberWidgets::ToggleSwitch("Chapéu chinês 3D", &CS2::config.chinese_hat);
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
    ImGui::EndDisabled();
    CyberWidgets::EndCard();
    ImGui::EndChild();

    // ── Column 2: PERSONALIZAÇÃO (scrollable) ───────────────────────
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
    ImGui::ColorEdit4("Cabeça##c", CS2::config.col_head, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Caixa##c", CS2::config.col_box, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Linhas guia##c", CS2::config.col_snaplines, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    CyberWidgets::SectionTitle("INFORMAÇÃO");
    ImGui::ColorEdit4("Vida##c", CS2::config.col_health, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Armadura##c", CS2::config.col_armor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Nome##c", CS2::config.col_name, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Distância##c", CS2::config.col_distance, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    CyberWidgets::EndCard();
    ImGui::EndChild();

    // ── Column 3: PRÉ-VISUALIZAÇÃO (fixed) ───────────────────────────
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
