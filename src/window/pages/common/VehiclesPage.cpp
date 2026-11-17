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
    const float full = CyberWidgets::CardContentWidth();
    ImGui::BeginGroup();

    CyberWidgets::BeginCard(Loc::Tr("nav.vehicles"), full);
    CyberWidgets::TextLine(Loc::Tr("veh.desc"),
                           CyberWidgets::TextTone::Secondary);
    CyberWidgets::Separator();
    CyberWidgets::ToggleSwitch(Loc::Tr("veh.enabled"), &vehicle_esp::config.enabled);
    if (vehicle_esp::config.enabled) {
        CyberWidgets::ToggleSwitch(Loc::Tr("veh.box3d"), &vehicle_esp::config.box_3d);
        CyberWidgets::ToggleSwitch(Loc::Tr("veh.box_2d"), &vehicle_esp::config.box_2d);
        CyberWidgets::ToggleSwitch(Loc::Tr("veh.marker"), &vehicle_esp::config.marker);
        CyberWidgets::ToggleSwitch(Loc::Tr("veh.snaplines"), &vehicle_esp::config.snaplines);
        if (vehicle_esp::config.snaplines) {
            const char* snap_pos[] = { Loc::Tr("common.top"), Loc::Tr("common.center"), Loc::Tr("common.bottom") };
            CyberWidgets::Combo(Loc::Tr("veh.snap_pos"), &vehicle_esp::config.snapline_pos, snap_pos, 3);
        }
        CyberWidgets::ToggleSwitch(Loc::Tr("veh.distance"), &vehicle_esp::config.distance);
        CyberWidgets::ToggleSwitch(Loc::Tr("veh.occupants"), &vehicle_esp::config.show_occupants);
        CyberWidgets::ToggleSwitch(Loc::Tr("veh.lock_status"), &vehicle_esp::config.lock_status);
        CyberWidgets::ToggleSwitch(Loc::Tr("veh.speed"), &vehicle_esp::config.show_speed);
        CyberWidgets::ToggleSwitch(Loc::Tr("veh.direction"), &vehicle_esp::config.show_direction);
        CyberWidgets::ToggleSwitch(Loc::Tr("veh.highlight_local"), &vehicle_esp::config.highlight_local);
        CyberWidgets::ToggleSwitch(Loc::Tr("veh.ignore_occ"), &vehicle_esp::config.ignore_occupied);
        CyberWidgets::SliderFloat(Loc::Tr("veh.max_dist"), &vehicle_esp::config.max_distance,
                                  10.f, 2000.f, "%.0f m");
        ImGui::SliderInt(Loc::Tr("veh.max_vehicles"),
                         &vehicle_esp::config.max_vehicles, 8, 128);
        CyberWidgets::SliderFloat(Loc::Tr("veh.box_thickness"),
                                  &vehicle_esp::config.box_thickness, .5f, 4.f, "%.1f px");
    } else {
        CyberWidgets::TextLine(Loc::Tr("veh.enable_hint"), CyberWidgets::TextTone::Secondary);
    }
    CyberWidgets::EndCard();

    CyberWidgets::CardGap(CyberTheme::Spacing::Sm);

    CyberWidgets::BeginCard(Loc::Tr("vis.customize"), full);
    CyberWidgets::TextLine(Loc::Tr("veh.colors_hint"),
                           CyberWidgets::TextTone::Secondary);
    if (!vehicle_esp::config.enabled) {
        CyberWidgets::Badge("ESP DESATIVADO", CyberWidgets::TextTone::Secondary);
    } else {
        if (vehicle_esp::config.box_3d)
            CyberWidgets::ColorEditU32(Loc::Tr("veh.color_box"), &vehicle_esp::config.color_box_3d);
        if (vehicle_esp::config.box_2d)
            CyberWidgets::ColorEditU32(app_settings::T("Caixa 2D", "2D box"), &vehicle_esp::config.color_box_3d);
        if (vehicle_esp::config.marker)
            CyberWidgets::ColorEditU32(app_settings::T("Marcador", "Marker"), &vehicle_esp::config.color_marker);
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
        CyberWidgets::ColorEditU32(app_settings::T("Veículo local", "Local vehicle"), &vehicle_esp::config.color_local);

    }

    CyberWidgets::EndCard();
    ImGui::EndGroup();
}



