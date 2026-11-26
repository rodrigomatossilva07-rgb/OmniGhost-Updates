#include "triggerbot_integration.h"
#include <algorithm>

namespace OmniGhost::Gameplay {

void TriggerIntegration::Initialize(const TriggerIntegrationConfig& cfg) {
    current_config_ = cfg;
    triggerbot_.SetConfig(ConvertConfig(cfg));
}

void TriggerIntegration::SetConfig(const TriggerIntegrationConfig& cfg) {
    current_config_ = cfg;
    triggerbot_.SetConfig(ConvertConfig(cfg));
}

TriggerbotConfig TriggerIntegration::ConvertConfig(const TriggerIntegrationConfig& cfg) {
    TriggerbotConfig out;
    
    out.enabled = cfg.enabled;
    out.visible_only = cfg.visible_only;
    out.fire_without_aim = cfg.fire_without_aim;
    out.require_target_lock = cfg.require_target_lock;
    out.mode = static_cast<TriggerMode>(cfg.mode);
    out.pre_fire_ms = cfg.pre_fire_ms;
    out.post_fire_ms = cfg.post_fire_ms;
    out.min_time_between_shots = cfg.min_time_between_shots;
    out.deadzone_px = cfg.deadzone_px;
    out.max_distance_m = cfg.max_distance_m;
    out.weapon_mask = cfg.weapon_mask;
    
    for (int i = 0; i < 64; ++i) {
        out.weapon_allowed[i] = cfg.weapon_allowed[i];
    }
    
    out.reaction_variance_ms = cfg.reaction_variance_ms;
    out.jitter_px = cfg.jitter_px;
    out.simulate_human_reaction = cfg.simulate_human_reaction;
    out.burst_shots = cfg.burst_shots;
    out.burst_interval_ms = cfg.burst_interval_ms;
    out.burst_cooldown_ms = cfg.burst_cooldown_ms;
    out.ignore_knocked = cfg.ignore_knocked;
    out.ignore_teammates = cfg.ignore_teammates;
    out.ignore_npcs = cfg.ignore_npcs;
    out.min_health_percent = cfg.min_health_percent;
    
    return out;
}

bool TriggerIntegration::Update(const TriggerTargetInfo& target, bool aim_key_held, bool crosshair_on_target) {
    if (!current_config_.enabled) {
        return false;
    }
    
    return triggerbot_.Update(
        crosshair_on_target,
        target.entity_id,
        target.distance_m,
        target.visible,
        target.weapon_index,
        target.knocked,
        target.teammate,
        target.npc,
        target.health_percent,
        aim_key_held
    );
}

void TriggerIntegration::Reset() {
    triggerbot_.Reset();
}

const char* TriggerIntegration::GetPhaseName() const {
    return triggerbot_.GetPhaseName();
}

TriggerbotState::Phase TriggerIntegration::GetPhase() const {
    return triggerbot_.GetState().phase;
}

} // namespace OmniGhost::Gameplay