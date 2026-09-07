#include "movement_assist.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <sstream>

namespace Gameplay::MovementAssist {

    MovementAssistManager& MovementAssistManager::Instance() {
        static MovementAssistManager instance;
        return instance;
    }

    void MovementAssistManager::Update(float dt, uint64_t local_player) {
        if (!config_.enabled || !config_.master_enable) return;
        if (config_.disable_in_menu && IsInMenu()) return;
        if (config_.disable_when_typing && IsTyping()) return;
        
        UpdateAutoStrafe(dt, local_player);
        UpdateAutoJump(dt, local_player);
        UpdateAutoCrouch(dt, local_player);
        UpdateEdgeJump(dt, local_player);
        
        last_update_ = std::chrono::steady_clock::now();
    }

    void MovementAssistManager::UpdateAutoStrafe(float dt, uint64_t local_player) {
        if (!config_.auto_strafe.enabled) return;
        if (config_.auto_strafe.disable_in_menu && IsInMenu()) return;
        if (config_.auto_strafe.disable_on_ladder && IsOnLadder()) return;
        if (config_.auto_strafe.disable_in_water && IsInWater()) return;
        if (config_.auto_strafe.require_forward && !IsKeyPressed(config_.auto_strafe.forward_key)) return;
        
        bool on_ground = IsOnGround();
        float speed = GetSpeed();
        
        if (speed < config_.auto_strafe.min_speed) return;
        if (config_.auto_strafe.max_speed > 0 && speed > config_.auto_strafe.max_speed) return;
        
        // Direction change logic
        if (config_.auto_strafe.random_direction_changes) {
            state_.next_direction_change -= dt;
            if (state_.next_direction_change <= 0) {
                static std::uniform_int_distribution<int> dir_dist(-1, 1);
                state_.strafe_direction = dir_dist(rng_) == 0 ? 1 : (dir_dist(rng_) > 0 ? 1 : -1);
                state_.next_direction_change = config_.auto_strafe.direction_change_interval;
            }
        }
        
        // Calculate strafe angle
        float target_angle = 0.0f;
        bool left = IsKeyPressed(config_.auto_strafe.left_key);
        bool right = IsKeyPressed(config_.auto_strafe.right_key);
        
        if (!left && !right) {
            // Auto strafe - calculate optimal angle
            Vec3 vel = GetVelocity();
            float speed_2d = vel.Length2D();
            
            if (speed_2d > config_.auto_strafe.min_speed) {
                // Optimal strafe angle based on speed
                float optimal_angle = 90.0f; // Perfect strafe is 90 degrees
                
                // Adjust for mode
                switch (config_.auto_strafe.mode) {
                    case AutoStrafeConfig::Mode::Silent:
                        optimal_angle = 90.0f;
                        break;
                    case AutoStrafeConfig::Mode::Legit:
                        optimal_angle = 90.0f - speed_2d * 0.1f; // Reduce angle at high speed
                        optimal_angle = std::max(30.0f, optimal_angle);
                        break;
                    case AutoStrafeConfig::Mode::Rage:
                        optimal_angle = 90.0f;
                        break;
                    case AutoStrafeConfig::Mode::Circle:
                        // Circle around target
                        target_angle = state_.strafe_angle + config_.auto_strafe.turn_speed * dt;
                        if (config_.auto_strafe.circle_clockwise) target_angle = -target_angle;
                        state_.strafe_angle = target_angle;
                        break;
                    case AutoStrafeConfig::Mode::WOnly:
                        // Only strafe when W held
                        if (IsKeyPressed(config_.auto_strafe.forward_key)) {
                            optimal_angle = 90.0f;
                        } else {
                            return;
                        }
                        break;
                }
                
                target_angle = optimal_angle * state_.strafe_direction;
                
                // Apply humanization
                if (config_.auto_strafe.humanize) {
                    static std::uniform_real_distribution<float> angle_dist(-1.0f, 1.0f);
                    target_angle += angle_dist(rng_) * config_.auto_strafe.angle_variance;
                    target_angle += angle_dist(rng_) * config_.auto_strafe.angle_variance;
                }
                
                state_.strafe_angle = target_angle;
            }
        } else {
            // Manual strafe
            if (left) {
                target_angle = -config_.auto_strafe.max_angle;
                state_.strafe_direction = -1;
            } else if (right) {
                target_angle = config_.auto_strafe.max_angle;
                state_.strafe_direction = 1;
            }
        }
        
        // Apply mouse movement for strafing
        float turn_rate = config_.auto_strafe.turn_speed * 3.14159f / 180.0f * dt;
        float current_angle = 0.0f; // Would get from view angles
        float angle_diff = target_angle - current_angle;
        
        // Normalize
        while (angle_diff > 180.0f) angle_diff -= 360.0f;
        while (angle_diff < -180.0f) angle_diff += 360.0f;
        
        float turn_amount = std::clamp(angle_diff * 0.5f, -turn_rate, turn_rate);
        SetMouseMove(turn_amount * 100.0f, 0); // Convert to mouse delta
        
        // Handle keys
        if (target_angle < -5.0f) {
            SetKeyPressed(config_.auto_strafe.left_key, true);
            SetKeyPressed(config_.auto_strafe.right_key, false);
        } else if (target_angle > 5.0f) {
            SetKeyPressed(config_.auto_strafe.left_key, false);
            SetKeyPressed(config_.auto_strafe.right_key, true);
        } else {
            SetKeyPressed(config_.auto_strafe.left_key, false);
            SetKeyPressed(config_.auto_strafe.right_key, false);
        }
    }

    void MovementAssistManager::UpdateAutoJump(float dt, uint64_t local_player) {
        if (!config_.auto_jump.enabled) return;
        if (config_.auto_jump.disable_in_menu && IsInMenu()) return;
        if (config_.auto_jump.disable_on_ladder && IsOnLadder()) return;
        if (config_.auto_jump.disable_in_water && IsInWater()) return;
        if (config_.auto_jump.only_when_moving && GetSpeed() < 10.0f) return;
        if (config_.auto_jump.require_forward && !IsKeyPressed(config_.auto_jump.jump_key)) return;
        
        bool on_ground = IsOnGround();
        
        // Detect ground contact
        if (on_ground && !state_.was_on_ground) {
            state_.last_jump_time = std::chrono::duration<float>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
        state_.was_on_ground = on_ground;
        
        if (!on_ground) return;
        
        float now = std::chrono::duration<float>(std::chrono::steady_clock::now().time_since_epoch()).count();
        float time_on_ground = now - state_.last_jump_time;
        
        if (time_on_ground < config_.auto_jump.min_ground_time) return;
        
        bool should_jump = false;
        
        switch (config_.auto_jump.mode) {
            case AutoJumpConfig::Mode::Bhop: {
                // Jump immediately when on ground
                if (IsKeyPressed(config_.auto_jump.jump_key) || config_.auto_jump.mode == AutoJumpConfig::Mode::Bhop) {
                    should_jump = true;
                }
                break;
            }
            case AutoJumpConfig::Mode::AutoJump: {
                // Jump at edges
                if (IsAtEdge(config_.auto_jump.edge_check_distance, config_.auto_jump.edge_check_down)) {
                    should_jump = true;
                }
                break;
            }
            case AutoJumpConfig::Mode::AutoCrouchJump: {
                // Crouch jump
                if (IsKeyPressed(config_.auto_jump.jump_key)) {
                    SetKeyPressed(config_.auto_jump.crouch_key, true);
                    should_jump = true;
                }
                break;
            }
            case AutoJumpConfig::Mode::LongJump: {
                // Long jump: duck + jump + forward
                if (IsKeyPressed(config_.auto_jump.jump_key) && GetSpeed() > 200.0f) {
                    SetKeyPressed(config_.auto_jump.crouch_key, true);
                    should_jump = true;
                }
                break;
            }
        }
        
        // Perfect timing
        if (should_jump && config_.auto_jump.perfect_timing > 0) {
            // Would wait for perfect timing window
        }
        
        // Humanization
        if (should_jump && config_.auto_jump.humanize) {
            static std::uniform_real_distribution<float> var(-1.0f, 1.0f);
            float variance = var(rng_) * config_.auto_jump.timing_variance;
            // Would add small delay
            
            static std::uniform_real_distribution<float> chance(0, 1);
            if (chance(rng_) < config_.auto_jump.late_jump_chance) {
                // Delay slightly
            }
            if (chance(rng_) < config_.auto_jump.early_jump_chance) {
                // Jump slightly early
            }
        }
        
        if (should_jump) {
            SetKeyPressed(config_.auto_jump.jump_key, true);
            state_.last_jump_time = std::chrono::duration<float>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
    }

    void MovementAssistManager::UpdateAutoCrouch(float dt, uint64_t local_player) {
        if (!config_.auto_crouch.enabled) return;
        if (config_.auto_crouch.disable_in_menu && IsInMenu()) return;
        
        bool should_crouch = false;
        bool is_crouching = IsKeyPressed(config_.auto_crouch.crouch_key);
        
        switch (config_.auto_crouch.mode) {
            case AutoCrouchConfig::Mode::Peek: {
                // Crouch when peeking (right click held)
                if (IsKeyPressed(VK_RBUTTON)) {
                    should_crouch = true;
                }
                break;
            }
            case AutoCrouchConfig::Mode::Shoot: {
                // Crouch when shooting (left click held)
                if (IsKeyPressed(VK_LBUTTON)) {
                    should_crouch = true;
                }
                break;
            }
            case AutoCrouchConfig::Mode::Dodge: {
                // Crouch randomly to dodge
                static float dodge_timer = 0.0f;
                dodge_timer -= 0.016f; // dt approx
                if (dodge_timer <= 0) {
                    if (rand() % 100 < 5) { // 5% chance
                        should_crouch = true;
                        dodge_timer = 2.0f + (rand() % 300) / 100.0f; // 2-5 seconds
                    }
                }
                break;
            }
            case AutoCrouchConfig::Mode::Silent: {
                // Crouch for silent movement
                float speed = GetSpeed();
                if (speed > 50.0f && speed < 150.0f) {
                    should_crouch = true;
                }
                break;
            }
        }
        
        // Handle toggle key
        if (config_.auto_crouch.toggle_key > 0 && IsKeyPressed(config_.auto_crouch.toggle_key)) {
            // Would toggle crouch mode
        }
        
        // Apply crouch
        if (should_crouch != is_crouching) {
            if (config_.auto_crouch.humanize) {
                static std::uniform_real_distribution<float> var(-1.0f, 1.0f);
                float delay = var(rng_) * config_.auto_crouch.reaction_variance;
                // Would add small delay
            }
            SetKeyPressed(config_.auto_crouch.crouch_key, should_crouch);
        }
        
        state_.was_crouching = is_crouching;
    }

    void MovementAssistManager::UpdateEdgeJump(float dt, uint64_t local_player) {
        if (!config_.edge_jump.enabled) return;
        if (config_.auto_jump.disable_in_menu && IsInMenu()) return;
        
        if (IsAtEdge(config_.edge_jump.check_distance, config_.edge_jump.check_down)) {
            float gap = 0.0f; // Would measure gap
            if (gap >= config_.edge_jump.min_gap) {
                if (config_.edge_jump.auto_jump) {
                    SetKeyPressed(VK_SPACE, true);
                }
                if (config_.edge_jump.auto_forward) {
                    SetKeyPressed('W', true);
                    // Would auto-release after forward_time
                }
            }
        }
    }

    void MovementAssistManager::DrawStatus(ImDrawList* draw_list) {
        if (!draw_list) return;
        
        ImVec2 pos(10, ImGui::GetIO().DisplaySize.y - 150);
        ImU32 color = IM_COL32(212, 175, 55, 255);
        
        draw_list->AddText(pos, color, "Movement Assists:");
        pos.y += 20;
        
        if (config_.auto_strafe.enabled) {
            draw_list->AddText(pos, IM_COL32(80, 255, 100, 255), "Auto-Strafe: ON");
            pos.y += 18;
        }
        if (config_.auto_jump.enabled) {
            draw_list->AddText(pos, IM_COL32(80, 255, 100, 255), "Auto-Jump: ON");
            pos.y += 18;
        }
        if (config_.auto_crouch.enabled) {
            draw_list->AddText(pos, IM_COL32(80, 255, 100, 255), "Auto-Crouch: ON");
            pos.y += 18;
        }
        if (config_.edge_jump.enabled) {
            draw_list->AddText(pos, IM_COL32(80, 255, 100, 255), "Edge-Jump: ON");
            pos.y += 18;
        }
    }

    void MovementAssistManager::SaveProfile(const std::string& name) {
        MovementAssistConfig::Profile profile;
        profile.name = name;
        profile.strafe = config_.auto_strafe;
        profile.jump = config_.auto_jump;
        profile.crouch = config_.auto_crouch;
        profile.edge = config_.edge_jump;
        
        // Remove existing with same name
        config_.profiles.erase(
            std::remove_if(config_.profiles.begin(), config_.profiles.end(),
                [&name](const MovementAssistConfig::Profile& p) { return p.name == name; }),
            config_.profiles.end());
        
        config_.profiles.push_back(profile);
        config_.active_profile = (int)config_.profiles.size() - 1;
    }

    void MovementAssistManager::LoadProfile(const std::string& name) {
        for (size_t i = 0; i < config_.profiles.size(); ++i) {
            if (config_.profiles[i].name == name) {
                config_.auto_strafe = config_.profiles[i].strafe;
                config_.auto_jump = config_.profiles[i].jump;
                config_.auto_crouch = config_.profiles[i].crouch;
                config_.edge_jump = config_.profiles[i].edge;
                config_.active_profile = (int)i;
                break;
            }
        }
    }

    void MovementAssistManager::DeleteProfile(const std::string& name) {
        config_.profiles.erase(
            std::remove_if(config_.profiles.begin(), config_.profiles.end(),
                [&name](const MovementAssistConfig::Profile& p) { return p.name == name; }),
            config_.profiles.end());
    }

    std::vector<std::string> MovementAssistManager::GetProfileNames() const {
        std::vector<std::string> names;
        for (const auto& p : config_.profiles) names.push_back(p.name);
        return names;
    }

    std::string MovementAssistManager::SerializeConfig() const {
        std::ostringstream out;
        out << config_.enabled << '|'
            << config_.master_enable << '|'
            << config_.auto_strafe.enabled << '|'
            << static_cast<int>(config_.auto_strafe.mode) << '|'
            << config_.auto_strafe.forward_key << '|'
            << config_.auto_strafe.left_key << '|'
            << config_.auto_strafe.right_key << '|'
            << config_.auto_strafe.back_key << '|'
            << config_.auto_strafe.jump_key << '|'
            << config_.auto_strafe.turn_speed << '|'
            << config_.auto_strafe.max_angle << '|'
            << config_.auto_strafe.humanize << '|'
            << config_.auto_jump.enabled << '|'
            << static_cast<int>(config_.auto_jump.mode) << '|'
            << config_.auto_jump.jump_key << '|'
            << config_.auto_crouch.enabled << '|'
            << config_.edge_jump.enabled << '|'
            << config_.profiles.size() << '|';
        
        for (const auto& profile : config_.profiles) {
            out << profile.name << '|';
        }
        return out.str();
    }

    bool MovementAssistManager::DeserializeConfig(const std::string& data) {
        std::istringstream in(data);
        std::string token;
        
        auto read_bool = [&](bool& b) {
            std::getline(in, token, '|');
            b = token == "1";
        };
        auto read_int = [&](int& i) {
            std::getline(in, token, '|');
            i = std::stoi(token);
        };
        auto read_float = [&](float& f) {
            std::getline(in, token, '|');
            f = std::stof(token);
        };
        
        read_bool(config_.enabled);
        read_bool(config_.master_enable);
        read_bool(config_.auto_strafe.enabled);
        read_int(*reinterpret_cast<int*>(&config_.auto_strafe.mode));
        read_int(config_.auto_strafe.forward_key);
        read_int(config_.auto_strafe.left_key);
        read_int(config_.auto_strafe.right_key);
        read_int(config_.auto_strafe.back_key);
        read_int(config_.auto_strafe.jump_key);
        read_float(config_.auto_strafe.turn_speed);
        read_float(config_.auto_strafe.max_angle);
        read_bool(config_.auto_strafe.humanize);
        read_bool(config_.auto_jump.enabled);
        read_int(*reinterpret_cast<int*>(&config_.auto_jump.mode));
        read_int(config_.auto_jump.jump_key);
        read_bool(config_.auto_crouch.enabled);
        read_bool(config_.edge_jump.enabled);
        
        int profile_count = 0;
        std::getline(in, token, '|');
        if (!token.empty()) profile_count = std::stoi(token);
        
        for (int i = 0; i < profile_count; ++i) {
            std::getline(in, token, '|');
            // Would need to deserialize full profile
        }
        
        return true;
    }

    MovementAssistConfig::Profile GetLegitMovementProfile() {
        MovementAssistConfig::Profile profile;
        profile.name = "Legit";
        
        profile.strafe.enabled = true;
        profile.strafe.mode = AutoStrafeConfig::Mode::Legit;
        profile.strafe.humanize = true;
        profile.strafe.turn_speed = 60.0f;
        profile.strafe.max_angle = 45.0f;
        profile.strafe.humanize = true;
        profile.strafe.reaction_variance = 0.03f;
        profile.strafe.angle_variance = 3.0f;
        
        profile.jump.enabled = true;
        profile.jump.mode = AutoJumpConfig::Mode::Bhop;
        profile.jump.humanize = true;
        profile.jump.timing_variance = 0.008f;
        
        profile.crouch.enabled = true;
        profile.crouch.mode = AutoCrouchConfig::Mode::Peek;
        profile.crouch.humanize = true;
        
        profile.edge.enabled = false;
        
        return profile;
    }

    MovementAssistConfig::Profile GetRageMovementProfile() {
        MovementAssistConfig::Profile profile;
        profile.name = "Rage";
        
        profile.strafe.enabled = true;
        profile.strafe.mode = AutoStrafeConfig::Mode::Rage;
        profile.strafe.humanize = false;
        profile.strafe.turn_speed = 180.0f;
        profile.strafe.max_angle = 90.0f;
        
        profile.jump.enabled = true;
        profile.jump.mode = AutoJumpConfig::Mode::Bhop;
        profile.jump.humanize = false;
        
        profile.crouch.enabled = false;
        profile.edge.enabled = true;
        profile.edge.auto_jump = true;
        profile.edge.auto_forward = true;
        
        return profile;
    }

    MovementAssistConfig::Profile GetKZMovementProfile() {
        MovementAssistConfig::Profile profile;
        profile.name = "KZ/Climb";
        
        profile.strafe.enabled = true;
        profile.strafe.mode = AutoStrafeConfig::Mode::Circle;
        profile.strafe.circle_radius = 50.0f;
        profile.strafe.circle_clockwise = true;
        profile.strafe.humanize = false;
        
        profile.jump.enabled = true;
        profile.jump.mode = AutoJumpConfig::Mode::LongJump;
        profile.jump.humanize = false;
        
        profile.crouch.enabled = true;
        profile.crouch.mode = AutoCrouchConfig::Mode::Silent;
        
        profile.edge.enabled = true;
        profile.edge.auto_jump = true;
        profile.edge.auto_forward = true;
        
        return profile;
    }

    MovementAssistConfig::Profile GetSurfMovementProfile() {
        MovementAssistConfig::Profile profile;
        profile.name = "Surf";
        
        profile.strafe.enabled = true;
        profile.strafe.mode = AutoStrafeConfig::Mode::Circle;
        profile.strafe.circle_radius = 150.0f;
        profile.strafe.turn_speed = 120.0f;
        profile.strafe.humanize = false;
        
        profile.jump.enabled = false;
        profile.crouch.enabled = false;
        profile.edge.enabled = false;
        
        return profile;
    }

    MovementAssistConfig::Profile GetBhopMovementProfile() {
        MovementAssistConfig::Profile profile;
        profile.name = "Bhop";
        
        profile.strafe.enabled = true;
        profile.strafe.mode = AutoStrafeConfig::Mode::Rage;
        profile.strafe.humanize = false;
        
        profile.jump.enabled = true;
        profile.jump.mode = AutoJumpConfig::Mode::Bhop;
        profile.jump.humanize = false;
        profile.jump.perfect_timing = 0.0f;
        
        profile.crouch.enabled = false;
        profile.edge.enabled = false;
        
        return profile;
    }

    std::string SerializeMovementConfig(const MovementAssistConfig& config) {
        std::ostringstream out;
        out << config.enabled << '|'
            << config.master_enable << '|'
            << config.auto_strafe.enabled << '|'
            << static_cast<int>(config_.auto_strafe.mode) << '|'
            << config_.auto_strafe.forward_key << '|'
            << config_.auto_strafe.left_key << '|'
            << config_.auto_strafe.right_key << '|'
            << config_.auto_strafe.back_key << '|'
            << config_.auto_strafe.jump_key << '|'
            << config_.auto_strafe.turn_speed << '|'
            << config_.auto_strafe.max_angle << '|'
            << config_.auto_strafe.humanize << '|'
            << config_.auto_jump.enabled << '|'
            << static_cast<int>(config_.auto_jump.mode) << '|'
            << config_.auto_jump.jump_key << '|'
            << config_.auto_crouch.enabled << '|'
            << config_.edge_jump.enabled << '|'
            << config_.profiles.size() << '|';
        
        for (const auto& profile : config_.profiles) {
            out << profile.name << '|';
        }
        return out.str();
    }

    bool DeserializeMovementConfig(const std::string& data, MovementAssistConfig& config) {
        std::istringstream in(data);
        std::string token;
        
        auto read_bool = [&](bool& b) {
            std::getline(in, token, '|');
            b = token == "1";
        };
        auto read_int = [&](int& i) {
            std::getline(in, token, '|');
            i = std::stoi(token);
        };
        auto read_float = [&](float& f) {
            std::getline(in, token, '|');
            f = std::stof(token);
        };
        
        read_bool(config.enabled);
        read_bool(config.master_enable);
        read_bool(config.auto_strafe.enabled);
        read_int(*reinterpret_cast<int*>(&config.auto_strafe.mode));
        read_int(config.auto_strafe.forward_key);
        read_int(config.auto_strafe.left_key);
        read_int(config.auto_strafe.right_key);
        read_int(config.auto_strafe.back_key);
        read_int(config.auto_strafe.jump_key);
        read_float(config.auto_strafe.turn_speed);
        read_float(config.auto_strafe.max_angle);
        read_bool(config.auto_strafe.humanize);
        read_bool(config.auto_jump.enabled);
        read_int(*reinterpret_cast<int*>(&config.auto_jump.mode));
        read_int(config.auto_jump.jump_key);
        read_bool(config.auto_crouch.enabled);
        read_bool(config.edge_jump.enabled);
        
        int profile_count = 0;
        std::getline(in, token, '|');
        if (!token.empty()) profile_count = std::stoi(token);
        
        for (int i = 0; i < profile_count; ++i) {
            std::getline(in, token, '|');
        }
        
        return true;
    }

} // namespace Gameplay::MovementAssist