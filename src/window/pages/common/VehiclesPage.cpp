#include "../../widgets.h"
#include "../../theme.h"
#include "../../localization.h"
#include "config/app_settings.h"
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

    CyberWidgets::BeginCard(Loc::Tr("nav.vehicles"), half);
    CyberWidgets::TextLine(app_settings::T("Estado, ocupantes e movimento sem sobrecarregar o ecrã.",
                                          "Status, occupants and movement in a clean layout."),
                           CyberWidgets::TextTone::Secondary);
    CyberWidgets::Separator();
    CyberWidgets::ToggleSwitch(Loc::Tr("veh.enabled"), &vehicle_esp::config.enabled);
    if (vehicle_esp::config.enabled) {
        CyberWidgets::ToggleSwitch(Loc::Tr("veh.box3d"), &vehicle_esp::config.box_3d);
        CyberWidgets::ToggleSwitch(Loc::Tr("veh.snaplines"), &vehicle_esp::config.snaplines);
        if (vehicle_esp::config.snaplines) {
            const char* snap_pos[] = { Loc::Tr("common.top"), Loc::Tr("common.center"), Loc::Tr("common.bottom") };
            CyberWidgets::Combo(Loc::Tr("veh.snap_pos"), &vehicle_esp::config.snapline_pos, snap_pos, 3);
        }
        CyberWidgets::ToggleSwitch(Loc::Tr("veh.distance"), &vehicle_esp::config.distance);
        CyberWidgets::ToggleSwitch(Loc::Tr("veh.occupants"), &vehicle_esp::config.show_occupants);
        CyberWidgets::ToggleSwitch(Loc::Tr("veh.lock_status"), &vehicle_esp::config.lock_status);
        CyberWidgets::ToggleSwitch(Loc::Tr("veh.speed"), &vehicle_esp::config.show_speed);
        CyberWidgets::ToggleSwitch(Loc::Tr("veh.ignore_occ"), &vehicle_esp::config.ignore_occupied);
        CyberWidgets::SliderFloat(Loc::Tr("veh.max_dist"), &vehicle_esp::config.max_distance,
                                  10.f, 500.f, "%.0f m");
    } else {
        CyberWidgets::TextLine(app_settings::T("Ativa o ESP para revelar as opções relacionadas.",
                                              "Enable ESP to reveal its options."), CyberWidgets::TextTone::Secondary);
    }
    CyberWidgets::EndCard();

    CyberWidgets::NextCardColumn();

    CyberWidgets::BeginCard(Loc::Tr("vis.customize"), half);
    CyberWidgets::TextLine(app_settings::T("As cores só aparecem quando o respetivo elemento está ativo.",
                                          "Colors appear when their corresponding element is enabled."),
                           CyberWidgets::TextTone::Secondary);
    if (!vehicle_esp::config.enabled) {
        CyberWidgets::Badge("ESP DESATIVADO", CyberWidgets::TextTone::Secondary);
    } else {
        if (vehicle_esp::config.box_3d)
            CyberWidgets::ColorEditU32(Loc::Tr("veh.color_box"), &vehicle_esp::config.color_box_3d);
        if (vehicle_esp::config.snaplines)
            CyberWidgets::ColorEditU32(Loc::Tr("veh.color_snap"), &vehicle_esp::config.color_snaplines);
        if (vehicle_esp::config.distance)
            CyberWidgets::ColorEditU32(Loc::Tr("veh.color_dist"), &vehicle_esp::config.color_distance);
        if (vehicle_esp::config.lock_status) {
            CyberWidgets::SectionTitle("ESTADO");
            CyberWidgets::ColorEditU32(Loc::Tr("veh.color_unlock"), &vehicle_esp::config.color_unlocked);
            CyberWidgets::ColorEditU32(Loc::Tr("veh.color_lock"), &vehicle_esp::config.color_locked);
        }
        CyberWidgets::ColorEditU32(Loc::Tr("veh.color_occ"), &vehicle_esp::config.color_occupants);
        CyberWidgets::ColorEditU32(Loc::Tr("veh.color_name"), &vehicle_esp::config.color_name);
    }
    // Preserve legacy profile compatibility without exposing rainbow effects.
    vehicle_esp::config.rgb_mode = false;
    CyberWidgets::EndCard();

    CyberWidgets::EndCardRow();
}
