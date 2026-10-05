#pragma once

#include "unified_esp.h"
#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <variant>
#include <vector>

namespace OmniGhost::Gameplay::UnifiedConfig {

// Type-erased config value
using ConfigValue = std::variant<bool, int, float, std::array<float, 4>, std::string>;

// Unified config entry
struct ConfigEntry {
    std::string key;
    ConfigValue value;
    std::string category;  // "visuals", "aim", "radar", "colors", "advanced"
    std::string description;
    bool requires_restart = false;
};

// Config schema for serialization
class ConfigSchema {
public:
    virtual ~ConfigSchema() = default;
    
    // Game-specific feature flags (must be implemented by derived class)
    virtual OmniGhost::Gameplay::UnifiedESP::FeatureFlags GetFeatureFlags() const = 0;
    
    // Register a config variable
    template<typename T>
    void Register(const std::string& key, T* ptr, const std::string& category, const std::string& desc) {
        entries_.push_back({key, ConfigValue(*ptr), category, desc});
        pointers_[key] = [ptr](const ConfigValue& v) { *ptr = std::get<T>(v); };
    }

    // Register color (array<float,4>)
    void RegisterColor(const std::string& key, std::array<float, 4>* ptr, const std::string& category, const std::string& desc) {
        entries_.push_back({key, ConfigValue(*ptr), category, desc});
        pointers_[key] = [ptr](const ConfigValue& v) { *ptr = std::get<std::array<float, 4>>(v); };
    }

    // Serialize all registered configs
    std::string Serialize() const {
        std::ostringstream oss;
        for (const auto& e : entries_) {
            if (e.category.empty()) continue;
            oss << "[" << e.category << "]\n";
            if (!e.description.empty()) oss << "; " << e.description << "\n";
            WriteValue(oss, e.key, e.value);
            oss << "\n";
        }
        return oss.str();
    }

    // Deserialize from string
    bool Deserialize(const std::string& data) {
        std::istringstream iss(data);
        std::string line;
        std::string current_category;
        while (std::getline(iss, line)) {
            // Skip comments
            if (line.empty() || line[0] == ';') continue;
            
            // Category header
            if (line[0] == '[' && line.back() == ']') {
                current_category = line.substr(1, line.size() - 2);
                continue;
            }
            
            // Key=value
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            Trim(key); Trim(val);
            
            auto it = pointers_.find(key);
            if (it != pointers_.end()) {
                try {
                    ConfigValue cv = ParseValue(val);
                    it->second(cv);
                } catch (...) {}
            }
        }
        return true;
    }

    // Save to file
    bool SaveToFile(const std::string& path) const {
        std::ofstream f(path);
        if (!f) return false;
        f << Serialize();
        return true;
    }

    // Load from file
    bool LoadFromFile(const std::string& path) {
        std::ifstream f(path);
        if (!f) return false;
        std::ostringstream ss; ss << f.rdbuf();
        return Deserialize(ss.str());
    }

    // Get all entries for UI rendering
    const std::vector<ConfigEntry>& GetEntries() const { return entries_; }

protected:
    std::vector<ConfigEntry> entries_;
    std::unordered_map<std::string, std::function<void(const ConfigValue&)>> pointers_;

    static void WriteValue(std::ostringstream& oss, const std::string& key, const ConfigValue& v) {
        std::visit([&](auto&& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, bool>) {
                oss << key << "=" << (val ? "1" : "0");
            } else if constexpr (std::is_same_v<T, int>) {
                oss << key << "=" << val;
            } else if constexpr (std::is_same_v<T, float>) {
                oss << key << "=" << val;
            } else if constexpr (std::is_same_v<T, std::array<float, 4>>) {
                oss << key << "=" << val[0] << "," << val[1] << "," << val[2] << "," << val[3];
            } else if constexpr (std::is_same_v<T, std::string>) {
                oss << key << "=" << val;
            }
        }, v);
        oss << "\n";
    }

    static ConfigValue ParseValue(const std::string& s) {
        // Try bool
        if (s == "1" || s == "true") return ConfigValue(true);
        if (s == "0" || s == "false") return ConfigValue(false);
        // Try int
        try { return ConfigValue(std::stoi(s)); } catch (...) {}
        // Try float
        try { return ConfigValue(std::stof(s)); } catch (...) {}
        // Try color (r,g,b,a)
        if (s.find(',') != std::string::npos) {
            std::array<float, 4> c{};
            std::istringstream iss(s);
            char comma;
            if (iss >> c[0] >> comma >> c[1] >> comma >> c[2] >> comma >> c[3])
                return ConfigValue(c);
        }
        return ConfigValue(s);
    }

    static void Trim(std::string& s) {
        s.erase(0, s.find_first_not_of(" \t\r\n"));
        s.erase(s.find_last_not_of(" \t\r\n") + 1);
    }
};

// Game-specific config implementations
class CS2Config : public ConfigSchema {
public:
    // Visual
    bool esp_enabled = true;
    bool box = false, corner_box = false, box_fill = false;
    float box_thickness = 1.8f, box_rounding = 0.f;
    bool skeleton = false, skeleton_joints = false;
    float skeleton_thickness = 1.8f;
    bool head_dot = false, head_halo = false;
    float head_circle_thickness = 1.6f;
    bool health_bar = false, armor_bar = false;
    bool health_value = false, armor_value = false;
    bool name = false, distance = false, weapon_name = false, weapon_ammo = false;
    bool text_outline = true; float text_outline_thickness = 1.f;
    bool snaplines = false; int snapline_position = 2; float snapline_thickness = 1.5f;
    bool look_direction = false; float eye_line_thickness = 1.6f; float look_direction_length = 90.f;
    bool chinese_hat = false, angel_wings = false, devil_horns = false, floating_crown = false;
    bool trails = false; float chinese_hat_scale = 1.f; float fun_effects_scale = 1.f;
    float trail_duration = 1.f, trail_thickness = 2.f;
    bool rgb_mode = false, visibility_colors = false, visible_check = false, team_check = true;
    bool show_bots = false;
    float max_distance = 200.f;
    bool bomb_timer = false, spectator_list = false, radar_2d = false;
    bool webradar_enabled = false, webradar_cloudflare = false; int webradar_port = 8080;
    bool telemetry_enabled = false, performance_mode = false, hotkey_overlay = false;
    float optional_lane_budget_ms = 0.f;
    bool dropped_weapons = false;
    bool dropped_weapon_pistols = true;
    bool dropped_weapon_smgs = true;
    bool dropped_weapon_rifles = true;
    bool dropped_weapon_heavy = true;
    bool dropped_weapon_icons = true;
    bool dropped_weapon_ammo = false;

    // Aim
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

    // Colors
    std::array<float, 4> col_box = {1,1,1,0.9f};
    std::array<float, 4> col_box_corner = {0,0.78f,1,1};
    std::array<float, 4> col_box_fill = {1,0.25f,0.25f,0.16f};
    std::array<float, 4> col_skeleton = {0.95f,0.85f,0.35f,1};
    std::array<float, 4> col_joints = {0.27f,0.9f,0.37f,1};
    std::array<float, 4> col_head = {0.95f,0.85f,0.35f,1};
    std::array<float, 4> col_halo = {1,0.89f,0.54f,0.9f};
    std::array<float, 4> col_health = {0.25f,0.86f,0.35f,1};
    std::array<float, 4> col_armor = {0.27f,0.59f,1,1};
    std::array<float, 4> col_name = {1,1,1,1};
    std::array<float, 4> col_distance = {0.85f,0.85f,0.9f,1};
    std::array<float, 4> col_weapon = {0.85f,0.85f,0.9f,1};
    std::array<float, 4> col_snaplines = {0.83f,0.69f,0.22f,0.55f};
    std::array<float, 4> col_trail = {0.83f,0.69f,0.22f,0.82f};
    std::array<float, 4> col_fun_effects = {1,0.72f,0.18f,0.95f};
    std::array<float, 4> col_look = {0.83f,0.69f,0.22f,0.86f};
    std::array<float, 4> col_text_outline = {0,0,0,1};
    std::array<float, 4> col_visible = {0.27f,0.9f,0.37f,1};
    std::array<float, 4> col_occluded = {0.9f,0.25f,0.25f,1};
    std::array<float, 4> col_team = {0.25f,0.85f,0.35f,1};
    std::array<float, 4> col_target = {1,0.3f,0.3f,1};

    CS2Config() { RegisterAll(); }

    OmniGhost::Gameplay::UnifiedESP::FeatureFlags GetFeatureFlags() const override {
        OmniGhost::Gameplay::UnifiedESP::FeatureFlags f;
        f.box = box; f.corner_box = corner_box; f.box_fill = box_fill;
        f.skeleton = skeleton; f.skeleton_joints = skeleton_joints;
        f.head_dot = head_dot; f.head_halo = head_halo; f.head_circle = head_dot;
        f.health_bar = health_bar; f.armor_bar = armor_bar;
        f.health_value = health_value; f.armor_value = armor_value;
        f.name = name; f.distance = distance; f.weapon_name = weapon_name; f.weapon_ammo = weapon_ammo;
        f.snaplines = snaplines; f.trails = trails;
        f.chinese_hat = chinese_hat; f.angel_wings = angel_wings; f.devil_horns = devil_horns; f.floating_crown = floating_crown;
        f.hit_marker = false; f.look_direction = look_direction;
        f.radar_2d = radar_2d; f.webradar_enabled = webradar_enabled;
        f.aim_enabled = aim_enabled; f.trigger_enabled = trigger_enabled;
        f.aim_visibility_check = aim_visibility_check; f.trigger_head_only = trigger_head_only;
        f.trigger_scoped_only = trigger_scoped_only; f.aim_prediction = aim_prediction;
        f.visible_check = visible_check; f.visibility_colors = visibility_colors; f.team_check = team_check;
        f.show_bots = show_bots; f.bomb_timer = bomb_timer; f.spectator_list = spectator_list;
        f.dropped_weapons = dropped_weapons; f.radar_enabled = radar_2d;
        f.trigger_use_ident = trigger_use_ident; f.trigger_scoped_only = trigger_scoped_only;
        f.aim_ignore_bots = aim_ignore_bots; f.aim_ignore_spectators = aim_ignore_spectators;
        return f;
    }

    void RegisterAll() {
        // Visuals
        Register("esp_enabled", &esp_enabled, "visuals", "Master ESP toggle");
        Register("box", &box, "visuals", "2D box");
        Register("corner_box", &corner_box, "visuals", "Corner box");
        Register("box_fill", &box_fill, "visuals", "Box fill");
        Register("box_thickness", &box_thickness, "visuals", "Box thickness");
        Register("box_rounding", &box_rounding, "visuals", "Box rounding");
        Register("skeleton", &skeleton, "visuals", "Skeleton ESP");
        Register("skeleton_joints", &skeleton_joints, "visuals", "Skeleton joints");
        Register("skeleton_thickness", &skeleton_thickness, "visuals", "Skeleton thickness");
        Register("head_dot", &head_dot, "visuals", "Head dot");
        Register("head_halo", &head_halo, "visuals", "Head halo");
        Register("head_circle_thickness", &head_circle_thickness, "visuals", "Head circle thickness");
        Register("health_bar", &health_bar, "visuals", "Health bar");
        Register("armor_bar", &armor_bar, "visuals", "Armor bar");
        Register("health_value", &health_value, "visuals", "Health value");
        Register("armor_value", &armor_value, "visuals", "Armor value");
        Register("name", &name, "visuals", "Player name");
        Register("distance", &distance, "visuals", "Distance");
        Register("weapon_name", &weapon_name, "visuals", "Weapon name");
        Register("weapon_ammo", &weapon_ammo, "visuals", "Weapon ammo");
        Register("text_outline", &text_outline, "visuals", "Text outline");
        Register("text_outline_thickness", &text_outline_thickness, "visuals", "Text outline thickness");
        Register("snaplines", &snaplines, "visuals", "Snaplines");
        Register("snapline_position", &snapline_position, "visuals", "Snapline position");
        Register("snapline_thickness", &snapline_thickness, "visuals", "Snapline thickness");
        Register("look_direction", &look_direction, "visuals", "Look direction");
        Register("eye_line_thickness", &eye_line_thickness, "visuals", "Eye line thickness");
        Register("look_direction_length", &look_direction_length, "visuals", "Look direction length");
        Register("chinese_hat", &chinese_hat, "visuals", "Chinese hat");
        Register("angel_wings", &angel_wings, "visuals", "Angel wings");
        Register("devil_horns", &devil_horns, "visuals", "Devil horns");
        Register("floating_crown", &floating_crown, "visuals", "Floating crown");
        Register("trails", &trails, "visuals", "Trails");
        Register("chinese_hat_scale", &chinese_hat_scale, "visuals", "Chinese hat scale");
        Register("fun_effects_scale", &fun_effects_scale, "visuals", "Fun effects scale");
        Register("trail_duration", &trail_duration, "visuals", "Trail duration");
        Register("trail_thickness", &trail_thickness, "visuals", "Trail thickness");
        Register("rgb_mode", &rgb_mode, "visuals", "RGB mode");
        Register("visibility_colors", &visibility_colors, "visuals", "Visibility colors");
        Register("visible_check", &visible_check, "visuals", "Visibility check");
        Register("team_check", &team_check, "visuals", "Team check");
        Register("show_bots", &show_bots, "visuals", "Show bots");
        Register("max_distance", &max_distance, "visuals", "Max ESP distance");
        
        // Colors
        RegisterColor("col_box", &col_box, "colors", "Box color");
        RegisterColor("col_box_corner", &col_box_corner, "colors", "Corner box color");
        RegisterColor("col_box_fill", &col_box_fill, "colors", "Box fill color");
        RegisterColor("col_skeleton", &col_skeleton, "colors", "Skeleton color");
        RegisterColor("col_joints", &col_joints, "colors", "Joints color");
        RegisterColor("col_head", &col_head, "colors", "Head color");
        RegisterColor("col_halo", &col_halo, "colors", "Halo color");
        RegisterColor("col_health", &col_health, "colors", "Health color");
        RegisterColor("col_armor", &col_armor, "colors", "Armor color");
        RegisterColor("col_name", &col_name, "colors", "Name color");
        RegisterColor("col_distance", &col_distance, "colors", "Distance color");
        RegisterColor("col_weapon", &col_weapon, "colors", "Weapon color");
        RegisterColor("col_snaplines", &col_snaplines, "colors", "Snaplines color");
        RegisterColor("col_trail", &col_trail, "colors", "Trail color");
        RegisterColor("col_fun_effects", &col_fun_effects, "colors", "Fun effects color");
        RegisterColor("col_look", &col_look, "colors", "Look direction color");
        RegisterColor("col_text_outline", &col_text_outline, "colors", "Text outline color");
        RegisterColor("col_visible", &col_visible, "colors", "Visible color");
        RegisterColor("col_occluded", &col_occluded, "colors", "Occluded color");
        RegisterColor("col_team", &col_team, "colors", "Team color");
        RegisterColor("col_target", &col_target, "colors", "Target color");
        
        // Misc
        Register("bomb_timer", &bomb_timer, "misc", "Bomb timer");
        Register("spectator_list", &spectator_list, "misc", "Spectator list");
        Register("radar_2d", &radar_2d, "misc", "2D radar");
        Register("webradar_enabled", &webradar_enabled, "misc", "Web radar");
        Register("webradar_cloudflare", &webradar_cloudflare, "misc", "Cloudflare tunnel");
        Register("webradar_port", &webradar_port, "misc", "Web radar port");
        Register("telemetry_enabled", &telemetry_enabled, "misc", "Telemetry");
        Register("performance_mode", &performance_mode, "misc", "Performance mode");
        Register("hotkey_overlay", &hotkey_overlay, "misc", "Hotkey overlay");
        Register("optional_lane_budget_ms", &optional_lane_budget_ms, "misc", "Optional DMA budget");
        Register("dropped_weapons", &dropped_weapons, "misc", "Dropped weapons");
        Register("dropped_weapon_pistols", &dropped_weapon_pistols, "misc", "Dropped weapon pistols");
        Register("dropped_weapon_smgs", &dropped_weapon_smgs, "misc", "Dropped weapon SMGs");
        Register("dropped_weapon_rifles", &dropped_weapon_rifles, "misc", "Dropped weapon rifles");
        Register("dropped_weapon_heavy", &dropped_weapon_heavy, "misc", "Dropped weapon heavy");
        Register("dropped_weapon_icons", &dropped_weapon_icons, "misc", "Dropped weapon icons");
        Register("dropped_weapon_ammo", &dropped_weapon_ammo, "misc", "Dropped weapon ammo");
        
        // Aim
        Register("aim_enabled", &aim_enabled, "aim", "Aimbot enabled");
        Register("trigger_enabled", &trigger_enabled, "aim", "Triggerbot");
        Register("aim_visibility_check", &aim_visibility_check, "aim", "Aim visibility check");
        Register("trigger_head_only", &trigger_head_only, "aim", "Trigger head only");
        Register("trigger_scoped_only", &trigger_scoped_only, "aim", "Trigger scoped only");
        Register("aim_prediction", &aim_prediction, "aim", "Aim prediction");
        Register("trigger_use_ident", &trigger_use_ident, "aim", "Trigger use ident");
        Register("aim_ignore_bots", &aim_ignore_bots, "aim", "Ignore bots");
        Register("aim_ignore_spectators", &aim_ignore_spectators, "aim", "Ignore spectators");
        Register("aim_auto_bone", &aim_auto_bone, "aim", "Auto bone");
        Register("aim_humanize", &aim_humanize, "aim", "Humanize");
    }
};

// FiveM Config
class FiveMConfig : public ConfigSchema {
public:
    bool enabled = true, self_esp = false, visible_check = false, visibility_colors = false;
    bool team_check = true, show_dead = false, show_knocked = false, npc_esp = false;
    bool box_2d = false, corner_box = false, filled_box = false;
    bool skeleton = false, joints = false, head_circle = false;
    bool health_bar = false, armor_bar = false, weapon_name = false;
    bool player_name = false, player_id = false, distance = false, snaplines = false;
    bool head_halo = false, chinese_hat = false, angel_wings = false, devil_horns = false;
    bool floating_crown = false, trails = false, look_direction = false, hit_marker = false;
    bool fun_effects_rainbow = false;
    float box_thickness = 1.8f, skeleton_thickness = 1.5f, head_circle_thickness = 1.6f;
    float snapline_thickness = 1.5f, eye_line_thickness = 1.5f;
    float max_esp_distance = 200.f, chinese_hat_scale = 1.f, fun_effects_scale = 1.f;
    float trail_duration = 1.f, trail_thickness = 2.f, look_direction_length = 1.f;
    int snapline_pos = 2, circle_type = 0;
    bool rgb_mode = false, radar_enabled = false, triangle_radar = false;
    bool blip_esp = false, waypoint_line = false, square_radar = false;
    float triangle_radar_radius = 100.f;
    bool aimbot_enabled = false, trigger_enabled = false, visible_check_aim = false;

    // Colors (ImU32 but stored as float[4] for serialization)
    std::array<float, 4> color_visible = {0.27f, 0.9f, 0.37f, 1};
    std::array<float, 4> color_invisible = {1, 0, 0, 1};
    std::array<float, 4> color_dead = {1, 0.2f, 0.2f, 1};
    std::array<float, 4> color_knocked = {1, 0.5f, 0, 1};
    std::array<float, 4> color_team = {0, 1, 1, 1};
    std::array<float, 4> color_skeleton = {1, 0, 0, 1};
    std::array<float, 4> color_skeleton_points = {0.3f, 0.86f, 0.35f, 1};
    std::array<float, 4> color_head_circle = {1, 1, 0, 1};
    std::array<float, 4> color_health = {0, 1, 0, 1};
    std::array<float, 4> color_armor = {0, 0.5f, 1, 1};
    std::array<float, 4> color_name = {1, 1, 1, 1};
    std::array<float, 4> color_distance = {1, 1, 1, 1};
    std::array<float, 4> color_weapon = {1, 1, 0, 1};
    std::array<float, 4> color_snaplines = {1, 1, 1, 0.5f};
    std::array<float, 4> color_trail = {1, 0.8f, 0, 0.8f};
    std::array<float, 4> color_fun_effects = {1, 0.7f, 0.2f, 0.9f};
    std::array<float, 4> color_look_direction = {1, 0.5f, 0, 0.8f};
    std::array<float, 4> color_halo = {1, 0.8f, 0.5f, 0.9f};
    std::array<float, 4> color_box_2d = {1, 1, 1, 0.9f};
    std::array<float, 4> color_corner_box = {0, 0.8f, 1, 1};
    std::array<float, 4> color_id = {0.8f, 0.8f, 0.8f, 1};
    std::array<float, 4> color_npc = {0.5f, 0.5f, 0.5f, 1};

    FiveMConfig() { RegisterAll(); }

    OmniGhost::Gameplay::UnifiedESP::FeatureFlags GetFeatureFlags() const override {
        OmniGhost::Gameplay::UnifiedESP::FeatureFlags f;
        f.box = box_2d; f.corner_box = corner_box; f.box_fill = filled_box;
        f.skeleton = skeleton; f.skeleton_joints = joints; f.head_circle = head_circle;
        f.health_bar = health_bar; f.armor_bar = armor_bar; f.weapon_name = weapon_name;
        f.player_name = player_name; f.player_id = player_id; f.distance = distance;
        f.snaplines = snaplines; f.trails = trails; f.chinese_hat = chinese_hat;
        f.angel_wings = angel_wings; f.devil_horns = devil_horns; f.floating_crown = floating_crown;
        f.head_halo = head_halo; f.look_direction = look_direction; f.hit_marker = hit_marker;
        f.radar_enabled = radar_enabled; f.triangle_radar = triangle_radar; f.blip_esp = blip_esp;
        f.waypoint_line = waypoint_line; f.square_radar = square_radar;
        f.self_esp = self_esp; f.show_dead = show_dead; f.show_knocked = show_knocked;
        f.npc_esp = npc_esp; f.visible_check = visible_check; f.visibility_colors = visibility_colors;
        f.team_check = team_check; f.aim_enabled = aimbot_enabled; f.trigger_enabled = trigger_enabled;
        f.aim_visibility_check = visible_check_aim;
        return f;
    }

    void RegisterAll() {
        Register("enabled", &enabled, "visuals", "Master ESP toggle");
        // ... register all fields
    }
};

// Warzone Config
class WarzoneConfig : public ConfigSchema {
public:
    bool esp_enabled = true, team_check = true, ignore_downed = false, ignore_ai = false;
    bool skeleton = false, head_dot = false, health_bar = false, armor_bar = false;
    bool name = false, distance = false, box = false, box_corner = false, snaplines = false;
    int max_distance = 500;
    bool aim_enabled = false, trigger_enabled = false;

    std::array<float, 4> col_enemy = {1, 0.25f, 0.25f, 1};
    std::array<float, 4> col_team = {0.25f, 0.85f, 0.35f, 1};
    std::array<float, 4> col_target = {1, 0.3f, 0.3f, 1};
    std::array<float, 4> col_skeleton = {0.95f, 0.85f, 0.35f, 1};
    std::array<float, 4> col_armor = {0.2f, 0.5f, 1, 1};

    WarzoneConfig() { RegisterAll(); }

    OmniGhost::Gameplay::UnifiedESP::FeatureFlags GetFeatureFlags() const override {
        OmniGhost::Gameplay::UnifiedESP::FeatureFlags f;
        f.box = box; f.corner_box = box_corner; f.skeleton = skeleton;
        f.head_dot = head_dot; f.health_bar = health_bar; f.armor_bar = armor_bar;
        f.name = name; f.distance = distance; f.snaplines = snaplines;
        f.aim_enabled = aim_enabled; f.trigger_enabled = trigger_enabled;
        f.team_check = team_check; f.ignore_downed = ignore_downed; f.ignore_ai = ignore_ai;
        return f;
    }

    void RegisterAll() {
        Register("esp_enabled", &esp_enabled, "visuals", "Master ESP toggle");
        Register("team_check", &team_check, "visuals", "Team check");
        Register("ignore_downed", &ignore_downed, "visuals", "Ignore downed");
        Register("ignore_ai", &ignore_ai, "visuals", "Ignore AI");
        Register("skeleton", &skeleton, "visuals", "Skeleton");
        Register("head_dot", &head_dot, "visuals", "Head dot");
        Register("health_bar", &health_bar, "visuals", "Health bar");
        Register("armor_bar", &armor_bar, "visuals", "Armor bar");
        Register("name", &name, "visuals", "Name");
        Register("distance", &distance, "visuals", "Distance");
        Register("box", &box, "visuals", "Box");
        Register("box_corner", &box_corner, "visuals", "Corner box");
        Register("snaplines", &snaplines, "visuals", "Snaplines");
        Register("max_distance", &max_distance, "visuals", "Max distance");
        Register("aim_enabled", &aim_enabled, "aim", "Aimbot");
        Register("trigger_enabled", &trigger_enabled, "aim", "Triggerbot");
        
        RegisterColor("col_enemy", &col_enemy, "colors", "Enemy color");
        RegisterColor("col_team", &col_team, "colors", "Team color");
        RegisterColor("col_target", &col_target, "colors", "Target color");
        RegisterColor("col_skeleton", &col_skeleton, "colors", "Skeleton color");
        RegisterColor("col_armor", &col_armor, "colors", "Armor color");
    }
};

// Fortnite Config (local only)
class FortniteConfig : public ConfigSchema {
public:
    bool esp_enabled = true, self_esp = true, show_bots = false;
    bool visible_check = false, team_check = true;
    bool box = false, corner_box = false, box_fill = false;
    bool skeleton = false, skeleton_joints = false, head_dot = false, head_halo = false;
    bool health_bar = false, armor_bar = false, health_value = false, armor_value = false;
    bool name = true, distance = true, weapon_name = false, weapon_ammo = false;
    bool text_outline = true; float text_outline_thickness = 1.f;
    bool snaplines = false; int snapline_position = 2; float snapline_thickness = 1.5f;
    bool look_direction = false; float eye_line_thickness = 1.6f; float look_direction_length = 90.f;
    bool chinese_hat = false, angel_wings = false, devil_horns = false, floating_crown = false;
    bool trails = false, hit_marker = false;
    float box_thickness = 1.8f, box_rounding = 0.f, skeleton_thickness = 1.8f;
    float head_circle_thickness = 1.6f; 
    float snapline_thickness = 1.5f, eye_line_thickness = 1.6f;
    float max_distance = 300.f;
    bool rgb_mode = false, visibility_colors = false;

    std::array<float, 4> col_enemy = {1, 0.25f, 0.25f, 1};
    std::array<float, 4> col_team = {0.25f, 0.85f, 0.35f, 1};
    std::array<float, 4> col_box = {1, 1, 1, 0.9f};
    std::array<float, 4> col_box_corner = {0, 0.78f, 1, 1};
    std::array<float, 4> col_box_fill = {1, 0.25f, 0.25f, 0.16f};
    std::array<float, 4> col_skeleton = {0.95f, 0.85f, 0.35f, 1};
    std::array<float, 4> col_joints = {0.27f, 0.9f, 0.37f, 1};
    std::array<float, 4> col_head = {0.95f, 0.85f, 0.35f, 1};
    std::array<float, 4> col_halo = {1, 0.89f, 0.54f, 0.9f};
    std::array<float, 4> col_health = {0.25f, 0.86f, 0.35f, 1};
    std::array<float, 4> col_armor = {0.27f, 0.59f, 1, 1};
    std::array<float, 4> col_name = {1, 1, 1, 1};
    std::array<float, 4> col_local = {0.2f, 0.9f, 1, 1};
    std::array<float, 4> col_distance = {0.85f, 0.85f, 0.9f, 1};
    std::array<float, 4> col_weapon = {0.85f, 0.85f, 0.9f, 1};
    std::array<float, 4> col_snaplines = {0.83f, 0.69f, 0.22f, 0.55f};
    std::array<float, 4> col_trail = {0.83f, 0.69f, 0.22f, 0.82f};
    std::array<float, 4> col_fun_effects = {1, 0.72f, 0.18f, 0.95f};
    std::array<float, 4> col_look = {0.83f, 0.69f, 0.22f, 0.86f};
    std::array<float, 4> col_text_outline = {0, 0, 0, 1};
    std::array<float, 4> col_visible = {0.27f, 0.9f, 0.37f, 1};
    std::array<float, 4> col_occluded = {0.9f, 0.25f, 0.25f, 1};

    FortniteConfig() { RegisterAll(); }

    OmniGhost::Gameplay::UnifiedESP::FeatureFlags GetFeatureFlags() const override {
        OmniGhost::Gameplay::UnifiedESP::FeatureFlags f;
        f.box = box; f.corner_box = corner_box; f.box_fill = box_fill;
        f.skeleton = skeleton; f.skeleton_joints = skeleton_joints;
        f.head_dot = head_dot; f.head_halo = head_halo;
        f.health_bar = health_bar; f.armor_bar = armor_bar;
        f.health_value = health_value; f.armor_value = armor_value;
        f.name = name; f.distance = distance; f.weapon_name = weapon_name; f.weapon_ammo = weapon_ammo;
        f.snaplines = snaplines; f.trails = trails;
        f.chinese_hat = chinese_hat; f.angel_wings = angel_wings; f.devil_horns = devil_horns; f.floating_crown = floating_crown;
        f.head_halo = head_halo; f.look_direction = look_direction; f.hit_marker = hit_marker;
        f.self_esp = self_esp; f.show_bots = show_bots; f.visible_check = visible_check; f.team_check = team_check;
        f.visibility_colors = visibility_colors; f.rgb_mode = rgb_mode;
        return f;
    }

    void RegisterAll() {
        Register("esp_enabled", &esp_enabled, "visuals", "Master ESP toggle");
        // ... register all fields
    }
};

// Global access
inline CS2Config& GetCS2Config() { static CS2Config c; return c; }
inline FiveMConfig& GetFiveMConfig() { static FiveMConfig c; return c; }
inline WarzoneConfig& GetWarzoneConfig() { static WarzoneConfig c; return c; }
inline FortniteConfig& GetFortniteConfig() { static FortniteConfig c; return c; }

} // namespace OmniGhost::Gameplay::UnifiedConfig