#include "../../widgets.h"
#include "../../theme.h"
#include "../../../launcher/launcher_assets.h"
#include "../../../games/Fivem/esp/esp.h"
#include "../../../ui/ui_models.h"
#include "../common/EspPreview.h"
#include "imgui.h"

#include <algorithm>

namespace {





struct FivemPreviewAdapter : EspPreview::IConfigAdapter {
    const EspPreview::PreviewConfig& GetPreviewConfig() const override {
        static EspPreview::PreviewConfig cfg;

        cfg.esp_enabled = esp::config.enabled;
        cfg.box = esp::config.box_2d;
        cfg.box_corner = esp::config.corner_box;
        cfg.box_fill = esp::config.filled_box;
        cfg.box_thickness = esp::config.box_thickness;
        cfg.box_rounding = 0.f;

        cfg.skeleton = esp::config.skeleton;
        cfg.skeleton_joints = esp::config.joints;
        cfg.skeleton_thickness = esp::config.skeleton_thickness;

        cfg.head_dot = esp::config.head_circle;
        cfg.head_halo = esp::config.head_halo;
        cfg.head_circle_thickness = esp::config.head_circle_thickness;

        cfg.health_bar = esp::config.health_bar;
        cfg.armor_bar = esp::config.armor_bar;
        cfg.health_value = false;
        cfg.armor_value = false;

        cfg.name = esp::config.player_name;
        cfg.distance = esp::config.distance;
        cfg.weapon_name = esp::config.weapon_name;
        cfg.weapon_ammo = false;
        cfg.text_outline = true;
        cfg.text_outline_thickness = 1.f;

        cfg.snaplines = esp::config.snaplines;
        cfg.snapline_position = esp::config.snapline_pos;
        cfg.snapline_thickness = esp::config.snapline_thickness;

        cfg.look_direction = esp::config.look_direction;
        cfg.eye_line_thickness = esp::config.eye_line_thickness;
        cfg.look_direction_length = esp::config.look_direction_length;

        cfg.chinese_hat = esp::config.chinese_hat;
        cfg.angel_wings = esp::config.angel_wings;
        cfg.devil_horns = esp::config.devil_horns;
        cfg.floating_crown = esp::config.floating_crown;
        cfg.trails = esp::config.trails;
        cfg.chinese_hat_scale = esp::config.chinese_hat_scale;
        cfg.fun_effects_scale = esp::config.fun_effects_scale;


        auto u32ToFloat = [](ImU32 c, float out[4]) {
            out[0] = ((c >> 0) & 0xFF) / 255.f;
            out[1] = ((c >> 8) & 0xFF) / 255.f;
            out[2] = ((c >> 16) & 0xFF) / 255.f;
            out[3] = ((c >> 24) & 0xFF) / 255.f;
        };

        u32ToFloat(esp::config.color_box_2d, cfg.col_box);
        u32ToFloat(esp::config.color_corner_box, cfg.col_box_corner);
        u32ToFloat(esp::config.color_box_2d, cfg.col_box_fill);
        u32ToFloat(esp::config.color_skeleton, cfg.col_skeleton);
        u32ToFloat(esp::config.color_skeleton_points, cfg.col_joints);
        u32ToFloat(esp::config.color_head_circle, cfg.col_head);
        u32ToFloat(esp::config.color_halo, cfg.col_halo);
        u32ToFloat(esp::config.color_health, cfg.col_health);
        u32ToFloat(esp::config.color_armor, cfg.col_armor);
        u32ToFloat(esp::config.color_name, cfg.col_name);
        u32ToFloat(esp::config.color_distance, cfg.col_distance);
        u32ToFloat(esp::config.color_weapon, cfg.col_weapon);
        u32ToFloat(esp::config.color_snaplines, cfg.col_snaplines);
        u32ToFloat(esp::config.color_trail, cfg.col_trail);
        u32ToFloat(esp::config.color_fun_effects, cfg.col_fun_effects);
        u32ToFloat(esp::config.color_look_direction, cfg.col_look);
        u32ToFloat(IM_COL32(0, 0, 0, 255), cfg.col_text_outline);
        u32ToFloat(esp::config.color_visible, cfg.col_visible);
        u32ToFloat(esp::config.color_invisible, cfg.col_occluded);
        u32ToFloat(esp::config.color_team, cfg.col_team);

        cfg.rgb_mode = esp::config.rgb_mode;
        cfg.visibility_colors = esp::config.visibility_colors;
        cfg.visible_check = esp::config.visible_check;
        cfg.team_check = esp::config.team_check;

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
            return esp::config.color_visible;
        return esp::config.corner_box ? esp::config.color_corner_box : esp::config.color_box_2d;
    }
};

static FivemPreviewAdapter g_fivemAdapter;

}

void DrawFivemVisuals() {
    const float full = ImGui::GetContentRegionAvail().x;
    const float gap = CyberTheme::Metrics::GridGap;
    const float childHeight = (std::max)(CyberTheme::Px(440.f), ImGui::GetContentRegionAvail().y);
    const bool stacked = full < CyberTheme::Px(850.f);
    const float previewWidth = stacked ? full : std::clamp(full * 0.30f, CyberTheme::Px(250.f), CyberTheme::Px(310.f));
    const float columnWidth = stacked ? full : (full - previewWidth - 2.f * gap) * 0.5f;


    ImGui::BeginChild("##fm_visual_features", ImVec2(columnWidth, childHeight), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    CyberWidgets::BeginCard("ESP DE JOGADORES");
    CyberWidgets::ToggleSwitch("Ativar ESP", &esp::config.enabled);
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("FILTROS");
    CyberWidgets::ToggleSwitch("ESP próprio", &esp::config.self_esp);
    CyberWidgets::ToggleSwitch("Verificação de visibilidade", &esp::config.visible_check);
    CyberWidgets::ToggleSwitch("Cores por visibilidade", &esp::config.visibility_colors);
    CyberWidgets::ToggleSwitch("Ocultar equipa", &esp::config.team_check);
    CyberWidgets::ToggleSwitch("Mostrar mortos", &esp::config.show_dead);



    CyberWidgets::ToggleSwitch("NPCs", &esp::config.npc_esp);
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("ELEMENTOS");
    CyberWidgets::ToggleSwitch("Caixa 2D", &esp::config.box_2d);
    CyberWidgets::ToggleSwitch("Caixa de cantos", &esp::config.corner_box);
    CyberWidgets::ToggleSwitch("Preenchimento da caixa", &esp::config.filled_box);
    CyberWidgets::ToggleSwitch("Esqueleto", &esp::config.skeleton);
    CyberWidgets::ToggleSwitch("Articulações", &esp::config.joints);
    CyberWidgets::ToggleSwitch("Círculo na cabeça", &esp::config.head_circle);
    CyberWidgets::ToggleSwitch("Barra de vida", &esp::config.health_bar);
    CyberWidgets::ToggleSwitch("Barra de armadura", &esp::config.armor_bar);
    CyberWidgets::ToggleSwitch("Nome da arma", &esp::config.weapon_name);
    CyberWidgets::ToggleSwitch("Nome", &esp::config.player_name);
    CyberWidgets::ToggleSwitch("ID", &esp::config.player_id);
    CyberWidgets::ToggleSwitch("Distância", &esp::config.distance);
    CyberWidgets::ToggleSwitch("Linhas guia", &esp::config.snaplines);
    CyberWidgets::SectionTitle("EFEITOS");
    CyberWidgets::ToggleSwitch("Halo na cabeça", &esp::config.head_halo);
    CyberWidgets::ToggleSwitch("Chapéu chinês", &esp::config.chinese_hat);
    CyberWidgets::ToggleSwitch("Asas de anjo", &esp::config.angel_wings);
    CyberWidgets::ToggleSwitch("Chifres de demónio", &esp::config.devil_horns);
    CyberWidgets::ToggleSwitch("Coroa flutuante", &esp::config.floating_crown);
    CyberWidgets::ToggleSwitch("Rastros", &esp::config.trails);
    CyberWidgets::ToggleSwitch("Eye Line", &esp::config.look_direction);
    CyberWidgets::ToggleSwitch("Hit marker", &esp::config.hit_marker);
    CyberWidgets::SectionTitle("AJUSTES");
    CyberWidgets::SliderFloat("Espessura da caixa", &esp::config.box_thickness, 0.5f, 4.f, "%.1f");
    CyberWidgets::SliderFloat("Esqueleto", &esp::config.skeleton_thickness, 0.5f, 4.f, "%.1f");
    CyberWidgets::SliderFloat("Círculo cabeça", &esp::config.head_circle_thickness, 0.5f, 4.f, "%.1f");
    CyberWidgets::SliderFloat("Linhas guia", &esp::config.snapline_thickness, 0.5f, 4.f, "%.1f");
    CyberWidgets::SliderFloat("Eye Line", &esp::config.eye_line_thickness, 0.5f, 4.f, "%.1f");
    CyberWidgets::SliderFloat("Distância máxima", &esp::config.max_esp_distance, 20.f, 500.f, "%.0f m");
    CyberWidgets::SliderFloat("Escala chapéu", &esp::config.chinese_hat_scale, 0.5f, 2.f, "%.1f");
    CyberWidgets::SliderFloat("Escala efeitos", &esp::config.fun_effects_scale, 0.5f, 2.f, "%.1f");
    CyberWidgets::SliderFloat("Duração rastros", &esp::config.trail_duration, 0.1f, 3.f, "%.1f");
    CyberWidgets::SliderFloat("Espessura rastros", &esp::config.trail_thickness, 0.5f, 5.f, "%.1f");
    CyberWidgets::EndCard();
    ImGui::EndChild();

    if (!stacked) ImGui::SameLine(0.f, gap);


    ImGui::BeginChild("##fm_visual_colors", ImVec2(columnWidth, childHeight), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    CyberWidgets::BeginCard("PERSONALIZAÇÃO");
    CyberWidgets::ToggleSwitch("Modo RGB", &esp::config.rgb_mode);
    CyberWidgets::ToggleSwitch("Cores por visibilidade", &esp::config.visibility_colors);
    CyberWidgets::SectionTitle("JOGADORES");
    ImGui::ColorEdit4("Visível", (float*)&esp::config.color_visible, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Oculto", (float*)&esp::config.color_invisible, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Morto", (float*)&esp::config.color_dead, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);

    ImGui::ColorEdit4("Equipa", (float*)&esp::config.color_team, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("DESENHO");
    ImGui::ColorEdit4("Caixa 2D", (float*)&esp::config.color_box_2d, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Caixa de cantos", (float*)&esp::config.color_corner_box, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Esqueleto", (float*)&esp::config.color_skeleton, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Articulações", (float*)&esp::config.color_skeleton_points, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Círculo na cabeça", (float*)&esp::config.color_head_circle, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Linhas guia", (float*)&esp::config.color_snaplines, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("INFORMAÇÃO");
    ImGui::ColorEdit4("Nome", (float*)&esp::config.color_name, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("ID", (float*)&esp::config.color_id, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Distância", (float*)&esp::config.color_distance, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Arma", (float*)&esp::config.color_weapon, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Vida", (float*)&esp::config.color_health, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Armadura", (float*)&esp::config.color_armor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("NPCs", (float*)&esp::config.color_npc, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("EFEITOS");
    ImGui::ColorEdit4("Halo", (float*)&esp::config.color_halo, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Rastros", (float*)&esp::config.color_trail, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Eye Line", (float*)&esp::config.color_look_direction, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Outros efeitos", (float*)&esp::config.color_fun_effects, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    CyberWidgets::EndCard();
    ImGui::EndChild();

    if (!stacked) ImGui::SameLine(0.f, gap);


    ImGui::BeginChild("##fm_visual_preview", ImVec2(previewWidth, childHeight), false);
    CyberWidgets::BeginCard("PRÉ-VISUALIZAÇÃO", previewWidth);

    const float panelHeight = (std::max)(CyberTheme::Px(300.f), childHeight - CyberTheme::Metrics::CardHeaderHeight - CyberTheme::Metrics::CardPadding * 2.f);
    const float drawWidth = (std::max)(CyberTheme::Px(180.f), previewWidth - CyberTheme::Metrics::CardPadding * 2.f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    EspPreview::DrawPreview(dl, origin, drawWidth, panelHeight, g_fivemAdapter, "PRÉ-VISUALIZAÇÃO ESP");

    CyberWidgets::EndCard();
    ImGui::EndChild();
}