#include "../../widgets.h"
#include "../../theme.h"
#include "imgui.h"
#include "src/games/Warzone/warzone_config.h"
#include "../common/EspPreview.h"

namespace {

// ============================================================
// WARZONE CONFIG ADAPTER FOR UNIFIED PREVIEW
// ============================================================

struct WarzonePreviewAdapter : EspPreview::IConfigAdapter {
    const EspPreview::PreviewConfig& GetPreviewConfig() const override {
        static EspPreview::PreviewConfig cfg;
        // Map Warzone config to unified preview config
        cfg.esp_enabled = Warzone::config.esp_enabled;
        cfg.box = Warzone::config.box;
        cfg.box_corner = Warzone::config.box_corner;
        cfg.box_fill = false; // Warzone doesn't have box_fill
        cfg.box_thickness = 1.8f; // Warzone doesn't have thickness config
        cfg.box_rounding = 0.f;

        cfg.skeleton = Warzone::config.skeleton;
        cfg.skeleton_joints = false; // Warzone doesn't have joints
        cfg.skeleton_thickness = 1.8f;

        cfg.head_dot = Warzone::config.head_dot;
        cfg.head_halo = false; // Warzone doesn't have halo
        cfg.head_circle_thickness = 1.5f;

        cfg.health_bar = Warzone::config.health_bar;
        cfg.armor_bar = false; // Warzone doesn't have armor bar
        cfg.health_value = false;
        cfg.armor_value = false;

        cfg.name = Warzone::config.name;
        cfg.distance = Warzone::config.distance;
        cfg.weapon_name = false; // Warzone doesn't have weapon name
        cfg.weapon_ammo = false;
        cfg.text_outline = true;
        cfg.text_outline_thickness = 1.f;

        cfg.snaplines = Warzone::config.snaplines;
        cfg.snapline_position = 2; // bottom
        cfg.snapline_thickness = 1.5f;

        cfg.look_direction = false;
        cfg.eye_line_thickness = 1.6f;
        cfg.look_direction_length = 90.f;

        cfg.chinese_hat = false;
        cfg.angel_wings = false;
        cfg.devil_horns = false;
        cfg.floating_crown = false;
        cfg.trails = false;
        cfg.chinese_hat_scale = 1.f;
        cfg.fun_effects_scale = 1.f;

        // Colors - map Warzone colors to unified
        for (int i = 0; i < 4; ++i) {
            cfg.col_box[i] = Warzone::config.col_enemy[i];
            cfg.col_box_corner[i] = Warzone::config.col_enemy[i];
            cfg.col_box_fill[i] = Warzone::config.col_enemy[i];
            cfg.col_skeleton[i] = Warzone::config.col_enemy[i];
            cfg.col_joints[i] = Warzone::config.col_enemy[i];
            cfg.col_head[i] = Warzone::config.col_enemy[i];
            cfg.col_halo[i] = Warzone::config.col_enemy[i];
            cfg.col_health[i] = Warzone::config.col_enemy[i];
            cfg.col_armor[i] = Warzone::config.col_enemy[i];
            cfg.col_name[i] = Warzone::config.col_enemy[i];
            cfg.col_distance[i] = Warzone::config.col_enemy[i];
            cfg.col_weapon[i] = Warzone::config.col_enemy[i];
            cfg.col_snaplines[i] = Warzone::config.col_enemy[i];
            cfg.col_trail[i] = Warzone::config.col_enemy[i];
            cfg.col_fun_effects[i] = Warzone::config.col_enemy[i];
            cfg.col_look[i] = Warzone::config.col_enemy[i];
            cfg.col_text_outline[i] = 0.f; // black
            cfg.col_visible[i] = Warzone::config.col_enemy[i];
            cfg.col_occluded[i] = Warzone::config.col_enemy[i];
            cfg.col_team[i] = Warzone::config.col_team[i];
        }

        cfg.rgb_mode = false;
        cfg.visibility_colors = false;
        cfg.visible_check = false;
        cfg.team_check = Warzone::config.team_check;

        return cfg;
    }

    ImU32 GetBoxColor(bool rgbMode, bool visibilityColors, bool visibleCheck) const override {
        // Warzone uses single enemy color
        return EspPreview::ToImU32(Warzone::config.col_enemy);
    }
};

static WarzonePreviewAdapter g_warzoneAdapter;

} // namespace

void DrawWarzoneVisuals() {
    const float full = ImGui::GetContentRegionAvail().x;
    const float gap = CyberTheme::Metrics::GridGap;
    const float previewWidth = std::clamp(full * 0.30f, CyberTheme::Px(250.f), CyberTheme::Px(310.f));
    const float controlsWidth = full - previewWidth - gap;

    // ---- CONTROLS COLUMN ----
    ImGui::BeginChild("##wz_visual_controls", ImVec2(controlsWidth, 0), false,
        ImGuiWindowFlags_AlwaysVerticalScrollbar);
    CyberWidgets::BeginCard("FUNÇÕES ESP");
    CyberWidgets::ToggleSwitch("Ativar ESP", &Warzone::config.esp_enabled);
    CyberWidgets::Separator();
    CyberWidgets::ToggleSwitch("Ocultar equipa", &Warzone::config.team_check);
    CyberWidgets::ToggleSwitch("Ignorar derrubados", &Warzone::config.ignore_downed);
    CyberWidgets::ToggleSwitch("Ignorar IA", &Warzone::config.ignore_ai);
    CyberWidgets::SectionTitle("ELEMENTOS");
    CyberWidgets::ToggleSwitch("Esqueleto", &Warzone::config.skeleton);
    CyberWidgets::ToggleSwitch("Ponto na cabeça", &Warzone::config.head_dot);
    CyberWidgets::ToggleSwitch("Vida", &Warzone::config.health_bar);
    CyberWidgets::ToggleSwitch("Nome", &Warzone::config.name);
    CyberWidgets::ToggleSwitch("Distância", &Warzone::config.distance);
    CyberWidgets::ToggleSwitch("Caixa", &Warzone::config.box);
    CyberWidgets::ToggleSwitch("Caixa de cantos", &Warzone::config.box_corner);
    CyberWidgets::ToggleSwitch("Linhas guia", &Warzone::config.snaplines);
    ImGui::SliderInt("Distância máxima", &Warzone::config.max_distance, 50, 800, "%d m");
    CyberWidgets::EndCard();
    ImGui::EndChild();

    ImGui::SameLine(0.f, gap);

    // ---- PREVIEW + COLORS COLUMN ----
    ImGui::BeginChild("##wz_visual_preview", ImVec2(previewWidth, 0), false);
    CyberWidgets::BeginCard("PRÉ-VISUALIZAÇÃO DO ESP");

    const float panelHeight = 430.f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    EspPreview::DrawPreview(dl, origin, previewWidth - CyberTheme::Metrics::CardPadding * 2.f, panelHeight, g_warzoneAdapter, "PRÉ-VISUALIZAÇÃO ESP");

    CyberWidgets::EndCard();
    CyberWidgets::CardGap();
    CyberWidgets::BeginCard("CORES / ESTADO");
    ImGui::ColorEdit4("Inimigo", Warzone::config.col_enemy, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Equipa", Warzone::config.col_team, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Alvo", Warzone::config.col_target, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::Text("Jogadores: %d · matriz: %s · lista: %s",
        Warzone::runtime.player_count,
        Warzone::runtime.matrix_ok ? "OK" : "—",
        Warzone::runtime.list_ok ? "OK" : "—");
    ImGui::TextWrapped("%s", Warzone::StatusLine());
    CyberWidgets::EndCard();
    ImGui::EndChild();
}