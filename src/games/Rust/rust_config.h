#pragma once

namespace Rust {

struct Config {
    bool esp_enabled = true;
    bool box_2d = true;
    bool box_fill = false;
    float box_thickness = 1.5f;
    float box_rounding = 0.0f;
    float max_distance = 300.0f;
    float box_color[4] = {0.92f, 0.72f, 0.20f, 1.0f};
    float box_fill_color[4] = {0.92f, 0.72f, 0.20f, 0.10f};
};

}
