#pragma once
#include "../../ImGui/imgui.h"
#include <array>
#include <vector>
#include <string>
#include <unordered_map>

namespace Gameplay::RecoilControl {

    // Weapon classes for categorization
    enum class WeaponClass : int {
        Pistol = 0,
        SMG = 1,
        Rifle = 2,
        Sniper = 3,
        Shotgun = 4,
        LMG = 5,
        Marksman = 6,
        Heavy = 7,
        Special = 8,
        Melee = 9,
        Grenade = 10,
        Unknown = 99
    };

    // Recoil pattern point
    struct RecoilPoint {
        float x = 0.0f;        // Horizontal offset
        float y = 0.0f;        // Vertical offset
        float time = 0.0f;     // Time from first shot (seconds)
        float weight = 1.0f;   // Pattern weight/importance
    };

    // Recoil pattern for a weapon
    struct RecoilPattern {
        std::string weapon_name;
        std::string weapon_hash;    // Unique identifier
        WeaponClass weapon_class = WeaponClass::Unknown;
        
        std::vector<RecoilPoint> pattern;
        float pattern_duration = 0.0f;
        int shots_in_pattern = 0;
        
        // Pattern properties
        float vertical_recoil = 0.0f;      // Total vertical climb
        float horizontal_spread = 0.0f;    // Max horizontal deviation
        float recovery_time = 0.0f;        // Time to return to center
        bool is_cyclic = false;            // Pattern repeats
        
        // Compensation
        std::vector<RecoilPoint> compensation;  // Inverse pattern for compensation
        float compensation_strength = 1.0f;     // 0-1, how much to compensate
        bool auto_compensate = true;
        
        // Advanced
        float first_shot_multiplier = 1.0f;
        float ads_multiplier = 1.0f;         // Aim down sights modifier
        float crouch_multiplier = 0.8f;      // Crouching modifier
        float move_multiplier = 1.2f;        // Moving modifier
    };

    // Per-weapon recoil config
    struct WeaponRecoilConfig {
        std::string weapon_name;
        std::string weapon_hash;
        
        // Basic compensation
        bool enabled = true;
        float vertical_strength = 1.0f;    // 0-1, how much to pull down
        float horizontal_strength = 0.5f;  // 0-1, how much to correct sideways
        
        // Timing
        float start_delay = 0.0f;          // Delay before compensation starts (seconds)
        float compensation_delay = 0.0f;   // Delay per shot
        float max_compensation_time = 2.0f; // Max time to compensate
        
        // Advanced
        bool use_pattern = true;           // Use learned pattern vs simple pull-down
        float pattern_blend = 0.8f;        // Blend between pattern and simple
        bool adapt_to_attachments = true;  // Adjust for muzzle brake, compensator, etc.
        
        // Attachment modifiers
        float suppressor_mod = 1.0f;
        float compensator_mod = 0.7f;
        float muzzle_brake_mod = 0.6f;
        float flash_hider_mod = 0.9f;
        float vertical_grip_mod = 0.85f;
        float angled_grip_mod = 0.9f;
        
        // Stance modifiers
        float standing_mod = 1.0f;
        float crouching_mod = 0.8f;
        float prone_mod = 0.6f;
        
        // Movement modifiers
        float stationary_mod = 1.0f;
        float walking_mod = 1.1f;
        float running_mod = 1.3f;
        
        // Humanization
        bool humanize = true;
        float micro_jitter = 0.1f;
        float overcorrection_chance = 0.02f;
        float overcorrection_amount = 0.15f;
        float reaction_variance = 0.01f;
        
        // Visual feedback
        bool show_pattern_preview = false;
        bool show_compensation_preview = false;
    };

    // Global recoil control settings
    struct RecoilControlConfig {
        bool enabled = true;
        bool only_when_aiming = true;      // Only compensate when ADS
        bool require_attack_held = true;   // Only when LMB held
        
        // Global multipliers
        float global_vertical = 1.0f;
        float global_horizontal = 1.0f;
        
        // Per-weapon configs
        std::unordered_map<std::string, WeaponRecoilConfig> weapon_configs;
        
        // Pattern learning
        bool learn_patterns = true;
        int min_shots_to_learn = 30;
        float pattern_confidence_threshold = 0.7f;
        int max_patterns_per_weapon = 5;   // Different attachment combos
        
        // Anti-recoil modes
        enum class Mode {
            Off = 0,
            Simple = 1,        // Simple pull-down
            Pattern = 2,       // Full pattern compensation
            Hybrid = 3,        // Blend of both
            Adaptive = 4       // Automatically switch
        };
        Mode mode = Mode::Hybrid;
        
        // Safety
        bool require_valid_target = false;
        float max_compensation_angle = 45.0f; // Degrees
        bool stop_on_target_switch = true;
        
        // Visual
        bool show_crosshair_offset = false;
        ImU32 pattern_color = IM_COL32(255, 100, 100, 200);
        ImU32 compensation_color = IM_COL32(100, 255, 100, 200);
    };

    // Recoil controller - manages compensation per weapon
    class RecoilController {
    public:
        struct CompensationState {
            int shots_fired = 0;
            float accumulated_x = 0.0f;
            float accumulated_y = 0.0f;
            float last_shot_time = 0.0f;
            bool is_compensating = false;
            int pattern_index = 0;
        };
        
        RecoilController();
        
        void SetConfig(const RecoilControlConfig& config) { config_ = config; }
        const RecoilControlConfig& GetConfig() const { return config_; }
        
        // Update with current weapon
        void SetCurrentWeapon(const std::string& weapon_hash, WeaponClass weapon_class);
        
        // Process shot - call when shot fired
        void OnShotFired(float shot_time, const ImVec2& view_angles);
        
        // Get compensation for current frame
        ImVec2 GetCompensation(float dt, const ImVec2& current_view_angles);
        
        // Reset state (weapon swap, reload, etc.)
        void Reset();
        void ResetWeapon(const std::string& weapon_hash);
        
        // Pattern learning
        void RecordShot(const ImVec2& view_angles, float time);
        void FinalizePattern();
        bool HasLearnedPattern(const std::string& weapon_hash) const;
        const RecoilPattern* GetLearnedPattern(const std::string& weapon_hash) const;
        
        // Import/export patterns
        std::string ExportPatterns() const;
        bool ImportPatterns(const std::string& data);
        
        // Get current state for UI
        const CompensationState& GetState() const { return state_; }
        const WeaponRecoilConfig* GetCurrentWeaponConfig() const;
        
    private:
        RecoilControlConfig config_;
        CompensationState state_;
        std::string current_weapon_hash_;
        WeaponClass current_weapon_class_ = WeaponClass::Unknown;
        
        // Learned patterns
        std::unordered_map<std::string, RecoilPattern> learned_patterns_;
        std::vector<ImVec2> recent_shots_;  // For pattern learning
        float pattern_start_time_ = 0.0f;
        
        // Calculate compensation from pattern
        ImVec2 CalculatePatternCompensation(float dt, const ImVec2& view_angles);
        ImVec2 CalculateSimpleCompensation(float dt, const ImVec2& view_angles);
        ImVec2 BlendCompensation(const ImVec2& pattern, const ImVec2& simple, float blend);
        
        // Apply modifiers
        float GetStanceModifier() const;
        float GetMovementModifier() const;
        float GetAttachmentModifier() const;
        
        // Humanization
        ImVec2 ApplyHumanization(const ImVec2& compensation, float dt);
    };
    
    // Recoil pattern database manager
    class RecoilPatternDatabase {
    public:
        static RecoilPatternDatabase& Instance();
        
        // Add/update pattern
        void AddPattern(const RecoilPattern& pattern);
        bool RemovePattern(const std::string& weapon_hash);
        const RecoilPattern* GetPattern(const std::string& weapon_hash) const;
        const RecoilPattern* GetPatternByName(const std::string& weapon_name) const;
        
        // Search patterns
        std::vector<const RecoilPattern*> FindPatterns(WeaponClass weapon_class) const;
        std::vector<const RecoilPattern*> SearchPatterns(const std::string& query) const;
        
        // Load/save
        bool LoadFromFile(const char* path);
        bool SaveToFile(const char* path) const;
        bool LoadFromJSON(const std::string& json);
        std::string ExportToJSON() const;
        
        // Community patterns
        bool DownloadCommunityPatterns(const char* url);
        bool UploadPattern(const RecoilPattern& pattern);
        
        // Get all patterns
        const std::unordered_map<std::string, RecoilPattern>& GetAllPatterns() const { return patterns_; }
        
    private:
        std::unordered_map<std::string, RecoilPattern> patterns_;
    };
    
    // Anti-recoil crosshair - visual feedback
    class RecoilCrosshair {
    public:
        struct Config {
            bool enabled = true;
            bool show_recoil_offset = true;
            bool show_compensation = true;
            bool show_pattern = false;
            
            float crosshair_size = 8.0f;
            float gap = 4.0f;
            float thickness = 1.5f;
            
            ImU32 base_color = IM_COL32(212, 175, 55, 255);
            ImU32 recoil_color = IM_COL32(255, 80, 80, 255);
            ImU32 compensation_color = IM_COL32(80, 255, 100, 255);
            ImU32 pattern_color = IM_COL32(255, 200, 80, 200);
            
            bool dynamic_gap = true;
            float max_gap = 20.0f;
        };
        
        explicit RecoilCrosshair(const Config& config = {});
        void SetConfig(const Config& config) { config_ = config; }
        const Config& GetConfig() const { return config_; }
        
        // Update with current recoil state
        void Update(const ImVec2& recoil_offset, const ImVec2& compensation, 
                   const RecoilPattern* pattern = nullptr, int pattern_index = 0);
        
        // Draw crosshair
        void Draw(ImDrawList* draw_list, const ImVec2& center) const;
        
    private:
        Config config_;
        ImVec2 current_recoil_{};
        ImVec2 current_compensation_{};
        const RecoilPattern* current_pattern_ = nullptr;
        int current_pattern_index_ = 0;
        float animation_progress_ = 0.0f;
    };

    // Helper functions
    WeaponClass GetWeaponClassFromName(const std::string& weapon_name);
    const char* GetWeaponClassName(WeaponClass cls);
    
    // Preset configurations
    WeaponRecoilConfig GetDefaultPistolConfig();
    WeaponRecoilConfig GetDefaultSMGConfig();
    WeaponRecoilConfig GetDefaultRifleConfig();
    WeaponRecoilConfig GetDefaultSniperConfig();
    WeaponRecoilConfig GetDefaultShotgunConfig();
    WeaponRecoilConfig GetDefaultLMGConfig();
    
    // Serialize/deserialize
    std::string SerializeWeaponConfig(const WeaponRecoilConfig& config);
    bool DeserializeWeaponConfig(const std::string& data, WeaponRecoilConfig& config);
    std::string SerializeRecoilConfig(const RecoilControlConfig& config);
    bool DeserializeRecoilConfig(const std::string& data, RecoilControlConfig& config);

} // namespace Gameplay::RecoilControl