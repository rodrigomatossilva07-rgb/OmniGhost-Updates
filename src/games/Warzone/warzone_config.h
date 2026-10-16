#pragma once

#include "gameplay/esp_config_base.h"

namespace Warzone {

struct Config : OmniGhost::Gameplay::EspConfigBase {

    bool ignore_downed = true;
    bool ignore_ai = true;
    int max_distance = 400;


    bool aim_enabled = false;
    bool aim_draw_fov = true;
    bool aim_prediction = false;
    bool aim_humanize = true;
    bool aim_always_on = false;
    float aim_fov = 100.f;
    float aim_smooth = 25.f;
    float aim_deadzone = 1.5f;
    float aim_max_dist = 250.f;
    int aim_bind = 0x02;
    int aim_bind2 = 0;
    int aim_bone = 0;
    float sticky_ms = 140.f;
    bool aim_ignore_team = true;
    bool aim_ignore_downed = true;
    bool aim_ignore_ai = true;


    bool trigger_enabled = false;
    int trigger_bind = 0x06;
    int trigger_delay_ms = 40;
    bool trigger_team_check = true;


    bool radar_2d = false;
    float radar_size = 160.f;


    bool radar_only_mode = false;
    int  esp_density_limit = 40;
    bool esp_auto_hide_names = true;
    char scenario_preset[32] = "custom";


    bool player_list_enabled = false;
    bool misc_airstrike_alert = false;
    bool misc_top250_alert = false;
    bool misc_watched_alert = false;
    bool misc_clan_tags = false;
    bool misc_track_between_matches = false;
    bool misc_spectator_list = false;
};

extern Config config;

}