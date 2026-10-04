#include "../../widgets.h"
#include "../../theme.h"
#include "../../../launcher/launcher_assets.h"
#include "src/games/Fortnite/fortnite_game.h"
#include "../common/EspPreview.h"
#include "imgui.h"

#include <algorithm>

namespace {

// ============================================================
// FORTNITE CONFIG ADAPTER FOR UNIFIED PREVIEW
// ============================================================

struct FortnitePreviewAdapter : EspPreview::IConfigAdapter {
    const EspPreview::PreviewConfig& GetPreviewConfig() const override {
        static EspPreview::PreviewConfig cfg;
        // Map Fortnite config to unified preview config
        // Fortnite uses Fortnite::config namespace
        cfg.esp_enabled = Fortnite::config.esp_enabled;
        cfg.box = Fortnite::config.box;
        cfg.box_corner = Fortnite::config.box_corner;
        cfg.box_fill = Fortnite::config.box_fill;
        cfg.box_thickness = Fortnite::config.box_thickness;
        cfg.box_rounding = Fortnite::config.box_rounding;

        cfg.skeleton = Fortnite::config.skeleton;
        cfg.skeleton_joints = Fortnite::config.skeleton_joints;
        cfg.skeleton_thickness = Fortnite::config.skeleton_thickness;

        cfg.head_dot = Fortnite::config.head_dot;
        cfg.head_halo = Fortnite::config.head_halo;
        cfg.head_circle_thickness = Fortnite::config.head_circle_thickness;

        cfg.health_bar = Fortnite::config.health_bar;
        cfg.armor_bar = Fortnite::config.armor_bar; // shield bar in Fortnite
        cfg.health_value = Fortnite::config.health_value;
        cfg.armor_value = Fortnite::config.armor_value;

        cfg.name = Fortnite::config.name;
        cfg.distance = Fortnite::config.distance;
        cfg.weapon_name = Fortnite::config.weapon_name;
        cfg.weapon_ammo = Fortnite::config.weapon_ammo;
        cfg.text_outline = Fortnite::config.text_outline;
        cfg.text_outline_thickness = Fortnite::config.text_outline_thickness;

        cfg.snaplines = Fortnite::config.snaplines;
        cfg.snapline_position = Fortnite::config.snapline_position;
        cfg.snapline_thickness = Fortnite::config.snapline_thickness;

        cfg.look_direction = Fortnite::config.look_direction;
        cfg.eye_line_thickness = Fortnite::config.eye_line_thickness;
        cfg.look_direction_length = Fortnite::config.look_direction_length;

        cfg.chinese_hat = Fortnite::config.chinese_hat;
        cfg.angel_wings = Fortnite::config.angel_wings;
        cfg.devil_horns = Fortnite::config.devil_horns;
        cfg.floating_crown = Fortnite::config.floating_crown;
        cfg.trails = Fortnite::config.trails;
        cfg.chinese_hat_scale = Fortnite::config.chinese_hat_scale;
        cfg.fun_effects_scale = Fortnite::config.fun_effects_scale;

        // Colors
        for (int i = 0; i < 4; ++i) {
            cfg.col_box[i] = Fortnite::config.col_box[i];
            cfg.col_box_corner[i] = Fortnite::config.col_box_corner[i];
            cfg.col_box_fill[i] = Fortnite::config.col_box_fill[i];
            cfg.col_skeleton[i] = Fortnite::config.col_skeleton[i];
            cfg.col_joints[i] = Fortnite::config.col_joints[i];
            cfg.col_head[i] = Fortnite::config.col_head[i];
            cfg.col_halo[i] = Fortnite::config.col_halo[i];
            cfg.col_health[i] = Fortnite::config.col_health[i];
            cfg.col_armor[i] = Fortnite::config.col_armor[i];
            cfg.col_name[i] = Fortnite::config.col_name[i];
            cfg.col_distance[i] = Fortnite::config.col_distance[i];
            cfg.col_weapon[i] = Fortnite::config.col_weapon[i];
            cfg.col_snaplines[i] = Fortnite::config.col_snaplines[i];
            cfg.col_trail[i] = Fortnite::config.col_trail[i];
            cfg.col_fun_effects[i] = Fortnite::config.col_fun_effects[i];
            cfg.col_look[i] = Fortnite::config.col_look[i];
            cfg.col_text_outline[i] = Fortnite::config.col_text_outline[i];
            cfg.col_visible[i] = Fortnite::config.col_visible[i];
            cfg.col_occluded[i] = Fortnite::config.col_occluded[i];
            cfg.col_team[i] = Fortnite::config.col_team[i];
        }

        cfg.rgb_mode = Fortnite::config.rgb_mode;
        cfg.visibility_colors = Fortnite::config.visibility_colors;
        cfg.visible_check = Fortnite::config.visible_check;
        cfg.team_check = Fortnite::config.team_check;

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
            return EspPreview::ToImU32(Fortnite::config.col_visible);
        return EspPreview::ToImU32(Fortnite::config.box_corner ? Fortnite::config.col_box_corner : Fortnite::config.col_box);
    }
};

static FortnitePreviewAdapter g_fortniteAdapter;

} // namespace

void DrawFortniteVisuals() {
    const float full = ImGui::GetContentRegionAvail().x;
    const float gap = CyberTheme::Metrics::GridGap;
    const float height = (std::max)(CyberTheme::Px(440.f), ImGui::GetContentRegionAvail().y);
    const bool stacked = full < CyberTheme::Px(850.f);
    const float previewWidth = stacked ? full : std::clamp(full * 0.30f, CyberTheme::Px(250.f), CyberTheme::Px(310.f));
    const float columnWidth = stacked ? full : (full - previewWidth - 2.f * gap) * 0.5f;

    // ---- FEATURES COLUMN ----
    ImGui::BeginChild("##fn_visual_features", ImVec2(columnWidth, height), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    CyberWidgets::BeginCard("ESP DE JOGADORES");
    // WARNING: Fortnite ESP is currently local-player only (lobby test)
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 180, 0, 255));
    ImGui::TextWrapped("[AVISO] ESP do Fortnite: Apenas jogador local (teste de lobby). Funcionalidades de outros jogadores NAO IMPLEMENTADAS.");
    ImGui::PopStyleColor();
    ImGui::Separator();
    CyberWidgets::ToggleSwitch("Ativar pré-visualização", &Fortnite::config.esp_enabled);
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("FILTROS");
    CyberWidgets::ToggleSwitch("ESP próprio", &Fortnite::config.self_esp);
    CyberWidgets::ToggleSwitch("Jogadores bot", &Fortnite::config.show_bots);
    CyberWidgets::ToggleSwitch("Verificação de visibilidade", &Fortnite::config.visible_check);
    CyberWidgets::ToggleSwitch("Ocultar equipa", &Fortnite::config.team_check);
    CyberWidgets::SectionTitle("ELEMENTOS");
    CyberWidgets::ToggleSwitch("Caixa 2D", &Fortnite::config.box);
    CyberWidgets::ToggleSwitch("Caixa de cantos", &Fortnite::config.box_corner);
    CyberWidgets::ToggleSwitch("Preenchimento da caixa", &Fortnite::config.box_fill);
    CyberWidgets::ToggleSwitch("Esqueleto", &Fortnite::config.skeleton);
    CyberWidgets::ToggleSwitch("Articulações", &Fortnite::config.skeleton_joints);
    CyberWidgets::ToggleSwitch("Círculo na cabeça", &Fortnite::config.head_dot);
    CyberWidgets::ToggleSwitch("Barra de vida", &Fortnite::config.health_bar);
    CyberWidgets::ToggleSwitch("Valor de vida", &Fortnite::config.health_value);
    CyberWidgets::ToggleSwitch("Barra de escudo", &Fortnite::config.armor_bar);
    CyberWidgets::ToggleSwitch("Valor de escudo", &Fortnite::config.armor_value);
    CyberWidgets::ToggleSwitch("Nome", &Fortnite::config.name);
    CyberWidgets::ToggleSwitch("Distância", &Fortnite::config.distance);
    CyberWidgets::ToggleSwitch("Nome da arma", &Fortnite::config.weapon_name);
    CyberWidgets::ToggleSwitch("Munição", &Fortnite::config.weapon_ammo);
    CyberWidgets::ToggleSwitch("Linhas guia", &Fortnite::config.snaplines);
    CyberWidgets::SectionTitle("EFEITOS");
    CyberWidgets::ToggleSwitch("Halo na cabeça", &Fortnite::config.head_halo);
    CyberWidgets::ToggleSwitch("Chapéu chinês", &Fortnite::config.chinese_hat);
    CyberWidgets::ToggleSwitch("Asas de anjo", &Fortnite::config.angel_wings);
    CyberWidgets::ToggleSwitch("Chifres de demónio", &Fortnite::config.devil_horns);
    CyberWidgets::ToggleSwitch("Coroa flutuante", &Fortnite::config.floating_crown);
    CyberWidgets::ToggleSwitch("Rastros", &Fortnite::config.trails);
    CyberWidgets::ToggleSwitch("Eye Line", &Fortnite::config.look_direction);
    CyberWidgets::ToggleSwitch("Hit marker", &Fortnite::config.hit_marker);
    CyberWidgets::SectionTitle("AJUSTES");
    CyberWidgets::SliderFloat("Espessura da caixa", &Fortnite::config.box_thickness, 0.5f, 4.f, "%.1f");
    CyberWidgets::SliderFloat("Arredondamento", &Fortnite::config.box_rounding, 0.f, 18.f, "%.0f");
    CyberWidgets::SliderFloat("Esqueleto", &Fortnite::config.skeleton_thickness, 0.5f, 4.f, "%.1f");
    CyberWidgets::SliderFloat("Círculo cabeça", &Fortnite::config.head_circle_thickness, 0.5f, 4.f, "%.1f");
    CyberWidgets::SliderFloat("Linhas guia", &Fortnite::config.snapline_thickness, 0.5f, 4.f, "%.1f");
    CyberWidgets::SliderFloat("Eye Line", &Fortnite::config.eye_line_thickness, 0.5f, 4.f, "%.1f");
    CyberWidgets::SliderFloat("Distância máxima", &Fortnite::config.max_distance, 20.f, 500.f, "%.0f m");
    CyberWidgets::ToggleSwitch("Outline de texto", &Fortnite::config.text_outline);
    CyberWidgets::SliderFloat("Espessura outline", &Fortnite::config.text_outline_thickness, 0.5f, 2.f, "%.1f");
    CyberWidgets::EndCard();
    ImGui::EndChild();

    if (!stacked) ImGui::SameLine(0.f, gap);

    // ---- COLORS COLUMN ----
    ImGui::BeginChild("##fn_visual_colors", ImVec2(columnWidth, height), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    CyberWidgets::BeginCard("PERSONALIZAÇÃO");
    CyberWidgets::ToggleSwitch("Modo RGB", &Fortnite::config.rgb_mode);
    CyberWidgets::ToggleSwitch("Cores por visibilidade", &Fortnite::config.visibility_colors);
    CyberWidgets::SectionTitle("JOGADORES");
    ImGui::ColorEdit4("Visível", Fortnite::config.col_visible, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Oculto", Fortnite::config.col_occluded, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Equipa", Fortnite::config.col_team, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("DESENHO");
    ImGui::ColorEdit4("Caixa 2D", Fortnite::config.col_box, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Caixa de cantos", Fortnite::config.col_box_corner, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Preenchimento", Fortnite::config.col_box_fill, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Esqueleto", Fortnite::config.col_skeleton, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Articulações", Fortnite::config.col_joints, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Círculo na cabeça", Fortnite::config.col_head, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Linhas guia", Fortnite::config.col_snaplines, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Outline de texto", Fortnite::config.col_text_outline, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("INFORMAÇÃO");
    ImGui::ColorEdit4("Nome", Fortnite::config.col_name, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Distância", Fortnite::config.col_distance, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Arma e munição", Fortnite::config.col_weapon, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Vida", Fortnite::config.col_health, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Escudo", Fortnite::config.col_armor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("EFEITOS");
    ImGui::ColorEdit4("Halo", Fortnite::config.col_halo, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Rastros", Fortnite::config.col_trail, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Eye Line", Fortnite::config.col_look, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Outros efeitos", Fortnite::config.col_fun_effects, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    CyberWidgets::EndCard();
    ImGui::EndChild();

    if (!stacked) ImGui::SameLine(0.f, gap);

    // ---- PREVIEW PANEL ----
    ImGui::BeginChild("##fn_visual_preview", ImVec2(previewWidth, height), false);
    CyberWidgets::BeginCard("PRÉ-VISUALIZAÇÃO", previewWidth);

    const float panelHeight = (std::max)(CyberTheme::Px(300.f), height - CyberTheme::Metrics::CardHeaderHeight - CyberTheme::Metrics::CardPadding * 2.f);
    const float drawWidth = (std::max)(CyberTheme::Px(180.f), previewWidth - CyberTheme::Metrics::CardPadding * 2.f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    EspPreview::DrawPreview(dl, origin, drawWidth, panelHeight, g_fortniteAdapter, "PREVIEW LOCAL");

    CyberWidgets::EndCard();
    ImGui::EndChild();
}