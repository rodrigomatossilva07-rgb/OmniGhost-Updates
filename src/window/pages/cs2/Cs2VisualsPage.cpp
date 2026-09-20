#include "../../widgets.h"
#include "config/cs2_config.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace {

// These switches reproduce the FiveM ESP layout without advertising features
// that have not been connected to the CS2 renderer yet.
struct EspMenuPreview {
    bool selfEsp = false, bots = true, dead = false, knocked = false;
    bool skeleton = true, joints = false, headCircle = false;
    bool health = true, armor = false, weapon = false, distance = true;
    bool snaplines = false, halo = false, hat = false, wings = false, horns = false, crown = false;
    bool hitMarker = false, trails = false, eyeLine = false;
};

ImU32 ConfigColor(const float color[4]) {
    return IM_COL32(static_cast<int>(color[0] * 255.f), static_cast<int>(color[1] * 255.f),
        static_cast<int>(color[2] * 255.f), static_cast<int>(color[3] * 255.f));
}

ImU32 PreviewBoxColor() {
    if (CS2::config.rgb_mode) {
        const float time = static_cast<float>(ImGui::GetTime()) * .35f;
        const auto channel = [time](float phase) {
            return static_cast<int>((std::sin(time + phase) * .5f + .5f) * 255.f);
        };
        return IM_COL32(channel(0.f), channel(2.094f), channel(4.188f), 255);
    }
    if (CS2::config.visibility_colors && CS2::config.visible_check)
        return ConfigColor(CS2::config.col_visible);
    return ConfigColor(CS2::config.box_corner ? CS2::config.col_box_corner : CS2::config.col_box);
}

void DrawPreview(float width, float height) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 end(origin.x + width, origin.y + height);
    const ImVec2 center(origin.x + width * .5f, origin.y + height * .53f);
    const float bodyTop = center.y - height * .30f;
    const float bodyBottom = center.y + height * .30f;
    const float halfWidth = (std::min)(width * .19f, height * .14f);
    const ImVec2 min(center.x - halfWidth, bodyTop);
    const ImVec2 max(center.x + halfWidth, bodyBottom);

    draw->AddRectFilled(origin, end, IM_COL32(8, 10, 12, 255), 8.f);
    draw->AddRect(origin, end, IM_COL32(199, 165, 43, 105), 8.f);
    draw->AddCircleFilled(ImVec2(center.x, bodyTop + 20.f), 16.f, IM_COL32(42, 47, 52, 255), 24);
    draw->AddLine(ImVec2(center.x, bodyTop + 38.f), ImVec2(center.x, bodyBottom - 20.f), IM_COL32(50, 56, 62, 255), 18.f);
    draw->AddLine(ImVec2(center.x, bodyTop + 72.f), ImVec2(center.x - halfWidth * 1.25f, bodyTop + 150.f), IM_COL32(50, 56, 62, 255), 11.f);
    draw->AddLine(ImVec2(center.x, bodyTop + 72.f), ImVec2(center.x + halfWidth * 1.25f, bodyTop + 150.f), IM_COL32(50, 56, 62, 255), 11.f);
    draw->AddLine(ImVec2(center.x, bodyBottom - 30.f), ImVec2(center.x - halfWidth * .65f, bodyBottom), IM_COL32(50, 56, 62, 255), 13.f);
    draw->AddLine(ImVec2(center.x, bodyBottom - 30.f), ImVec2(center.x + halfWidth * .65f, bodyBottom), IM_COL32(50, 56, 62, 255), 13.f);
    if (CS2::config.esp_enabled && (CS2::config.box || CS2::config.box_corner)) {
        const float thickness = std::clamp(CS2::config.box_thickness, .5f, 5.f);
        const ImU32 color = PreviewBoxColor();
        draw->AddRect(ImVec2(min.x - 1.f, min.y - 1.f), ImVec2(max.x + 1.f, max.y + 1.f), IM_COL32(0, 0, 0, 190), 0.f, 0, thickness + 1.f);
        if (CS2::config.box_corner) {
            const float horizontal = (max.x - min.x) * .25f;
            const float vertical = (max.y - min.y) * .20f;
            draw->AddLine(min, ImVec2(min.x + horizontal, min.y), color, thickness);
            draw->AddLine(min, ImVec2(min.x, min.y + vertical), color, thickness);
            draw->AddLine(ImVec2(max.x - horizontal, min.y), ImVec2(max.x, min.y), color, thickness);
            draw->AddLine(ImVec2(max.x, min.y), ImVec2(max.x, min.y + vertical), color, thickness);
            draw->AddLine(ImVec2(min.x, max.y - vertical), ImVec2(min.x, max.y), color, thickness);
            draw->AddLine(ImVec2(min.x, max.y), ImVec2(min.x + horizontal, max.y), color, thickness);
            draw->AddLine(ImVec2(max.x - horizontal, max.y), max, color, thickness);
            draw->AddLine(ImVec2(max.x, max.y - vertical), max, color, thickness);
        } else {
            draw->AddRect(min, max, color, 0.f, 0, thickness);
        }
    }
    const auto drawBar = [&](float x, float fraction, ImU32 color) {
        const float top = min.y, bottom = max.y;
        draw->AddRectFilled(ImVec2(x - 1.f, top - 1.f), ImVec2(x + 5.f, bottom + 1.f), IM_COL32(0, 0, 0, 190));
        draw->AddRectFilled(ImVec2(x, top), ImVec2(x + 4.f, bottom), IM_COL32(18, 18, 18, 235));
        draw->AddRectFilled(ImVec2(x, bottom - (bottom - top) * fraction), ImVec2(x + 4.f, bottom), color);
    };
    if (CS2::config.esp_enabled && CS2::config.health_bar)
        drawBar(min.x - 7.f, .72f, ConfigColor(CS2::config.col_health));
    if (CS2::config.esp_enabled && CS2::config.armor_bar)
        drawBar(max.x + 3.f, .48f, ConfigColor(CS2::config.col_armor));
    draw->AddText(ImVec2(origin.x + 12.f, origin.y + 12.f), IM_COL32(215, 215, 210, 210), "PRÉ-VISUALIZAÇÃO ESP");
    ImGui::Dummy(ImVec2(width, height));
}

void PreviewToggle(const char* label, bool* value) { CyberWidgets::ToggleSwitch(label, value); }

} // namespace

void DrawCs2Visuals() {
    static EspMenuPreview previewSettings{};
    const float full = ImGui::GetContentRegionAvail().x;
    const float height = (std::max)(CyberTheme::Px(410.f), ImGui::GetContentRegionAvail().y);
    const float gap = CyberTheme::Metrics::GridGap;
    const float previewWidth = std::clamp(full * .31f, CyberTheme::Px(250.f), CyberTheme::Px(310.f));
    const float settingsWidth = (std::max)(CyberTheme::Px(220.f), full - previewWidth - gap * 2.f);
    const float featuresWidth = settingsWidth * .5f;
    const float colorsWidth = settingsWidth - featuresWidth;

    ImGui::BeginChild("##cs2_esp_features", ImVec2(featuresWidth, height), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    CyberWidgets::BeginCard("ESP DE JOGADORES");
    CyberWidgets::ToggleSwitch("Ativar ESP", &CS2::config.esp_enabled);
    CyberWidgets::Separator();
    ImGui::BeginDisabled(!CS2::config.esp_enabled);
    PreviewToggle("ESP próprio", &previewSettings.selfEsp); PreviewToggle("Jogadores bot", &previewSettings.bots);
    PreviewToggle("Mostrar mortos", &previewSettings.dead); PreviewToggle("Mostrar caídos", &previewSettings.knocked);
    CyberWidgets::ToggleSwitch("Verificação de visibilidade", &CS2::config.visible_check);
    CyberWidgets::ToggleSwitch("Ocultar equipa", &CS2::config.team_check);
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("ELEMENTOS");
    PreviewToggle("Esqueleto", &previewSettings.skeleton); PreviewToggle("Articulações", &previewSettings.joints);
    PreviewToggle("Círculo na cabeça", &previewSettings.headCircle); CyberWidgets::ToggleSwitch("Barra de vida", &CS2::config.health_bar);
    CyberWidgets::ToggleSwitch("Barra de armadura", &CS2::config.armor_bar); PreviewToggle("Nome da arma", &previewSettings.weapon);
    PreviewToggle("Distância", &previewSettings.distance); CyberWidgets::ToggleSwitch("Caixa 2D", &CS2::config.box);
    CyberWidgets::ToggleSwitch("Caixa de cantos", &CS2::config.box_corner); PreviewToggle("Linhas guia", &previewSettings.snaplines);
    PreviewToggle("Halo na cabeça", &previewSettings.halo); PreviewToggle("Chapéu chinês", &previewSettings.hat);
    PreviewToggle("Asas de anjo", &previewSettings.wings); PreviewToggle("Chifres de demónio", &previewSettings.horns);
    PreviewToggle("Coroa flutuante", &previewSettings.crown); PreviewToggle("Marcador de acerto", &previewSettings.hitMarker);
    PreviewToggle("Rastros", &previewSettings.trails); PreviewToggle("Eye Line", &previewSettings.eyeLine);
    CyberWidgets::SectionTitle("ESPESSURA");
    CyberWidgets::SliderFloat("Caixa", &CS2::config.box_thickness, .5f, 4.f, "%.1f");
    CyberWidgets::SliderFloat("Distância máxima", &CS2::config.max_distance, 20.f, 500.f, "%.0f m");
    ImGui::EndDisabled();
    CyberWidgets::EndCard();
    ImGui::EndChild();

    ImGui::SameLine(0.f, gap);
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
    ImGui::BeginDisabled(!previewSettings.skeleton);
    ImGui::ColorEdit4("Esqueleto", CS2::config.col_skeleton, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!previewSettings.joints);
    ImGui::ColorEdit4("Articulações", CS2::config.col_joints, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!previewSettings.headCircle);
    ImGui::ColorEdit4("Círculo na cabeça", CS2::config.col_head, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::EndDisabled();
    ImGui::ColorEdit4("Caixa 2D", CS2::config.col_box, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Caixa de cantos", CS2::config.col_box_corner, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::BeginDisabled(!previewSettings.snaplines);
    ImGui::ColorEdit4("Linhas guia", CS2::config.col_snaplines, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::EndDisabled();
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("INFORMAÇÃO");
    ImGui::BeginDisabled(!CS2::config.health_bar);
    ImGui::ColorEdit4("Vida", CS2::config.col_health, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!CS2::config.armor_bar);
    ImGui::ColorEdit4("Armadura", CS2::config.col_armor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!previewSettings.distance);
    ImGui::ColorEdit4("Distância", CS2::config.col_distance, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!previewSettings.weapon);
    ImGui::ColorEdit4("Arma", CS2::config.col_weapon, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    CyberWidgets::EndCard();
    ImGui::EndChild();

    ImGui::SameLine(0.f, gap);
    ImGui::BeginGroup();
    CyberWidgets::BeginCard("PRÉ-VISUALIZAÇÃO", previewWidth);
    const float panelHeight = (std::max)(CyberTheme::Px(300.f), height - CyberTheme::Metrics::CardHeaderHeight - CyberTheme::Metrics::CardPadding * 2.f - CyberTheme::Px(12.f));
    DrawPreview((std::max)(CyberTheme::Px(180.f), previewWidth - CyberTheme::Metrics::CardPadding * 2.f), panelHeight);
    CyberWidgets::EndCard();
    ImGui::EndGroup();
}
