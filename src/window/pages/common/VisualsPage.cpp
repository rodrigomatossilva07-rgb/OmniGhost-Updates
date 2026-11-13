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
        cfg.layout_name_x = esp::config.layout_name_x;
        cfg.layout_name_y = esp::config.layout_name_y;
        cfg.layout_health_x = esp::config.layout_health_x;
        cfg.layout_health_y = esp::config.layout_health_y;
        cfg.layout_armor_x = esp::config.layout_armor_x;
        cfg.layout_armor_y = esp::config.layout_armor_y;
        cfg.layout_weapon_x = esp::config.layout_weapon_x;
        cfg.layout_weapon_y = esp::config.layout_weapon_y;
        cfg.layout_distance_x = esp::config.layout_distance_x;
        cfg.layout_distance_y = esp::config.layout_distance_y;

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

void DrawEspLayoutBuilder(const ImVec2& origin, float canvasWidth, float canvasHeight)
{
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const EspPreview::PlayerGeometry geometry = EspPreview::BuildGeometry(origin, canvasWidth, canvasHeight);
    const ImVec2 boxMin = geometry.boundsMin;
    const ImVec2 boxMax = geometry.boundsMax;
    const ImVec2 restoreCursor = ImGui::GetCursorScreenPos();

    struct Item { const char* label; float* x; float* y; bool enabled; bool vertical; };
    Item items[] = {
        { "Enemy", &esp::config.layout_name_x, &esp::config.layout_name_y, esp::config.player_name, false },
        { "75 HP", &esp::config.layout_health_x, &esp::config.layout_health_y, esp::config.health_bar, true },
        { "65 AP", &esp::config.layout_armor_x, &esp::config.layout_armor_y, esp::config.armor_bar, true },
        { "AK-47", &esp::config.layout_weapon_x, &esp::config.layout_weapon_y, esp::config.weapon_name, false },
        { "18m", &esp::config.layout_distance_x, &esp::config.layout_distance_y, esp::config.distance, false }
    };
    for (int i = 0; i < 5; ++i) {
        if (!items[i].enabled) continue;
        const ImVec2 textSize = ImGui::CalcTextSize(items[i].label);
        ImVec2 chip(boxMin.x + *items[i].x * (boxMax.x - boxMin.x) - textSize.x * .5f,
                    boxMin.y + *items[i].y * (boxMax.y - boxMin.y) - textSize.y * .5f);
        const ImVec2 hitSize = items[i].vertical
            ? ImVec2((std::max)(18.f, textSize.x), boxMax.y - boxMin.y)
            : ImVec2(textSize.x + 12.f, textSize.y + 10.f);
        if (items[i].vertical) chip.y = boxMin.y;
        ImGui::PushID(i);
        ImGui::SetCursorScreenPos(chip);
        ImGui::InvisibleButton("##layout_item", hitSize);
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            *items[i].x = std::clamp((mouse.x - boxMin.x) / (boxMax.x - boxMin.x), -.20f, 1.20f);
            *items[i].y = std::clamp((mouse.y - boxMin.y) / (boxMax.y - boxMin.y), -.20f, 1.20f);
        }
        const bool active = ImGui::IsItemActive() || ImGui::IsItemHovered();
        if (active)
            draw->AddRect(chip, ImVec2(chip.x + hitSize.x, chip.y + hitSize.y),
                ImGui::IsItemActive() ? IM_COL32(229, 184, 55, 190) : IM_COL32(229, 184, 55, 90), 4.f);
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(restoreCursor);
}

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
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.show_knocked"), &esp::config.show_knocked);

    CyberWidgets::ToggleSwitch(Loc::Tr("vis.visible_check"), &esp::config.visible_check);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.hide_team"), &esp::config.team_check);
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle(Loc::Tr("vis.elements"));
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.skeleton"), &esp::config.skeleton);


    esp::config.skeleton_lod = true;
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.joints"), &esp::config.joints);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.head_circle"), &esp::config.head_circle);
    esp::config.circle_type = 0;
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.health_bar"), &esp::config.health_bar);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.armor_bar"), &esp::config.armor_bar);
    esp::config.player_name = false;
    esp::config.player_id = false;
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.weapon_name"), &esp::config.weapon_name);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.distance"), &esp::config.distance);
    esp::config.offscreen_arrows = false;
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle(app_settings::T("PAINEL ATIVO", "ACTIVE PANEL"));
    CyberWidgets::ToggleSwitch(app_settings::T("Mostrar funcionalidades ativas", "Show active features"),
        &esp::config.active_features_panel);
    if (esp::config.active_features_panel) {
        CyberWidgets::SliderFloat(app_settings::T("Escala", "Scale"),
            &esp::config.active_panel_scale, .65f, 1.6f, "%.2f");
        CyberWidgets::SliderFloat(app_settings::T("Opacidade do painel", "Panel opacity"),
            &esp::config.active_panel_opacity, .15f, 1.f, "%.2f");
        CyberWidgets::ToggleSwitch(app_settings::T("Modo compacto", "Compact mode"),
            &esp::config.active_panel_compact);
        CyberWidgets::ToggleSwitch("ESP", &esp::config.active_panel_show_esp);
        CyberWidgets::ToggleSwitch("Aimbot", &esp::config.active_panel_show_aim);
        CyberWidgets::ToggleSwitch("Triggerbot", &esp::config.active_panel_show_trigger);
        CyberWidgets::ToggleSwitch("Radar", &esp::config.active_panel_show_radar);
        CyberWidgets::TextLine(app_settings::T("Arrasta o painel com o menu aberto.",
                                              "Drag the panel while the menu is open."),
            CyberWidgets::TextTone::Secondary);
    }
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.box_2d"), &esp::config.box_2d);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.corner_box"), &esp::config.corner_box);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.snaplines"), &esp::config.snaplines);
    if (esp::config.snaplines) {
        const char* snapPositions[] = { Loc::Tr("common.top"), Loc::Tr("common.center"), Loc::Tr("common.bottom") };
        CyberWidgets::Combo(Loc::Tr("vis.snap_pos"), &esp::config.snapline_pos, snapPositions, 3);
    }
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.head_halo"), &esp::config.head_halo);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.chinese_hat"), &esp::config.chinese_hat);
    esp::config.angel_wings = false;
    esp::config.devil_horns = false;
    esp::config.floating_crown = false;
    esp::config.hit_marker = false;
    CyberWidgets::ToggleSwitch("Rastros", &esp::config.trails);
    if (esp::config.trails)
        CyberWidgets::ToggleSwitch("Rastros arco-iris", &esp::config.rainbow_trails);
    esp::config.look_direction = false;

    CyberWidgets::SectionTitle("ESPESSURA");
    CyberWidgets::SliderFloat("Esqueleto", &esp::config.skeleton_thickness, 0.5f, 6.f, "%.1f");
    CyberWidgets::SliderFloat("Linhas guia", &esp::config.snapline_thickness, 0.5f, 6.f, "%.1f");
    CyberWidgets::SliderFloat("Circulo cabeca", &esp::config.head_circle_thickness, 0.5f, 6.f, "%.1f");
    CyberWidgets::SliderFloat("Caixa", &esp::config.box_thickness, 0.5f, 6.f, "%.1f");
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

    colorWhenEnabled(Loc::Tr("vis.dead"), &esp::config.color_dead, espEnabled && esp::config.show_dead);
    colorWhenEnabled(Loc::Tr("vis.color_knocked"), &esp::config.color_knocked,
                     espEnabled && esp::config.show_knocked);
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
    DrawEspLayoutBuilder(origin, drawWidth, panelHeight);

    CyberWidgets::EndCard();
    ImGui::EndGroup();
}
