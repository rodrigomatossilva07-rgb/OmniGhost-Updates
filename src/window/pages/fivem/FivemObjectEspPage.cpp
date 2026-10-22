#include "../../widgets.h"
#include "../../theme.h"
#include "config/app_settings.h"

#include "third_party/ImGui/imgui.h"

namespace {

void DrawObjectPreview(float width, float height)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + width, min.y + height);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(min, max, IM_COL32(5, 5, 5, 255), CyberTheme::Px(8.f));
    draw->AddRect(min, max, CyberTheme::WithAlpha(CyberTheme::Colors.Gold, .18f),
        CyberTheme::Px(8.f));

    const ImVec2 objectMin(min.x + width * .28f, min.y + height * .30f);
    const ImVec2 objectMax(min.x + width * .72f, min.y + height * .72f);
    draw->AddRect(objectMin, objectMax,
        CyberTheme::WithAlpha(CyberTheme::Colors.Gold, .75f), CyberTheme::Px(3.f), 0,
        CyberTheme::Px(1.5f));
    draw->AddLine(ImVec2(objectMin.x, objectMin.y),
        ImVec2(objectMin.x - CyberTheme::Px(11.f), objectMin.y - CyberTheme::Px(11.f)),
        CyberTheme::WithAlpha(CyberTheme::Colors.Gold, .36f));
    draw->AddLine(ImVec2(objectMax.x, objectMin.y),
        ImVec2(objectMax.x + CyberTheme::Px(11.f), objectMin.y - CyberTheme::Px(11.f)),
        CyberTheme::WithAlpha(CyberTheme::Colors.Gold, .36f));
    draw->AddText(ImVec2(objectMin.x, objectMin.y - CyberTheme::Px(24.f)),
        CyberTheme::U32(CyberTheme::Colors.Text), "World object");
    draw->AddText(ImVec2(objectMin.x, objectMax.y + CyberTheme::Px(8.f)),
        CyberTheme::U32(CyberTheme::Colors.TextDisabled), "24 m");
    draw->AddText(ImVec2(min.x + CyberTheme::Px(12.f), min.y + CyberTheme::Px(10.f)),
        CyberTheme::U32(CyberTheme::Colors.TextDisabled),
        app_settings::T("PRE-VISUALIZACAO", "PREVIEW"));

    ImGui::Dummy(ImVec2(width, height));
}

}

void DrawObjectEsp()
{
    static bool enabled = false;
    static bool worldObjects = true;
    static bool pickups = true;
    static bool droppedWeapons = true;
    static bool missionObjects = false;
    static bool showName = true;
    static bool showDistance = true;
    static bool showIcon = true;
    static float maximumDistance = 150.f;

    const float full = CyberWidgets::CardContentWidth();
    const float gap = CyberTheme::Spacing::Sm;
    const float left = (full - gap) * .58f;
    const float right = full - left - gap;

    ImGui::BeginGroup();
    CyberWidgets::BeginCard("OBJECT ESP", left);
    CyberWidgets::Badge(app_settings::T("EM PREPARACAO", "IN PREPARATION"),
        CyberWidgets::TextTone::Warning);
    CyberWidgets::TextLine(
        app_settings::T(
            "A interface esta preparada para a futura leitura validada de objetos do FiveM.",
            "The interface is prepared for future validated FiveM object acquisition."),
        CyberWidgets::TextTone::Secondary);

    ImGui::BeginDisabled();
    CyberWidgets::ToggleSwitch(app_settings::T("Ativar Object ESP", "Enable Object ESP"), &enabled);
    CyberWidgets::Separator();
    CyberWidgets::ToggleSwitch(app_settings::T("Objetos do mundo", "World objects"), &worldObjects);
    CyberWidgets::ToggleSwitch("Pickups", &pickups);
    CyberWidgets::ToggleSwitch(app_settings::T("Armas largadas", "Dropped weapons"), &droppedWeapons);
    CyberWidgets::ToggleSwitch(app_settings::T("Objetos de missao", "Mission objects"), &missionObjects);
    ImGui::EndDisabled();
    CyberWidgets::EndCard();

    CyberWidgets::CardGap(gap);
    CyberWidgets::BeginCard(app_settings::T("APRESENTACAO", "APPEARANCE"), left);
    ImGui::BeginDisabled();
    CyberWidgets::ToggleSwitch(app_settings::T("Mostrar nome", "Show name"), &showName);
    CyberWidgets::ToggleSwitch(app_settings::T("Mostrar distancia", "Show distance"), &showDistance);
    CyberWidgets::ToggleSwitch(app_settings::T("Mostrar icone", "Show icon"), &showIcon);
    CyberWidgets::SliderFloat(app_settings::T("Distancia maxima", "Maximum distance"),
        &maximumDistance, 10.f, 500.f, "%.0f m");
    ImGui::EndDisabled();
    CyberWidgets::EndCard();
    ImGui::EndGroup();

    ImGui::SameLine(0.f, gap);
    ImGui::BeginGroup();
    CyberWidgets::BeginCard(app_settings::T("PRE-VISUALIZACAO", "PREVIEW"), right);
    DrawObjectPreview((std::max)(CyberTheme::Px(160.f), right - CyberTheme::Px(20.f)),
        CyberTheme::Px(230.f));
    CyberWidgets::EndCard();

    CyberWidgets::CardGap(gap);
    CyberWidgets::BeginCard(app_settings::T("ESTADO", "STATUS"), right);
    CyberWidgets::KeyValueRow("Object pool",
        app_settings::T("Offset disponivel", "Offset available"));
    CyberWidgets::KeyValueRow(app_settings::T("Aquisicao", "Acquisition"),
        app_settings::T("Ainda nao implementada", "Not implemented yet"));
    CyberWidgets::KeyValueRow(app_settings::T("Renderizacao", "Rendering"),
        app_settings::T("Inativa", "Inactive"));
    CyberWidgets::TextLine(
        app_settings::T(
            "Os controlos permanecem bloqueados ate existir uma pipeline de leitura segura e validada.",
            "Controls remain locked until a safe, validated acquisition pipeline exists."),
        CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();
    ImGui::EndGroup();
}
