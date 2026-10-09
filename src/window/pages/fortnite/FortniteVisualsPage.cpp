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
    
    CyberWidgets::ToggleSwitch("Ativar diagnóstico visual local", &Fortnite::config.esp_enabled);
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("DIAGNÓSTICO SUPORTADO");
    CyberWidgets::ToggleSwitch("Marcador do jogador local", &Fortnite::config.show_local_marker);
    CyberWidgets::ToggleSwitch("Coordenadas locais", &Fortnite::config.show_local_coords);
    CyberWidgets::ToggleSwitch("Dados da câmara", &Fortnite::config.show_camera_debug);
    CyberWidgets::ToggleSwitch("Painel de diagnóstico", &Fortnite::config.show_debug_panel);
    CyberWidgets::EndCard();
    ImGui::EndChild();

    if (!stacked) ImGui::SameLine(0.f, gap);

    // ---- COLORS COLUMN ----
    ImGui::BeginChild("##fn_visual_colors", ImVec2(columnWidth, height), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    CyberWidgets::BeginCard("PERSONALIZAÇÃO");
    
#ifdef FORTNITE_ESP_IMPL
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
#else
    ImGui::TextDisabled("Cores disponíveis quando ESP estiver implementado.");
    ImGui::TextDisabled("Defina FORTNITE_ESP_IMPL no build para ativar.");
#endif
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

    (void)dl; (void)origin; (void)drawWidth; (void)panelHeight;
    ImGui::TextWrapped("A pré-visualização de jogadores remotos fica oculta enquanto o ESP remoto não estiver implementado.");

    CyberWidgets::EndCard();
    ImGui::EndChild();
}
