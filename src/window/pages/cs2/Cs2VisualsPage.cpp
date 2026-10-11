#include "../../widgets.h"
#include "../../theme.h"
#include "../../../launcher/launcher_assets.h"
#include "config/cs2_config.h"
#include "../common/EspPreview.h"
#include "imgui.h"

#include <algorithm>

namespace {

// ============================================================
// CS2 CONFIG ADAPTER FOR UNIFIED PREVIEW
// ============================================================

struct Cs2PreviewAdapter : EspPreview::IConfigAdapter {
    const EspPreview::PreviewConfig& GetPreviewConfig() const override {
        static EspPreview::PreviewConfig cfg;
        // Map CS2 config to unified preview config
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

        cfg.snaplines = CS2::config.snaplines;
        cfg.snapline_position = CS2::config.snapline_position;
        cfg.snapline_thickness = CS2::config.snapline_thickness;

        cfg.look_direction = CS2::config.look_direction;
        cfg.eye_line_thickness = CS2::config.eye_line_thickness;
        cfg.look_direction_length = CS2::config.look_direction_length;

        cfg.chinese_hat = CS2::config.chinese_hat;
        cfg.angel_wings = CS2::config.angel_wings;
        cfg.devil_horns = CS2::config.devil_horns;
        cfg.floating_crown = CS2::config.floating_crown;
        cfg.trails = CS2::config.trails;
        cfg.chinese_hat_scale = CS2::config.chinese_hat_scale;
        cfg.fun_effects_scale = CS2::config.fun_effects_scale;

        // Self ESP (was preview-only, now connected to real config)
        cfg.self_esp = CS2::config.self_esp;

        // Colors
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
        if (rgbMode) {
            float time = static_cast<float>(ImGui::GetTime()) * 0.35f;
            auto channel = [time](float phase) {
                return static_cast<int>((std::sin(time + phase) * 0.5f + 0.5f) * 255.f);
            };
            return IM_COL32(channel(0.f), channel(2.094f), channel(4.188f), 255);
        }
        if (visibilityColors && visibleCheck)
            return EspPreview::ToImU32(CS2::config.col_visible);
        return EspPreview::ToImU32(CS2::config.box_corner ? CS2::config.col_box_corner : CS2::config.col_box);
    }
};

static Cs2PreviewAdapter g_cs2Adapter;

} // namespace

void DrawCs2Visuals() {
    const float full = ImGui::GetContentRegionAvail().x;
    const float height = (std::max)(CyberTheme::Px(410.f), ImGui::GetContentRegionAvail().y);
    const float gap = CyberTheme::Metrics::GridGap;
    const float previewWidth = std::clamp(full * 0.31f, CyberTheme::Px(250.f), CyberTheme::Px(310.f));
    const float settingsWidth = (std::max)(CyberTheme::Px(220.f), full - previewWidth - gap * 2.f);
    const float featuresWidth = settingsWidth * 0.5f;
    const float colorsWidth = settingsWidth - featuresWidth;

    // ---- FEATURES COLUMN ----
    ImGui::BeginChild("##cs2_esp_features", ImVec2(featuresWidth, height), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    CyberWidgets::BeginCard("ESP DE JOGADORES");
    CyberWidgets::ToggleSwitch("Ativar ESP", &CS2::config.esp_enabled);
    CyberWidgets::Separator();
    ImGui::BeginDisabled(!CS2::config.esp_enabled);
    CyberWidgets::ToggleSwitch("ESP próprio", &CS2::config.self_esp); CyberWidgets::ToggleSwitch("Mostrar bots", &CS2::config.show_bots);
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
    CyberWidgets::ToggleSwitch("Caixa de cantos", &CS2::config.box_corner); CyberWidgets::ToggleSwitch("Linhas guia", &CS2::config.snaplines);
    CyberWidgets::ToggleSwitch("Halo na cabeça", &CS2::config.head_halo); CyberWidgets::ToggleSwitch("Chapéu chinês", &CS2::config.chinese_hat);
    CyberWidgets::ToggleSwitch("Asas de anjo", &CS2::config.angel_wings); CyberWidgets::ToggleSwitch("Chifres de demónio", &CS2::config.devil_horns);
    CyberWidgets::ToggleSwitch("Coroa flutuante", &CS2::config.floating_crown); CyberWidgets::ToggleSwitch("Rastros", &CS2::config.trails);
    CyberWidgets::ToggleSwitch("Eye Line", &CS2::config.look_direction);
    CyberWidgets::SectionTitle("ESPESSURA");
    CyberWidgets::SliderFloat("Caixa", &CS2::config.box_thickness, 0.5f, 4.f, "%.1f");
    CyberWidgets::ToggleSwitch("Preenchimento da caixa", &CS2::config.box_fill);
    CyberWidgets::SliderFloat("Arredondamento", &CS2::config.box_rounding, 0.f, 18.f, "%.0f");
    CyberWidgets::ToggleSwitch("Outline de texto", &CS2::config.text_outline);
    CyberWidgets::SliderFloat("Espessura outline", &CS2::config.text_outline_thickness, 0.5f, 2.f, "%.1f");
    CyberWidgets::SliderFloat("Círculo cabeça", &CS2::config.head_circle_thickness, 0.5f, 4.f, "%.1f");
    CyberWidgets::SliderFloat("Linhas guia", &CS2::config.snapline_thickness, 0.5f, 4.f, "%.1f");
    CyberWidgets::SliderFloat("Eye Line", &CS2::config.eye_line_thickness, 0.5f, 4.f, "%.1f");
    CyberWidgets::SliderFloat("Distância máxima", &CS2::config.max_distance, 20.f, 500.f, "%.0f m");
    ImGui::EndDisabled();
    CyberWidgets::EndCard();
    ImGui::EndChild();

    ImGui::SameLine(0.f, gap);

    // ---- COLORS COLUMN ----
    ImGui::BeginChild("##cs2_esp_colors", ImVec2(colorsWidth, height), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    CyberWidgets::BeginCard("PERSONALIZAÇÃO");
    ImGui::BeginDisabled(!CS2::config.esp_enabled);
    CyberWidgets::ToggleSwitch("Modo RGB", &CS2::config.rgb_mode);
    CyberWidgets::ToggleSwitch("Cores por visibilidade", &CS2::config.visibility_colors);
    CyberWidgets::SectionTitle("JOGADORES");
    ImGui::BeginDisabled(!CS2::config.visibility_colors || !CS2::config.visible_check);
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
    ImGui::BeginDisabled(!CS2::config.chinese_hat || !CS2::config.angel_wings || !CS2::config.devil_horns || !CS2::config.floating_crown);
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
    ImGui::ColorEdit4("Arma", CS2::config.col_weapon, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    CyberWidgets::EndCard();
    ImGui::EndChild();

    ImGui::SameLine(0.f, gap);

    // ---- PREVIEW PANEL ----
    ImGui::BeginGroup();
    CyberWidgets::BeginCard("", previewWidth);
    const float panelHeight = (std::max)(CyberTheme::Px(300.f), height - CyberTheme::Metrics::CardHeaderHeight - CyberTheme::Metrics::CardPadding * 2.f - CyberTheme::Px(12.f));
    const float drawWidth = (std::max)(CyberTheme::Px(180.f), previewWidth - CyberTheme::Metrics::CardPadding * 2.f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    EspPreview::DrawPreview(dl, origin, drawWidth, panelHeight, g_cs2Adapter, "");

    CyberWidgets::EndCard();
    ImGui::EndGroup();
}
