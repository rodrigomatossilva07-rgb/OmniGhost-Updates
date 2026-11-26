#include "triggerbot.h"
#include <algorithm>
#include <random>

namespace OmniGhost::Gameplay {

bool Triggerbot::Update(bool crosshair_on_target,
                                uint64_t target_entity_id,
                                float target_distance,
                                bool target_visible,
                                int target_weapon_index,
                                bool target_knocked,
                                bool target_teammate,
                                bool target_npc,
                                float target_health_percent,
                                bool aim_key_held) {

    using namespace std::chrono;
    auto now = steady_clock::now();

    // Verifica se alvo é válido
    bool target_valid = CheckTargetValid(target_entity_id, target_distance, target_visible,
                                         target_weapon_index, target_knocked, target_teammate,
                                         target_npc, target_health_percent);

    // Se mudou de alvo, reset
    if (target_valid && target_entity_id != state_.target_entity) {
        state_.target_entity = target_entity_id;
        state_.shots_in_burst = 0;
        state_.accumulated_pre_fire = 0.0f;
        if (state_.phase != TriggerbotState::Phase::Idle) {
            TransitionTo(TriggerbotState::Phase::Idle);
        }
    }

    // Se alvo inválido
    if (!target_valid) {
        if (state_.phase != TriggerbotState::Phase::Idle &&
            state_.phase != TriggerbotState::Phase::Cooldown) {
            TransitionTo(TriggerbotState::Phase::Idle);
        }
        state_.target_entity = 0;
    }

    // Verifica aim key
    bool can_fire = config_.fire_without_aim || aim_key_held;
    if (config_.require_target_lock && !aim_key_held) can_fire = false;

    // Cooldown entre tiros
    auto time_since_last_shot = duration_cast<milliseconds>(now - state_.last_shot).count();
    bool cooldown_ready = time_since_last_shot >= config_.min_time_between_shots;

    // State machine
    switch (state_.phase) {
        case TriggerbotState::Phase::Idle: {
            if (target_valid && crosshair_on_target && can_fire && cooldown_ready) {
                // Inicia pre-fire ou dispara direto
                if (config_.mode == TriggerMode::PreFire || config_.mode == TriggerMode::PrePost) {
                    state_.accumulated_pre_fire = 0.0f;
                    state_.phase_start = now;
                    TransitionTo(TriggerbotState::Phase::PreFire);
                } else {
                    // Disparo imediato
                    state_.last_shot = now;
                    state_.shots_in_burst = 1;
                    if (config_.mode == TriggerMode::Burst) {
                        TransitionTo(TriggerbotState::Phase::Firing);
                    } else if (config_.post_fire_ms > 0) {
                        TransitionTo(TriggerbotState::Phase::PostFire);
                    } else {
                        TransitionTo(TriggerbotState::Phase::Cooldown);
                    }
                    return true; // FIRE!
                }
            }
            break;
        }

        case TriggerbotState::Phase::PreFire: {
            float elapsed = duration_cast<duration<float, std::milli>>(now - state_.phase_start).count();
            float required = GetRandomizedDelay(config_.pre_fire_ms);

            if (elapsed >= required) {
                // Pre-fire completo - dispara
                state_.last_shot = now;
                state_.shots_in_burst = 1;

                if (config_.mode == TriggerMode::Burst) {
                    TransitionTo(TriggerbotState::Phase::Firing);
                } else if (config_.post_fire_ms > 0) {
                    TransitionTo(TriggerbotState::Phase::PostFire);
                } else {
                    TransitionTo(TriggerbotState::Phase::Cooldown);
                }
                return true; // FIRE!
            } else {
                // Verifica se ainda válido durante pre-fire
                if (!target_valid || !crosshair_on_target || !can_fire) {
                    TransitionTo(TriggerbotState::Phase::Idle);
                }
            }
            break;
        }

        case TriggerbotState::Phase::Firing: {
            // Modo burst - dispara tiros em sequência
            if (state_.shots_in_burst < config_.burst_shots) {
                float elapsed = duration_cast<duration<float, std::milli>>(now - state_.last_shot).count();
                if (elapsed >= config_.burst_interval_ms) {
                    state_.last_shot = now;
                    state_.shots_in_burst++;
                    return true; // FIRE!
                }
            } else {
                // Rajada completa
                if (config_.post_fire_ms > 0) {
                    TransitionTo(TriggerbotState::Phase::PostFire);
                } else {
                    TransitionTo(TriggerbotState::Phase::BurstCooldown);
                }
            }
            break;
        }

        case TriggerbotState::Phase::PostFire: {
            float elapsed = duration_cast<duration<float, std::milli>>(now - state_.last_shot).count();
            if (elapsed >= config_.post_fire_ms) {
                TransitionTo(TriggerbotState::Phase::Cooldown);
            }
            break;
        }

        case TriggerbotState::Phase::BurstCooldown: {
            float elapsed = duration_cast<duration<float, std::milli>>(now - state_.last_shot).count();
            if (elapsed >= config_.burst_cooldown_ms) {
                TransitionTo(TriggerbotState::Phase::Cooldown);
            }
            break;
        }

        case TriggerbotState::Phase::Cooldown: {
            if (cooldown_ready) {
                TransitionTo(TriggerbotState::Phase::Idle);
            }
            break;
        }
    }

    return false; // Não atira neste frame
}

void Triggerbot::Reset() {
    state_ = TriggerbotState{};
}

const char* Triggerbot::GetPhaseName() const {
    switch (state_.phase) {
        case TriggerbotState::Phase::Idle: return "Idle";
        case TriggerbotState::Phase::PreFire: return "Pre-Fire";
        case TriggerbotState::Phase::Firing: return "Firing";
        case TriggerbotState::Phase::PostFire: return "Post-Fire";
        case TriggerbotState::Phase::BurstCooldown: return "Burst CD";
        case TriggerbotState::Phase::Cooldown: return "Cooldown";
        default: return "Unknown";
    }
}

void Triggerbot::SetWeaponAllowed(int weapon_index, bool allowed) {
    if (weapon_index >= 0 && weapon_index < 64) {
        config_.weapon_allowed[weapon_index] = allowed;
        if (allowed) config_.weapon_mask |= (1ULL << weapon_index);
        else config_.weapon_mask &= ~(1ULL << weapon_index);
    }
}

void Triggerbot::SetWeaponMask(TriggerbotConfig::WeaponMask mask) {
    config_.weapon_mask = mask;
    for (int i = 0; i < 64; ++i) {
        config_.weapon_allowed[i] = (mask & (1ULL << i)) != 0;
    }
}

bool Triggerbot::IsWeaponAllowed(int weapon_index) const {
    if (weapon_index < 0 || weapon_index >= 64) return false;
    return config_.weapon_allowed[weapon_index];
}

bool Triggerbot::CheckTargetValid(uint64_t entity_id, float distance, bool visible,
                                  int weapon_idx, bool knocked, bool teammate,
                                  bool npc, float hp_pct) const {
    if (entity_id == 0) return false;
    if (distance > config_.max_distance_m) return false;
    if (config_.visible_only && !visible) return false;
    if (config_.ignore_knocked && knocked) return false;
    if (config_.ignore_teammates && teammate) return false;
    if (config_.ignore_npcs && npc) return false;
    if (hp_pct < config_.min_health_percent) return false;
    if (!IsWeaponAllowed(weapon_idx)) return false;
    return true;
}

float Triggerbot::GetRandomizedDelay(float base_ms) const {
    if (!config_.simulate_human_reaction || config_.reaction_variance_ms <= 0) {
        return base_ms;
    }
    std::normal_distribution<float> dist(base_ms, config_.reaction_variance_ms);
    float result = dist(rng_);
    return std::max(1.0f, result); // Mínimo 1ms
}

void Triggerbot::TransitionTo(TriggerbotState::Phase new_phase) {
    state_.phase = new_phase;
    state_.phase_start = std::chrono::steady_clock::now();
}

} // namespace OmniGhost::Gameplay
