#include "unified_aim.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <sstream>
#include <fstream>

#include "../../ImGui/imgui.h"
#include "../../ImGui/imgui_internal.h"

namespace Gameplay::UnifiedAim {

    UnifiedAimbot::UnifiedAimbot() {
        InitializeSubSystems();
        RegisterDefaultProfiles();
    }
    
    UnifiedAimbot::~UnifiedAimbot() = default;
    
    void UnifiedAimbot::InitializeSubSystems() {
        // Smooth curves evaluator
        smooth_evaluator_ = std::make_unique<Gameplay::SmoothCurves::SmoothCurveEvaluator>(config_.smoothing);
        
        // Prediction
        predictor_ = std::make_unique<Gameplay::Prediction::EntityPredictor>(0);
        predictor_->SetConfig(config_.prediction);
        
        // Visibility
        visibility_mgr_ = std::make_unique<Gameplay::Visibility::VisibilityManager>();
        visibility_mgr_->SetConfig(config_.visibility);
        
        // Recoil control
        recoil_controller_ = std::make_unique<Gameplay::RecoilControl::RecoilController>();
        recoil_controller_->SetConfig(config_.recoil);
        
        // Triggerbot
        triggerbot_mgr_ = std::make_unique<Gameplay::Triggerbot::TriggerbotManager>();
        triggerbot_mgr_->SetConfig(config_.triggerbot.config);
        
        // Movement assist (CS2)
        movement_mgr_ = std::make_unique<Gameplay::MovementAssist::MovementAssistManager>();
        movement_mgr_->SetConfig(config_.movement.config);
        
        // Entity cache
        entity_cache_ = std::make_unique<Gameplay::EntityCache::EntityCache>();
        
        // Bone mapping
        bone_mapping_ = std::make_unique<Gameplay::BoneSystem::GameBoneMapping>();
    }
    
    void UnifiedAimbot::RegisterDefaultProfiles() {
        // These will be loaded from ProfileManager
    }
    
    void UnifiedAimbot::SetConfig(const UnifiedConfig& config) {
        config_ = config;
        
        // Update sub-systems
        if (smooth_evaluator_) smooth_evaluator_->SetConfig(config.smoothing);
        if (predictor_) predictor_->SetConfig(config_.prediction);
        if (visibility_mgr_) visibility_mgr_->SetConfig(config_.visibility);
        if (recoil_controller_) recoil_controller_->SetConfig(config_.recoil);
        if (triggerbot_mgr_) triggerbot_mgr_->SetConfig(config_.triggerbot.config);
        if (movement_mgr_) movement_mgr_->SetConfig(config_.movement.config);
    }
    
    AimResult UnifiedAimbot::Update(const AimContext& context) {
        AimResult result;
        
        // Safety checks
        if (!CheckSafetyConditions(context)) {
            state_.active = false;
            state_.can_fire = false;
            return result;
        }
        
        // Check aim key
        bool aim_key = IsAimKeyDown();
        if (!aim_key) {
            if (state_.active) {
                CancelTarget();
            }
            return result;
        }
        
        // Update sub-systems
        UpdateSubSystems(context.frame_time);
        
        // Process aim
        result = ProcessAim(context);
        
        // Update state
        state_.active = result.success;
        state_.current_target = result.target_id;
        
        return result;
    }
    
    AimResult UnifiedAimbot::ProcessAim(const AimContext& context) {
        AimResult result;
        
        // Select best target
        uint64_t target_id = SelectBestTarget(context);
        if (target_id == 0) {
            if (state_.locked_target != 0) {
                CancelTarget();
            }
            return result;
        }
        
        // Validate target
        if (!CheckTargetValidity(context, target_id)) {
            return result;
        }
        
        // Check targeting conditions
        if (!CheckTargetingConditions(context, target_id)) {
            return result;
        }
        
        // Calculate aim
        result = CalculateAim(context, target_id);
        if (!result.success) return result;
        
        // Update state
        state_.current_target = target_id;
        state_.locked_target = target_id;
        state_.tracking_active = true;
        state_.lock_start = std::chrono::steady_clock::now();
        
        return result;
    }
    
    uint64_t UnifiedAimbot::SelectBestTarget(const AimContext& context) {
        float best_score = 1e9f;
        uint64_t best_target = 0;
        
        for (uint64_t entity_id : context.entity_list) {
            // Check basic validity
            if (!context.is_entity_alive(entity_id)) continue;
            if (context.is_entity_dormant(entity_id)) continue;
            if (entity_id == 0) continue;
            
            // Check targeting conditions
            if (!CheckTargetingConditions(context, entity_id)) continue;
            
            // Get distance and FOV
            float distance = GetDistanceToTarget(context, entity_id);
            if (distance < config_.targeting.min_distance || distance > config_.targeting.max_distance) continue;
            
            Vec3 target_pos = ResolveHitbox(context, entity_id);
            if (target_pos.IsZero()) continue;
            
            float fov = GetFOVToTarget(context, target_pos);
            if (fov > config_.targeting.fov) continue;
            
            // Check visibility
            int bone = SelectBestBone(context, entity_id, distance);
            if (!CheckVisibility(context, entity_id, bone)) continue;
            
            // Score target
            float score = ScoreTarget(context, entity_id, distance, fov);
            
            if (score < best_score) {
                best_score = score;
                best_target = entity_id;
            }
        }
        
        return best_target;
    }
    
    float UnifiedAimbot::ScoreTarget(const AimContext& context, uint64_t target_id, float distance, float fov) {
        const auto& targeting = config_.targeting;
        float score = 0.0f;
        
        switch (targeting.priority) {
            case UnifiedConfig::Targeting::Priority::CrosshairDistance:
                score = fov;
                break;
            case UnifiedConfig::Targeting::Priority::Distance:
                score = distance;
                break;
            case UnifiedConfig::Targeting::Priority::Threat: {
                float health = context.get_entity_health(target_id);
                float dist_factor = 1.0f - (distance / config_.targeting.max_distance);
                score = fov * 0.5f + (1.0f - health / 100.0f) * 0.3f + dist_factor * 0.2f;
                break;
            }
            case UnifiedConfig::Targeting::Priority::Health: {
                float health = context.get_entity_health(target_id);
                score = (100.0f - health) * 10.0f + fov;
                break;
            }
            case UnifiedConfig::Targeting::Priority::Velocity: {
                Vec3 vel = context.get_entity_velocity(target_id);
                score = fov + vel.Length2D() * 0.1f;
                break;
            }
            case UnifiedConfig::Targeting::Priority::Exposure: {
                // Prefer more exposed targets
                score = fov; // Would need visibility percentage
                break;
            }
            default:
                score = fov;
        }
        
        // Apply distance modifier
        if (targeting.priority != UnifiedConfig::Targeting::Priority::Distance) {
            score += distance * 0.01f;
        }
        
        return score;
    }
    
    bool UnifiedAimbot::CheckTargetingConditions(const AimContext& context, uint64_t target_id) {
        const auto& targeting = config_.targeting;
        
        // Teammates
        if (targeting.ignore_teammates && context.is_entity_friendly(target_id)) return false;
        if (targeting.ignore_friends && context.is_entity_friendly(target_id)) return false;
        
        // Dead check
        if (targeting.ignore_dead && !context.is_entity_alive(target_id)) return false;
        
        // Health
        int health = context.get_entity_health(target_id);
        if (health < targeting.min_health || health > targeting.max_health) return false;
        
        // Distance
        float distance = GetDistanceToTarget(context, target_id);
        if (distance < targeting.min_distance || distance > targeting.max_distance) return false;
        
        return true;
    }
    
    bool UnifiedAimbot::CheckSafetyConditions(const AimContext& context) {
        const auto& safety = config_.safety;
        
        if (safety.disable_in_menu && context.in_menu) return false;
        if (safety.disable_when_typing && context.typing) return false;
        
        if (safety.require_aim_key && safety.aim_key > 0) {
            if (!IsKeyPressed(safety.aim_key)) return false;
        }
        
        return true;
    }
    
    bool UnifiedAimbot::IsAimKeyDown() const {
        if (config_.aim_bind > 0 && IsKeyPressed(config_.aim_bind)) return true;
        if (config_.aim_bind2 > 0 && IsKeyPressed(config_.aim_bind2)) return true;
        return false;
    }
    
    bool UnifiedAimbot::IsKeyPressed(int key) const {
        if (key <= 0) return false;
        return (GetAsyncKeyState(key) & 0x8000) != 0;
    }
    
    float UnifiedAimbot::GetDistanceToTarget(const AimContext& context, uint64_t target_id) {
        if (context.get_entity_distance) {
            return context.get_entity_distance(target_id);
        }
        
        Vec3 target_pos = context.get_entity_position(target_id);
        if (target_pos.IsZero()) return 999999.0f;
        
        return context.local_position.DistTo(target_pos);
    }
    
    float UnifiedAimbot::GetFOVToTarget(const AimContext& context, const Vec3& target_pos) {
        Vec2 screen_pos;
        // Simplified - would use WorldToScreen
        Vec3 rel = target_pos - context.local_position;
        float dist = rel.Length2D();
        if (dist < 1.0f) return 0.0f;
        
        // Simplified FOV calculation
        float angle = atan2f(rel.x, rel.z) * 180.0f / 3.14159f;
        return fabsf(angle);
    }
    
    bool UnifiedAimbot::CheckVisibility(const AimContext& context, uint64_t target_id, int bone_index) {
        if (!config_.visibility.enabled) return true;
        if (!context.is_entity_visible) return true; // No visibility check available
        
        return context.is_entity_visible(target_id);
    }
    
    int UnifiedAimbot::SelectBestBone(const AimContext& context, uint64_t target_id, float distance) {
        const auto& targeting = config_.targeting;
        
        // Use bone mapping for this game
        if (bone_mapping_) {
            const auto& mapping = *bone_mapping_;
            
            // Try hitbox preset first
            if (targeting.hitbox_preset != Gameplay::BoneSystem::HitboxPreset::Custom) {
                auto priority_list = Gameplay::BoneSystem::GetDefaultPriorityList(targeting.hitbox_preset);
                for (int i = 0; i < targeting.bone_switch_distance && i < 8; ++i) {
                    Gameplay::BoneSystem::BoneId bone = priority_list[i];
                    int game_bone = Gameplay::BoneSystem::UnifiedToGameBone(bone, mapping);
                    if (game_bone >= 0 && mapping.bone_valid[game_bone]) {
                        if (targeting.visible_bone_only) {
                            if (context.is_bone_visible && context.is_bone_visible(target_id, game_bone)) {
                                return game_bone;
                            }
                        } else {
                            return game_bone;
                        }
                    }
                }
            }
            
            // Custom priority list
            if (!targeting.hitbox_priority.empty()) {
                for (int bone : targeting.hitbox_priority) {
                    if (bone >= 0 && bone < 36 && mapping.bone_valid[bone]) {
                        if (targeting.visible_bone_only) {
                            if (context.is_bone_visible && context.is_bone_visible(target_id, bone)) {
                                return bone;
                            }
                        } else {
                            return bone;
                        }
                    }
                }
            }
        }
        
        // Fallback to head
        int head_bone = bone_mapping_ ? bone_mapping_->head_bone : 0;
        return head_bone;
    }
    
    Vec3 UnifiedAimbot::ResolveHitbox(const AimContext& context, uint64_t target_id) {
        int bone = SelectBestBone(context, target_id, GetDistanceToTarget(context, target_id));
        if (context.get_entity_bone) {
            return context.get_entity_bone(target_id, bone);
        }
        return context.get_entity_position(target_id);
    }
    
    bool UnifiedAimbot::CheckTargetValidity(const AimContext& context, uint64_t target_id) {
        if (target_id == 0) return false;
        if (!context.is_entity_alive(target_id)) return false;
        if (context.is_entity_dormant(target_id)) return false;
        
        Vec3 pos = ResolveHitbox(context, target_id);
        if (pos.IsZero()) return false;
        
        return true;
    }
    
    AimResult UnifiedAimbot::CalculateAim(const AimContext& context, uint64_t target_id) {
        AimResult result;
        
        // Resolve target position (with prediction)
        Vec3 target_pos = ResolveHitbox(context, target_id);
        if (target_pos.IsZero()) {
            result.debug[0] = '\0';
            return result;
        }
        
        // Apply prediction
        float distance = GetDistanceToTarget(context, target_id);
        float prediction_time = config_.prediction.prediction_time;
        
        if (config_.prediction.enabled && config_.prediction.velocity_prediction) {
            // Scale prediction by distance
            if (config_.prediction.distance_scaling) {
                if (distance < config_.prediction.close_range) {
                    prediction_time *= config_.prediction.close_scale;
                } else if (distance < config_.prediction.mid_range) {
                    prediction_time *= config_.prediction.mid_scale;
                } else if (distance < config_.prediction.far_range) {
                    prediction_time *= config_.prediction.far_scale;
                } else {
                    prediction_time *= config_.prediction.far_scale * 0.5f;
                }
            }
            
            // Cap prediction time
            prediction_time = std::min(prediction_time, config_.prediction.max_prediction_time);
            
            // Get predicted position
            Vec3 predicted = PredictTargetPosition(context, target_id, prediction_time);
            if (!predicted.IsZero()) {
                target_pos = predicted;
            }
        }
        
        // World to screen
        Vec2 screen_pos;
        // Simplified - would use actual WorldToScreen
        // For now, calculate from world position
        Vec3 rel = target_pos - context.local_position;
        float dist = rel.Length2D();
        float angle = atan2f(rel.x, rel.z) * 180.0f / 3.14159f;
        
        result.world_target = target_pos;
        result.distance = distance;
        result.fov_to_target = fabsf(angle);
        result.target_id = target_id;
        
        // Calculate screen position delta
        float fov_rad = config_.targeting.fov * 3.14159f / 180.0f;
        float screen_fov = tanf(fov_rad) * (context.screen_size.y * 0.5f);
        float pixels_per_deg = screen_fov / config_.targeting.fov;
        
        result.screen_target.x = context.screen_center.x + angle * pixels_per_deg;
        result.screen_target.y = context.screen_center.y; // Simplified
        
        // Calculate mouse delta
        Vec2 raw_delta = result.screen_target - context.screen_center;
        
        // Apply smoothing
        Vec2 smoothed_delta = ApplySmoothing(raw_delta, context, distance);
        
        // Apply recoil compensation
        Vec2 recoil_comp = GetRecoilCompensation(context, context.frame_time);
        smoothed_delta = smoothed_delta + recoil_comp;
        
        // Apply humanization
        smoothed_delta = ApplyHumanization(smoothed_delta, distance, context.frame_time);
        
        result.mouse_delta = smoothed_delta;
        result.success = true;
        result.confidence = 1.0f;
        result.hitbone = 0; // Would be actual bone
        
        // Update state
        state_.tracking_confidence = result.confidence;
        
        // Debug
        snprintf(result.debug, sizeof(result.debug), 
            "Target: %llu | Dist: %.1fm | FOV: %.1f | Delta: %.1f,%.1f",
            target_id, distance, result.fov_to_target, result.mouse_delta.x, result.mouse_delta.y);
        
        return result;
    }
    
    Vec2 UnifiedAimbot::ApplySmoothing(const Vec2& raw_delta, const AimContext& context, float distance) {
        const auto& smoothing = config_.smoothing;
        
        if (smoothing.smooth <= 0) return raw_delta;
        
        // Calculate effective smoothing
        float effective_smooth = smoothing.smooth;
        
        // Distance-based smoothing
        if (smoothing.distance_based && smoothing.use_distance_curves) {
            float curve_val = smoothing.distance_curves.Evaluate(1.0f, distance);
            effective_smooth *= curve_val;
        }
        
        // Velocity-based
        if (smoothing.velocity_based) {
            float speed = context.local_speed;
            effective_smooth *= (1.0f + speed * 0.01f);
        }
        
        // Angle-based (FOV)
        if (smoothing.fov_based) {
            // Would calculate based on FOV
        }
        
        // Clamp
        effective_smooth = std::clamp(effective_smooth, smoothing.smooth_min, smoothing.smooth_max);
        
        // Apply curve
        float strength = 1.0f - (effective_smooth / 100.0f);
        strength = std::clamp(strength, 0.0f, 1.0f);
        
        // Apply curve evaluation
        if (smoothing.curve_type == Gameplay::SmoothCurves::CurveType::Bezier) {
            strength = smooth_evaluator_->Evaluate(strength);
        } else if (smoothing.curve_type == Gameplay::SmoothCurves::CurveType::CatmullRom) {
            // Would use spline
        }
        
        // Apply strength
        Vec2 result = raw_delta * strength;
        
        // Apply acceleration/deceleration limits
        float max_delta = smoothing.max_angular_velocity * context.frame_time;
        float len = result.Length();
        if (len > max_delta && len > 0) {
            result = result * (max_delta / len);
        }
        
        return result;
    }
    
    Vec2 UnifiedAimbot::ApplyHumanization(const Vec2& delta, float distance, float dt) {
        const auto& humanizer = config_.smoothing.humanizer;
        if (!humanizer.enabled) return delta;
        
        Vec2 result = delta;
        
        // Distance multiplier
        float dist_mult = 1.0f;
        if (distance < 30) dist_mult = humanizer.close_range_multiplier;
        else if (distance < 80) dist_mult = humanizer.mid_range_multiplier;
        else dist_mult = humanizer.far_range_multiplier;
        
        static std::mt19937 rng(std::random_device{}());
        static std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        
        // Micro jitter
        if (humanizer.micro_jitter > 0) {
            result.x += dist(rng) * humanizer.micro_jitter * dist_mult;
            result.y += dist(rng) * humanizer.micro_jitter * dist_mult;
        }
        
        // Overshoot
        if (humanizer.overshoot_chance > 0) {
            static std::uniform_real_distribution<float> chance(0, 1);
            if (chance(rng) < humanizer.overshoot_chance) {
                result.x *= 1.0f + humanizer.overshoot_amount;
                result.y *= 1.0f + humanizer.overshoot_amount;
            }
        }
        
        return result;
    }
    
    Vec3 UnifiedAimbot::PredictTargetPosition(const AimContext& context, uint64_t target_id, float time_ahead) {
        // Use predictor if available
        if (predictor_) {
            predictor_->Update(context.get_entity_position(target_id), 
                             &context.get_entity_velocity(target_id));
            return predictor_->Predict(time_ahead).predicted_position;
        }
        
        // Simple prediction fallback
        Vec3 pos = context.get_entity_position(target_id);
        Vec3 vel = context.get_entity_velocity(target_id);
        return pos + vel * time_ahead;
    }
    
    Vec2 UnifiedAimbot::GetRecoilCompensation(const AimContext& context, float dt) {
        if (!config_.recoil.enabled || !recoil_controller_) return Vec2(0, 0);
        
        if (!config_.recoil.only_when_aiming || context.attack_key_down) {
            recoil_controller_->OnShotFired();
            return recoil_controller_->GetCompensation(dt, context.local_angles);
        }
        return Vec2(0, 0);
    }
    
    void UnifiedAimbot::OnShotFired() {
        if (recoil_controller_) {
            recoil_controller_->OnShotFired();
        }
    }
    
    void UnifiedAimbot::OnWeaponReload() {
        if (recoil_controller_) {
            recoil_controller_->ResetWeapon("");
        }
    }
    
    void UnifiedAimbot::OnWeaponSwap() {
        if (recoil_controller_) {
            recoil_controller_->ResetWeapon("");
        }
    }
    
    void UnifiedAimbot::UpdateMovementAssist(float dt) {
        if (movement_mgr_) {
            movement_mgr_->Update(dt, 0);
        }
    }
    
    void UnifiedAimbot::UpdateSubSystems(float dt) {
        if (predictor_) {
            // Predictor updates automatically on Update()
        }
        if (visibility_mgr_) {
            visibility_mgr_->Update(dt);
        }
        if (movement_mgr_) {
            movement_mgr_->Update(dt, 0);
        }
    }
    
    void UnifiedAimbot::ForceTarget(uint64_t target_id) {
        state_.locked_target = target_id;
        state_.active = true;
    }
    
    void UnifiedAimbot::CancelTarget() {
        state_.locked_target = 0;
        state_.active = false;
        state_.tracking_active = false;
    }
    
    void UnifiedAimbot::ApplyProfile(const std::string& profile_name) {
        // Would load from ProfileManager
        config_.profiles.current_profile = profile_name;
    }
    
    void UnifiedAimbot::SaveProfile(const std::string& name) {
        // Would save to ProfileManager
    }
    
    void UnifiedAimbot::LoadProfile(const std::string& name) {
        ApplyProfile(name);
    }
    
    std::vector<std::string> UnifiedAimbot::GetAvailableProfiles() const {
        return {"Legit", "Competitive", "Rage", "Sniper", "Custom"};
    }
    
    void UnifiedAimbot::ApplyProfileSettings(const std::string& profile_name) {
        if (profile_name == "Legit") {
            config_.targeting.fov = 3.0f;
            config_.smoothing.smooth = 15.0f;
            config_.smoothing.curve_type = Gameplay::SmoothCurves::CurveType::EaseOutCubic;
            config_.smoothing.humanize = true;
            config_.smoothing.humanizer.micro_jitter = 0.3f;
            config_.prediction.prediction_time = 0.03f;
            config_.recoil.enabled = false;
        } else if (profile_name == "Competitive") {
            config_.targeting.fov = 4.0f;
            config_.smoothing.smooth = 8.0f;
            config_.smoothing.curve_type = Gameplay::SmoothCurves::CurveType::EaseOutCubic;
            config_.smoothing.humanize = true;
            config_.smoothing.humanizer.micro_jitter = 0.15f;
            config_.prediction.prediction_time = 0.02f;
            config_.recoil.enabled = true;
        } else if (profile_name == "Rage") {
            config_.targeting.fov = 180.0f;
            config_.smoothing.smooth = 0.0f;
            config_.smoothing.curve_type = Gameplay::SmoothCurves::CurveType::Linear;
            config_.smoothing.humanize = false;
            config_.prediction.prediction_time = 0.0f;
            config_.recoil.enabled = true;
            config_.recoil.mode = Gameplay::RecoilControl::RecoilControlConfig::Mode::Rage;
        } else if (profile_name == "Sniper") {
            config_.targeting.fov = 0.5f;
            config_.targeting.max_distance = 500.0f;
            config_.smoothing.smooth = 20.0f;
            config_.smoothing.curve_type = Gameplay::SmoothCurves::CurveType::EaseOutQuint;
            config_.targeting.hitbox_preset = Gameplay::BoneSystem::HitboxPreset::Head;
        }
        config_.profiles.current_profile = profile_name;
    }
    
    void UnifiedAimbot::DrawDebug(ImDrawList* draw_list) {
        if (!draw_list) return;
        
        if (config_.visuals.show_fov) {
            DrawFOV(draw_list);
        }
        if (config_.visuals.show_target && state_.current_target != 0) {
            DrawTargetInfo(nullptr, AimContext{}); // Would need context
        }
        if (config_.visuals.show_prediction) {
            DrawPredictionDebug(nullptr, AimContext{});
        }
        if (config_.visuals.show_bone_debug) {
            DrawBoneDebug(nullptr, AimContext{}, state_.current_target);
        }
        if (config_.visuals.show_visibility_debug) {
            DrawVisibilityDebug(nullptr, AimContext{}, state_.current_target);
        }
    }
    
    void UnifiedAimbot::DrawFOV(ImDrawList* draw_list) {
        if (!draw_list) return;
        
        ImVec2 center = ImGui::GetIO().DisplaySize * 0.5f;
        float fov_px = tanf(config_.targeting.fov * 3.14159f / 360.0f) * (ImGui::GetIO().DisplaySize.y * 0.5f);
        
        draw_list->AddCircle(center, fov_px, config_.visuals.fov_color, 64, 1.5f);
    }
    
    void UnifiedAimbot::DrawTargetInfo(ImDrawList* draw_list, const AimContext& context) {
        // Would draw target info
    }
    
    void UnifiedAimbot::DrawPredictionDebug(ImDrawList* draw_list, const AimContext& context) {
        // Would draw prediction debug
    }
    
    void UnifiedAimbot::DrawBoneDebug(ImDrawList* draw_list, const AimContext& context, uint64_t target_id) {
        // Would draw bone debug
    }
    
    void UnifiedAimbot::DrawVisibilityDebug(ImDrawList* draw_list, const AimContext& context, uint64_t target_id) {
        // Would draw visibility debug
    }
    
    UnifiedAimbot& GetUnifiedAimbot() {
        static UnifiedAimbot instance;
        return instance;
    }
    
    std::unique_ptr<UnifiedAimbot> CreateAimbotForGame(const char* game_name) {
        auto aimbot = std::make_unique<UnifiedAimbot>();
        
        // Configure bone mapping for specific game
        if (strcmp(game_name, "FiveM") == 0) {
            aimbot->current_bone_mapping_ = Gameplay::BoneSystem::GetGameMapping("FiveM");
        } else if (strcmp(game_name, "CS2") == 0) {
            aimbot->current_bone_mapping_ = Gameplay::BoneSystem::GetGameMapping("CS2");
        } else if (strcmp(game_name, "Warzone") == 0) {
            aimbot->current_bone_mapping_ = Gameplay::BoneSystem::GetGameMapping("Warzone");
        }
        
        return aimbot;
    }

} // namespace Gameplay::UnifiedAim
