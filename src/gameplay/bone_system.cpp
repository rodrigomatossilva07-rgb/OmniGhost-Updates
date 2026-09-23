#include "bone_system.h"
#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace Gameplay::BoneSystem {

    namespace {
        // Bone definitions
        const std::array<BoneInfo, static_cast<int>(BoneId::Count)> kBoneInfos = {{
            // Head hierarchy
            {BoneId::Head, "head", "Head", HitboxPreset::Head, BoneGroup::Head, 1.0f, 1.0f, true, true},
            {BoneId::Neck, "neck", "Neck", HitboxPreset::Neck, BoneGroup::Head, 0.9f, 0.9f, false, true},
            {BoneId::Jaw, "jaw", "Jaw", HitboxPreset::Head, BoneGroup::Head, 0.7f, 0.5f, false, false},
            {BoneId::LeftEye, "left_eye", "Left Eye", HitboxPreset::Head, BoneGroup::Head, 0.6f, 0.4f, false, false},
            {BoneId::RightEye, "right_eye", "Right Eye", HitboxPreset::Head, BoneGroup::Head, 0.6f, 0.4f, false, false},
            
            // Spine hierarchy
            {BoneId::SpineRoot, "spine_root", "Spine Root", HitboxPreset::Torso, BoneGroup::Spine, 0.5f, 0.6f, false, false},
            {BoneId::SpineLower, "spine_lower", "Lower Spine", HitboxPreset::Torso, BoneGroup::Spine, 0.6f, 0.7f, false, false},
            {BoneId::SpineMid, "spine_mid", "Mid Spine", HitboxPreset::CenterMass, BoneGroup::Spine, 0.8f, 0.9f, false, true},
            {BoneId::SpineUpper, "spine_upper", "Upper Spine", HitboxPreset::UpperBody, BoneGroup::Spine, 0.85f, 0.85f, false, true},
            {BoneId::SpineNeck, "spine_neck", "Neck Base", HitboxPreset::UpperBody, BoneGroup::Spine, 0.75f, 0.7f, false, false},
            
            // Left arm
            {BoneId::LeftClavicle, "left_clavicle", "Left Clavicle", HitboxPreset::UpperBody, BoneGroup::LeftArm, 0.4f, 0.5f, false, false},
            {BoneId::LeftShoulder, "left_shoulder", "Left Shoulder", HitboxPreset::UpperBody, BoneGroup::LeftArm, 0.5f, 0.6f, false, false},
            {BoneId::LeftElbow, "left_elbow", "Left Elbow", HitboxPreset::UpperBody, BoneGroup::LeftArm, 0.4f, 0.6f, false, false},
            {BoneId::LeftWrist, "left_wrist", "Left Wrist", HitboxPreset::UpperBody, BoneGroup::LeftArm, 0.3f, 0.5f, false, false},
            {BoneId::LeftHand, "left_hand", "Left Hand", HitboxPreset::UpperBody, BoneGroup::LeftArm, 0.3f, 0.5f, false, false},
            {BoneId::LeftThumb, "left_thumb", "Left Thumb", HitboxPreset::UpperBody, BoneGroup::LeftArm, 0.2f, 0.3f, false, false},
            {BoneId::LeftFingers, "left_fingers", "Left Fingers", HitboxPreset::UpperBody, BoneGroup::LeftArm, 0.1f, 0.2f, false, false},
            
            // Right arm
            {BoneId::RightClavicle, "right_clavicle", "Right Clavicle", HitboxPreset::UpperBody, BoneGroup::RightArm, 0.4f, 0.5f, false, false},
            {BoneId::RightShoulder, "right_shoulder", "Right Shoulder", HitboxPreset::UpperBody, BoneGroup::RightArm, 0.5f, 0.6f, false, false},
            {BoneId::RightElbow, "right_elbow", "Right Elbow", HitboxPreset::UpperBody, BoneGroup::RightArm, 0.4f, 0.6f, false, false},
            {BoneId::RightWrist, "right_wrist", "Right Wrist", HitboxPreset::UpperBody, BoneGroup::RightArm, 0.3f, 0.5f, false, false},
            {BoneId::RightHand, "right_hand", "Right Hand", HitboxPreset::UpperBody, BoneGroup::RightArm, 0.3f, 0.5f, false, false},
            {BoneId::RightThumb, "right_thumb", "Right Thumb", HitboxPreset::UpperBody, BoneGroup::RightArm, 0.2f, 0.3f, false, false},
            {BoneId::RightFingers, "right_fingers", "Right Fingers", HitboxPreset::UpperBody, BoneGroup::RightArm, 0.1f, 0.2f, false, false},
            
            // Left leg
            {BoneId::LeftHip, "left_hip", "Left Hip", HitboxPreset::Torso, BoneGroup::LeftLeg, 0.4f, 0.5f, false, false},
            {BoneId::LeftKnee, "left_knee", "Left Knee", HitboxPreset::Torso, BoneGroup::LeftLeg, 0.3f, 0.4f, false, false},
            {BoneId::LeftAnkle, "left_ankle", "Left Ankle", HitboxPreset::Torso, BoneGroup::LeftLeg, 0.2f, 0.3f, false, false},
            {BoneId::LeftFoot, "left_foot", "Left Foot", HitboxPreset::Pelvis, BoneGroup::LeftLeg, 0.1f, 0.2f, false, false},
            {BoneId::LeftToes, "left_toes", "Left Toes", HitboxPreset::Pelvis, BoneGroup::LeftLeg, 0.05f, 0.1f, false, false},
            
            // Right leg
            {BoneId::RightHip, "right_hip", "Right Hip", HitboxPreset::Torso, BoneGroup::RightLeg, 0.4f, 0.5f, false, false},
            {BoneId::RightKnee, "right_knee", "Right Knee", HitboxPreset::Torso, BoneGroup::RightLeg, 0.3f, 0.4f, false, false},
            {BoneId::RightAnkle, "right_ankle", "Right Ankle", HitboxPreset::Torso, BoneGroup::RightLeg, 0.2f, 0.3f, false, false},
            {BoneId::RightFoot, "right_foot", "Right Foot", HitboxPreset::Pelvis, BoneGroup::RightLeg, 0.1f, 0.2f, false, false},
            {BoneId::RightToes, "right_toes", "Right Toes", HitboxPreset::Pelvis, BoneGroup::RightLeg, 0.05f, 0.1f, false, false},
            
            // Special
            {BoneId::Pelvis, "pelvis", "Pelvis", HitboxPreset::Pelvis, BoneGroup::Spine, 0.7f, 0.8f, false, true},
            {BoneId::Root, "root", "Root", HitboxPreset::CenterMass, BoneGroup::Spine, 0.5f, 0.4f, false, false},
        }};
        
        // Skeleton connections for drawing
        const std::vector<BoneConnection> kBoneConnections = {
            // Head
            {BoneId::Head, BoneId::Neck, BoneGroup::Head},
            {BoneId::Neck, BoneId::SpineNeck, BoneGroup::Spine},
            
            // Spine
            {BoneId::SpineNeck, BoneId::SpineUpper, BoneGroup::Spine},
            {BoneId::SpineUpper, BoneId::SpineMid, BoneGroup::Spine},
            {BoneId::SpineMid, BoneId::SpineLower, BoneGroup::Spine},
            {BoneId::SpineLower, BoneId::SpineRoot, BoneGroup::Spine},
            {BoneId::SpineRoot, BoneId::Pelvis, BoneGroup::Spine},
            
            // Left arm
            {BoneId::SpineUpper, BoneId::LeftClavicle, BoneGroup::LeftArm},
            {BoneId::LeftClavicle, BoneId::LeftShoulder, BoneGroup::LeftArm},
            {BoneId::LeftShoulder, BoneId::LeftElbow, BoneGroup::LeftArm},
            {BoneId::LeftElbow, BoneId::LeftWrist, BoneGroup::LeftArm},
            {BoneId::LeftWrist, BoneId::LeftHand, BoneGroup::LeftArm},
            {BoneId::LeftHand, BoneId::LeftThumb, BoneGroup::LeftArm},
            {BoneId::LeftHand, BoneId::LeftFingers, BoneGroup::LeftArm},
            
            // Right arm
            {BoneId::SpineUpper, BoneId::RightClavicle, BoneGroup::RightArm},
            {BoneId::RightClavicle, BoneId::RightShoulder, BoneGroup::RightArm},
            {BoneId::RightShoulder, BoneId::RightElbow, BoneGroup::RightArm},
            {BoneId::RightElbow, BoneId::RightWrist, BoneGroup::RightArm},
            {BoneId::RightWrist, BoneId::RightHand, BoneGroup::RightArm},
            {BoneId::RightHand, BoneId::RightThumb, BoneGroup::RightArm},
            {BoneId::RightHand, BoneId::RightFingers, BoneGroup::RightArm},
            
            // Left leg
            {BoneId::Pelvis, BoneId::LeftHip, BoneGroup::LeftLeg},
            {BoneId::LeftHip, BoneId::LeftKnee, BoneGroup::LeftLeg},
            {BoneId::LeftKnee, BoneId::LeftAnkle, BoneGroup::LeftLeg},
            {BoneId::LeftAnkle, BoneId::LeftFoot, BoneGroup::LeftLeg},
            {BoneId::LeftFoot, BoneId::LeftToes, BoneGroup::LeftLeg},
            
            // Right leg
            {BoneId::Pelvis, BoneId::RightHip, BoneGroup::RightLeg},
            {BoneId::RightHip, BoneId::RightKnee, BoneGroup::RightLeg},
            {BoneId::RightKnee, BoneId::RightAnkle, BoneGroup::RightLeg},
            {BoneId::RightAnkle, BoneId::RightFoot, BoneGroup::RightLeg},
            {BoneId::RightFoot, BoneId::RightToes, BoneGroup::RightLeg},
        };
        
        // Default priority lists for each hitbox preset
        const std::array<std::array<BoneId, 8>, 8> kDefaultPriorityLists = {{
            // Head
            {BoneId::Head, BoneId::Neck, BoneId::Jaw, BoneId::LeftEye, BoneId::RightEye, BoneId::SpineNeck, BoneId::SpineUpper, BoneId::SpineMid},
            // Neck
            {BoneId::Neck, BoneId::Head, BoneId::SpineNeck, BoneId::SpineUpper, BoneId::LeftClavicle, BoneId::RightClavicle, BoneId::SpineMid, BoneId::Pelvis},
            // HeadNeck
            {BoneId::Head, BoneId::Neck, BoneId::SpineNeck, BoneId::SpineUpper, BoneId::SpineMid, BoneId::Pelvis, BoneId::LeftShoulder, BoneId::RightShoulder},
            // UpperBody
            {BoneId::Head, BoneId::Neck, BoneId::SpineNeck, BoneId::SpineUpper, BoneId::SpineMid, BoneId::LeftShoulder, BoneId::RightShoulder, BoneId::Pelvis},
            // Torso
            {BoneId::SpineUpper, BoneId::SpineMid, BoneId::Pelvis, BoneId::SpineLower, BoneId::SpineRoot, BoneId::LeftHip, BoneId::RightHip, BoneId::Neck},
            // CenterMass
            {BoneId::SpineMid, BoneId::SpineUpper, BoneId::Pelvis, BoneId::SpineLower, BoneId::SpineRoot, BoneId::LeftHip, BoneId::RightHip, BoneId::SpineNeck},
            // Pelvis
            {BoneId::Pelvis, BoneId::SpineRoot, BoneId::SpineLower, BoneId::LeftHip, BoneId::RightHip, BoneId::SpineMid, BoneId::SpineUpper, BoneId::Root},
            // ClosestBone (dynamic)
            {BoneId::Head, BoneId::Neck, BoneId::SpineUpper, BoneId::SpineMid, BoneId::Pelvis, BoneId::LeftShoulder, BoneId::RightShoulder, BoneId::SpineNeck},
        }};
        
        // Game-specific bone mappings
        const std::unordered_map<std::string, GameBoneMapping> kGameMappings = {
            {
                "FiveM",
                GameBoneMapping{
                    "FiveM",
                    {0, 7, -1, -1, -1, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 8, -1},
                    {true, true, false, false, false, true, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false},
                    0, 7, 8, 8, 1, 2
                }
            },
            {
                "CS2",
                GameBoneMapping{
                    "CS2",
                    {6, 5, 13, 14, 15, 0, 1, 2, 3, 4, 8, 9, 10, 11, 12, 24, 25, 26, 27, 16, 17, 18, 19, 20, 21, 22, 23, 28, 29, 30, 31, 32, 33, 34, 35, 36, -1, 7, 0, -1, -1, -1, -1},
                    {true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, false, true, true, false, false, false, false},
                    6, 5, 0, 7, 14, 15
                }
            },
            {
                "Warzone",
                GameBoneMapping{
                    "Warzone",
                    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, -1, 1, 0, -1, -1, -1, -1},
                    {true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, false, true, true, false, false, false, false},
                    0, 1, 5, 1, 27, 28
                }
            },
            {
                "Valorant",
                GameBoneMapping{
                    "Valorant",
                    {0, 1, -1, -1, -1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, -1, 1, 0, -1, -1, -1, -1},
                    {true, true, false, false, false, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, false, true, true, false, false, false, false},
                    0, 1, 2, 1, 25, 26
                }
            }
        };
    }

    void InitializeDefaults(BoneSettings& settings) {
        settings.hitbox.preset = HitboxPreset::HeadNeck;
        settings.hitbox.dynamic_switching = true;
        settings.hitbox.switch_distance = 50.0f;
        
        // Copy default priority list for HeadNeck
        auto& default_list = GetDefaultPriorityList(HitboxPreset::HeadNeck);
        settings.hitbox.priority_list = default_list;
        settings.hitbox.priority_count = 4;
        
        // Skeleton
        settings.draw_skeleton = true;
        settings.draw_groups.fill(true);
        settings.skeleton_thickness = 1.5f;
        settings.skeleton_color = IM_COL32(212, 175, 55, 200);
        settings.skeleton_color_visible = IM_COL32(0, 255, 100, 200);
        settings.skeleton_color_occluded = IM_COL32(255, 100, 100, 150);
        
        // Bone dots
        settings.show_bone_dots = false;
        settings.bone_dot_size = 3.0f;
        settings.bone_dot_color = IM_COL32(212, 175, 55, 255);
        
        // Hitbox preview
        settings.show_hitbox_preview = false;
        settings.hitbox_preview_color = IM_COL32(255, 255, 0, 180);
        
        // Per-bone
        settings.aim_priority_multiplier.fill(1.0f);
        settings.bone_enabled.fill(true);
    }

    const BoneInfo& GetBoneInfo(BoneId id) {
        int idx = static_cast<int>(id);
        if (idx >= 0 && idx < static_cast<int>(BoneId::Count)) {
            return kBoneInfos[idx];
        }
        static const BoneInfo invalid = {BoneId::Count, "invalid", "Invalid", HitboxPreset::Custom, BoneGroup::Full, 0.0f, 0.0f, false, false};
        return invalid;
    }

    const std::vector<BoneConnection>& GetBoneConnections() {
        return kBoneConnections;
    }

    std::vector<BoneId> GetBonesInGroup(BoneGroup group) {
        std::vector<BoneId> result;
        int group_idx = static_cast<int>(group);
        for (const auto& info : kBoneInfos) {
            if (static_cast<int>(info.group) == group_idx || group == BoneGroup::Full) {
                result.push_back(info.id);
            }
        }
        return result;
    }

    BoneId ResolveHitbox(const HitboxConfig& config, const GameBoneMapping& mapping, 
                         float distance, bool head_visible) {
        // Dynamic switching: head at close range, body at long range
        if (config.dynamic_switching && distance > config.switch_distance && config.preset == HitboxPreset::HeadNeck) {
            // Switch to upper body at long range
            for (int i = 0; i < config.priority_count; ++i) {
                BoneId bone = config.priority_list[i];
                int game_idx = UnifiedToGameBone(bone, mapping);
                if (game_idx >= 0 && mapping.bone_valid[game_idx]) {
                    return bone;
                }
            }
        }
        
        // Use priority list
        for (int i = 0; i < config.priority_count; ++i) {
            BoneId bone = config.priority_list[i];
            int game_idx = UnifiedToGameBone(bone, mapping);
            if (game_idx >= 0 && mapping.bone_valid[game_idx]) {
                // For head preset, check if head is visible
                if (config.preset == HitboxPreset::Head && bone == BoneId::Head && !head_visible) {
                    continue; // Skip head if not visible
                }
                return bone;
            }
        }
        
        // Fallback to first valid bone in mapping
        for (int i = 0; i < static_cast<int>(BoneId::Count); ++i) {
            if (mapping.bone_valid[i]) {
                return static_cast<BoneId>(i);
            }
        }
        
        return BoneId::Head;
    }

    const char* GetHitboxPresetName(HitboxPreset preset) {
        switch (preset) {
            case HitboxPreset::Head: return "Head";
            case HitboxPreset::Neck: return "Neck";
            case HitboxPreset::HeadNeck: return "Head > Neck";
            case HitboxPreset::UpperBody: return "Upper Body";
            case HitboxPreset::Torso: return "Torso";
            case HitboxPreset::CenterMass: return "Center Mass";
            case HitboxPreset::Pelvis: return "Pelvis";
            case HitboxPreset::ClosestBone: return "Closest Bone";
            case HitboxPreset::Custom: return "Custom";
        }
        return "Unknown";
    }

    const char* GetBoneGroupName(BoneGroup group) {
        switch (group) {
            case BoneGroup::Head: return "Head";
            case BoneGroup::Spine: return "Spine";
            case BoneGroup::LeftArm: return "Left Arm";
            case BoneGroup::RightArm: return "Right Arm";
            case BoneGroup::LeftLeg: return "Left Leg";
            case BoneGroup::RightLeg: return "Right Leg";
            case BoneGroup::Full: return "Full Body";
        }
        return "Unknown";
    }

    const GameBoneMapping& GetGameMapping(const char* game_name) {
        auto it = kGameMappings.find(game_name);
        if (it != kGameMappings.end()) {
            return it->second;
        }
        static const GameBoneMapping default_mapping = {"Default", {}, {}, 0, 7, 8, 8, 1, 2};
        return default_mapping;
    }

    BoneId GameBoneToUnified(int game_bone, const GameBoneMapping& mapping) {
        for (int i = 0; i < static_cast<int>(BoneId::Count); ++i) {
            if (mapping.bone_indices[i] == game_bone) {
                return static_cast<BoneId>(i);
            }
        }
        return BoneId::Head;
    }

    int UnifiedToGameBone(BoneId bone, const GameBoneMapping& mapping) {
        int idx = static_cast<int>(bone);
        if (idx >= 0 && idx < static_cast<int>(BoneId::Count)) {
            return mapping.bone_indices[idx];
        }
        return -1;
    }

    const std::array<BoneId, 8>& GetDefaultPriorityList(HitboxPreset preset) {
        int idx = static_cast<int>(preset);
        if (idx >= 0 && idx < 8) {
            return kDefaultPriorityLists[idx];
        }
        return kDefaultPriorityLists[0];
    }

    std::string SerializeHitboxConfig(const HitboxConfig& config) {
        std::ostringstream out;
        out << static_cast<int>(config.preset) << '|'
            << config.priority_count << '|'
            << (config.dynamic_switching ? 1 : 0) << '|'
            << config.switch_distance << '|';
        for (int i = 0; i < 8; ++i) {
            out << static_cast<int>(config.priority_list[i]) << (i < 7 ? ',' : '');
        }
        out << '|' << config.custom_name;
        return out.str();
    }

    bool DeserializeHitboxConfig(const std::string& data, HitboxConfig& config) {
        std::istringstream in(data);
        std::string token;
        
        if (!std::getline(in, token, '|')) return false;
        config.preset = static_cast<HitboxPreset>(std::stoi(token));
        
        if (!std::getline(in, token, '|')) return false;
        config.priority_count = std::stoi(token);
        
        if (!std::getline(in, token, '|')) return false;
        config.dynamic_switching = token == "1";
        
        if (!std::getline(in, token, '|')) return false;
        config.switch_distance = std::stof(token);
        
        if (!std::getline(in, token, '|')) return false;
        std::istringstream bones(token);
        std::string bone_token;
        int i = 0;
        while (std::getline(bones, bone_token, ',') && i < 8) {
            config.priority_list[i] = static_cast<BoneId>(std::stoi(bone_token));
            ++i;
        }
        
        std::getline(in, config.custom_name);
        return true;
    }

    std::string SerializeBoneSettings(const BoneSettings& settings) {
        std::ostringstream out;
        out << SerializeHitboxConfig(settings.hitbox) << '|'
            << (settings.draw_skeleton ? 1 : 0) << '|';
        
        for (int i = 0; i < 7; ++i) {
            out << (settings.draw_groups[i] ? 1 : 0) << (i < 6 ? ',' : '');
        }
        out << '|' << settings.skeleton_thickness << '|'
            << settings.skeleton_color << '|'
            << settings.skeleton_color_visible << '|'
            << settings.skeleton_color_occluded << '|'
            << (settings.show_bone_dots ? 1 : 0) << '|'
            << settings.bone_dot_size << '|'
            << settings.bone_dot_color << '|'
            << (settings.show_hitbox_preview ? 1 : 0) << '|'
            << settings.hitbox_preview_color << '|';
        
        for (int i = 0; i < static_cast<int>(BoneId::Count); ++i) {
            out << settings.aim_priority_multiplier[i] << (i < static_cast<int>(BoneId::Count) - 1 ? ',' : '');
        }
        out << '|';
        for (int i = 0; i < static_cast<int>(BoneId::Count); ++i) {
            out << (settings.bone_enabled[i] ? 1 : 0) << (i < static_cast<int>(BoneId::Count) - 1 ? ',' : '');
        }
        return out.str();
    }

    bool DeserializeBoneSettings(const std::string& data, BoneSettings& settings) {
        std::istringstream in(data);
        std::string token;
        
        // Parse hitbox config first
        std::string hitbox_data;
        std::getline(in, hitbox_data, '|'); // preset
        std::getline(in, token, '|'); // priority_count
        hitbox_data += '|' + token + '|';
        std::getline(in, token, '|'); // dynamic_switching
        hitbox_data += token + '|';
        std::getline(in, token, '|'); // switch_distance
        hitbox_data += token + '|';
        std::getline(in, token, '|'); // priority_list
        hitbox_data += token + '|';
        std::getline(in, token, '|'); // custom_name
        hitbox_data += token;
        
        if (!DeserializeHitboxConfig(hitbox_data, settings.hitbox)) return false;
        
        // Parse rest
        if (!std::getline(in, token, '|')) return false;
        settings.draw_skeleton = token == "1";
        
        if (!std::getline(in, token, '|')) return false;
        std::istringstream groups(token);
        for (int i = 0; i < 7; ++i) {
            if (!std::getline(groups, token, ',')) return false;
            settings.draw_groups[i] = token == "1";
        }
        
        if (!std::getline(in, token, '|')) return false;
        settings.skeleton_thickness = std::stof(token);
        
        if (!std::getline(in, token, '|')) return false;
        settings.skeleton_color = std::stoul(token);
        
        if (!std::getline(in, token, '|')) return false;
        settings.skeleton_color_visible = std::stoul(token);
        
        if (!std::getline(in, token, '|')) return false;
        settings.skeleton_color_occluded = std::stoul(token);
        
        if (!std::getline(in, token, '|')) return false;
        settings.show_bone_dots = token == "1";
        
        if (!std::getline(in, token, '|')) return false;
        settings.bone_dot_size = std::stof(token);
        
        if (!std::getline(in, token, '|')) return false;
        settings.bone_dot_color = std::stoul(token);
        
        if (!std::getline(in, token, '|')) return false;
        settings.show_hitbox_preview = token == "1";
        
        if (!std::getline(in, token, '|')) return false;
        settings.hitbox_preview_color = std::stoul(token);
        
        if (!std::getline(in, token, '|')) return false;
        std::istringstream multipliers(token);
        for (int i = 0; i < static_cast<int>(BoneId::Count); ++i) {
            if (!std::getline(multipliers, token, ',')) return false;
            settings.aim_priority_multiplier[i] = std::stof(token);
        }
        
        if (!std::getline(in, token)) return false;
        std::istringstream enabled(token);
        for (int i = 0; i < static_cast<int>(BoneId::Count); ++i) {
            if (!std::getline(enabled, token, ',')) return false;
            settings.bone_enabled[i] = token == "1";
        }
        
        return true;
    }

} // namespace Gameplay::BoneSystem
