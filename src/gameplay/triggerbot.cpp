#include "triggerbot.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <sstream>

namespace Gameplay::Triggerbot {

    TriggerbotManager& TriggerbotManager::Instance() {
        static TriggerbotManager instance;
        return instance;
    }

    void TriggerbotManager::Update(float dt, uint64_t local_player, const std::vector<uint64_t>& entities) {
        if (!config_.enabled) {
            state_.active = false;
            return;
        }
        
        state_.active = true;
        UpdateTiming(dt);
        
        // Safety checks
        if (config_.safety.disable_in_menu) {
            // Would check if menu open
        }
        if (config_.safety.disable_when_typing) {
            // Would check if typing
        }
        if (config_.safety.require_aim_key && config_.safety.aim_key > 0) {
            if (!IsKeyPressed(config_.safety.aim_key)) {
                state_.can_fire = false;
                return;
            }
        }
        if (config_.safety.disable_on_reload && IsReloading()) {
            state_.can_fire = false;
            return;
        }
        if (config_.safety.disable_on_weapon_swap) {
            // Would check weapon swap
        }
        
        // Find best target
        uint64_t best_target = 0;
        float best_score = 1e9f;
        
        for (uint64_t entity : entities) {
            if (entity == local_player) continue;
            
            if (CheckTargeting(local_player, entity)) {
                if (CheckConditions(local_player, entity)) {
                    float score = GetFOVToEntity(entity); // Prioritize closest to crosshair
                    if (score < best_score) {
                        best_score = score;
                        best_target = entity;
                    }
                }
            }
        }
        
        state_.current_target = best_target;
        
        // Execute trigger
        if (best_target != 0 && state_.can_fire && best_score <= config_.targeting.fov) {
            if (state_.next_shot_time <= 0.0f) {
                ExecuteActions(best_target);
            }
        }
    }

    void TriggerbotManager::UpdateTiming(float dt) {
        if (state_.next_shot_time > 0) {
            state_.next_shot_time -= dt;
        }
        
        if (state_.in_burst) {
            if (state_.burst_end_time <= 0) {
                state_.in_burst = false;
                state_.burst_shots = 0;
                state_.next_shot_time = config_.timing.burst_cooldown;
            }
        }
        
        state_.last_condition_check = std::chrono::steady_clock::now();
    }

    bool TriggerbotManager::CheckConditions(uint64_t local_player, uint64_t target) {
        for (const auto& cond : config_.conditions) {
            if (!CheckCondition(cond, local_player, target)) {
                return false;
            }
        }
        return true;
    }

    bool TriggerbotManager::CheckCondition(const TriggerCondition& cond, uint64_t local_player, uint64_t target) {
        bool result = false;
        
        switch (cond.type) {
            case ConditionType::Always:
                result = true;
                break;
            case ConditionType::OnKeyHold:
                result = IsKeyPressed(cond.key);
                break;
            case ConditionType::OnKeyToggle:
                // Would need toggle state tracking
                result = true;
                break;
            case ConditionType::WhenAiming:
                result = IsScoping() || IsKeyPressed(0x02); // Right click or scope
                break;
            case ConditionType::WhenScoping:
                result = IsScoping();
                break;
            case ConditionType::WhenCrouching:
                result = IsCrouching();
                break;
            case ConditionType::WhenMoving:
                result = IsMoving();
                break;
            case ConditionType::WhenStanding:
                result = !IsCrouching() && !IsInAir();
                break;
            case ConditionType::InAir:
                result = IsInAir();
                break;
            case ConditionType::OnGround:
                result = !IsInAir();
                break;
            case ConditionType::TargetVisible:
                result = IsEntityVisible(target);
                break;
            case ConditionType::TargetInFOV:
                result = GetFOVToEntity(target) <= config_.targeting.fov;
                break;
            case ConditionType::TargetDistance: {
                float dist = GetEntityDistance(target);
                result = dist >= cond.value && dist <= cond.value2;
                break;
            }
            case ConditionType::TargetHealth: {
                float health = GetEntityHealth(target);
                result = health >= cond.value && health <= cond.value2;
                break;
            }
            case ConditionType::WeaponType:
                result = GetWeaponType() == (int)cond.value;
                break;
            case ConditionType::AmmoCount:
                result = GetAmmoCount() >= (int)cond.value;
                break;
            case ConditionType::Custom:
                // Would evaluate custom script
                result = true;
                break;
        }
        
        return cond.inverted ? !result : result;
    }

    bool TriggerbotManager::CheckTargeting(uint64_t local_player, uint64_t target) {
        if (target == local_player) return false;
        
        if (config_.targeting.ignore_teammates && IsTeammate(target)) return false;
        if (config_.targeting.ignore_friends && IsFriend(target)) return false;
        if (config_.targeting.ignore_dead && IsEntityDead(target)) return false;
        
        float health = GetEntityHealth(target);
        if (health < config_.targeting.min_health || health > config_.targeting.max_health) return false;
        
        float dist = GetEntityDistance(target);
        if (dist < config_.targeting.min_distance || dist > config_.targeting.max_distance) return false;
        
        if (config_.targeting.require_visible && !IsEntityVisible(target)) return false;
        
        float fov = GetFOVToEntity(target);
        if (fov > config_.targeting.fov) return false;
        
        // Check hitbox priority
        if (!config_.targeting.hitboxes.empty()) {
            // Would check if target's visible hitbox is in list
        }
        
        return true;
    }

    void TriggerbotManager::ExecuteActions(uint64_t target) {
        state_.active = true;
        state_.shots_fired++;
        state_.last_shot_time = std::chrono::duration<float>(std::chrono::steady_clock::now().time_since_epoch()).count();
        
        // Calculate next shot time
        float delay = config_.timing.min_delay;
        if (config_.timing.randomize_delay && config_.timing.max_delay > config_.timing.min_delay) {
            static std::mt19937 rng(std::random_device{}());
            std::uniform_real_distribution<float> dist(config_.timing.min_delay, config_.timing.max_delay);
            delay = dist(rng);
        }
        delay += config_.timing.reaction_time;
        
        if (config_.timing.randomize_delay && config_.timing.reaction_variance > 0) {
            static std::mt19937 rng(std::random_device{}());
            std::uniform_real_distribution<float> var(-config_.timing.reaction_variance, config_.timing.reaction_variance);
            delay += delay * var(rng);
        }
        
        state_.next_shot_time = delay;
        
        // Handle burst
        if (config_.timing.burst_time > 0 && config_.timing.max_burst_shots > 0) {
            if (!state_.in_burst) {
                state_.in_burst = true;
                state_.burst_shots = 0;
                state_.burst_end_time = config_.timing.burst_time;
            }
            
            if (state_.in_burst) {
                state_.burst_shots++;
                if (state_.burst_shots >= config_.timing.max_burst_shots) {
                    state_.in_burst = false;
                    state_.next_shot_time = config_.timing.burst_cooldown;
                }
            }
        }
        
        // Execute actions
        for (const auto& action : config_.actions) {
            switch (action.type) {
                case TriggerAction::Type::LeftClick:
                    SimulateClick(0, true);
                    if (action.release_after) {
                        // Would schedule release
                    }
                    break;
                case TriggerAction::Type::RightClick:
                    SimulateClick(1, true);
                    break;
                case TriggerAction::Type::MiddleClick:
                    SimulateClick(2, true);
                    break;
                case TriggerAction::Type::KeyPress:
                    SimulateKeyPress(action.key, true);
                    break;
            }
        }
        
        // Humanization
        if (config_.humanize.enabled) {
            static std::mt19937 rng(std::random_device{}());
            std::uniform_real_distribution<float> jitter(-config_.humanize.micro_jitter, config_.humanize.micro_jitter);
            // Would add micro-movement
            
            if (config_.humanize.overshoot_chance > 0) {
                std::uniform_real_distribution<float> chance(0, 1);
                if (chance(rng) < config_.humanize.overshoot_chance) {
                    // Would add overshoot
                }
            }
        }
    }

    bool TriggerbotManager::IsKeyPressed(int key) const {
        if (key <= 0) return false;
        return (GetAsyncKeyState(key) & 0x8000) != 0;
    }

    void TriggerbotManager::DrawFOV(ImDrawList* draw_list) {
        if (!config_.visual.show_fov) return;
        if (!draw_list) return;
        
        ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
        float fov_px = tanf(config_.targeting.fov * 3.14159f / 180.0f) * (ImGui::GetIO().DisplaySize.y * 0.5f);
        
        draw_list->AddCircle(center, fov_px, config_.visual.fov_color, 64, 1.5f);
        
        if (state_.current_target != 0) {
            // Draw target indicator
            // Would need target screen position
        }
    }

    void TriggerbotManager::DrawStatus(ImDrawList* draw_list) {
        if (!config_.visual.show_status) return;
        
        ImVec2 pos(10, 10);
        ImU32 color = state_.can_fire ? config_.visual.ready_color : IM_COL32(200, 200, 200, 255);
        
        char buf[64];
        snprintf(buf, sizeof(buf), "Triggerbot: %s", state_.can_fire ? "READY" : "WAIT");
        draw_list->AddText(pos, color, buf);
        
        if (state_.current_target != 0) {
            pos.y += 20;
            snprintf(buf, sizeof(buf), "Target: %llu", state_.current_target);
            draw_list->AddText(pos, config_.visual.target_color, buf);
        }
    }

    void TriggerbotManager::ForceTrigger(uint64_t target) {
        if (CheckTargeting(0, target) && CheckConditions(0, target)) {
            ExecuteActions(target);
        }
    }

    void TriggerbotManager::CancelTrigger() {
        state_.active = false;
        state_.can_fire = false;
        state_.current_target = 0;
        state_.next_shot_time = 1.0f;
    }

    std::string TriggerbotManager::SerializeConfig() const {
        std::ostringstream out;
        out << config_.enabled << '|'
            << config_.conditions.size() << '|'
            << config_.actions.size() << '|'
            << config_.targeting.fov << '|'
            << config_.targeting.max_distance << '|'
            << config_.targeting.min_distance << '|'
            << config_.targeting.require_visible << '|'
            << config_.targeting.ignore_teammates << '|'
            << config_.targeting.ignore_friends << '|'
            << config_.targeting.ignore_dead << '|'
            << config_.targeting.min_health << '|'
            << config_.targeting.max_health << '|'
            << config_.targeting.hitboxes.size() << '|'
            << config_.timing.reaction_time << '|'
            << config_.timing.reaction_variance << '|'
            << config_.timing.min_delay << '|'
            << config_.timing.max_delay << '|'
            << config_.timing.randomize_delay << '|'
            << config_.timing.burst_time << '|'
            << config_.timing.burst_cooldown << '|'
            << config_.timing.max_burst_shots << '|'
            << config_.humanize.enabled << '|'
            << config_.humanize.micro_jitter << '|'
            << config_.humanize.reaction_variance << '|'
            << config_.humanize.click_variance << '|'
            << config_.humanize.overshoot_chance << '|'
            << config_.humanize.overshoot_amount << '|'
            << config_.humanize.simulate_recoil << '|'
            << config_.safety.disable_in_menu << '|'
            << config_.safety.disable_when_typing << '|'
            << config_.safety.require_aim_key << '|'
            << config_.safety.aim_key << '|'
            << config_.safety.disable_on_reload << '|'
            << config_.safety.disable_on_weapon_swap << '|'
            << config_.safety.max_shots_per_second << '|'
            << config_.safety.anti_afk << '|'
            << config_.visual.show_fov << '|'
            << config_.visual.show_target << '|'
            << config_.visual.show_status << '|'
            << config_.visual.fov_color << '|'
            << config_.visual.target_color << '|'
            << config_.visual.ready_color;
        return out.str();
    }

    bool TriggerbotManager::DeserializeConfig(const std::string& data) {
        std::istringstream in(data);
        std::string token;
        
        auto read_bool = [&](bool& b) {
            std::getline(in, token, '|');
            b = token == "1";
        };
        auto read_float = [&](float& f) {
            std::getline(in, token, '|');
            f = std::stof(token);
        };
        auto read_int = [&](int& i) {
            std::getline(in, token, '|');
            i = std::stoi(token);
        };
        auto read_uint = [&](ImU32& u) {
            std::getline(in, token, '|');
            u = std::stoul(token);
        };
        
        read_bool(config_.enabled);
        read_int(*reinterpret_cast<int*>(&config_.conditions.size()));
        read_int(*reinterpret_cast<int*>(&config_.actions.size()));
        read_float(config_.targeting.fov);
        read_float(config_.targeting.max_distance);
        read_float(config_.targeting.min_distance);
        read_bool(config_.targeting.require_visible);
        read_bool(config_.targeting.ignore_teammates);
        read_bool(config_.targeting.ignore_friends);
        read_bool(config_.targeting.ignore_dead);
        read_float(config_.targeting.min_health);
        read_float(config_.targeting.max_health);
        read_int(*reinterpret_cast<int*>(&config_.targeting.hitboxes.size()));
        read_float(config_.timing.reaction_time);
        read_float(config_.timing.reaction_variance);
        read_float(config_.timing.min_delay);
        read_float(config_.timing.max_delay);
        read_bool(config_.timing.randomize_delay);
        read_float(config_.timing.burst_time);
        read_float(config_.timing.burst_cooldown);
        read_int(config_.timing.max_burst_shots);
        read_bool(config_.humanize.enabled);
        read_float(config_.humanize.micro_jitter);
        read_float(config_.humanize.reaction_variance);
        read_float(config_.humanize.click_variance);
        read_float(config_.humanize.overshoot_chance);
        read_float(config_.humanize.overshoot_amount);
        read_bool(config_.humanize.simulate_recoil);
        read_bool(config_.safety.disable_in_menu);
        read_bool(config_.safety.disable_when_typing);
        read_bool(config_.safety.require_aim_key);
        read_int(config_.safety.aim_key);
        read_bool(config_.safety.disable_on_reload);
        read_bool(config_.safety.disable_on_weapon_swap);
        read_int(config_.safety.max_shots_per_second);
        read_bool(config_.safety.anti_afk);
        read_bool(config_.visual.show_fov);
        read_bool(config_.visual.show_target);
        read_bool(config_.visual.show_status);
        read_uint(config_.visual.fov_color);
        read_uint(config_.visual.target_color);
        read_uint(config_.visual.ready_color);
        
        return true;
    }

    TriggerbotConfig GetLegitTriggerbotConfig() {
        TriggerbotConfig cfg;
        cfg.enabled = true;
        
        // Conditions
        cfg.conditions.push_back({ConditionType::OnKeyHold, 0, 0, 0x02}); // Right click hold
        cfg.conditions.push_back({ConditionType::TargetVisible});
        cfg.conditions.push_back({ConditionType::TargetInFOV});
        
        // Actions
        cfg.actions.push_back({TriggerAction::Type::LeftClick, 0, 0.0f, 0.0f, true});
        
        // Targeting
        cfg.targeting.fov = 2.0f;
        cfg.targeting.max_distance = 200.0f;
        cfg.targeting.require_visible = true;
        cfg.targeting.ignore_teammates = true;
        cfg.targeting.ignore_friends = true;
        cfg.targeting.ignore_dead = true;
        cfg.targeting.hitboxes = {0, 1, 2}; // Head, neck, chest
        
        // Timing
        cfg.timing.reaction_time = 0.08f;
        cfg.timing.reaction_variance = 0.02f;
        cfg.timing.min_delay = 0.05f;
        cfg.timing.max_delay = 0.15f;
        cfg.timing.randomize_delay = true;
        cfg.timing.burst_time = 0.0f;
        
        // Humanize
        cfg.humanize.enabled = true;
        cfg.humanize.micro_jitter = 0.3f;
        cfg.humanize.reaction_variance = 0.03f;
        cfg.humanize.overshoot_chance = 0.03f;
        
        // Safety
        cfg.safety.disable_in_menu = true;
        cfg.safety.disable_when_typing = true;
        cfg.safety.disable_on_reload = true;
        cfg.safety.disable_on_weapon_swap = true;
        
        // Visual
        cfg.visual.show_fov = true;
        cfg.visual.show_status = true;
        
        return cfg;
    }

    TriggerbotConfig GetCompetitiveTriggerbotConfig() {
        TriggerbotConfig cfg = GetLegitTriggerbotConfig();
        cfg.targeting.fov = 4.0f;
        cfg.timing.reaction_time = 0.03f;
        cfg.timing.reaction_variance = 0.01f;
        cfg.humanize.micro_jitter = 0.1f;
        return cfg;
    }

    TriggerbotConfig GetSniperTriggerbotConfig() {
        TriggerbotConfig cfg = GetLegitTriggerbotConfig();
        cfg.targeting.fov = 0.5f;
        cfg.targeting.max_distance = 500.0f;
        cfg.targeting.hitboxes = {0}; // Head only
        cfg.timing.reaction_time = 0.15f;
        cfg.humanize.micro_jitter = 0.05f;
        return cfg;
    }

    TriggerbotConfig GetRageTriggerbotConfig() {
        TriggerbotConfig cfg;
        cfg.enabled = true;
        cfg.conditions.push_back({ConditionType::Always});
        cfg.actions.push_back({TriggerAction::Type::LeftClick});
        cfg.targeting.fov = 180.0f;
        cfg.targeting.max_distance = 500.0f;
        cfg.targeting.require_visible = false;
        cfg.timing.reaction_time = 0.0f;
        cfg.timing.min_delay = 0.0f;
        cfg.timing.max_delay = 0.0f;
        cfg.humanize.enabled = false;
        cfg.safety.disable_in_menu = false;
        return cfg;
    }

    std::string SerializeTriggerbotConfig(const TriggerbotConfig& config) {
        return ""; // Handled by manager
    }

    bool DeserializeTriggerbotConfig(const std::string& data, TriggerbotConfig& config) {
        return true; // Handled by manager
    }

} // namespace Gameplay::Triggerbot