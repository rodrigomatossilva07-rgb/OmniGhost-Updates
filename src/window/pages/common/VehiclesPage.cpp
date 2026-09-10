#include "../../widgets.h"
#include "../../theme.h"
#include "../../localization.h"
#include <cstdio>
#ifdef UI_PREVIEW
#include "preview/preview_runtime.h"
#else
#include "esp/vehicle_esp.h"
#endif

void DrawVehicles()
{
    CyberWidgets::BeginCardRow();
    float half = CyberWidgets::CardRowHalfWidth();

    CyberWidgets::BeginCard("VEH // 03   ESP DE VEÍCULOS", half);
    CyberWidgets::TextLine("Estado, ocupantes e movimento sem sobrecarregar o ecrã.",
                           CyberWidgets::TextTone::Secondary);
    CyberWidgets::Separator();
    CyberWidgets::ToggleSwitch("Ativar ESP de veículos", &vehicle_esp::config.enabled);
    if (vehicle_esp::config.enabled) {
        CyberWidgets::ToggleSwitch("Caixa 3D", &vehicle_esp::config.box_3d);
        CyberWidgets::ToggleSwitch("Linhas guia", &vehicle_esp::config.snaplines);
        if (vehicle_esp::config.snaplines) {
            const char* snap_pos[] = { "Topo", "Centro", "Fundo" };
            CyberWidgets::Combo("Origem das linhas", &vehicle_esp::config.snapline_pos, snap_pos, 3);
        }
        CyberWidgets::ToggleSwitch("Distância", &vehicle_esp::config.distance);
        CyberWidgets::ToggleSwitch("Ocupantes", &vehicle_esp::config.show_occupants);
        CyberWidgets::ToggleSwitch("Estado do fecho", &vehicle_esp::config.lock_status);
        CyberWidgets::ToggleSwitch("Velocidade", &vehicle_esp::config.show_speed);
        CyberWidgets::SliderFloat("Distância máxima", &vehicle_esp::config.max_distance,
                                  10.f, 500.f, "%.0f m");
        if (ImGui::CollapsingHeader("AVANÇADO", ImGuiTreeNodeFlags_None))
            CyberWidgets::ToggleSwitch("Ignorar veículos ocupados", &vehicle_esp::config.ignore_occupied);
    } else {
        CyberWidgets::InlineMessage("Ativa o ESP para revelar as opções relacionadas.",
                                    CyberWidgets::TextTone::Secondary, "vehicle_esp_disabled");
    }
    CyberWidgets::EndCard();

    CyberWidgets::NextCardColumn();

    CyberWidgets::BeginCard("APARÊNCIA", half);
    CyberWidgets::TextLine("As cores só aparecem quando o respetivo elemento está ativo.",
                           CyberWidgets::TextTone::Secondary);
    if (!vehicle_esp::config.enabled) {
        CyberWidgets::Badge("ESP DESATIVADO", CyberWidgets::TextTone::Secondary);
    } else {
        if (vehicle_esp::config.box_3d)
            CyberWidgets::ColorEditU32("Caixa", &vehicle_esp::config.color_box_3d);
        if (vehicle_esp::config.snaplines)
            CyberWidgets::ColorEditU32("Linhas guia", &vehicle_esp::config.color_snaplines);
        if (vehicle_esp::config.distance)
            CyberWidgets::ColorEditU32("Informação", &vehicle_esp::config.color_distance);
        if (vehicle_esp::config.lock_status) {
            CyberWidgets::SectionTitle("ESTADO");
            CyberWidgets::ColorEditU32("Destrancado", &vehicle_esp::config.color_unlocked);
            CyberWidgets::ColorEditU32("Trancado", &vehicle_esp::config.color_locked);
        }
        if (ImGui::CollapsingHeader("CORES AVANÇADAS", ImGuiTreeNodeFlags_None)) {
            CyberWidgets::ColorEditU32("Ocupantes", &vehicle_esp::config.color_occupants);
            CyberWidgets::ColorEditU32("Nome", &vehicle_esp::config.color_name);
        }
    }
    // Preserve legacy profile compatibility without exposing rainbow effects.
    vehicle_esp::config.rgb_mode = false;
    CyberWidgets::EndCard();

    CyberWidgets::EndCardRow();
}
