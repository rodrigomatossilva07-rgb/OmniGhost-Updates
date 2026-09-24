#pragma once

#include <cstdint>
#include "math/math.h"

// One ped's acquisition snapshot, consumed by ESP without DMA on render.
struct PreparedEspData {
    uintptr_t ped = 0;
    Vec3 origin{};
    float health = 0.0f;
    float max_health = 200.0f;
    float armor = 0.0f;
    float armor_alt_1 = 0.0f;
    float armor_alt_2 = 0.0f;
    uintptr_t player_info = 0;
    uint32_t network_id = 0;
    uintptr_t weapon_manager = 0;
    uintptr_t weapon_info = 0;
    uint32_t weapon_hash = 0;
    uintptr_t vehicle = 0;
    bool visible = false;
    bool visibility_known = false;
    uint8_t visibility_flag = 0;
    bool valid = false;
};
