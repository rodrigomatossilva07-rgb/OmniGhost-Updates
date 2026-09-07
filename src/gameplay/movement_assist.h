#pragma once
#include "../../ImGui/imgui.h"
#include <vector>
#include <string>
#include <array>
#include <chrono>
#include <random>
#include <cmath>

namespace Gameplay::MovementAssist {

    // Basic math types
    struct Vec3 {
        float x = 0, y = 0, z = 0;
        Vec3() = default;
        Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
        Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
        Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
        Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
        float Length() const { return sqrtf(x*x + y*y + z*z); }
        float Length2D() const { return sqrtf(x*x + y*y); }
        float Dot(const Vec3& o) const { return x*o.x + y*o.y + z*o.z; }
    };

    // Movement assist types
    enum class AssistType : int {
        AutoStrafe = 0,
        AutoJump = 1,
        AutoCrouch = 2,
        AutoSprint = 3,
        EdgeJump = 4,
        WallClimb = 5,
        LadderAssist = 6,
        SlideAssist = 7,
        AirControl = 8,
        Custom = 99
    };

    // Auto-strafe configuration
    struct AutoStrafeConfig {
        bool enabled = false;
        
        // Mode
        enum class Mode : int {
            Silent = 0,      // Only when moving
            Legit = 1,       // Smooth, human-like
            Rage = 2,        // Maximum efficiency
            Circle = 3,      // Circle strafing
            WOnly = 4        // W-only strafe
        } mode = Mode::Legit;
        
        // Keys
        int forward_key = 'W';
        int left_key = 'A';
        int right_key = 'D';
        int back_key = 'S';
        int jump_key = VK_SPACE;
        int strafe_left_key = 0;   // Optional dedicated strafe keys
        int strafe_right_key = 0;
        
        // Behavior
        float min_speed = 0.0f;          // Minimum speed to activate
        float max_speed = 0.0f;          // 0 = unlimited
        float acceleration = 1.0f;       // Acceleration multiplier
        float air_acceleration = 1.0f;   // Air acceleration multiplier
        float turn_speed = 90.0f;        // Degrees per second
        float max_angle = 90.0f;         // Max strafe angle
        
        // Circle strafe
        float circle_radius = 100.0f;
        bool circle_clockwise = true;
        float circle_height = 0.0f;      // Vertical offset
        
        // Humanization
        bool humanize = true;
        float reaction_variance = 0.02f;
        float angle_variance = 2.0f;     // Degrees
        float timing_variance = 0.01f;
        bool random_direction_changes = false;
        float direction_change_interval = 2.0f; // Seconds
        
        // Safety
        bool disable_in_menu = true;
        bool require_forward = true;
        bool disable_on_ladder = true;
        bool disable_in_water = true;
    };

    // Auto-jump configuration
    struct AutoJumpConfig {
        bool enabled = false;
        
        // Mode
        enum class Mode : int {
            Bhop = 0,           // Bunny hop
            AutoJump = 1,       // Jump when at edge
            AutoCrouchJump = 2, // Crouch jump assist
            LongJump = 3        // Long jump assist
        } mode = Mode::Bhop;
        
        // Keys
        int jump_key = VK_SPACE;
        int crouch_key = VK_CONTROL;
        
        // Behavior
        float min_height = 0.0f;          // Min height to trigger
        float max_height = 0.0f;          // Max height (0 = unlimited)
        float perfect_timing = 0.0f;      // Perfect timing window (ms)
        float min_ground_time = 0.0f;     // Min time on ground before jump
        bool only_when_moving = true;
        bool require_forward = false;
        
        // Edge jump
        float edge_check_distance = 50.0f;
        float edge_check_down = 100.0f;
        
        // Humanization
        bool humanize = true;
        float timing_variance = 0.005f;   // 5ms variance
        float late_jump_chance = 0.05f;   // Chance to jump slightly late
        float early_jump_chance = 0.02f;  // Chance to jump slightly early
        
        // Safety
        bool disable_in_menu = true;
        bool disable_on_ladder = true;
        bool disable_in_water = true;
    };

    // Auto-crouch configuration
    struct AutoCrouchConfig {
        bool enabled = false;
        
        enum class Mode : int {
            Peek = 0,           // Crouch when peeking
            Shoot = 1,          // Crouch when shooting
            Dodge = 2,          // Crouch to dodge
            Silent = 3          // Silent movement
        } mode = Mode::Peek;
        
        int crouch_key = VK_CONTROL;
        int toggle_key = 0;      // Toggle crouch mode
        
        float peek_delay = 0.05f;      // Delay before crouch
        float hold_time = 0.0f;        // How long to hold (0 = toggle)
        bool uncrouch_on_shot = false; // Stand up after shooting
        
        bool humanize = true;
        float reaction_variance = 0.01f;
        
        bool disable_in_menu = true;
    };

    // Edge jump / parkour assist
    struct EdgeJumpConfig {
        bool enabled = false;
        
        float check_distance = 60.0f;
        float check_down = 120.0f;
        float min_gap = 30.0f;
        float max_height_diff = 200.0f;
        
        bool auto_jump = true;
        bool auto_forward = true;
        float forward_time = 0.5f;
        
        bool humanize = true;
        float timing_variance = 0.01f;
    };

    // Master movement assist config
    struct MovementAssistConfig {
        bool enabled = false;
        bool master_enable = false;  // Master toggle
        
        AutoStrafeConfig auto_strafe;
        AutoJumpConfig auto_jump;
        AutoCrouchConfig auto_crouch;
        EdgeJumpConfig edge_jump;
        
        // Global
        bool disable_in_menu = true;
        bool disable_when_typing = true;
        bool require_any_key = false;  // Only when any movement key pressed
        
        // Profiles
        struct Profile {
            std::string name;
            AutoStrafeConfig strafe;
            AutoJumpConfig jump;
            AutoCrouchConfig crouch;
            EdgeJumpConfig edge;
        };
        
        std::vector<Profile> profiles;
        int active_profile = 0;
    };

    // Movement assist manager
    class MovementAssistManager {
    public:
        static MovementAssistManager& Instance();
        
        void SetConfig(const MovementAssistConfig& config) { config_ = config; }
        const MovementAssistConfig& GetConfig() const { return config_; }
        
        void Update(float dt, uint64_t local_player);
        void DrawStatus(ImDrawList* draw_list);
        
        // Profile management
        void SaveProfile(const std::string& name);
        void LoadProfile(const std::string& name);
        void DeleteProfile(const std::string& name);
        std::vector<std::string> GetProfileNames() const;
        
        // Quick toggles
        void ToggleAutoStrafe() { config_.auto_strafe.enabled = !config_.auto_strafe.enabled; }
        void ToggleAutoJump() { config_.auto_jump.enabled = !config_.auto_jump.enabled; }
        void ToggleAutoCrouch() { config_.auto_crouch.enabled = !config_.auto_crouch.enabled; }
        void ToggleEdgeJump() { config_.edge_jump.enabled = !config_.edge_jump.enabled; }
        
        // Serialization
        std::string SerializeConfig() const;
        bool DeserializeConfig(const std::string& data);
        
    private:
        MovementAssistConfig config_;
        std::chrono::steady_clock::time_point last_update_;
        std::mt19937 rng_;
        
        // State
        struct State {
            bool was_on_ground = true;
            float last_jump_time = 0.0f;
            float last_crouch_time = 0.0f;
            float strafe_angle = 0.0f;
            int strafe_direction = 1; // 1 = right, -1 = left
            float next_direction_change = 0.0f;
            bool was_crouching = false;
            float edge_check_timer = 0.0f;
        } state_;
        
        void UpdateAutoStrafe(float dt, uint64_t local_player);
        void UpdateAutoJump(float dt, uint64_t local_player);
        void UpdateAutoCrouch(float dt, uint64_t local_player);
        void UpdateEdgeJump(float dt, uint64_t local_player);
        
        // Game hooks (to be implemented per game)
        virtual bool IsOnGround() = 0;
        virtual bool IsInWater() = 0;
        virtual bool IsOnLadder() = 0;
        virtual float GetSpeed() = 0;
        virtual Vec3 GetVelocity() = 0;
        virtual bool IsKeyPressed(int key) = 0;
        virtual void SetKeyPressed(int key, bool pressed) = 0;
        virtual void SetMouseMove(float x, float y) = 0;
        virtual bool IsInMenu() = 0;
        virtual bool IsTyping() = 0;
        virtual Vec3 GetPosition() = 0;
        virtual bool TraceLine(const Vec3& start, const Vec3& end) = 0;
        virtual float GetDistanceToGround() = 0;
        virtual bool IsAtEdge(float forward_dist, float down_dist) = 0;
    };
    
    // Preset profiles
    MovementAssistConfig::Profile GetLegitMovementProfile();
    MovementAssistConfig::Profile GetRageMovementProfile();
    MovementAssistConfig::Profile GetKZMovementProfile(); // Kreedz/climb
    MovementAssistConfig::Profile GetSurfMovementProfile();
    MovementAssistConfig::Profile GetBhopMovementProfile();
    
    // Serialization
    std::string SerializeMovementConfig(const MovementAssistConfig& config);
    bool DeserializeMovementConfig(const std::string& data, MovementAssistConfig& config);

} // namespace Gameplay::MovementAssist