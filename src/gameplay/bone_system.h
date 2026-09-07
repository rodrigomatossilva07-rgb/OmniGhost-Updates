#pragma once
#include "../../ImGui/imgui.h"
#include <array>
#include <vector>
#include <string>
#include <cstdint>

namespace Gameplay::BoneSystem {

    // Unified bone IDs across all games
    enum class BoneId : int {
        // Head hierarchy
        Head = 0,
        Neck = 1,
        Jaw = 2,
        LeftEye = 3,
        RightEye = 4,
        
        // Spine hierarchy
        SpineRoot = 5,
        SpineLower = 6,
        SpineMid = 7,
        SpineUpper = 8,
        SpineNeck = 9,
        
        // Left arm
        LeftClavicle = 10,
        LeftShoulder = 11,
        LeftElbow = 12,
        LeftWrist = 13,
        LeftHand = 14,
        LeftThumb = 15,
        LeftFingers = 16,
        
        // Right arm
        RightClavicle = 17,
        RightShoulder = 18,
        RightElbow = 19,
        RightWrist = 20,
        RightHand = 21,
        RightThumb = 22,
        RightFingers = 23,
        
        // Left leg
        LeftHip = 24,
        LeftKnee = 25,
        LeftAnkle = 26,
        LeftFoot = 27,
        LeftToes = 28,
        
        // Right leg
        RightHip = 29,
        RightKnee = 30,
        RightAnkle = 31,
        RightFoot = 32,
        RightToes = 33,
        
        // Special
        Pelvis = 34,
        Root = 35,
        
        Count
    };

    // Hitbox presets for targeting
    enum class HitboxPreset : int {
        Head = 0,
        Neck = 1,
        HeadNeck = 2,        // Priority: head -> neck
        UpperBody = 3,       // Priority: head -> neck -> spine_upper
        Torso = 4,           // Priority: spine_upper -> spine_mid -> pelvis
        CenterMass = 5,      // Priority: spine_mid (center of mass)
        Pelvis = 6,
        ClosestBone = 7,     // Dynamic: whichever bone is closest to crosshair
        Custom = 99
    };

    // Bone group for skeleton drawing
    enum class BoneGroup : int {
        Head = 0,
        Spine = 1,
        LeftArm = 2,
        RightArm = 3,
        LeftLeg = 4,
        RightLeg = 5,
        Full = 6
    };

    struct BoneConnection {
        BoneId from;
        BoneId to;
        BoneGroup group;
    };

    struct BoneInfo {
        BoneId id;
        const char* name;
        const char* display_name;
        HitboxPreset hitbox_preset;
        BoneGroup group;
        float aim_priority;    // 0.0 - 1.0, higher = more priority for aim
        float esp_priority;    // 0.0 - 1.0, higher = more visible in ESP
        bool is_head;          // Special handling for headshots
        bool is_critical;      // Critical hit zone
    };

    struct HitboxConfig {
        HitboxPreset preset = HitboxPreset::Head;
        std::array<BoneId, 8> priority_list{};
        int priority_count = 0;
        bool dynamic_switching = false;  // Switch bones based on visibility/distance
        float switch_distance = 50.0f;   // Distance to switch from head to body
        std::string custom_name;
    };

    struct BoneSettings {
        // Hitbox configuration
        HitboxConfig hitbox;
        
        // Skeleton drawing
        bool draw_skeleton = true;
        std::array<bool, 7> draw_groups{};  // Per BoneGroup
        float skeleton_thickness = 1.5f;
        ImU32 skeleton_color = IM_COL32(212, 175, 55, 200);
        ImU32 skeleton_color_visible = IM_COL32(0, 255, 100, 200);
        ImU32 skeleton_color_occluded = IM_COL32(255, 100, 100, 150);
        
        // Bone ESP
        bool show_bone_dots = false;
        float bone_dot_size = 3.0f;
        ImU32 bone_dot_color = IM_COL32(212, 175, 55, 255);
        
        // Hitbox visualization
        bool show_hitbox_preview = false;
        ImU32 hitbox_preview_color = IM_COL32(255, 255, 0, 180);
        
        // Per-bone overrides
        std::array<float, static_cast<int>(BoneId::Count)> aim_priority_multiplier{};
        std::array<bool, static_cast<int>(BoneId::Count)> bone_enabled{};
    };

    // Game-specific bone mappings
    struct GameBoneMapping {
        const char* game_name;
        std::array<int, static_cast<int>(BoneId::Count)> bone_indices{};
        std::array<bool, static_cast<int>(BoneId::Count)> bone_valid{};
        
        // Special bones for this game
        int head_bone = 0;
        int neck_bone = 7;
        int spine_root = 8;
        int pelvis_bone = 8;
        int left_foot = 1;
        int right_foot = 2;
    };

    // Initialize default bone settings
    void InitializeDefaults(BoneSettings& settings);
    
    // Get bone info by ID
    const BoneInfo& GetBoneInfo(BoneId id);
    
    // Get all bone connections for skeleton drawing
    const std::vector<BoneConnection>& GetBoneConnections();
    
    // Get bones in a group
    std::vector<BoneId> GetBonesInGroup(BoneGroup group);
    
    // Resolve hitbox to bone based on priority list
    BoneId ResolveHitbox(const HitboxConfig& config, const GameBoneMapping& mapping, 
                         float distance = 0.0f, bool head_visible = true);
    
    // Get hitbox preset name
    const char* GetHitboxPresetName(HitboxPreset preset);
    
    // Get bone group name
    const char* GetBoneGroupName(BoneGroup group);
    
    // Game-specific mappings
    const GameBoneMapping& GetGameMapping(const char* game_name);
    
    // Convert game-specific bone index to unified BoneId
    BoneId GameBoneToUnified(int game_bone, const GameBoneMapping& mapping);
    
    // Convert unified BoneId to game-specific index
    int UnifiedToGameBone(BoneId bone, const GameBoneMapping& mapping);
    
    // Default priority lists for each preset
    const std::array<BoneId, 8>& GetDefaultPriorityList(HitboxPreset preset);
    
    // Serialize/deserialize
    std::string SerializeHitboxConfig(const HitboxConfig& config);
    bool DeserializeHitboxConfig(const std::string& data, HitboxConfig& config);
    std::string SerializeBoneSettings(const BoneSettings& settings);
    bool DeserializeBoneSettings(const std::string& data, BoneSettings& settings);

} // namespace Gameplay::BoneSystem