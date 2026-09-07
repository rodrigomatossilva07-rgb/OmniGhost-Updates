#pragma once
#include "../../ImGui/imgui.h"
#include <vector>
#include <string>
#include <chrono>
#include <functional>
#include <cmath>

namespace Gameplay::Triggerbot {

    // Helper: Vec3
    struct Vec3 {
        float x = 0, y = 0, z = 0;
        Vec3() = default;
        Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    };

    // Triggerbot activation conditions
    enum class ConditionType : int {
        Always = 0,
        OnKeyHold = 1,
        OnKeyToggle = 2,
        WhenAiming = 3,
        WhenScoping = 4,
        WhenCrouching = 5,
        WhenMoving = 6,
        WhenStanding = 7,
        InAir = 8,
        OnGround = 9,
        TargetVisible = 10,
        TargetInFOV = 11,
        TargetDistance = 12,
        TargetHealth = 13,
        WeaponType = 14,
        AmmoCount = 15,
        Custom = 99
    };

    // Condition for triggerbot activation
    struct TriggerCondition {
        ConditionType type = ConditionType::Always;
        float value = 0.0f;           // For numeric conditions (distance, health, etc.)
        float value2 = 0.0f;          // For range conditions (min-max)
        int key = 0;                  // For key conditions
        std::string custom_script;    // For custom conditions
        bool inverted = false;        // Invert condition
    };

    // Triggerbot action when triggered
    struct TriggerAction {
        enum class Type : int {
            LeftClick = 0,
            RightClick = 1,
            MiddleClick = 2,
            KeyPress = 3,
            Custom = 99
        };
        
        Type type = Type::LeftClick;
        int key = 0;                  // For KeyPress
        float hold_time = 0.0f;       // How long to hold (0 = instant)
        float delay = 0.0f;           // Delay before action
        bool release_after = true;    // Release after hold_time
    };

    // Triggerbot configuration
    struct TriggerbotConfig {
        bool enabled = false;
        
        // Conditions (ALL must be true - AND logic)
        std::vector<TriggerCondition> conditions;
        
        // Actions to perform
        std::vector<TriggerAction> actions;
        
        // Targeting
        struct TargetingConfig {
            float fov = 3.0f;              // FOV in degrees
            float max_distance = 100.0f;   // Max distance
            float min_distance = 0.0f;     // Min distance
            bool require_visible = true;   // Must be visible
            bool ignore_teammates = true;
            bool ignore_friends = true;
            bool ignore_dead = true;
            float min_health = 1.0f;       // Min target health
            float max_health = 1000.0f;    // Max target health
            
            // Hitboxes to trigger on (priority order)
            std::vector<int> hitboxes;
            
            // Hitbox priorities (0 = highest priority)
            std::array<float, 20> hitbox_priorities{};
        } targeting;
        
        // Timing
        struct TimingConfig {
            float reaction_time = 0.0f;        // Minimum reaction delay
            float reaction_variance = 0.0f;    // Random variance
            float min_delay = 0.0f;            // Min delay between shots
            float max_delay = 0.0f;            // Max delay between shots
            bool randomize_delay = true;
            float burst_time = 0.0f;           // Burst fire duration
            float burst_cooldown = 0.0f;       // Cooldown after burst
            int max_burst_shots = 0;           // 0 = unlimited
        } timing;
        
        // Humanization
        struct HumanizeConfig {
            bool enabled = true;
            float micro_jitter = 0.5f;         // Pixel jitter
            float reaction_variance = 0.02f;   // Reaction time variance
            float click_variance = 0.01f;      // Click timing variance
            float overshoot_chance = 0.02f;
            float overshoot_amount = 0.1f;
            bool simulate_recoil = false;
        } humanize;
        
        // Safety
        struct SafetyConfig {
            bool disable_in_menu = true;
            bool disable_when_typing = true;
            bool require_aim_key = false;
            int aim_key = 0;
            bool disable_on_reload = true;
            bool disable_on_weapon_swap = true;
            int max_shots_per_second = 0;  // 0 = unlimited
            bool anti_afk = false;
        } safety;
        
        // Visual feedback
        struct VisualConfig {
            bool show_fov = true;
            bool show_target = true;
            bool show_status = true;
            ImU32 fov_color = IM_COL32(255, 180, 50, 160);
            ImU32 target_color = IM_COL32(255, 80, 80, 255);
            ImU32 ready_color = IM_COL32(80, 255, 100, 255);
        } visual;
    };

    // Triggerbot state
    struct TriggerbotState {
        bool active = false;
        bool can_fire = true;
        uint64_t current_target = 0;
        int shots_fired = 0;
        float last_shot_time = 0.0f;
        float next_shot_time = 0.0f;
        int burst_shots = 0;
        bool in_burst = false;
        float burst_end_time = 0.0f;
        std::chrono::steady_clock::time_point last_condition_check;
    };

    // Triggerbot manager
    class TriggerbotManager {
    public:
        static TriggerbotManager& Instance();
        
        void SetConfig(const TriggerbotConfig& config) { config_ = config; }
        const TriggerbotConfig& GetConfig() const { return config_; }
        TriggerbotConfig& GetMutableConfig() { return config_; }
        
        void Update(float dt, uint64_t local_player, const std::vector<uint64_t>& entities);
        void DrawFOV(ImDrawList* draw_list);
        void DrawStatus(ImDrawList* draw_list);
        
        // Get current state
        const TriggerbotState& GetState() const { return state_; }
        uint64_t GetCurrentTarget() const { return state_.current_target; }
        bool IsActive() const { return state_.active; }
        bool CanFire() const { return state_.can_fire; }
        
        // Force trigger
        void ForceTrigger(uint64_t target);
        void CancelTrigger();
        
        // Serialization
        std::string SerializeConfig() const;
        bool DeserializeConfig(const std::string& data);
        
    private:
        TriggerbotConfig config_;
        TriggerbotState state_;
        
        bool CheckConditions(uint64_t local_player, uint64_t target);
        bool CheckCondition(const TriggerCondition& cond, uint64_t local_player, uint64_t target);
        bool CheckTargeting(uint64_t local_player, uint64_t target);
        void ExecuteActions(uint64_t target);
        void UpdateTiming(float dt);
        bool IsKeyPressed(int key) const;
        
        // Game-specific hooks (to be implemented per game)
        virtual bool IsEntityVisible(uint64_t target) = 0;
        virtual float GetDistanceTo(uint64_t target) = 0;
        virtual float GetEntityHealth(uint64_t target) = 0;
        virtual bool IsEntityDead(uint64_t target) = 0;
        virtual bool IsTeammate(uint64_t target) = 0;
        virtual bool IsFriend(uint64_t target) = 0;
        virtual int GetWeaponType() = 0;
        virtual int GetAmmoCount() = 0;
        virtual bool IsReloading() = 0;
        virtual bool IsScoping() = 0;
        virtual bool IsCrouching() = 0;
        virtual bool IsMoving() = 0;
        virtual bool IsInAir() = 0;
        virtual Vec3 GetLocalPosition() = 0;
        virtual Vec3 GetEntityPosition(uint64_t id) = 0;
        virtual float GetEntityDistance(uint64_t id) = 0;
        virtual float GetFOVToEntity(uint64_t id) = 0;
        virtual void SimulateClick(int button, bool down) = 0;
        virtual void SimulateKeyPress(int key, bool down) = 0;
    };
    
    // Preset configurations
    TriggerbotConfig GetLegitTriggerbotConfig();
    TriggerbotConfig GetCompetitiveTriggerbotConfig();
    TriggerbotConfig GetSniperTriggerbotConfig();
    TriggerbotConfig GetRageTriggerbotConfig();
    
    // Serialization
    std::string SerializeTriggerbotConfig(const TriggerbotConfig& config);
    bool DeserializeTriggerbotConfig(const std::string& data, TriggerbotConfig& config);

} // namespace Gameplay::Triggerbot