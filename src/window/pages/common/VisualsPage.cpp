#include "../../window.hpp"
#include "../../widgets.h"
#include "../../theme.h"
#include "../../fonts.h"
#include "../../localization.h"
#include "../../../launcher/launcher_assets.h"
#include "config/app_settings.h"
#ifdef UI_PREVIEW
#include "preview/preview_runtime.h"
#else
#include "esp/esp.h"
#endif
#include "../common/EspPreview.h"
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <algorithm>
#include <string>

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

void DrawVisuals()
{



    const float full = ImGui::GetContentRegionAvail().x;
    const float columnHeight = (std::max)(CyberTheme::Px(410.0f), ImGui::GetContentRegionAvail().y);
    const float gap = CyberTheme::Metrics::GridGap;
    const float previewWidth = std::clamp(full * 0.31f, CyberTheme::Px(250.0f), CyberTheme::Px(310.0f));
    const float settingsWidth = (std::max)(CyberTheme::Px(220.0f), full - previewWidth - gap * 2.0f);
    const float featuresWidth = settingsWidth * 0.50f;
    const float colorsWidth = settingsWidth - featuresWidth;

    ImGui::BeginChild("##esp_features_scroll", ImVec2(featuresWidth, columnHeight), false,
        ImGuiWindowFlags_AlwaysVerticalScrollbar);
    CyberWidgets::BeginCard(Loc::Tr("vis.features"));
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.enable"), &esp::config.enabled);
    CyberWidgets::Separator();
    ImGui::BeginDisabled(!esp::config.enabled);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.self_esp"), &esp::config.self_esp);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.npc"), &esp::config.npc_esp);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.show_dead"), &esp::config.show_dead);
    // FIVEM-004 Fix: "knocked" has no data source in FiveM — hidden.
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.visible_check"), &esp::config.visible_check);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.hide_team"), &esp::config.team_check);
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle(Loc::Tr("vis.elements"));
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.skeleton"), &esp::config.skeleton);
    if (esp::config.skeleton) {
        CyberWidgets::ToggleSwitch(Loc::Tr("vis.skeleton_lod"), &esp::config.skeleton_lod);
        if (esp::config.skeleton_lod) {
            CyberWidgets::SliderFloat(Loc::Tr("vis.lod_full"), &esp::config.lod_full_dist, 10.f, 100.f, "%.0f m");
            CyberWidgets::SliderFloat(Loc::Tr("vis.lod_high"), &esp::config.lod_high_dist, 50.f, 200.f, "%.0f m");
            CyberWidgets::SliderFloat(Loc::Tr("vis.lod_med"), &esp::config.lod_med_dist, 100.f, 300.f, "%.0f m");
        }
    }
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.joints"), &esp::config.joints);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.head_circle"), &esp::config.head_circle);
    if (esp::config.head_circle) {
        const char* circleTypes[] = { Loc::Tr("vis.circle_normal"), Loc::Tr("vis.circle_filled"), Loc::Tr("vis.circle_cross") };
        CyberWidgets::Combo(Loc::Tr("vis.circle_type"), &esp::config.circle_type, circleTypes, 3);
    }
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.health_bar"), &esp::config.health_bar);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.armor_bar"), &esp::config.armor_bar);
    esp::config.player_name = false;
    esp::config.player_id = false;
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.weapon_name"), &esp::config.weapon_name);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.distance"), &esp::config.distance);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.box_2d"), &esp::config.box_2d);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.corner_box"), &esp::config.corner_box);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.snaplines"), &esp::config.snaplines);
    if (esp::config.snaplines) {
        const char* snapPositions[] = { Loc::Tr("common.top"), Loc::Tr("common.center"), Loc::Tr("common.bottom") };
        CyberWidgets::Combo(Loc::Tr("vis.snap_pos"), &esp::config.snapline_pos, snapPositions, 3);
    }
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.head_halo"), &esp::config.head_halo);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.chinese_hat"), &esp::config.chinese_hat);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.angel_wings"), &esp::config.angel_wings);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.devil_horns"), &esp::config.devil_horns);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.floating_crown"), &esp::config.floating_crown);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.hit_marker"), &esp::config.hit_marker);
    if (esp::config.angel_wings || esp::config.devil_horns || esp::config.floating_crown) {
        CyberWidgets::ToggleSwitch(Loc::Tr("vis.fun_rainbow"), &esp::config.fun_effects_rainbow);
        CyberWidgets::SliderFloat(Loc::Tr("vis.fun_size"), &esp::config.fun_effects_scale, .5f, 2.5f, "%.2f");
    }
    CyberWidgets::ToggleSwitch("Rastros", &esp::config.trails);
    if (esp::config.trails)
        CyberWidgets::ToggleSwitch("Rastros arco-iris", &esp::config.rainbow_trails);
    CyberWidgets::ToggleSwitch("Eye Line", &esp::config.look_direction);

    CyberWidgets::SectionTitle("ESPESSURA");
    CyberWidgets::SliderFloat("Esqueleto", &esp::config.skeleton_thickness, 0.5f, 6.f, "%.1f");
    CyberWidgets::SliderFloat("Linhas guia", &esp::config.snapline_thickness, 0.5f, 6.f, "%.1f");
    CyberWidgets::SliderFloat("Circulo cabeca", &esp::config.head_circle_thickness, 0.5f, 6.f, "%.1f");
    CyberWidgets::SliderFloat("Caixa", &esp::config.box_thickness, 0.5f, 6.f, "%.1f");
    CyberWidgets::SliderFloat("Eye Line", &esp::config.eye_line_thickness, 0.5f, 6.f, "%.1f");
    if (esp::config.trails)
        CyberWidgets::SliderFloat("Rastro", &esp::config.trail_thickness, 1.f, 8.f, "%.1f");
    if (esp::config.chinese_hat)
        CyberWidgets::SliderFloat("Chapeu Chines 3D", &esp::config.chinese_hat_scale, 0.4f, 2.5f, "%.2f");

    CyberWidgets::SliderFloat(Loc::Tr("vis.max_dist"), &esp::config.max_esp_distance,
                              0.f, 500.f, "%.0f m");
    ImGui::EndDisabled();
    CyberWidgets::EndCard();

    ImGui::EndChild();

    ImGui::SameLine(0.0f, gap);
    ImGui::BeginChild("##esp_colors_scroll", ImVec2(colorsWidth, columnHeight), false,
        ImGuiWindowFlags_AlwaysVerticalScrollbar);
    CyberWidgets::BeginCard(Loc::Tr("vis.customize"));
    const auto colorWhenEnabled = [](const char* label, ImU32* color, bool enabled) {
        ImGui::BeginDisabled(!enabled);
        CyberWidgets::ColorEditU32(label, color);
        ImGui::EndDisabled();
    };
    const bool espEnabled = esp::config.enabled;
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.rgb"), &esp::config.rgb_mode);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.visibility_colors"), &esp::config.visibility_colors);
    CyberWidgets::SectionTitle(Loc::Tr("fr.players"));
    const bool visibilityColorsEnabled = espEnabled && esp::config.visibility_colors && esp::config.visible_check;
    colorWhenEnabled(Loc::Tr("vis.visible"), &esp::config.color_visible, visibilityColorsEnabled);
    colorWhenEnabled(Loc::Tr("vis.hidden"), &esp::config.color_invisible, visibilityColorsEnabled);
    // FIVEM-004: "knocked" color hidden — no data source in FiveM.
    colorWhenEnabled(Loc::Tr("vis.dead"), &esp::config.color_dead, espEnabled && esp::config.show_dead);
    colorWhenEnabled(Loc::Tr("vis.team"), &esp::config.color_team, espEnabled && !esp::config.team_check);
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle(Loc::Tr("vis.drawing"));
    colorWhenEnabled(Loc::Tr("vis.skeleton"), &esp::config.color_skeleton, espEnabled && esp::config.skeleton);
    colorWhenEnabled(Loc::Tr("vis.color_points"), &esp::config.color_skeleton_points,
                     espEnabled && esp::config.skeleton && esp::config.joints);
    colorWhenEnabled(Loc::Tr("vis.color_head"), &esp::config.color_head_circle,
                     espEnabled && esp::config.head_circle);
    colorWhenEnabled(Loc::Tr("vis.box_2d"), &esp::config.color_box_2d, espEnabled && esp::config.box_2d);
    colorWhenEnabled(Loc::Tr("vis.corner_box"), &esp::config.color_corner_box,
                     espEnabled && esp::config.corner_box);
    colorWhenEnabled(Loc::Tr("vis.snaplines"), &esp::config.color_snaplines,
                     espEnabled && esp::config.snaplines);
    colorWhenEnabled(Loc::Tr("vis.fun_color"), &esp::config.color_fun_effects,
                     espEnabled && !esp::config.fun_effects_rainbow &&
                     (esp::config.angel_wings || esp::config.devil_horns || esp::config.floating_crown));
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle(Loc::Tr("vis.information"));
    colorWhenEnabled(Loc::Tr("vis.health"), &esp::config.color_health, espEnabled && esp::config.health_bar);
    colorWhenEnabled(Loc::Tr("vis.armor"), &esp::config.color_armor, espEnabled && esp::config.armor_bar);
    colorWhenEnabled(Loc::Tr("common.distance"), &esp::config.color_distance, espEnabled && esp::config.distance);
    colorWhenEnabled(Loc::Tr("vis.weapon_short"), &esp::config.color_weapon, espEnabled && esp::config.weapon_name);
    colorWhenEnabled("NPC", &esp::config.color_npc, espEnabled && esp::config.npc_esp);
    CyberWidgets::Separator();
    if (CyberWidgets::Button(Loc::Tr("vis.reset_colors"), CyberWidgets::ButtonStyle::Ghost,
                             ImVec2(CyberWidgets::CardContentWidth(), CyberTheme::Metrics::ControlHeight))) {
        const esp::Config defaults{};
        esp::config.color_visible = defaults.color_visible;
        esp::config.color_invisible = defaults.color_invisible;
        esp::config.color_dead = defaults.color_dead;
        esp::config.color_knocked = defaults.color_knocked;
        esp::config.color_team = defaults.color_team;
        esp::config.color_skeleton = defaults.color_skeleton;
        esp::config.color_skeleton_points = defaults.color_skeleton_points;
        esp::config.color_head_circle = defaults.color_head_circle;
        esp::config.color_health = defaults.color_health;
        esp::config.color_armor = defaults.color_armor;
        esp::config.color_weapon = defaults.color_weapon;
        esp::config.color_box_2d = defaults.color_box_2d;
        esp::config.color_corner_box = defaults.color_corner_box;
        esp::config.color_snaplines = defaults.color_snaplines;
        esp::config.color_name = defaults.color_name;
        esp::config.color_id = defaults.color_id;
        esp::config.color_distance = defaults.color_distance;
        esp::config.color_npc = defaults.color_npc;
        esp::config.color_fun_effects = defaults.color_fun_effects;
    }
    CyberWidgets::EndCard();
    ImGui::EndChild();

    ImGui::SameLine(0.0f, gap);
    ImGui::BeginGroup();
    CyberWidgets::BeginCard(Loc::Tr("vis.preview"), previewWidth);
    const float panelHeight = (std::max)(CyberTheme::Px(300.0f),
        columnHeight - CyberTheme::Metrics::CardHeaderHeight -
        CyberTheme::Metrics::CardPadding * 2.0f - CyberTheme::Px(12.0f));
    const float drawWidth = (std::max)(CyberTheme::Px(180.0f),
        previewWidth - CyberTheme::Metrics::CardPadding * 2.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();


    EspPreview::DrawPreview(dl, origin, drawWidth, panelHeight, g_fivemAdapter, Loc::Tr("vis.preview"));

    CyberWidgets::EndCard();
    ImGui::EndGroup();
}
