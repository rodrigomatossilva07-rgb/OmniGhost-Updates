#include "../../widgets.h"
#include "../../theme.h"
#include "../../../games/Cs2/config/cs2_config.h"
#include "../../../launcher/launcher_assets.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace {
// This instance is intentionally private to the menu. The Fortnite runtime does
// not read it; controls below change only the illustration in this page.
CS2::Config preview = [] {
    CS2::Config value{};
    value.esp_enabled = true;
    return value;
}();
bool showDead = false;
bool showKnocked = false;

ImU32 Color(const float c[4]) {
    return IM_COL32(static_cast<int>(c[0] * 255.f), static_cast<int>(c[1] * 255.f),
        static_cast<int>(c[2] * 255.f), static_cast<int>(c[3] * 255.f));
}

void DrawPreview(float width, float height) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 end(origin.x + width, origin.y + height);
    const float scale = (std::min)(width / 260.f, height / 370.f);
    const float cx = origin.x + width * .5f;
    const float top = origin.y + (std::max)(55.f, height * .17f);
    const float bottom = origin.y + height * .78f;
    const float half = 46.f * scale;
    const ImVec2 min(cx - half, top), max(cx + half, bottom);
    draw->AddRectFilled(origin, end, IM_COL32(8, 10, 13, 255), 8.f);
    draw->AddRect(origin, end, IM_COL32(202, 169, 64, 92), 8.f);
    for (int i = 1; i < 5; ++i) {
        const float x = origin.x + width * i / 5.f;
        draw->AddLine(ImVec2(x, origin.y), ImVec2(x, end.y), IM_COL32(255, 255, 255, 9));
    }
    const LauncherAssets::Texture portrait = LauncherAssets::FiveMEspPreview();
    if (portrait) {
        const float aspect = static_cast<float>(portrait.width) / static_cast<float>(portrait.height);
        const float portraitHeight = height - 20.f;
        const float portraitWidth = (std::min)(portraitHeight * aspect, width - 12.f);
        draw->AddImage(portrait.id,
            ImVec2(cx - portraitWidth * .5f, origin.y + 10.f),
            ImVec2(cx + portraitWidth * .5f, end.y - 10.f),
            ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), IM_COL32(255, 255, 255, 235));
    } else {
        const ImU32 body = IM_COL32(44, 49, 56, 255);
        draw->AddCircleFilled(ImVec2(cx, top + 25.f * scale), 16.f * scale, body, 24);
        draw->AddLine(ImVec2(cx, top + 45.f * scale), ImVec2(cx, bottom - 65.f * scale), body, 23.f * scale);
        draw->AddLine(ImVec2(cx, top + 78.f * scale), ImVec2(cx - 58.f * scale, top + 155.f * scale), body, 12.f * scale);
        draw->AddLine(ImVec2(cx, top + 78.f * scale), ImVec2(cx + 58.f * scale, top + 155.f * scale), body, 12.f * scale);
        draw->AddLine(ImVec2(cx, bottom - 67.f * scale), ImVec2(cx - 32.f * scale, bottom), body, 14.f * scale);
        draw->AddLine(ImVec2(cx, bottom - 67.f * scale), ImVec2(cx + 32.f * scale, bottom), body, 14.f * scale);
    }
    if (preview.esp_enabled) {
        ImU32 box = Color(preview.box_corner ? preview.col_box_corner : preview.col_box);
        if (preview.visibility_colors && preview.visible_check) box = Color(preview.col_visible);
        if (preview.rgb_mode) {
            const float t = static_cast<float>(ImGui::GetTime()) * .35f;
            box = IM_COL32(static_cast<int>((std::sin(t) * .5f + .5f) * 255),
                static_cast<int>((std::sin(t + 2.09f) * .5f + .5f) * 255),
                static_cast<int>((std::sin(t + 4.19f) * .5f + .5f) * 255), 255);
        }
        if (preview.box_fill) draw->AddRectFilled(min, max, Color(preview.col_box_fill), preview.box_rounding);
        if (preview.box) draw->AddRect(min, max, box, preview.box_rounding, 0, preview.box_thickness);
        if (preview.box_corner) {
            const float dx = half * .55f, dy = (bottom - top) * .16f;
            for (int sx : {-1, 1}) for (int sy : {-1, 1}) {
                const float x = cx + sx * half, y = sy < 0 ? top : bottom;
                draw->AddLine(ImVec2(x, y), ImVec2(x - sx * dx, y), box, preview.box_thickness);
                draw->AddLine(ImVec2(x, y), ImVec2(x, y - sy * dy), box, preview.box_thickness);
            }
        }
        if (preview.health_bar || preview.armor_bar) {
            const auto bar = [&](float x, float fraction, ImU32 c) {
                draw->AddRectFilled(ImVec2(x, top), ImVec2(x + 4.f, bottom), IM_COL32(15, 16, 17, 255));
                draw->AddRectFilled(ImVec2(x, bottom - (bottom - top) * fraction), ImVec2(x + 4.f, bottom), c);
            };
            if (preview.health_bar) bar(min.x - 9.f, .72f, Color(preview.col_health));
            if (preview.armor_bar) bar(max.x + 5.f, .46f, Color(preview.col_armor));
        }
        if (preview.health_value) draw->AddText(ImVec2(min.x - 31.f, bottom - 12.f), Color(preview.col_health), "72");
        if (preview.armor_value) draw->AddText(ImVec2(max.x + 13.f, bottom - 12.f), Color(preview.col_armor), "46");
        if (preview.skeleton) {
            const ImU32 c = Color(preview.col_skeleton);
            const ImVec2 head(cx, top + 25.f * scale), neck(cx, top + 52.f * scale);
            const ImVec2 chest(cx, top + 89.f * scale), pelvis(cx, bottom - 68.f * scale);
            const ImVec2 lArm(cx - 56.f * scale, top + 155.f * scale), rArm(cx + 56.f * scale, top + 155.f * scale);
            const ImVec2 lFoot(cx - 31.f * scale, bottom), rFoot(cx + 31.f * scale, bottom);
            const ImVec2 points[] = {head, neck, chest, pelvis, lArm, rArm, lFoot, rFoot};
            const int edges[][2] = {{0,1},{1,2},{2,3},{1,4},{1,5},{3,6},{3,7}};
            for (const auto& edge : edges) draw->AddLine(points[edge[0]], points[edge[1]], c, preview.skeleton_thickness);
            if (preview.skeleton_joints) for (const ImVec2 p : points) draw->AddCircleFilled(p, 3.f, Color(preview.col_joints));
        }
        if (preview.head_dot) draw->AddCircle(ImVec2(cx, top + 25.f * scale), 13.f * scale, Color(preview.col_head), 24, preview.head_circle_thickness);
        if (preview.head_halo) draw->AddCircle(ImVec2(cx, top + 3.f * scale), 16.f * scale, Color(preview.col_halo), 24, 2.f);
        if (preview.snaplines) draw->AddLine(ImVec2(cx, end.y), ImVec2(cx, bottom), Color(preview.col_snaplines), preview.snapline_thickness);
        if (preview.look_direction) draw->AddLine(ImVec2(cx, top + 25.f * scale), ImVec2(cx + 45.f * scale, top + 25.f * scale), Color(preview.col_look), preview.eye_line_thickness);
        if (preview.name) draw->AddText(ImVec2(cx - 34.f, top - 20.f), Color(preview.col_name), "PLAYER");
        if (preview.distance) draw->AddText(ImVec2(cx - 22.f, bottom + 8.f), Color(preview.col_distance), "42 m");
        if (preview.weapon_name) draw->AddText(ImVec2(cx - 47.f, bottom + 26.f), Color(preview.col_weapon), "Assault Rifle");
        if (preview.weapon_ammo) draw->AddText(ImVec2(cx - 16.f, bottom + 44.f), Color(preview.col_weapon), "30/120");
        if (preview.chinese_hat) draw->AddTriangle(ImVec2(cx, top - 33.f), ImVec2(cx - 24.f, top - 5.f), ImVec2(cx + 24.f, top - 5.f), Color(preview.col_fun_effects), 2.f);
        if (preview.devil_horns) {
            draw->AddLine(ImVec2(cx - 7.f, top + 8.f), ImVec2(cx - 21.f, top - 17.f), Color(preview.col_fun_effects), 2.f);
            draw->AddLine(ImVec2(cx + 7.f, top + 8.f), ImVec2(cx + 21.f, top - 17.f), Color(preview.col_fun_effects), 2.f);
        }
        if (preview.floating_crown) {
            const ImVec2 crown[] = {{cx-20.f,top-26.f},{cx-10.f,top-40.f},{cx,top-30.f},{cx+10.f,top-40.f},{cx+20.f,top-26.f}};
            draw->AddPolyline(crown, 5, Color(preview.col_fun_effects), false, 2.f);
        }
        if (preview.angel_wings) {
            draw->AddBezierCubic(ImVec2(cx-12.f,top+95.f), ImVec2(cx-80.f,top+25.f), ImVec2(cx-78.f,top+185.f), ImVec2(cx-24.f,top+170.f), Color(preview.col_fun_effects), 2.f);
            draw->AddBezierCubic(ImVec2(cx+12.f,top+95.f), ImVec2(cx+80.f,top+25.f), ImVec2(cx+78.f,top+185.f), ImVec2(cx+24.f,top+170.f), Color(preview.col_fun_effects), 2.f);
        }
        if (preview.trails) draw->AddLine(ImVec2(cx - 30.f, bottom), ImVec2(cx - 50.f, bottom + 24.f), Color(preview.col_trail), preview.trail_thickness);
        if (preview.hit_marker) {
            const ImVec2 mark(cx + half + 19.f, top + 50.f);
            for (int sx : {-1,1}) for (int sy : {-1,1}) draw->AddLine(ImVec2(mark.x + sx*4.f, mark.y + sy*4.f), ImVec2(mark.x + sx*9.f, mark.y + sy*9.f), box, 2.f);
        }
    }
    draw->AddText(ImVec2(origin.x + 12.f, origin.y + 12.f), IM_COL32(220, 205, 158, 255), "PREVIEW LOCAL");
    ImGui::Dummy(ImVec2(width, height));
}

void ColorControl(const char* label, float value[4]) {
    ImGui::ColorEdit4(label, value, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
}
}

void DrawFortniteVisuals() {
    const float full = ImGui::GetContentRegionAvail().x;
    const float gap = CyberTheme::Metrics::GridGap;
    const float height = (std::max)(CyberTheme::Px(440.f), ImGui::GetContentRegionAvail().y);
    const bool stacked = full < CyberTheme::Px(850.f);
    const float previewWidth = stacked ? full : std::clamp(full * .30f, CyberTheme::Px(250.f), CyberTheme::Px(310.f));
    const float columnWidth = stacked ? full : (full - previewWidth - 2.f * gap) * .5f;
    ImGui::BeginChild("##fn_visual_features", ImVec2(columnWidth, height), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    CyberWidgets::BeginCard("ESP DE JOGADORES");
    CyberWidgets::ToggleSwitch("Ativar pré-visualização", &preview.esp_enabled);
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("FILTROS");
    CyberWidgets::ToggleSwitch("ESP próprio", &preview.self_esp);
    CyberWidgets::ToggleSwitch("Jogadores bot", &preview.show_bots);
    CyberWidgets::ToggleSwitch("Verificação de visibilidade", &preview.visible_check);
    CyberWidgets::ToggleSwitch("Ocultar equipa", &preview.team_check);
    CyberWidgets::ToggleSwitch("Mostrar mortos", &showDead);
    CyberWidgets::ToggleSwitch("Mostrar caídos", &showKnocked);
    CyberWidgets::SectionTitle("ELEMENTOS");
    CyberWidgets::ToggleSwitch("Caixa 2D", &preview.box);
    CyberWidgets::ToggleSwitch("Caixa de cantos", &preview.box_corner);
    CyberWidgets::ToggleSwitch("Preenchimento da caixa", &preview.box_fill);
    CyberWidgets::ToggleSwitch("Esqueleto", &preview.skeleton);
    CyberWidgets::ToggleSwitch("Articulações", &preview.skeleton_joints);
    CyberWidgets::ToggleSwitch("Círculo na cabeça", &preview.head_dot);
    CyberWidgets::ToggleSwitch("Barra de vida", &preview.health_bar);
    CyberWidgets::ToggleSwitch("Valor de vida", &preview.health_value);
    CyberWidgets::ToggleSwitch("Barra de escudo", &preview.armor_bar);
    CyberWidgets::ToggleSwitch("Valor de escudo", &preview.armor_value);
    CyberWidgets::ToggleSwitch("Nome", &preview.name);
    CyberWidgets::ToggleSwitch("Distância", &preview.distance);
    CyberWidgets::ToggleSwitch("Nome da arma", &preview.weapon_name);
    CyberWidgets::ToggleSwitch("Munição", &preview.weapon_ammo);
    CyberWidgets::ToggleSwitch("Linhas guia", &preview.snaplines);
    CyberWidgets::SectionTitle("EFEITOS");
    CyberWidgets::ToggleSwitch("Halo na cabeça", &preview.head_halo);
    CyberWidgets::ToggleSwitch("Chapéu chinês", &preview.chinese_hat);
    CyberWidgets::ToggleSwitch("Asas de anjo", &preview.angel_wings);
    CyberWidgets::ToggleSwitch("Chifres de demónio", &preview.devil_horns);
    CyberWidgets::ToggleSwitch("Coroa flutuante", &preview.floating_crown);
    CyberWidgets::ToggleSwitch("Rastros", &preview.trails);
    CyberWidgets::ToggleSwitch("Eye Line", &preview.look_direction);
    CyberWidgets::ToggleSwitch("Hit marker", &preview.hit_marker);
    CyberWidgets::SectionTitle("AJUSTES");
    CyberWidgets::SliderFloat("Espessura da caixa", &preview.box_thickness, .5f, 4.f, "%.1f");
    CyberWidgets::SliderFloat("Arredondamento", &preview.box_rounding, 0.f, 18.f, "%.0f");
    CyberWidgets::SliderFloat("Esqueleto", &preview.skeleton_thickness, .5f, 4.f, "%.1f");
    CyberWidgets::SliderFloat("Círculo cabeça", &preview.head_circle_thickness, .5f, 4.f, "%.1f");
    CyberWidgets::SliderFloat("Linhas guia", &preview.snapline_thickness, .5f, 4.f, "%.1f");
    CyberWidgets::SliderFloat("Eye Line", &preview.eye_line_thickness, .5f, 4.f, "%.1f");
    CyberWidgets::SliderFloat("Distância máxima", &preview.max_distance, 20.f, 500.f, "%.0f m");
    CyberWidgets::ToggleSwitch("Outline de texto", &preview.text_outline);
    CyberWidgets::SliderFloat("Espessura outline", &preview.text_outline_thickness, .5f, 2.f, "%.1f");
    CyberWidgets::EndCard();
    ImGui::EndChild();
    if (!stacked) ImGui::SameLine(0.f, gap);
    ImGui::BeginChild("##fn_visual_colors", ImVec2(columnWidth, height), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    CyberWidgets::BeginCard("PERSONALIZAÇÃO");
    CyberWidgets::ToggleSwitch("Modo RGB", &preview.rgb_mode);
    CyberWidgets::ToggleSwitch("Cores por visibilidade", &preview.visibility_colors);
    CyberWidgets::SectionTitle("JOGADORES");
    ColorControl("Visível", preview.col_visible);
    ColorControl("Oculto", preview.col_occluded);
    ColorControl("Equipa", preview.col_team);
    CyberWidgets::SectionTitle("DESENHO");
    ColorControl("Caixa 2D", preview.col_box);
    ColorControl("Caixa de cantos", preview.col_box_corner);
    ColorControl("Preenchimento", preview.col_box_fill);
    ColorControl("Esqueleto", preview.col_skeleton);
    ColorControl("Articulações", preview.col_joints);
    ColorControl("Círculo na cabeça", preview.col_head);
    ColorControl("Linhas guia", preview.col_snaplines);
    ColorControl("Outline de texto", preview.col_text_outline);
    CyberWidgets::SectionTitle("INFORMAÇÃO");
    ColorControl("Nome", preview.col_name);
    ColorControl("Distância", preview.col_distance);
    ColorControl("Arma e munição", preview.col_weapon);
    ColorControl("Vida", preview.col_health);
    ColorControl("Escudo", preview.col_armor);
    CyberWidgets::SectionTitle("EFEITOS");
    ColorControl("Halo", preview.col_halo);
    ColorControl("Rastros", preview.col_trail);
    ColorControl("Eye Line", preview.col_look);
    ColorControl("Outros efeitos", preview.col_fun_effects);
    CyberWidgets::EndCard();
    ImGui::EndChild();
    if (!stacked) ImGui::SameLine(0.f, gap);
    ImGui::BeginChild("##fn_visual_preview", ImVec2(previewWidth, height), false);
    CyberWidgets::BeginCard("PRÉ-VISUALIZAÇÃO", previewWidth);
    DrawPreview((std::max)(CyberTheme::Px(180.f), previewWidth - CyberTheme::Metrics::CardPadding * 2.f),
        (std::max)(CyberTheme::Px(300.f), height - CyberTheme::Metrics::CardHeaderHeight - CyberTheme::Metrics::CardPadding * 2.f));
    CyberWidgets::EndCard();
    ImGui::EndChild();
}
