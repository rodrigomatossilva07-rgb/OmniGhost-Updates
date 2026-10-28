#include "../../widgets.h"
#include "../../theme.h"
#include "../../../launcher/launcher_assets.h"
#include "config/cs2_config.h"
#include "../common/EspPreview.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace {





struct Cs2PreviewAdapter : EspPreview::IConfigAdapter {
    const EspPreview::PreviewConfig& GetPreviewConfig() const override {
        static EspPreview::PreviewConfig cfg;

        cfg.esp_enabled = CS2::config.esp_enabled;
        cfg.box = CS2::config.box;
        cfg.box_corner = CS2::config.box_corner;
        cfg.box_fill = CS2::config.box_fill;
        cfg.box_thickness = CS2::config.box_thickness;
        cfg.box_rounding = CS2::config.box_rounding;

        cfg.skeleton = CS2::config.skeleton;
        cfg.skeleton_joints = CS2::config.skeleton_joints;
        cfg.skeleton_thickness = CS2::config.skeleton_thickness;

        cfg.head_dot = CS2::config.head_dot;
        cfg.head_halo = CS2::config.head_halo;
        cfg.head_circle_thickness = CS2::config.head_circle_thickness;

        cfg.health_bar = CS2::config.health_bar;
        cfg.armor_bar = CS2::config.armor_bar;
        cfg.health_value = CS2::config.health_value;
        cfg.armor_value = CS2::config.armor_value;

        cfg.name = CS2::config.name;
        cfg.distance = CS2::config.distance;
        cfg.weapon_name = CS2::config.weapon_name;
        cfg.weapon_ammo = CS2::config.weapon_ammo;
        cfg.text_outline = CS2::config.text_outline;
        cfg.text_outline_thickness = CS2::config.text_outline_thickness;
        cfg.layout_name_x = CS2::config.layout_name_x;
        cfg.layout_name_y = CS2::config.layout_name_y;
        cfg.layout_health_x = CS2::config.layout_health_x;
        cfg.layout_health_y = CS2::config.layout_health_y;
        cfg.layout_armor_x = CS2::config.layout_armor_x;
        cfg.layout_armor_y = CS2::config.layout_armor_y;
        cfg.layout_weapon_x = CS2::config.layout_weapon_x;
        cfg.layout_weapon_y = CS2::config.layout_weapon_y;
        cfg.layout_distance_x = CS2::config.layout_distance_x;
        cfg.layout_distance_y = CS2::config.layout_distance_y;
        cfg.layout_ammo_x = CS2::config.layout_ammo_x;
        cfg.layout_ammo_y = CS2::config.layout_ammo_y;

        cfg.snaplines = CS2::config.snaplines;
        cfg.snapline_position = CS2::config.snapline_position;
        cfg.snapline_thickness = CS2::config.snapline_thickness;

        cfg.look_direction = CS2::config.look_direction;
        cfg.eye_line_thickness = CS2::config.eye_line_thickness;
        cfg.look_direction_length = CS2::config.look_direction_length;

        cfg.chinese_hat = CS2::config.chinese_hat;
        cfg.angel_wings = false;
        cfg.devil_horns = false;
        cfg.floating_crown = false;
        cfg.trails = CS2::config.trails;
        cfg.chinese_hat_scale = CS2::config.chinese_hat_scale;
        cfg.fun_effects_scale = CS2::config.fun_effects_scale;
        for (int i = 0; i < 4; ++i) {
            cfg.col_box[i] = CS2::config.col_box[i];
            cfg.col_box_corner[i] = CS2::config.col_box_corner[i];
            cfg.col_box_fill[i] = CS2::config.col_box_fill[i];
            cfg.col_skeleton[i] = CS2::config.col_skeleton[i];
            cfg.col_joints[i] = CS2::config.col_joints[i];
            cfg.col_head[i] = CS2::config.col_head[i];
            cfg.col_halo[i] = CS2::config.col_halo[i];
            cfg.col_health[i] = CS2::config.col_health[i];
            cfg.col_armor[i] = CS2::config.col_armor[i];
            cfg.col_name[i] = CS2::config.col_name[i];
            cfg.col_distance[i] = CS2::config.col_distance[i];
            cfg.col_weapon[i] = CS2::config.col_weapon[i];
            cfg.col_snaplines[i] = CS2::config.col_snaplines[i];
            cfg.col_trail[i] = CS2::config.col_trail[i];
            cfg.col_fun_effects[i] = CS2::config.col_fun_effects[i];
            cfg.col_look[i] = CS2::config.col_look[i];
            cfg.col_text_outline[i] = CS2::config.col_text_outline[i];
            cfg.col_visible[i] = CS2::config.col_visible[i];
            cfg.col_occluded[i] = CS2::config.col_occluded[i];
            cfg.col_team[i] = CS2::config.col_team[i];
        }

        cfg.rgb_mode = CS2::config.rgb_mode;
        cfg.visibility_colors = CS2::config.visibility_colors;
        cfg.visible_check = CS2::config.visible_check;
        cfg.team_check = CS2::config.team_check;

        return cfg;
    }

    ImU32 GetBoxColor(bool rgbMode, bool visibilityColors, bool visibleCheck) const override {
        (void)visibleCheck;
        if (rgbMode) {
            float time = static_cast<float>(ImGui::GetTime()) * 0.35f;
            auto channel = [time](float phase) {
                return static_cast<int>((std::sin(time + phase) * 0.5f + 0.5f) * 255.f);
            };
            return IM_COL32(channel(0.f), channel(2.094f), channel(4.188f), 255);
        }
        if (visibilityColors)
            return EspPreview::ToImU32(CS2::config.col_visible);
        return EspPreview::ToImU32(CS2::config.box_corner ? CS2::config.col_box_corner : CS2::config.col_box);
    }
};

static Cs2PreviewAdapter g_cs2Adapter;

void DrawLayoutBuilder(const ImVec2& origin, float width, float height) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const EspPreview::PlayerGeometry geometry = EspPreview::BuildGeometry(origin, width, height);
    const ImVec2 boxMin = geometry.boundsMin;
    const ImVec2 boxMax = geometry.boundsMax;
    const ImVec2 restoreCursor = ImGui::GetCursorScreenPos();

    struct Item { const char* sample; float* x; float* y; bool enabled; bool verticalBar; };
    Item items[] = {
        { "Enemy", &CS2::config.layout_name_x, &CS2::config.layout_name_y, CS2::config.name, false },
        { "75 HP", &CS2::config.layout_health_x, &CS2::config.layout_health_y, CS2::config.health_bar, true },
        { "65 AP", &CS2::config.layout_armor_x, &CS2::config.layout_armor_y, CS2::config.armor_bar, true },
        { "AK-47", &CS2::config.layout_weapon_x, &CS2::config.layout_weapon_y, CS2::config.weapon_name, false },
        { "18m", &CS2::config.layout_distance_x, &CS2::config.layout_distance_y, CS2::config.distance, false },
        { "24 / 30", &CS2::config.layout_ammo_x, &CS2::config.layout_ammo_y, CS2::config.weapon_ammo, false }
    };
    for (int index = 0; index < static_cast<int>(std::size(items)); ++index) {
        if (!items[index].enabled) continue;
        const ImVec2 text = ImGui::CalcTextSize(items[index].sample);
        ImVec2 pos(boxMin.x + *items[index].x * (boxMax.x - boxMin.x) - text.x * .5f,
                   boxMin.y + *items[index].y * (boxMax.y - boxMin.y) - text.y * .5f);
        const ImVec2 hitSize = items[index].verticalBar
            ? ImVec2((std::max)(18.f, text.x), boxMax.y - boxMin.y)
            : ImVec2(text.x + 12.f, text.y + 10.f);
        if (items[index].verticalBar)
            pos.y = boxMin.y;
        ImGui::SetCursorScreenPos(pos);
        ImGui::PushID(index);
        ImGui::InvisibleButton("##layout_item", hitSize);
        const bool active = ImGui::IsItemActive();
        if (active || ImGui::IsItemHovered())
            draw->AddRect(pos, ImVec2(pos.x + hitSize.x, pos.y + hitSize.y),
                active ? IM_COL32(229, 184, 55, 180) : IM_COL32(229, 184, 55, 85), 4.f, 0, 1.f);
        if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            *items[index].x = std::clamp((mouse.x - boxMin.x) / (boxMax.x - boxMin.x), -.20f, 1.20f);
            *items[index].y = std::clamp((mouse.y - boxMin.y) / (boxMax.y - boxMin.y), -.20f, 1.20f);
        }
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(restoreCursor);
}

}

void DrawCs2Visuals() {
    const float full = ImGui::GetContentRegionAvail().x;
    const float height = (std::max)(CyberTheme::Px(410.f), ImGui::GetContentRegionAvail().y);
    const float gap = CyberTheme::Metrics::GridGap;
    const float previewWidth = std::clamp(full * 0.31f, CyberTheme::Px(250.f), CyberTheme::Px(310.f));
    const float settingsWidth = (std::max)(CyberTheme::Px(220.f), full - previewWidth - gap * 2.f);
    const float featuresWidth = settingsWidth * 0.5f;
    const float colorsWidth = settingsWidth - featuresWidth;


    ImGui::BeginChild("##cs2_esp_features", ImVec2(featuresWidth, height), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    CyberWidgets::BeginCard("ESP DE JOGADORES");
    CyberWidgets::ToggleSwitch("Ativar ESP", &CS2::config.esp_enabled);
    CyberWidgets::Separator();
    ImGui::BeginDisabled(!CS2::config.esp_enabled);
    CyberWidgets::ToggleSwitch("Mostrar bots", &CS2::config.show_bots);
    CyberWidgets::ToggleSwitch("Verificação de visibilidade", &CS2::config.visible_check);
    CyberWidgets::ToggleSwitch("Ocultar equipa", &CS2::config.team_check);
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("ELEMENTOS");
    CyberWidgets::ToggleSwitch("Esqueleto", &CS2::config.skeleton); CyberWidgets::ToggleSwitch("Articulações", &CS2::config.skeleton_joints);
    CyberWidgets::ToggleSwitch("Círculo na cabeça", &CS2::config.head_dot); CyberWidgets::ToggleSwitch("Barra de vida", &CS2::config.health_bar);
    CyberWidgets::ToggleSwitch("Barra de armadura", &CS2::config.armor_bar); CyberWidgets::ToggleSwitch("Nome da arma", &CS2::config.weapon_name);
    CyberWidgets::ToggleSwitch("Munição", &CS2::config.weapon_ammo); CyberWidgets::ToggleSwitch("Nome", &CS2::config.name);
    CyberWidgets::ToggleSwitch("Distância", &CS2::config.distance); CyberWidgets::ToggleSwitch("Valor de vida", &CS2::config.health_value);
    CyberWidgets::ToggleSwitch("Valor de armadura", &CS2::config.armor_value); CyberWidgets::ToggleSwitch("Caixa 2D", &CS2::config.box);
    CyberWidgets::ToggleSwitch("Caixa de cantos", &CS2::config.box_corner); CyberWidgets::ToggleSwitch("Preenchimento da caixa", &CS2::config.box_fill);
    CyberWidgets::ToggleSwitch("Outline de texto", &CS2::config.text_outline); CyberWidgets::ToggleSwitch("Linhas guia", &CS2::config.snaplines);
    CyberWidgets::ToggleSwitch("Halo na cabeça", &CS2::config.head_halo); CyberWidgets::ToggleSwitch("Chapéu chinês", &CS2::config.chinese_hat);
    CS2::config.angel_wings = false; CS2::config.devil_horns = false; CS2::config.floating_crown = false;
    CyberWidgets::ToggleSwitch("Rastros", &CS2::config.trails);
    CyberWidgets::ToggleSwitch("Eye Line", &CS2::config.look_direction);
    CyberWidgets::ToggleSwitch("Setas fora do ecrã", &CS2::config.offscreen_arrows);
    CyberWidgets::ToggleSwitch("C4 equipada", &CS2::config.c4_carrier_esp);
    if (CS2::config.c4_carrier_esp)
        ImGui::TextDisabled("Confirma o portador quando a C4 está ativa; sem assumir inventário desconhecido.");
    CyberWidgets::ToggleSwitch("Painel de funções ativas", &CS2::config.active_features_panel);
    ImGui::EndDisabled();
    CyberWidgets::EndCard();
    ImGui::EndChild();

    ImGui::SameLine(0.f, gap);


    ImGui::BeginChild("##cs2_esp_colors", ImVec2(colorsWidth, height), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    CyberWidgets::BeginCard("PERSONALIZAÇÃO");
    ImGui::BeginDisabled(!CS2::config.esp_enabled);
    CyberWidgets::ToggleSwitch("Modo RGB", &CS2::config.rgb_mode);
    CyberWidgets::ToggleSwitch("Cores por visibilidade", &CS2::config.visibility_colors);
    CyberWidgets::SectionTitle("AJUSTES");
    CyberWidgets::SliderFloat("Distância máxima", &CS2::config.max_distance, 20.f, 500.f, "%.0f m");
    if (CS2::config.box || CS2::config.box_corner) {
        CyberWidgets::SliderFloat("Espessura da caixa", &CS2::config.box_thickness, 0.5f, 4.f, "%.1f");
        CyberWidgets::SliderFloat("Arredondamento da caixa", &CS2::config.box_rounding, 0.f, 18.f, "%.0f px");
        CyberWidgets::SliderFloat("Margem da caixa", &CS2::config.box_margin, 0.f, 16.f, "%.0f px");
        if (CS2::config.box_corner)
            CyberWidgets::SliderFloat("Comprimento dos cantos", &CS2::config.box_corner_length, .10f, .50f, "%.2f");
        if (CS2::config.box_fill)
            CyberWidgets::SliderFloat("Opacidade do preenchimento", &CS2::config.box_fill_opacity, 0.f, .85f, "%.2f");
    }
    if (CS2::config.skeleton) {
        CyberWidgets::SliderFloat("Espessura do esqueleto", &CS2::config.skeleton_thickness, 0.5f, 4.f, "%.1f");
        CyberWidgets::SliderFloat("Opacidade do esqueleto", &CS2::config.skeleton_opacity, .10f, 1.f, "%.2f");
        if (CS2::config.skeleton_joints)
            CyberWidgets::SliderFloat("Tamanho das articulações", &CS2::config.skeleton_joint_size, .6f, 5.f, "%.1f px");
    }
    if (CS2::config.head_dot) {
        CyberWidgets::SliderFloat("Espessura do círculo", &CS2::config.head_circle_thickness, 0.5f, 4.f, "%.1f");
        CyberWidgets::SliderFloat("Escala do círculo", &CS2::config.head_circle_scale, .50f, 2.f, "%.2fx");
        ImGui::SliderInt("Segmentos do círculo", &CS2::config.head_circle_segments, 8, 64);
    }
    if (CS2::config.health_bar || CS2::config.armor_bar) {
        CyberWidgets::SliderFloat("Largura das barras", &CS2::config.bar_width, 2.f, 12.f, "%.1f px");
        CyberWidgets::SliderFloat("Espessura das barras", &CS2::config.bar_thickness, .5f, 3.f, "%.1f px");
        CyberWidgets::SliderFloat("Espaçamento das barras", &CS2::config.bar_spacing, 0.f, 16.f, "%.1f px");
    }
    if (CS2::config.name || CS2::config.weapon_name || CS2::config.distance || CS2::config.weapon_ammo) {
        CyberWidgets::SliderFloat("Tamanho do texto", &CS2::config.layout_text_scale, .65f, 1.6f, "%.2fx");
        CyberWidgets::SliderFloat("Espaçamento do texto", &CS2::config.layout_spacing, 0.f, 18.f, "%.1f px");
    }
    if (CS2::config.text_outline)
        CyberWidgets::SliderFloat("Espessura do outline", &CS2::config.text_outline_thickness, 0.5f, 2.f, "%.1f");
    if (CS2::config.snaplines) {
        CyberWidgets::SliderFloat("Espessura das linhas guia", &CS2::config.snapline_thickness, 0.5f, 4.f, "%.1f");
        CyberWidgets::SliderFloat("Opacidade das linhas guia", &CS2::config.snapline_opacity, .1f, 1.f, "%.2f");
        const char* origins[] = { "Topo", "Centro", "Fundo" };
        ImGui::Combo("Origem das linhas guia", &CS2::config.snapline_position, origins, 3);
    }
    if (CS2::config.look_direction) {
        CyberWidgets::SliderFloat("Espessura da Eye Line", &CS2::config.eye_line_thickness, 0.5f, 4.f, "%.1f");
        CyberWidgets::SliderFloat("Comprimento da Eye Line", &CS2::config.look_direction_length, 20.f, 220.f, "%.0f px");
        CyberWidgets::SliderFloat("Opacidade da Eye Line", &CS2::config.eye_line_opacity, .1f, 1.f, "%.2f");
    }
    if (CS2::config.head_halo) {
        CyberWidgets::SliderFloat("Tamanho do halo", &CS2::config.halo_scale, .5f, 2.f, "%.2fx");
        CyberWidgets::SliderFloat("Espessura do halo", &CS2::config.halo_thickness, .5f, 5.f, "%.1f");
        CyberWidgets::SliderFloat("Altura do halo", &CS2::config.halo_height, .4f, 2.f, "%.2fx");
    }
    if (CS2::config.chinese_hat) {
        CyberWidgets::SliderFloat("Escala do chapéu", &CS2::config.chinese_hat_scale, 0.5f, 2.f, "%.1fx");
        CyberWidgets::SliderFloat("Altura do chapéu", &CS2::config.chinese_hat_height, .5f, 1.8f, "%.2fx");
        CyberWidgets::SliderFloat("Largura do chapéu", &CS2::config.chinese_hat_width, .5f, 1.8f, "%.2fx");
        CyberWidgets::SliderFloat("Velocidade do chapéu", &CS2::config.chinese_hat_rotation_speed, .1f, 3.f, "%.2fx");
    }
    if (CS2::config.trails) {
        CyberWidgets::SliderFloat("Tempo visível dos rastros", &CS2::config.trail_duration, 0.5f, 10.f, "%.1f s");
        CyberWidgets::SliderFloat("Espessura dos rastros", &CS2::config.trail_thickness, 0.5f, 5.f, "%.1f");
        CyberWidgets::SliderFloat("Opacidade dos rastros", &CS2::config.trail_opacity, .1f, 1.f, "%.2f");
        CyberWidgets::SliderFloat("Suavização dos rastros", &CS2::config.trail_smoothing, 0.f, 1.f, "%.2f");
    }
    if (CS2::config.offscreen_arrows) {
        CyberWidgets::SliderFloat("Tamanho das setas", &CS2::config.offscreen_arrow_size, 6.f, 32.f, "%.0f px");
        CyberWidgets::SliderFloat("Margem das setas", &CS2::config.offscreen_arrow_margin, 12.f, 160.f, "%.0f px");
        CyberWidgets::SliderFloat("Alcance das setas", &CS2::config.offscreen_arrow_max_distance, 10.f, 500.f, "%.0f m");
        CyberWidgets::SliderFloat("Opacidade das setas", &CS2::config.offscreen_arrow_opacity, .1f, 1.f, "%.2f");
        CyberWidgets::SliderFloat("Espessura das setas", &CS2::config.offscreen_arrow_thickness, .5f, 5.f, "%.1f");
    }
    CyberWidgets::ToggleSwitch("Escala por distância", &CS2::config.esp_distance_scaling);
    if (CS2::config.esp_distance_scaling)
        CyberWidgets::SliderFloat("Tamanho mínimo do ESP", &CS2::config.esp_min_scale, .25f, 1.f, "%.2fx");
    CyberWidgets::SliderFloat("Suavização visual", &CS2::config.esp_visual_smoothing, 0.f, 1.f, "%.2f");
    if (CS2::config.active_features_panel) {
        CyberWidgets::SliderFloat("Escala do painel", &CS2::config.active_features_scale, .65f, 1.6f, "%.2fx");
        CyberWidgets::SliderFloat("Opacidade do painel", &CS2::config.active_features_opacity, .15f, 1.f, "%.2f");
    }
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("JOGADORES");
    ImGui::BeginDisabled(!CS2::config.visibility_colors);
    ImGui::ColorEdit4("Visível", CS2::config.col_visible, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Oculto", CS2::config.col_occluded, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(CS2::config.team_check);
    ImGui::ColorEdit4("Equipa", CS2::config.col_team, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::EndDisabled();
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("DESENHO");
    ImGui::BeginDisabled(!CS2::config.skeleton);
    ImGui::ColorEdit4("Esqueleto", CS2::config.col_skeleton, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!CS2::config.skeleton || !CS2::config.skeleton_joints);
    ImGui::ColorEdit4("Articulações", CS2::config.col_joints, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!CS2::config.head_dot);
    ImGui::ColorEdit4("Círculo na cabeça", CS2::config.col_head, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::EndDisabled();
    ImGui::ColorEdit4("Caixa 2D", CS2::config.col_box, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Caixa de cantos", CS2::config.col_box_corner, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Preenchimento", CS2::config.col_box_fill, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Outline de texto", CS2::config.col_text_outline, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Setas offscreen", CS2::config.col_offscreen_arrow, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::BeginDisabled(!CS2::config.snaplines);
    ImGui::ColorEdit4("Linhas guia", CS2::config.col_snaplines, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!CS2::config.weapon_name);
    ImGui::ColorEdit4("Arma", CS2::config.col_weapon, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!CS2::config.head_halo);
    ImGui::ColorEdit4("Halo", CS2::config.col_halo, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!CS2::config.trails);
    ImGui::ColorEdit4("Rastros", CS2::config.col_trail, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!CS2::config.look_direction);
    ImGui::ColorEdit4("Eye Line", CS2::config.col_look, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::EndDisabled();


    ImGui::BeginDisabled(!CS2::config.chinese_hat && !CS2::config.angel_wings && !CS2::config.devil_horns && !CS2::config.floating_crown);
    ImGui::ColorEdit4("Efeitos", CS2::config.col_fun_effects, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::EndDisabled();
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("INFORMAÇÃO");
    ImGui::BeginDisabled(!CS2::config.health_bar);
    ImGui::ColorEdit4("Vida", CS2::config.col_health, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!CS2::config.armor_bar);
    ImGui::ColorEdit4("Armadura", CS2::config.col_armor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!CS2::config.distance);
    ImGui::ColorEdit4("Distância", CS2::config.col_distance, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!CS2::config.weapon_name);
    ImGui::ColorEdit4("Arma##info", CS2::config.col_weapon, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    CyberWidgets::EndCard();
    ImGui::EndChild();

    ImGui::SameLine(0.f, gap);


    ImGui::BeginChild("##cs2_esp_preview_fixed", ImVec2(previewWidth, height), false,
        ImGuiWindowFlags_NoScrollbar);



    ImGui::SetScrollY(0.0f);
    CyberWidgets::BeginCard("PREVIEW & ESP BUILDER", previewWidth);
    CyberWidgets::TextLine("Arrasta os rótulos na pré-visualização.", CyberWidgets::TextTone::Secondary);
    const float panelHeight = (std::max)(CyberTheme::Px(220.f), height - CyberTheme::Metrics::CardHeaderHeight - CyberTheme::Metrics::CardPadding * 2.f - CyberTheme::Px(116.f));
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float drawWidth = (std::max)(CyberTheme::Px(180.f), (std::min)(availableWidth,
        previewWidth - CyberTheme::Metrics::CardPadding * 2.f));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    origin.x += (std::max)(0.0f, (availableWidth - drawWidth) * 0.5f);

    EspPreview::DrawPreview(dl, origin, drawWidth, panelHeight, g_cs2Adapter, "");
    DrawLayoutBuilder(origin, drawWidth, panelHeight);

    CyberWidgets::EndCard();
    ImGui::EndChild();
}
