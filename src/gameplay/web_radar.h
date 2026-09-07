#pragma once
#include "../../ImGui/imgui.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>
#include <cmath>

namespace Gameplay::WebRadar {

    // Basic math types
    struct Vec2 {
        float x = 0, y = 0;
        Vec2() = default;
        Vec2(float x_, float y_) : x(x_), y(y_) {}
        Vec2 operator+(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
        Vec2 operator-(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
        Vec2 operator*(float s) const { return Vec2(x * s, y * s); }
        Vec2 operator/(float s) const { return Vec2(x / s, y / s); }
        float Length() const { return sqrtf(x*x + y*y); }
    };

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

    // Radar entity types
    enum class EntityType : int {
        Player = 0,
        Vehicle = 1,
        Animal = 2,
        Resource = 3,
        Loot = 4,
        Building = 5,
        DroppedItem = 6,
        Projectile = 7,
        NPC = 8,
        Custom = 99
    };

    // Radar layer for rendering
    enum class Layer : int {
        Background = 0,
        Grid = 1,
        Terrain = 2,
        Entities = 3,
        Labels = 4,
        UI = 5,
        Foreground = 6
    };

    // Entity icon style
    struct IconStyle {
        enum class Type : int {
            Circle = 0,
            Square = 1,
            Triangle = 2,
            Diamond = 3,
            Cross = 4,
            Arrow = 5,
            Custom = 99
        };
        
        Type type = Type::Circle;
        float size = 8.0f;
        ImU32 color = IM_COL32(255, 255, 255, 255);
        ImU32 outline_color = IM_COL32(0, 0, 0, 255);
        float outline_thickness = 1.0f;
        float rotation = 0.0f; // For arrow type
        bool use_texture = false;
        ImTextureID texture_id = nullptr;
        ImVec2 texture_uv_min = ImVec2(0, 0);
        ImVec2 texture_uv_max = ImVec2(1, 1);
    };

    // Radar entity
    struct RadarEntity {
        uint64_t id = 0;
        EntityType type = EntityType::Player;
        std::string name;
        Vec3 world_pos{};
        Vec3 velocity{};
        float distance = 0.0f;
        bool is_friendly = false;
        bool is_local = false;
        int team = 0;
        int health = 100;
        int max_health = 100;
        std::string weapon;
        IconStyle icon;
        float last_update = 0.0f;
        bool active = true;
        
        // Custom data for extensibility
        std::unordered_map<std::string, std::string> custom_data;
    };

    // Radar configuration
    struct RadarConfig {
        // General
        bool enabled = true;
        bool follow_player = true;
        bool rotate_with_player = true;
        
        // Range
        float min_range = 10.0f;
        float max_range = 500.0f;
        float default_range = 200.0f;
        float range_step = 50.0f;
        bool auto_range = false;
        
        // Display
        float radar_size = 250.0f;
        float background_alpha = 0.8f;
        ImU32 background_color = IM_COL32(15, 15, 20, 230);
        ImU32 border_color = IM_COL32(80, 80, 100, 180);
        
        // Grid
        bool show_grid = true;
        int grid_divisions = 4;
        ImU32 grid_color = IM_COL32(60, 70, 90, 180);
        float grid_thickness = 1.0f;
        bool show_cardinal = true;
        ImU32 cardinal_color = IM_COL32(100, 110, 130, 200);
        
        // Compass
        bool show_compass = true;
        float compass_radius = 120.0f;
        ImU32 compass_color = IM_COL32(200, 200, 220, 200);
        bool show_degrees = true;
        
        // Entities
        struct EntityDisplay {
            bool enabled = true;
            float max_distance = 500.0f;
            bool show_names = true;
            bool show_distance = true;
            bool show_health = true;
            bool show_weapon = false;
            bool show_team_color = true;
            IconStyle icon;
            float name_scale = 1.0f;
            float fade_distance = 300.0f;
            bool only_visible = false; // Only show if visibility check passes
        };
        
        std::array<EntityDisplay, 10> entity_displays{};
        
        // Local player
        IconStyle local_player_icon;
        ImU32 local_player_color = IM_COL32(0, 255, 100, 255);
        bool show_local_direction = true;
        
        // Friends
        bool highlight_friends = true;
        ImU32 friend_color = IM_COL32(0, 200, 255, 255);
        
        // Filtering
        std::vector<int> ignored_teams;
        std::vector<uint64_t> ignored_entities;
        float min_entity_distance = 0.0f;
        
        // Interaction
        bool click_to_ping = true;
        bool right_click_menu = true;
        bool drag_to_move = false;
        
        // Animation
        bool animate_entities = true;
        float entity_fade_time = 0.2f;
        bool smooth_rotation = true;
        float rotation_speed = 10.0f;
        
        // Performance
        int max_entities = 128;
        int updates_per_second = 20;
        bool cull_offscreen = true;
    };

    // Radar state
    struct RadarState {
        Vec3 center_pos{};
        float current_range = 200.0f;
        float target_range = 200.0f;
        float rotation = 0.0f;
        float target_rotation = 0.0f;
        Vec2 screen_center{};
        float screen_radius = 125.0f;
        float zoom_level = 1.0f;
        bool is_dragging = false;
        Vec2 drag_start{};
        float drag_start_rotation = 0.0f;
        float last_update = 0.0f;
        std::vector<RadarEntity> entities;
        std::vector<uint64_t> pinged_entities;
    };

    // Game adapter interface for radar
    class IRadarAdapter {
    public:
        virtual ~IRadarAdapter() = default;
        virtual std::vector<RadarEntity> GetEntities() = 0;
        virtual Vec3 GetLocalPlayerPos() = 0;
        virtual float GetLocalPlayerYaw() = 0;
        virtual bool IsEntityVisible(uint64_t id) = 0;
        virtual std::string GetGameName() = 0;
        virtual void OnRangeChanged([[maybe_unused]] float new_range) {}
        virtual void OnRotationChanged([[maybe_unused]] float new_rotation) {}
    };

    // Main radar class
    class WebRadar {
    public:
        WebRadar();
        ~WebRadar();
        
        void SetConfig(const RadarConfig& config) { config_ = config; }
        const RadarConfig& GetConfig() const { return config_; }
        RadarConfig& GetMutableConfig() { return config_; }
        
        void SetAdapter(IRadarAdapter* adapter) { adapter_ = adapter; }
        
        void Update(float dt);
        void Draw(ImDrawList* draw_list, const ImVec2& position);
        void DrawAtPosition(ImDrawList* draw_list, const ImVec2& position, const ImVec2& size);
        
        // Entity management
        void AddEntity(const RadarEntity& entity);
        void RemoveEntity(uint64_t id);
        void UpdateEntity(const RadarEntity& entity);
        void ClearEntities();
        const std::vector<RadarEntity>& GetEntities() const { return state_.entities; }
        
        // Range control
        void SetRange(float range);
        void ZoomIn(float factor = 1.2f);
        void ZoomOut(float factor = 1.2f);
        void ResetRange();
        
        // Rotation
        void SetRotation(float rotation);
        void AddRotation(float delta);
        void ResetRotation();
        
        // Ping system
        void PingEntity(uint64_t id);
        void PingPosition(const Vec3& world_pos);
        const std::vector<uint64_t>& GetPings() const { return state_.pinged_entities; }
        void ClearPings();
        
        // World <-> Screen conversion
        ImVec2 WorldToRadar(const Vec3& world_pos) const;
        Vec3 RadarToWorld(const ImVec2& radar_pos) const;
        
        // Entity filtering
        void SetEntityFilter(std::function<bool(const RadarEntity&)> filter) { entity_filter_ = filter; }
        void ClearEntityFilter() { entity_filter_ = nullptr; }
        
        // Serialization
        std::string SerializeConfig() const;
        bool DeserializeConfig(const std::string& data);
        
    private:
        RadarConfig config_;
        RadarState state_;
        IRadarAdapter* adapter_ = nullptr;
        std::function<bool(const RadarEntity&)> entity_filter_;
        
        void UpdateEntities();
        void UpdateRange(float dt);
        void UpdateRotation(float dt);
        void DrawBackground(ImDrawList* draw_list);
        void DrawGrid(ImDrawList* draw_list);
        void DrawCompass(ImDrawList* draw_list);
        void DrawEntities(ImDrawList* draw_list);
        void DrawLocalPlayer(ImDrawList* draw_list);
        void DrawPings(ImDrawList* draw_list);
        void DrawEntityIcon(ImDrawList* draw_list, const RadarEntity& entity, const ImVec2& pos);
        void DrawEntityLabel(ImDrawList* draw_list, const RadarEntity& entity, const ImVec2& pos);
        bool ShouldDrawEntity(const RadarEntity& entity) const;
        ImVec2 WorldToRadarInternal(const Vec3& world_pos) const;
        
        static void DrawIcon(ImDrawList* draw_list, const ImVec2& center, const IconStyle& style);
    };
    
    // Radar manager for multiple radars
    class RadarManager {
    public:
        static RadarManager& Instance();
        
        WebRadar* CreateRadar(const std::string& name, IRadarAdapter* adapter = nullptr);
        WebRadar* GetRadar(const std::string& name);
        void RemoveRadar(const std::string& name);
        void ClearAll();
        
        void UpdateAll(float dt);
        void DrawAll();
        
    private:
        std::unordered_map<std::string, std::unique_ptr<WebRadar>> radars_;
    };
    
    // Preset configurations
    RadarConfig GetDefaultRadarConfig();
    RadarConfig GetCompetitiveRadarConfig();
    RadarConfig GetSniperRadarConfig();
    RadarConfig GetVehicleRadarConfig();
    RadarConfig GetMinimalRadarConfig();
    
    // Default entity display configs
    void InitializeDefaultEntityDisplays(RadarConfig& config);
    
    // Serialization
    std::string SerializeRadarConfig(const RadarConfig& config);
    bool DeserializeRadarConfig(const std::string& data, RadarConfig& config);

} // namespace Gameplay::WebRadar