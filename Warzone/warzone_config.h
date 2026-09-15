#pragma once

namespace Warzone {

struct Config {
    // ESP
    bool esp_enabled = false;
    bool team_check = true;
    bool name = true;
    bool distance = true;
    bool box = true;
    bool box_corner = false;
    bool skeleton = false;
    bool health_bar = true;
    bool snaplines = false;
    bool head_dot = false;
    bool ignore_downed = true;
    bool ignore_ai = true;
    int max_distance = 400;

    float col_enemy[4] = { 0.95f, 0.25f, 0.20f, 1.f };
    float col_team[4]  = { 0.25f, 0.85f, 0.35f, 1.f };
    float col_target[4] = { 1.f, 0.85f, 0.20f, 1.f };
    float col_fov[4] = { 0.83f, 0.69f, 0.22f, 0.45f };

    // Aim
    bool aim_enabled = false;
    bool aim_draw_fov = true;
    bool aim_prediction = false;
    bool aim_humanize = true;
    bool aim_always_on = false; // never used unless bind==0 blocked
    float aim_fov = 100.f;
    float aim_smooth = 25.f;
    float aim_deadzone = 1.5f;
    float aim_max_dist = 250.f;
    int aim_bind = 0x02;
    int aim_bind2 = 0;
    int aim_bone = 0; // 0 head 1 chest
    float sticky_ms = 140.f;
    bool aim_ignore_team = true;
    bool aim_ignore_downed = true;
    bool aim_ignore_ai = true;

    // Trigger
    bool trigger_enabled = false;
    int trigger_bind = 0x06;
    int trigger_delay_ms = 40;
    bool trigger_team_check = true;

    // Radar
    bool radar_2d = false;
    float radar_size = 160.f;

    // Misc / product features (better-than-Blurred)
    bool performance_mode = false;
    bool radar_only_mode = false;     // ESP+aim off, radar on — teammate share
    int  esp_density_limit = 40;      // hide long labels when players > limit
    bool esp_auto_hide_names = true;  // density: prefer box/hp over name spam
    char scenario_preset[32] = "custom"; // stream|ranked|hotdrop|radar_only|visuals|custom

    // Visual-only menu state for the Warzone player list and miscellaneous pages.
    // These fields are persisted with the rest of the Warzone profile.
    bool player_list_enabled = false;
    bool misc_airstrike_alert = false;
    bool misc_top250_alert = false;
    bool misc_watched_alert = false;
    bool misc_clan_tags = false;
    bool misc_track_between_matches = false;
    bool misc_spectator_list = false;
};

extern Config config;

} // namespace Warzone
