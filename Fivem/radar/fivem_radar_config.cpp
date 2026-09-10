#include "fivem_radar_config.h"
#include "platform/app_paths.h"
#include "platform/text_encoding.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace Fivem_Radar {

Config config;

bool SaveConfig(const std::string& name)
{
    fs::path file = OmniGhost::Paths::Configs() / (name + ".cfg");
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if (ec) return false;

    std::ofstream out(file, std::ios::trunc);
    if (!out) return false;

    out << "enabled=" << (config.enabled ? "1" : "0") << "\n";
    out << "port=" << config.port << "\n";
    out << "lan=" << (config.lan ? "1" : "0") << "\n";
    out << "cloudflare=" << (config.cloudflare ? "1" : "0") << "\n";
    out << "show_local=" << (config.show_local ? "1" : "0") << "\n";
    out << "show_npcs=" << (config.show_npcs ? "1" : "0") << "\n";
    out << "update_rate_hz=" << config.update_rate_hz << "\n";
    out << "max_distance=" << config.max_distance << "\n";

    return out.good();
}

bool LoadConfig(const std::string& name)
{
    fs::path file = OmniGhost::Paths::Configs() / (name + ".cfg");
    std::error_code ec;
    if (!fs::is_regular_file(file, ec))
        return false;

    std::ifstream in(file);
    if (!in) return false;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        auto trim = [](std::string& s) {
            size_t start = s.find_first_not_of(" \t\r\n");
            size_t end = s.find_last_not_of(" \t\r\n");
            if (start == std::string::npos) { s.clear(); return; }
            s = s.substr(start, end - start + 1);
        };
        trim(key);
        trim(val);

        if (key == "enabled") config.enabled = (val == "1" || val == "true");
        else if (key == "port") config.port = std::stoi(val);
        else if (key == "lan") config.lan = (val == "1" || val == "true");
        else if (key == "cloudflare") config.cloudflare = (val == "1" || val == "true");
        else if (key == "show_local") config.show_local = (val == "1" || val == "true");
        else if (key == "show_npcs") config.show_npcs = (val == "1" || val == "true");
        else if (key == "update_rate_hz") config.update_rate_hz = std::stof(val);
        else if (key == "max_distance") config.max_distance = std::stof(val);
    }
    return true;
}

} // namespace Fivem_Radar
