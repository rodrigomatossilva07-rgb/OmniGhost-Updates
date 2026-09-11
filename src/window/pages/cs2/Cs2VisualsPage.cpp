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

void DrawDetailedPreview(float width, float height, bool visible) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 o = ImGui::GetCursorScreenPos();
    ImVec2 e(o.x + width, o.y + height);
    dl->AddRectFilled(o, e, IM_COL32(8, 8, 12, 240), 10.f);
    dl->AddRect(o, e, IM_COL32(212, 175, 55, 40), 10.f);

    const float sc = (std::min)(width / 280.f, (height - 24.f) / 380.f);
    const float cx = o.x + width * 0.5f;
    const float top = o.y + 18.f;

    // Detailed skeleton points (FiveM-like articulation density)
    const ImVec2 head(cx, top + 22.f * sc);
    const ImVec2 neck(cx, top + 46.f * sc);
    const ImVec2 clav_l(cx - 16.f * sc, top + 54.f * sc);
    const ImVec2 clav_r(cx + 16.f * sc, top + 54.f * sc);
    const ImVec2 chest(cx, top + 78.f * sc);
    const ImVec2 spine(cx, top + 110.f * sc);
    const ImVec2 pelvis(cx, top + 148.f * sc);
    const ImVec2 sh_l(cx - 42.f * sc, top + 66.f * sc);
    const ImVec2 sh_r(cx + 42.f * sc, top + 66.f * sc);
    const ImVec2 el_l(cx - 68.f * sc, top + 118.f * sc);
    const ImVec2 el_r(cx + 68.f * sc, top + 118.f * sc);
    const ImVec2 wr_l(cx - 78.f * sc, top + 162.f * sc);
    const ImVec2 wr_r(cx + 78.f * sc, top + 162.f * sc);
    const ImVec2 ha_l(cx - 82.f * sc, top + 178.f * sc);
    const ImVec2 ha_r(cx + 82.f * sc, top + 178.f * sc);
    const ImVec2 hip_l(cx - 16.f * sc, top + 148.f * sc);
    const ImVec2 hip_r(cx + 16.f * sc, top + 148.f * sc);
    const ImVec2 kn_l(cx - 22.f * sc, top + 220.f * sc);
    const ImVec2 kn_r(cx + 22.f * sc, top + 220.f * sc);
    const ImVec2 an_l(cx - 24.f * sc, top + 285.f * sc);
    const ImVec2 an_r(cx + 24.f * sc, top + 285.f * sc);
    const ImVec2 ft_l(cx - 26.f * sc, top + 302.f * sc);
    const ImVec2 ft_r(cx + 26.f * sc, top + 302.f * sc);

    auto col4 = [](const float c[4]) {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3]));
    };
    const ImU32 sk = visible ? col4(CS2::config.col_skeleton) : col4(CS2::config.col_occluded);
    const ImU32 jn = col4(CS2::config.col_joints);
    const ImU32 hd = col4(CS2::config.col_head);

    auto line = [&](ImVec2 a, ImVec2 b) {
        if (CS2::config.skeleton)
            dl->AddLine(a, b, sk, 2.0f);
    };
    auto joint = [&](ImVec2 p) {
        if (CS2::config.skeleton_joints)
            dl->AddCircleFilled(p, 3.2f * sc, jn, 10);
    };

    if (CS2::config.skeleton) {
        line(head, neck); line(neck, chest); line(chest, spine); line(spine, pelvis);
        line(neck, clav_l); line(clav_l, sh_l); line(sh_l, el_l); line(el_l, wr_l); line(wr_l, ha_l);
        line(neck, clav_r); line(clav_r, sh_r); line(sh_r, el_r); line(el_r, wr_r); line(wr_r, ha_r);
        line(pelvis, hip_l); line(hip_l, kn_l); line(kn_l, an_l); line(an_l, ft_l);
        line(pelvis, hip_r); line(hip_r, kn_r); line(kn_r, an_r); line(an_r, ft_r);
    }
    if (CS2::config.skeleton_joints) {
        for (ImVec2 p : { head, neck, clav_l, clav_r, chest, spine, pelvis,
                          sh_l, sh_r, el_l, el_r, wr_l, wr_r, ha_l, ha_r,
                          hip_l, hip_r, kn_l, kn_r, an_l, an_r, ft_l, ft_r })
            joint(p);
    }
    if (CS2::config.head_dot)
        dl->AddCircle(head, 8.f * sc, hd, 16, 1.6f);

    if (CS2::config.box || CS2::config.box_corner) {
        const float left = cx - 55.f * sc, right = cx + 55.f * sc;
        const float topb = top + 8.f * sc, bot = top + 310.f * sc;
        dl->AddRect(ImVec2(left, topb), ImVec2(right, bot), col4(CS2::config.col_box), 0.f, 0, 1.5f);
    }
    if (CS2::config.health_bar) {
        const float left = cx - 62.f * sc;
        dl->AddRectFilled(ImVec2(left - 5.f, top + 20.f * sc),
                          ImVec2(left - 2.f, top + 300.f * sc), IM_COL32(20, 20, 24, 200));
        dl->AddRectFilled(ImVec2(left - 5.f, top + 80.f * sc),
                          ImVec2(left - 2.f, top + 300.f * sc), col4(CS2::config.col_health));
    }
    if (CS2::config.armor_bar) {
        const float right = cx + 62.f * sc;
        dl->AddRectFilled(ImVec2(right + 2.f, top + 20.f * sc),
                          ImVec2(right + 5.f, top + 300.f * sc), IM_COL32(20, 20, 24, 200));
        dl->AddRectFilled(ImVec2(right + 2.f, top + 100.f * sc),
                          ImVec2(right + 5.f, top + 300.f * sc), col4(CS2::config.col_armor));
    }
    if (CS2::config.name)
        dl->AddText(ImVec2(cx - 20.f, top - 2.f), col4(CS2::config.col_name), "Jogador");
    if (CS2::config.distance)
        dl->AddText(ImVec2(cx - 12.f, top + 312.f * sc), col4(CS2::config.col_distance), "24m");

    ImGui::Dummy(ImVec2(width, height));
}

} // namespace

void DrawCs2Visuals() {
    const float full = CyberWidgets::CardContentWidth();
    const float gap = CyberTheme::Spacing::Sm;
    // Same proportions as FiveM VisualsPage
    const float previewWidth = std::clamp(full * 0.31f, CyberTheme::Px(250.0f), CyberTheme::Px(310.0f));
    const float settingsWidth = (std::max)(CyberTheme::Px(220.0f), full - previewWidth - gap * 2.0f);

    ImGui::BeginGroup();
    CyberWidgets::BeginCard("ESP DE JOGADORES", settingsWidth);
    CyberWidgets::ToggleSwitch("Ativar ESP", &CS2::config.esp_enabled);
    if (CS2::config.esp_enabled) {
        // Main elements — mirror FiveM density and order
        CyberWidgets::ToggleSwitch("Caixa", &CS2::config.box);
        CyberWidgets::ToggleSwitch("Esqueleto", &CS2::config.skeleton);
        if (CS2::config.skeleton) {
            CyberWidgets::ToggleSwitch("Pontos das articulações", &CS2::config.skeleton_joints);
            CyberWidgets::ToggleSwitch("Braços", &CS2::config.bone_draw_arms);
            CyberWidgets::ToggleSwitch("Pernas", &CS2::config.bone_draw_legs);
        }
        CyberWidgets::ToggleSwitch("Ponto na cabeça", &CS2::config.head_dot);
        CyberWidgets::ToggleSwitch("Vida", &CS2::config.health_bar);
        CyberWidgets::ToggleSwitch("Armadura", &CS2::config.armor_bar);
        CyberWidgets::ToggleSwitch("Nome", &CS2::config.name);
        CyberWidgets::ToggleSwitch("Distância", &CS2::config.distance);
        CyberWidgets::ToggleSwitch("Arma", &CS2::config.weapon_icons);

        CyberWidgets::SliderFloat("Distância máxima", &CS2::config.max_distance, 20.f, 500.f, "%.0f m");

        CyberWidgets::SectionTitle("ESPESSURA");
        CyberWidgets::SliderFloat("Esqueleto##th", &CS2::config.skeleton_thickness, 0.5f, 6.f, "%.1f");
        CyberWidgets::SliderFloat("Linhas guia##th", &CS2::config.snapline_thickness, 0.5f, 6.f, "%.1f");
        CyberWidgets::SliderFloat("Círculo cabeça", &CS2::config.head_circle_thickness, 0.5f, 6.f, "%.1f");
        CyberWidgets::SliderFloat("Caixa##th", &CS2::config.box_thickness, 0.5f, 6.f, "%.1f");
        CyberWidgets::SliderFloat("Eye Line##th", &CS2::config.eye_line_thickness, 0.5f, 6.f, "%.1f");
        if (CS2::config.trails)
            CyberWidgets::SliderFloat("Rastro##th", &CS2::config.trail_thickness, 1.f, 8.f, "%.1f");

        if (ImGui::CollapsingHeader("APARÊNCIA", ImGuiTreeNodeFlags_DefaultOpen)) {
            CyberWidgets::ToggleSwitch("Cores por visibilidade", &CS2::config.visibility_colors);
            if (CyberWidgets::Button("PERSONALIZAR CORES", CyberWidgets::ButtonStyle::Secondary, ImVec2(190.f, 32.f)))
                CyberWidgets::OpenModal("##cs2_esp_colors");
        }
        if (ImGui::CollapsingHeader("AVANÇADO")) {
            CyberWidgets::ToggleSwitch("Mostrar o jogador local", &CS2::config.self_esp);
            CyberWidgets::ToggleSwitch("Apenas alvos visíveis", &CS2::config.visible_check);
            CyberWidgets::ToggleSwitch("Ocultar equipa", &CS2::config.team_check);
            CyberWidgets::ToggleSwitch("Mostrar bots", &CS2::config.show_bots);
            CyberWidgets::ToggleSwitch("Caixa de cantos", &CS2::config.box_corner);
            CyberWidgets::ToggleSwitch("Linhas guia", &CS2::config.snaplines);
            CyberWidgets::ToggleSwitch("Auréola na cabeça", &CS2::config.head_halo);
            CyberWidgets::ToggleSwitch("Chapéu chinês 3D", &CS2::config.chinese_hat);
            CyberWidgets::ToggleSwitch("Rastros", &CS2::config.trails);
            if (CS2::config.trails)
                CyberWidgets::ToggleSwitch("Rastros arco-íris", &CS2::config.rainbow_trails);
            CyberWidgets::ToggleSwitch("Eye Line", &CS2::config.look_direction);
        }
    } else {
        CyberWidgets::TextLine("ESP desativado — ativa para configurar elementos.", CyberWidgets::TextTone::Secondary);
    }
    CyberWidgets::EndCard();
    ImGui::EndGroup();

    ImGui::SameLine(0.f, gap);
    ImGui::BeginGroup();
    CyberWidgets::BeginCard("PRÉ-VISUALIZAÇÃO", previewWidth);
    // No "Alvo visível" toggle — preview always shows a visible target like FiveM
    DrawDetailedPreview((std::max)(160.f, previewWidth - 20.f), 374.f, true);
    CyberWidgets::EndCard();
    ImGui::EndGroup();

if (CyberWidgets::BeginModal("##cs2_esp_colors", "PERSONALIZAR CORES", 520.f)) {
        CyberWidgets::SectionTitle("JOGADOR");
        ImGui::ColorEdit4("Visivel", CS2::config.col_visible, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        ImGui::ColorEdit4("Oculto", CS2::config.col_occluded, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        ImGui::ColorEdit4("Inimigo", CS2::config.col_enemy, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        ImGui::ColorEdit4("Equipa", CS2::config.col_team, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        CyberWidgets::Separator();
        CyberWidgets::SectionTitle("ESP");
        ImGui::ColorEdit4("Caixa", CS2::config.col_box, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        ImGui::ColorEdit4("Esqueleto", CS2::config.col_skeleton, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        ImGui::ColorEdit4("Articulacoes", CS2::config.col_joints, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        ImGui::ColorEdit4("Cabeca", CS2::config.col_head, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        ImGui::ColorEdit4("Aureola", CS2::config.col_halo, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        ImGui::ColorEdit4("Linhas guia", CS2::config.col_snaplines, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        CyberWidgets::Separator();
        CyberWidgets::SectionTitle("INFORMACAO");
        ImGui::ColorEdit4("Vida", CS2::config.col_health, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        ImGui::ColorEdit4("Armadura", CS2::config.col_armor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        ImGui::ColorEdit4("Nome", CS2::config.col_name, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        ImGui::ColorEdit4("Distancia", CS2::config.col_distance, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        ImGui::ColorEdit4("Arma", CS2::config.col_weapon, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        CyberWidgets::EndModal();
    }
}
