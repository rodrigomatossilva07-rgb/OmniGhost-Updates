#pragma once

#include <cstdint>
#include <array>

namespace OmniGhost::Gameplay::UnifiedESP {

// Universal ESP data fields - bitmask for dependency tracking
enum class DataField : uint32_t {
    None         = 0,
    Position     = 1u << 0,  // Origin + velocity
    Skeleton     = 1u << 1,  // Full bone hierarchy
    Health       = 1u << 2,  // Health + armor
    Name         = 1u << 3,  // Player name + ID
    Weapon       = 1u << 4,  // Weapon name + ammo
    Visibility   = 1u << 5,  // Spotted/raycast visibility
    Velocity     = 1u << 6,  // Velocity for prediction
    Facing       = 1u << 7,  // View angles / look direction
    Team         = 1u << 8,  // Team ID
    Vehicle      = 1u << 9,  // Vehicle info (FiveM)
    Bomb         = 1u << 10, // C4/bomb state (CS2)
    DroppedItem  = 1u << 11, // World items (CS2/Warzone)
    Armor        = 1u << 12, // Optional armor read, separate from core health
};

constexpr DataField operator|(DataField a, DataField b) noexcept {
    return static_cast<DataField>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
constexpr DataField& operator|=(DataField& a, DataField b) noexcept { a = a | b; return a; }
constexpr DataField operator&(DataField a, DataField b) noexcept { return static_cast<DataField>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b)); }
constexpr bool Has(DataField fields, DataField value) noexcept { return (static_cast<uint32_t>(fields) & static_cast<uint32_t>(value)) != 0; }

// Universal feature flags - each game implements FeatureFlags() mapping
struct FeatureFlags {
    // Visual elements
    bool box = false;
    bool corner_box = false;
    bool box_fill = false;
    bool skeleton = false;
    bool skeleton_joints = false;
    bool head_dot = false;
    bool head_halo = false;
    bool head_circle = false;  // FiveM alias
    bool health_bar = false;
    bool armor_bar = false;
    bool health_value = false;
    bool armor_value = false;
    bool name = false;
    bool player_name = false;  // FiveM alias
    bool player_id = false;    // FiveM
    bool distance = false;
    bool weapon_name = false;
    bool weapon_ammo = false;
    bool snaplines = false;
    bool trails = false;
    bool chinese_hat = false;
    bool angel_wings = false;
    bool devil_horns = false;
    bool floating_crown = false;
    bool hit_marker = false;
    bool look_direction = false;
    bool radar_enabled = false;
    bool triangle_radar = false;
    bool blip_esp = false;       // FiveM
    bool waypoint_line = false;  // FiveM
    bool square_radar = false;   // FiveM
    bool self_esp = false;       // FiveM
    bool show_dead = false;      // FiveM
    bool show_knocked = false;   // FiveM
    bool show_bots = false;      // CS2/Fortnite
    bool npc_esp = false;        // FiveM
    bool visibility_colors = false;
    bool visible_check = false;
    bool team_check = false;
    bool ignore_downed = false;  // Warzone
    bool ignore_ai = false;      // Warzone
    bool bomb_timer = false;     // CS2
    bool spectator_list = false; // CS2
    bool dropped_weapons = false; // CS2
    bool radar_2d = false;       // CS2
    bool webradar_enabled = false; // CS2
    bool aim_enabled = false;
    bool trigger_enabled = false;
    bool aim_visibility_check = false;
    bool trigger_head_only = false;
    bool trigger_scoped_only = false;
    bool aim_prediction = false;
    bool trigger_use_ident = false;
    bool aim_ignore_bots = false;
    bool aim_ignore_spectators = false;
    bool aim_auto_bone = false;
    bool aim_humanize = false;
    bool rgb_mode = false;

    // Convert to unified DataField mask
    DataField RequiredFields() const noexcept {
        DataField fields = DataField::None;
        
        // Position is the foundation for almost everything
        if (box || corner_box || box_fill || skeleton || skeleton_joints || head_dot || 
            head_halo || head_circle || health_bar || armor_bar || health_value || 
            armor_value || snaplines || distance || weapon_name || weapon_ammo || 
            trails || chinese_hat || angel_wings || devil_horns || floating_crown || 
            hit_marker || look_direction || radar_enabled || triangle_radar || 
            blip_esp || waypoint_line || square_radar || self_esp || show_dead || 
            show_knocked || npc_esp || bomb_timer || spectator_list || dropped_weapons ||
            radar_2d || webradar_enabled || aim_enabled || trigger_enabled || radar_enabled)
            fields |= DataField::Position;
        
        // Skeleton needed for bones
        if (skeleton || skeleton_joints || head_dot || head_halo || head_circle || 
            chinese_hat || angel_wings || devil_horns || floating_crown || 
            look_direction || aim_enabled || trigger_enabled)
            fields |= DataField::Skeleton;
        
        // Health/Armor
        if (health_bar || armor_bar || health_value || armor_value || aim_enabled)
            fields |= DataField::Health;
        if (armor_bar || armor_value)
            fields |= DataField::Armor;
        
        // Name/Identity
        if (name || player_name || player_id || npc_esp || show_bots || 
            bomb_timer || spectator_list || webradar_enabled || radar_2d ||
            (aim_enabled && aim_ignore_bots))
            fields |= DataField::Name;
        
        // Weapon
        if (weapon_name || weapon_ammo || aim_enabled || trigger_enabled)
            fields |= DataField::Weapon;
        
        // Visibility
        if (visibility_colors || visible_check || aim_visibility_check || trigger_enabled)
            fields |= DataField::Visibility;
        
        // Velocity (for prediction)
        if (aim_prediction || aim_enabled || trigger_enabled || trails)
            fields |= DataField::Velocity;
        
        // Facing (look direction)
        if (look_direction || radar_2d || aim_enabled)
            fields |= DataField::Facing;
        
        // Team
        if (team_check || visibility_colors || radar_2d || webradar_enabled || aim_enabled)
            fields |= DataField::Team;
        
        // Vehicle (FiveM)
        if (radar_enabled && blip_esp)
            fields |= DataField::Vehicle;
        
        // Bomb (CS2)
        if (bomb_timer)
            fields |= DataField::Bomb;
        
        // Dropped items (CS2)
        if (dropped_weapons)
            fields |= DataField::DroppedItem;
        
        return fields;
    }
};

// LOD configuration for skeleton/visibility
struct LODConfig {
    // Distance thresholds (meters)
    float full_dist = 30.0f;
    float high_dist = 80.0f;
    float med_dist = 150.0f;
    float max_dist = 200.0f;
    
    // Bone counts per LOD
    int full_bones = 31;
    int high_bones = 13;
    int med_bones = 7;
    int low_bones = 1;
};

// Acquisition budget configuration
struct AcquisitionBudget {
    float optional_lane_budget_ms = 6.0f;     // Max time for optional DMA
    float pressure_reduce_1 = 0.75f;          // Pressure 1: 75% budget
    float pressure_reduce_2 = 0.58f;          // Pressure 2: 58% budget
    float pressure_reduce_3 = 0.33f;          // Pressure 3: 33% budget
};

// Camera selection for temporal coherence
struct CameraSnapshot {
    std::array<float, 16> view_matrix{};
    uint64_t timestamp_ms = 0;
    uint64_t session_generation = 0;
};

// Unified player data (minimal subset shared across games)
struct UnifiedPlayer {
    uintptr_t pawn = 0;
    uintptr_t controller = 0;
    uintptr_t scene = 0;           // Scene node / bone base
    float pos[3] = {};
    float velocity[3] = {};
    float health = 0.f;
    float armor = 0.f;
    float max_health = 100.f;
    int team = 0;
    bool alive = false;
    bool is_local = false;
    bool is_bot = false;
    bool visible = false;
    bool visibility_known = false;
    uint64_t position_sample_ms = 0;
    uint64_t bones_sample_ms = 0;
    char name[64] = {};
    char weapon[32] = {};
    int weapon_def = 0;
    int ammo_clip = -1;
    float distance = 0.f;
    // Bones (up to 31 for Warzone, 20 for CS2, 9 for FiveM)
    std::array<std::array<float, 3>, 31> bones{};
    uint32_t bones_mask = 0;
    bool bones_ok = false;
    bool full_bones_ok = false;
};

// Game-specific config adapter interface
class IGameConfigAdapter {
public:
    virtual ~IGameConfigAdapter() = default;
    virtual FeatureFlags GetFeatureFlags() const = 0;
    virtual LODConfig GetLODConfig() const = 0;
    virtual AcquisitionBudget GetBudget() const = 0;
    virtual bool IsFeatureEnabled(const char* feature_name) const = 0;
};

} // namespace OmniGhost::Gameplay::UnifiedESP
