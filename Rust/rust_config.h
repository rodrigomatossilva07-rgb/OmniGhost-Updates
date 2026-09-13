#pragma once
#include <cstdint>

namespace Rust {

struct Config {
    // ESP
    bool esp_enabled = true;
    bool esp_box = true;
    bool esp_name = true;
    bool esp_health = true;
    bool esp_distance = true;
    bool esp_skeleton = false;
    bool esp_ignore_sleepers = true;
    bool esp_ignore_npc = false;
    float esp_max_distance = 350.f;
    float col_box[4] = {0.92f, 0.75f, 0.20f, 0.95f};
    float col_name[4] = {1.f, 1.f, 1.f, 0.95f};
    float col_skeleton[4] = {0.92f, 0.75f, 0.20f, 0.90f};

    // Aim (stub ready)
    bool aim_enabled = false;
    bool aim_visible_only = false;
    float aim_fov = 80.f;
    float aim_smooth = 6.f;
    int aim_bone = 0; // 0 head

    // System
    bool stream_proof = false;
};

inline Config config{};

} // namespace Rust
