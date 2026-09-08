#pragma once

#include <string>

namespace Fivem_Radar {

struct Config {
    bool enabled = false;
    int port = 8080;
    bool lan = false;
    bool cloudflare = false;
    bool show_local = true;
    bool show_npcs = false;
    float update_rate_hz = 15.0f;
    float max_distance = 5000.0f;
};

extern Config config;

bool SaveConfig(const std::string& name = "fivem_radar");
bool LoadConfig(const std::string& name = "fivem_radar");

} // namespace Fivem_Radar