#pragma once

#include "gameplay/unified_config.h"
#include "gameplay/unified_esp.h"
#include "warzone_config.h"

namespace Warzone {

// Adapter for Warzone::Config -> UnifiedConfig::WarzoneConfig
class UnifiedConfigAdapter {
public:
    static OmniGhost::Gameplay::UnifiedConfig::WarzoneConfig& Get() {
        static OmniGhost::Gameplay::UnifiedConfig::WarzoneConfig unified;
        return unified;
    }
    
    static void SyncToUnified(const Warzone::Config& src) {
        auto& dst = Get();
        
        dst.esp_enabled = src.esp_enabled;
        dst.team_check = src.team_check;
        dst.ignore_downed = src.ignore_downed;
        dst.ignore_ai = src.ignore_ai;
        dst.skeleton = src.skeleton;
        dst.head_dot = src.head_dot;
        dst.health_bar = src.health_bar;
        dst.armor_bar = src.armor_bar;
        dst.name = src.name;
        dst.distance = src.distance;
        dst.box = src.box;
        dst.box_corner = src.box_corner;
        dst.snaplines = src.snaplines;
        dst.max_distance = src.max_distance;
        dst.aim_enabled = src.aim_enabled;
        dst.trigger_enabled = src.trigger_enabled;
        
        for (int i = 0; i < 4; ++i) {
            dst.col_enemy[i] = src.col_enemy[i];
            dst.col_team[i] = src.col_team[i];
            dst.col_target[i] = src.col_target[i];
            dst.col_skeleton[i] = src.col_skeleton[i];
            dst.col_armor[i] = src.col_armor[i];
        }
    }
    
    static void SyncFromUnified(Warzone::Config& dst) {
        const auto& src = Get();
        
        dst.esp_enabled = src.esp_enabled;
        dst.team_check = src.team_check;
        dst.ignore_downed = src.ignore_downed;
        dst.ignore_ai = src.ignore_ai;
        dst.skeleton = src.skeleton;
        dst.head_dot = src.head_dot;
        dst.health_bar = src.health_bar;
        dst.armor_bar = src.armor_bar;
        dst.name = src.name;
        dst.distance = src.distance;
        dst.box = src.box;
        dst.box_corner = src.box_corner;
        dst.snaplines = src.snaplines;
        dst.max_distance = src.max_distance;
        dst.aim_enabled = src.aim_enabled;
        dst.trigger_enabled = src.trigger_enabled;
        
        for (int i = 0; i < 4; ++i) {
            dst.col_enemy[i] = src.col_enemy[i];
            dst.col_team[i] = src.col_team[i];
            dst.col_target[i] = src.col_target[i];
            dst.col_skeleton[i] = src.col_skeleton[i];
            dst.col_armor[i] = src.col_armor[i];
        }
    }
    
    // Returns by value: the unified config's GetFeatureFlags() materializes the
    // flags, so a const& would dangle (C4172).
    static OmniGhost::Gameplay::UnifiedESP::FeatureFlags GetFeatureFlags() {
        return Get().GetFeatureFlags();
    }
};

} // namespace Warzone