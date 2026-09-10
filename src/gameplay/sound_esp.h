#pragma once
#include "../../ImGui/imgui.h"
#include <vector>
#include <string>
#include <array>
#include <chrono>
#include <unordered_map>
#include <cmath>

namespace Gameplay::SoundESP {

    // Basic math types
    struct Vec3 {
        float x = 0, y = 0, z = 0;
        Vec3() = default;
        Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
        Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
        Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
        Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
        Vec3 operator/(float s) const { return Vec3(x / s, y / s, z / s); }
        float Length() const { return sqrtf(x*x + y*y + z*z); }
        float Length2D() const { return sqrtf(x*x + y*y); }
        float Dot(const Vec3& o) const { return x*o.x + y*o.y + z*o.z; }
        Vec3 Normalized() const { float l = Length(); return l > 0 ? *this / l : Vec3{}; }
    };

    // 4x4 Matrix
    struct Matrix {
        float m[16] = {};
        float& operator()(int row, int col) { return m[row * 4 + col]; }
        const float& operator()(int row, int col) const { return m[row * 4 + col]; }
    };

    // Sound event types
    enum class SoundType : int {
        Footstep = 0,
        Gunshot = 1,
        Explosion = 2,
        Reload = 3,
        Jump = 4,
        Land = 5,
        Vault = 6,
        Door = 7,
        Vehicle = 8,
        Equipment = 9,
        Voice = 10,
        Custom = 99
    };

    // Sound source info
    struct SoundEvent {
        uint64_t source_id = 0;          // Entity that made the sound
        SoundType type = SoundType::Footstep;
        Vec3 position{};                 // World position
        Vec3 direction{};                // Direction of sound
        float volume = 1.0f;             // 0.0 - 1.0
        float distance = 0.0f;           // Distance from listener
        float pitch = 1.0f;              // Pitch modification
        std::chrono::steady_clock::time_point timestamp;
        bool is_friendly = false;
        bool is_local = false;
        std::string weapon;              // For gunshots
        int ammo_remaining = -1;         // For reload sounds
        std::unordered_map<std::string, std::string> custom_data;
    };

    // Sound ESP configuration
    struct SoundESPConfig {
        bool enabled = true;
        bool only_when_aiming = false;
        
        // Visual
        struct VisualConfig {
            bool show_footsteps = true;
            bool show_gunshots = true;
            bool show_explosions = true;
            bool show_reloads = true;
            bool show_voice = false;
            
            // Icons
            struct IconConfig {
                bool enabled = true;
                ImVec2 size = ImVec2(20, 20);
                float fade_time = 3.0f;        // Seconds before fading
                float fade_duration = 1.0f;    // Fade out duration
                bool pulse = true;             // Pulse animation
                float pulse_speed = 2.0f;
                ImU32 color = IM_COL32(255, 255, 255, 255);
                ImU32 outline_color = IM_COL32(0, 0, 0, 255);
                float outline_thickness = 1.5f;
            };
            
            std::array<IconConfig, 11> type_icons{};
        } visual;
        
        // Directional indicators
        struct DirectionalConfig {
            bool enabled = true;
            bool show_on_radar = true;
            bool show_on_screen = true;
            bool show_3d = false;
            
            float indicator_size = 30.0f;
            float screen_edge_margin = 50.0f;
            ImU32 color = IM_COL32(255, 200, 0, 255);
            ImU32 friendly_color = IM_COL32(0, 200, 255, 255);
            float fade_distance = 100.0f;
            bool show_distance = true;
        } directionals;
        
        // Audio visualization
        struct AudioVisualConfig {
            bool enabled = false;
            int fft_size = 256;
            float history_time = 5.0f;
            ImU32 waveform_color = IM_COL32(212, 175, 55, 200);
            ImU32 spectrum_color = IM_COL32(0, 255, 150, 200);
        } audio_visual;
        
        // Filtering
        float max_distance = 200.0f;
        float min_volume = 0.1f;
        std::vector<SoundType> ignored_types;
        std::vector<int> ignored_teams;
        
        // Performance
        int max_events = 64;
        float cleanup_interval = 1.0f;
    };

    // Sound ESP manager
    class SoundESPManager {
    public:
        static SoundESPManager& Instance();
        
        void SetConfig(const SoundESPConfig& config) { config_ = config; }
        const SoundESPConfig& GetConfig() const { return config_; }
        
        // Event handling
        void AddEvent(const SoundEvent& event);
        void RemoveEvent(uint64_t event_id);
        void ClearEvents();
        
        // Update
        void Update(float dt);
        
        // Drawing
        void DrawEvents(ImDrawList* draw_list, const Vec3& camera_pos, const Matrix& view_proj);
        void DrawDirectionalIndicators(ImDrawList* draw_list, const Vec3& camera_pos, const Matrix& view_proj);
        void DrawAudioVisualizer(ImDrawList* draw_list, const ImVec2& pos, const ImVec2& size);
        
        // Game integration
        void OnGameSoundEvent(const SoundEvent& event);  // Called by game hook
        
        // Get events for UI
        const std::vector<SoundEvent>& GetEvents() const { return events_; }
        std::vector<SoundEvent> GetEventsByType(SoundType type) const;
        
        // Serialization
        std::string SerializeConfig() const;
        bool DeserializeConfig(const std::string& data);
        
    private:
        SoundESPConfig config_;
        std::vector<SoundEvent> events_;
        uint64_t next_event_id_ = 1;
        
        void CleanupOldEvents();
        void DrawEventIcon(ImDrawList* draw_list, const SoundEvent& event, const ImVec2& screen_pos);
        void DrawDirectionalArrow(ImDrawList* draw_list, const SoundEvent& event, const ImVec2& screen_center);
        bool WorldToScreen(const Vec3& world, const Vec3& camera_pos,
                           const Matrix& view_proj, ImVec2& out);
    };
    
    // Sound type presets
    void InitializeDefaultSoundIcons(SoundESPConfig::VisualConfig& visual);
    
    // Preset configurations
    SoundESPConfig GetLegitSoundESPConfig();
    SoundESPConfig GetCompetitiveSoundESPConfig();
    SoundESPConfig GetSniperSoundESPConfig();
    SoundESPConfig GetMinimalSoundESPConfig();
    
    // Serialization
    std::string SerializeSoundESPConfig(const SoundESPConfig& config);
    bool DeserializeSoundESPConfig(const std::string& data, SoundESPConfig& config);

} // namespace Gameplay::SoundESP
