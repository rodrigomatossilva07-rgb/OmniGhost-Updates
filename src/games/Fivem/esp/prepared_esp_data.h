#pragma once

#include <cstdint>
#include "math/math.h"
#include "../game/health_state.h"

// One ped's acquisition snapshot, consumed by ESP without DMA on render.
struct PreparedEspData {
    uintptr_t ped = 0;
    Vec3 origin{};
    float health = -1.0f;
    float max_health = -1.0f;
    bool health_valid = false;
    float armor = 0.0f;
    float armor_alt_1 = 0.0f;
    float armor_alt_2 = 0.0f;
    uintptr_t player_info = 0;
    uint32_t network_id = 0;
    char player_name[48]{};
    uintptr_t name_ptr = 0; // P1-6: name pointer for batched resolution
    uintptr_t weapon_manager = 0;
    uintptr_t weapon_info = 0;
    uint32_t weapon_hash = 0;
    uintptr_t vehicle = 0;
    bool visible = false;
    bool visibility_known = false;
    uint8_t visibility_flag = 0;
    bool valid = false;

    // Bone data for skeleton/head ESP (unified acquisition)
    Matrix bone_matrix{};
    // P2-5: Expanded from 9 to 20 slots for full skeleton support
    // 0=head, 1=neck, 2=spine_upper, 3=spine_mid, 4=spine_lower, 5=pelvis,
    // 6=clavicle_l, 7=shoulder_l, 8=elbow_l, 9=hand_l,
    // 10=clavicle_r, 11=shoulder_r, 12=elbow_r, 13=hand_r,
    // 14=hip_l, 15=knee_l, 16=ankle_l,
    // 17=hip_r, 18=knee_r, 19=ankle_r
    std::array<Vector3, 20> bone_offsets{};
    uint16_t bone_mask = 0;
    bool bones_ok = false;
    uint64_t bones_sample_ms = 0; // acquisition midpoint timestamp

    // Temporal coherence: acquisition timestamps for camera/world coherence
    uint64_t world_sample_ms = 0;       // midpoint of position/health scatter
    uint64_t camera_sample_ms = 0;      // selected camera sample time for this entity
};

// P2-7: Frame-level render config snapshot — render reads this instead of global esp::config.
struct PreparedRenderConfig {
    float max_esp_distance = 200.0f;
    bool show_dead = false;
    bool visible_check = false;
    bool visibility_colors = false;
    bool box_2d = false;
    bool corner_box = false;
    bool snaplines = false;
    bool health_bar = false;
    bool armor_bar = false;
    bool skeleton = false;
    bool head_circle = false;
    bool chinese_hat = false;
    bool angel_wings = false;
    bool devil_horns = false;
    bool floating_crown = false;
    bool head_halo = false;
    bool look_direction = false;
    bool team_check = false;
    bool npc_esp = false;
    bool player_name = false;
    bool player_id = false;
    bool distance = false;
    bool weapon_name = false;
    bool radar_enabled = false;
    bool triangle_radar = false;
    bool trails = false;
    float trail_duration = 1.0f;
    float trail_thickness = 2.0f;
    bool rainbow_trails = false;
    ImU32 color_visible = IM_COL32(0, 255, 0, 255);
    ImU32 color_invisible = IM_COL32(255, 0, 0, 255);
    ImU32 color_dead = IM_COL32(255, 50, 50, 255);
    ImU32 color_team = IM_COL32(0, 255, 255, 255);
    ImU32 color_npc = IM_COL32(128, 128, 128, 255);
    ImU32 color_box_2d = IM_COL32(255, 255, 255, 255);
    ImU32 color_corner_box = IM_COL32(255, 255, 255, 255);
    ImU32 color_snaplines = IM_COL32(255, 255, 255, 255);
    ImU32 color_health = IM_COL32(0, 255, 0, 255);
    ImU32 color_armor = IM_COL32(0, 100, 255, 255);
    ImU32 color_skeleton = IM_COL32(255, 0, 0, 255);
    ImU32 color_name = IM_COL32(255, 255, 255, 255);
    ImU32 color_id = IM_COL32(200, 200, 200, 255);
    ImU32 color_distance = IM_COL32(200, 200, 200, 255);
    ImU32 color_weapon = IM_COL32(255, 255, 0, 255);
    ImU32 color_skeleton_points = IM_COL32(80, 220, 90, 255);
    bool joints = false;
    float skeleton_thickness = 1.5f;
    int circle_type = 0;
    float head_circle_thickness = 2.0f;
    float box_thickness = 1.0f;
    float snapline_thickness = 1.0f;
    int snapline_pos = 0;
    bool filled_box = false;
    float box_rounding = 0.0f;
    bool health_value = false;
    bool armor_value = false;
    bool skeleton_lod = false;
    bool footstep_esp = false;
    bool hit_marker = false;
};
