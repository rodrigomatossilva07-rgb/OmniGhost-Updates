#include "widgets.h"
#include "theme.h"
#include "localization.h"
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

    CyberWidgets::BeginCard(Loc::Tr("veh.configs"), half);
    CyberWidgets::ToggleSwitch(Loc::TrID("veh.enabled"), &vehicle_esp::config.enabled);
    CyberWidgets::ToggleSwitch(Loc::TrID("veh.box3d"), &vehicle_esp::config.box_3d);
    CyberWidgets::ToggleSwitch(Loc::TrID("veh.snaplines"), &vehicle_esp::config.snaplines);
    CyberWidgets::ToggleSwitch(Loc::TrID("veh.distance"), &vehicle_esp::config.distance);
    CyberWidgets::ToggleSwitch(Loc::TrID("veh.occupants"), &vehicle_esp::config.show_occupants);
    CyberWidgets::ToggleSwitch(Loc::TrID("veh.ignore_occ"), &vehicle_esp::config.ignore_occupied);
    CyberWidgets::ToggleSwitch(Loc::TrID("veh.lock_status"), &vehicle_esp::config.lock_status);
    CyberWidgets::ToggleSwitch(Loc::TrID("veh.speed"), &vehicle_esp::config.show_speed);
    CyberWidgets::Separator();
    CyberWidgets::SliderFloat(Loc::TrID("veh.max_dist"), &vehicle_esp::config.max_distance, 10.0f, 500.0f, "%.0f m");
    CyberWidgets::EndCard();

    CyberWidgets::NextCardColumn();

    CyberWidgets::BeginCard(Loc::Tr("veh.customize"), half);
    CyberWidgets::ColorEditU32(Loc::TrID("veh.color_box"), &vehicle_esp::config.color_box_3d);
    CyberWidgets::ColorEditU32(Loc::TrID("veh.color_snap"), &vehicle_esp::config.color_snaplines);
    CyberWidgets::ColorEditU32(Loc::TrID("veh.color_dist"), &vehicle_esp::config.color_distance);
    CyberWidgets::ColorEditU32(Loc::TrID("veh.color_occ"), &vehicle_esp::config.color_occupants);
    CyberWidgets::ColorEditU32(Loc::TrID("veh.color_unlock"), &vehicle_esp::config.color_unlocked);
    CyberWidgets::ColorEditU32(Loc::TrID("veh.color_lock"), &vehicle_esp::config.color_locked);
    CyberWidgets::Separator();
    CyberWidgets::ToggleSwitch(Loc::TrID("veh.rgb"), &vehicle_esp::config.rgb_mode);
    {
        const char* snap_pos[] = {
            Loc::Tr("common.top"), Loc::Tr("common.center"), Loc::Tr("common.bottom")
        };
        CyberWidgets::Combo(Loc::TrID("veh.snap_pos"), &vehicle_esp::config.snapline_pos, snap_pos, 3);
    }

    CyberWidgets::EndCard();

    CyberWidgets::EndCardRow();
}
