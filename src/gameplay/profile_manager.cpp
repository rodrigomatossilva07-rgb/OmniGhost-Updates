#include "profile_manager.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <random>
#include <iomanip>

namespace Gameplay::ProfileManager {

    ProfileManager& ProfileManager::Instance() {
        static ProfileManager instance;
        return instance;
    }

    std::string ProfileManager::GenerateProfileId() const {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 15);
        static const char* hex = "0123456789abcdef";
        
        std::string id;
        for (int i = 0; i < 16; ++i) {
            id += hex[dis(gen)];
        }
        return id;
    }

    GameProfile* ProfileManager::CreateProfile(const std::string& name, GameType game, ProfileType type) {
        GameProfile profile;
        profile.id = GenerateProfileId();
        profile.name = name;
        profile.game = game;
        profile.type = type;
        profile.created = std::chrono::system_clock::now();
        profile.modified = profile.created;
        
        RegisterDefaultSettings(&profile, game);
        
        auto [it, inserted] = profiles_.emplace(profile.id, std::move(profile));
        return &it->second;
    }

    bool ProfileManager::DeleteProfile(const std::string& id) {
        auto it = profiles_.find(id);
        if (it == profiles_.end()) return false;
        
        if (it->second.is_builtin) return false; // Can't delete built-in
        
        // Check if active
        for (auto& [game, active_id] : active_profiles_) {
            if (active_id == id) {
                active_profiles_.erase(game);
            }
        }
        
        profiles_.erase(it);
        return true;
    }

    GameProfile* ProfileManager::GetProfile(const std::string& id) {
        auto it = profiles_.find(id);
        return it != profiles_.end() ? &it->second : nullptr;
    }

    GameProfile* ProfileManager::GetProfileByName(const std::string& name, GameType game) {
        for (auto& [id, profile] : profiles_) {
            if (profile.name == name && (game == GameType::Custom || profile.game == game)) {
                return &profile;
            }
        }
        return nullptr;
    }

    std::vector<GameProfile*> ProfileManager::GetProfiles(GameType game, ProfileType type) {
        std::vector<GameProfile*> result;
        for (auto& [id, profile] : profiles_) {
            if ((game == GameType::Custom || profile.game == game) &&
                (type == ProfileType::Custom || profile.type == type)) {
                result.push_back(&profile);
            }
        }
        return result;
    }

    std::vector<GameProfile*> ProfileManager::GetProfilesByTag(const std::string& tag) {
        std::vector<GameProfile*> result;
        for (auto& [id, profile] : profiles_) {
            if (std::find(profile.tags.begin(), profile.tags.end(), tag) != profile.tags.end()) {
                result.push_back(&profile);
            }
        }
        return result;
    }

    void ProfileManager::SetActiveProfile(GameType game, const std::string& profile_id) {
        auto it = profiles_.find(profile_id);
        if (it == profiles_.end()) return;
        if (it->second.game != game && game != GameType::Custom) return;
        
        active_profiles_[game] = profile_id;
        it->second.is_active = true;
        it->second.modified = std::chrono::system_clock::now();
        
        ApplyProfileToGame(&it->second, game);
    }

    GameProfile* ProfileManager::GetActiveProfile(GameType game) const {
        auto it = active_profiles_.find(game);
        if (it == active_profiles_.end()) return nullptr;
        
        auto pit = profiles_.find(it->second);
        return pit != profiles_.end() ? &pit->second : nullptr;
    }

    void ProfileManager::RegisterDefaultSettings(GameProfile* profile, GameType game) {
        // Aim settings
        profile->settings["aim_enabled"] = {
            "aim_enabled", "Enable Aim", "Enable aim assist", FeatureCategory::Aim,
            SettingValue{SettingValue::Type::Bool, .bool_val = true},
            SettingValue{SettingValue::Type::Bool, .bool_val = true}
        };
        
        profile->settings["aim_fov"] = {
            "aim_fov", "Aim FOV", "Field of view for aim assist (degrees)", FeatureCategory::Aim,
            SettingValue{SettingValue::Type::Float, .float_val = 5.0f},
            SettingValue{SettingValue::Type::Float, .float_val = 5.0f},
            SettingValue{SettingValue::Type::Float, .float_val = 0.5f},
            SettingValue{SettingValue::Type::Float, .float_val = 180.0f}
        };
        
        profile->settings["aim_smooth"] = {
            "aim_smooth", "Aim Smooth", "Smoothing factor (0 = instant)", FeatureCategory::Aim,
            SettingValue{SettingValue::Type::Float, .float_val = 10.0f},
            SettingValue{SettingValue::Type::Float, .float_val = 10.0f},
            SettingValue{SettingValue::Type::Float, .float_val = 0.0f},
            SettingValue{SettingValue::Type::Float, .float_val = 100.0f}
        };
        
        profile->settings["aim_hitbox"] = {
            "aim_hitbox", "Hitbox", "Target hitbox priority", FeatureCategory::Aim,
            SettingValue{SettingValue::Type::Enum, .int_val = 0},
            SettingValue{SettingValue::Type::Enum, .int_val = 0},
            {},
            {},
            false, false,
            {"Head", "Neck", "Chest", "Pelvis", "Closest"}
        };
        
        // ESP settings
        profile->settings["esp_enabled"] = {
            "esp_enabled", "Enable ESP", "Enable visual ESP", FeatureCategory::ESP,
            SettingValue{SettingValue::Type::Bool, .bool_val = true},
            SettingValue{SettingValue::Type::Bool, .bool_val = true}
        };
        
        profile->settings["esp_box"] = {
            "esp_box", "Box ESP", "Draw bounding boxes", FeatureCategory::ESP,
            SettingValue{SettingValue::Type::Bool, .bool_val = true},
            SettingValue{SettingValue::Type::Bool, .bool_val = true}
        };
        
        profile->settings["esp_health"] = {
            "esp_health", "Health ESP", "Show health bars", FeatureCategory::ESP,
            SettingValue{SettingValue::Type::Bool, .bool_val = true},
            SettingValue{SettingValue::Type::Bool, .bool_val = true}
        };
        
        // Triggerbot settings
        profile->settings["trigger_enabled"] = {
            "trigger_enabled", "Enable Triggerbot", "Enable triggerbot", FeatureCategory::Triggerbot,
            SettingValue{SettingValue::Type::Bool, .bool_val = false},
            SettingValue{SettingValue::Type::Bool, .bool_val = false}
        };
        
        // Movement settings
        profile->settings["auto_strafe"] = {
            "auto_strafe", "Auto Strafe", "Enable auto-strafe", FeatureCategory::Movement,
            SettingValue{SettingValue::Type::Bool, .bool_val = false},
            SettingValue{SettingValue::Type::Bool, .bool_val = false}
        };
        
        profile->settings["auto_jump"] = {
            "auto_jump", "Auto Jump", "Enable auto-jump/bhop", FeatureCategory::Movement,
            SettingValue{SettingValue::Type::Bool, .bool_val = false},
            SettingValue{SettingValue::Type::Bool, .bool_val = false}
        };
        
        // Recoil settings
        profile->settings["recoil_enabled"] = {
            "recoil_enabled", "Recoil Control", "Enable recoil control", FeatureCategory::Recoil,
            SettingValue{SettingValue::Type::Bool, .bool_val = false},
            SettingValue{SettingValue::Type::Bool, .bool_val = false}
        };
        
        // Visuals settings
        profile->settings["fov_circle"] = {
            "fov_circle", "FOV Circle", "Show FOV circle", FeatureCategory::Visuals,
            SettingValue{SettingValue::Type::Bool, .bool_val = true},
            SettingValue{SettingValue::Type::Bool, .bool_val = true}
        };
        
        profile->settings["crosshair"] = {
            "crosshair", "Crosshair", "Show custom crosshair", FeatureCategory::Visuals,
            SettingValue{SettingValue::Type::Bool, .bool_val = true},
            SettingValue{SettingValue::Type::Bool, .bool_val = true}
        };
        
        // Misc settings
        profile->settings["max_distance"] = {
            "max_distance", "Max Distance", "Maximum engagement distance", FeatureCategory::Misc,
            SettingValue{SettingValue::Type::Float, .float_val = 300.0f},
            SettingValue{SettingValue::Type::Float, .float_val = 300.0f},
            SettingValue{SettingValue::Type::Float, .float_val = 10.0f},
            SettingValue{SettingValue::Type::Float, .float_val = 2000.0f}
        };
    }

    void ProfileManager::ApplyProfileToGame(const GameProfile* profile, GameType game) {
        // Would apply settings to game-specific config
        // This is where each game's config system would be updated
    }

    GameProfile* ProfileManager::GetProfile(const std::string& id) {
        auto it = profiles_.find(id);
        return it != profiles_.end() ? &it->second : nullptr;
    }

    GameProfile* ProfileManager::GetProfileByName(const std::string& name, GameType game) {
        for (auto& [id, profile] : profiles_) {
            if (profile.name == name && (game == GameType::Custom || profile.game == game)) {
                return &profile;
            }
        }
        return nullptr;
    }

    std::vector<GameProfile*> ProfileManager::GetProfiles(GameType game, ProfileType type) {
        std::vector<GameProfile*> result;
        for (auto& [id, profile] : profiles_) {
            if ((game == GameType::Custom || profile.game == game) &&
                (type == ProfileType::Custom || profile.type == type)) {
                result.push_back(&profile);
            }
        }
        return result;
    }

    std::vector<GameProfile*> ProfileManager::GetProfilesByTag(const std::string& tag) {
        std::vector<GameProfile*> result;
        for (auto& [id, profile] : profiles_) {
            if (std::find(profile.tags.begin(), profile.tags.end(), tag) != profile.tags.end()) {
                result.push_back(&profile);
            }
        }
        return result;
    }

    bool ProfileManager::SetSetting(GameProfile* profile, const std::string& key, const SettingValue& value) {
        if (!profile) return false;
        auto it = profile->settings.find(key);
        if (it == profile->settings.end()) return false;
        
        // Validate value
        if (value.type != it->second.value.type) return false;
        
        // Check min/max for numeric types
        if (value.type == SettingValue::Type::Float) {
            if (value.float_val < it->second.min_value.float_val || 
                value.float_val > it->second.max_value.float_val) {
                return false;
            }
        }
        if (value.type == SettingValue::Type::Int) {
            if (value.int_val < it->second.min_value.int_val || 
                value.int_val > it->second.max_value.int_val) {
                return false;
            }
        }
        
        it->second.value = value;
        profile->modified = std::chrono::system_clock::now();
        return true;
    }

    SettingValue ProfileManager::GetSetting(const GameProfile* profile, const std::string& key) const {
        if (!profile) return SettingValue{};
        auto it = profile->settings.find(key);
        if (it == profile->settings.end()) return SettingValue{};
        return it->second.value;
    }

    bool ProfileManager::ResetSetting(GameProfile* profile, const std::string& key) {
        if (!profile) return false;
        auto it = profile->settings.find(key);
        if (it == profile->settings.end()) return false;
        
        it->second.value = it->second.default_value;
        profile->modified = std::chrono::system_clock::now();
        return true;
    }

    void ProfileManager::ResetAllSettings(GameProfile* profile) {
        if (!profile) return;
        for (auto& [key, setting] : profile->settings) {
            setting.value = setting.default_value;
        }
        profile->modified = std::chrono::system_clock::now();
    }

    GameProfile* ProfileManager::CreateChildProfile(const std::string& name, const std::string& parent_id) {
        auto* parent = GetProfile(parent_id);
        if (!parent) return nullptr;
        
        GameProfile* child = CreateProfile(name, parent->game, parent->type);
        child->settings = parent->settings; // Copy all settings
        child->description = "Child of " + parent->name;
        child->tags.push_back("derived");
        
        return child;
    }

    void ProfileManager::ApplyParentSettings(GameProfile* child, const GameProfile* parent) {
        if (!child || !parent) return;
        
        for (const auto& [key, setting] : parent->settings) {
            auto it = child->settings.find(key);
            if (it != child->settings.end()) {
                it->second.value = setting.value;
            }
        }
    }

    std::string ProfileManager::ExportProfile(const GameProfile* profile) const {
        if (!profile) return "";
        
        std::ostringstream out;
        out << "PROFILE_V1\n";
        out << "ID:" << profile->id << "\n";
        out << "NAME:" << profile->name << "\n";
        out << "DESC:" << profile->description << "\n";
        out << "GAME:" << static_cast<int>(profile->game) << "\n";
        out << "TYPE:" << static_cast<int>(profile->type) << "\n";
        out << "AUTHOR:" << profile->author << "\n";
        out << "VERSION:" << profile->version << "\n";
        out << "CREATED:" << std::chrono::duration_cast<std::chrono::seconds>(
            profile->created.time_since_epoch()).count() << "\n";
        out << "MODIFIED:" << std::chrono::duration_cast<std::chrono::seconds>(
            profile->modified.time_since_epoch()).count() << "\n";
        out << "BUILTIN:" << (profile->is_builtin ? 1 : 0) << "\n";
        out << "TAGS:";
        for (size_t i = 0; i < profile->tags.size(); ++i) {
            out << profile->tags[i] << (i < profile->tags.size() - 1 ? "," : "");
        }
        out << "\n";
        
        out << "SETTINGS:" << profile->settings.size() << "\n";
        for (const auto& [key, setting] : profile->settings) {
            out << "SETTING:" << key << "|"
                << setting.name << "|"
                << setting.description << "|"
                << static_cast<int>(setting.category) << "|"
                << static_cast<int>(setting.value.type) << "|";
            
            // Serialize value
            switch (setting.value.type) {
                case SettingValue::Type::Bool:
                    out << (setting.value.bool_val ? "1" : "0");
                    break;
                case SettingValue::Type::Int:
                    out << setting.value.int_val;
                    break;
                case SettingValue::Type::Float:
                    out << setting.value.float_val;
                    break;
                case SettingValue::Type::String:
                    out << setting.value.string_val;
                    break;
                case SettingValue::Type::Color:
                    out << std::hex << setting.value.color_val << std::dec;
                    break;
            }
            
            // Default value
            out << "|";
            switch (setting.default_value.type) {
                case SettingValue::Type::Bool:
                    out << (setting.default_value.bool_val ? "1" : "0");
                    break;
                case SettingValue::Type::Int:
                    out << setting.default_value.int_val;
                    break;
                case SettingValue::Type::Float:
                    out << setting.default_value.float_val;
                    break;
                case SettingValue::Type::String:
                    out << setting.default_value.string_val;
                    break;
                case SettingValue::Type::Color:
                    out << std::hex << setting.default_value.color_val << std::dec;
                    break;
            }
            
            out << "|" << setting.is_advanced << "|" << setting.requires_restart;
            for (const auto& opt : setting.enum_options) {
                out << "|" << opt;
            }
            out << "\n";
        }
        
        out << "END_PROFILE\n";
        return out.str();
    }

    GameProfile* ProfileManager::ImportProfile(const std::string& data) {
        // Parse exported profile data
        std::istringstream in(data);
        std::string line;
        
        std::string version;
        if (!std::getline(in, line)) return nullptr;
        if (line != "PROFILE_V1") return nullptr;
        
        GameProfile profile;
        std::string key, value;
        
        while (std::getline(in, line)) {
            if (line == "END_PROFILE") break;
            
            size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            
            key = line.substr(0, colon);
            value = line.substr(colon + 1);
            
            if (key == "ID") profile.id = value;
            else if (key == "NAME") profile.name = value;
            else if (key == "DESC") profile.description = value;
            else if (key == "GAME") profile.game = static_cast<GameType>(std::stoi(value));
            else if (key == "TYPE") profile.type = static_cast<ProfileType>(std::stoi(value));
            else if (key == "AUTHOR") profile.author = value;
            else if (key == "VERSION") profile.version = value;
            else if (key == "CREATED") profile.created = std::chrono::system_clock::from_time_t(std::stoll(value));
            else if (key == "MODIFIED") profile.modified = std::chrono::system_clock::from_time_t(std::stoll(value));
            else if (key == "BUILTIN") profile.is_builtin = value == "1";
            else if (key == "TAGS") {
                std::istringstream tag_stream(value);
                std::string tag;
                while (std::getline(tag_stream, tag, ',')) {
                    if (!tag.empty()) profile.tags.push_back(tag);
                }
            } else if (key == "SETTINGS") {
                int count = std::stoi(value);
                // Settings would be parsed from subsequent lines
            }
        }
        
        profile.id = GenerateProfileId(); // New ID for imported profile
        profile.created = std::chrono::system_clock::now();
        profile.modified = profile.created;
        profile.is_builtin = false;
        
        auto [it, inserted] = profiles_.emplace(profile.id, std::move(profile));
        return &it->second;
    }

    bool ProfileManager::ExportToFile(const GameProfile* profile, const char* path) {
        if (!profile) return false;
        std::string data = ExportProfile(profile);
        
        std::ofstream file(path);
        if (!file) return false;
        file << data;
        return true;
    }

    GameProfile* ProfileManager::ImportFromFile(const char* path) {
        std::ifstream file(path);
        if (!file) return nullptr;
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        return ImportProfile(buffer.str());
    }

    std::vector<std::string> ProfileManager::CompareProfiles(const GameProfile* a, const GameProfile* b) {
        std::vector<std::string> differences;
        
        if (!a || !b) return differences;
        
        // Compare settings
        for (const auto& [key, setting_a] : a->settings) {
            auto it_b = b->settings.find(key);
            if (it_b == b->settings.end()) {
                differences.push_back("Setting '" + key + "' only in profile A");
                continue;
            }
            
            const auto& setting_b = it_b->second;
            if (setting_a.value.type != setting_b.value.type) {
                differences.push_back("Setting '" + key + "': type mismatch");
                continue;
            }
            
            bool equal = false;
            switch (setting_a.value.type) {
                case SettingValue::Type::Bool:
                    equal = setting_a.value.bool_val == setting_b.value.bool_val;
                    break;
                case SettingValue::Type::Int:
                    equal = setting_a.value.int_val == setting_b.value.int_val;
                    break;
                case SettingValue::Type::Float:
                    equal = std::abs(setting_a.value.float_val - setting_b.value.float_val) < 0.001f;
                    break;
                case SettingValue::Type::String:
                    equal = setting_a.value.string_val == setting_b.value.string_val;
                    break;
                case SettingValue::Type::Color:
                    equal = setting_a.value.color_val == setting_b.value.color_val;
                    break;
            }
            
            if (!equal) {
                differences.push_back("Setting '" + key + "' differs between profiles");
            }
        }
        
        // Check for settings only in B
        for (const auto& [key, setting_b] : b->settings) {
            if (a->settings.find(key) == a->settings.end()) {
                differences.push_back("Setting '" + key + "' only in profile B");
            }
        }
        
        return differences;
    }

    std::string ProfileManager::SerializeProfile(const GameProfile& profile) {
        return ExportProfile(&profile);
    }

    GameProfile* ProfileManager::DeserializeProfile(const std::string& data) {
        return ImportProfile(data);
    }

    void RegisterBuiltinProfiles() {
        ProfileManager& pm = ProfileManager::Instance();
        
        // CS2 Legit
        auto* cs2_legit = pm.CreateProfile("CS2 Legit", GameType::CS2, ProfileType::Legit);
        cs2_legit->description = "Legitimate playstyle for CS2";
        cs2_legit->tags = {"legit", "competitive", "matchmaking"};
        pm.SetSetting(cs2_legit, "aim_fov", SettingValue{SettingValue::Type::Float, .float_val = 3.0f});
        pm.SetSetting(cs2_legit, "aim_smooth", SettingValue{SettingValue::Type::Float, .float_val = 15.0f});
        pm.SetSetting(cs2_legit, "aim_hitbox", SettingValue{SettingValue::Type::Enum, .int_val = 1}); // Neck
        pm.SetSetting(cs2_legit, "esp_enabled", SettingValue{SettingValue::Type::Bool, .bool_val = true});
        cs2_legit->tags = {"legit", "cs2", "matchmaking"};
        
        // CS2 Rage
        auto* cs2_rage = pm.CreateProfile("CS2 Rage", GameType::CS2, ProfileType::Rage);
        cs2_rage->description = "Rage settings for CS2";
        pm.SetSetting(cs2_rage, "aim_fov", SettingValue{SettingValue::Type::Float, .float_val = 180.0f});
        pm.SetSetting(cs2_rage, "aim_smooth", SettingValue{SettingValue::Type::Float, .float_val = 0.0f});
        pm.SetSetting(cs2_rage, "aim_hitbox", SettingValue{SettingValue::Type::Enum, .int_val = 0}); // Head
        pm.SetSetting(cs2_rage, "aim_enabled", SettingValue{SettingValue::Type::Bool, .bool_val = true});
        pm.SetSetting(cs2_rage, "trigger_enabled", SettingValue{SettingValue::Type::Bool, .bool_val = true});
        cs2_rage->tags = {"rage", "hvh", "cs2"};
        
        // FiveM Legit
        auto* fivem_legit = pm.CreateProfile("FiveM Legit", GameType::FiveM, ProfileType::Legit);
        fivem_legit->description = "Legitimate FiveM roleplay settings";
        pm.SetSetting(fivem_legit, "aim_fov", SettingValue{SettingValue::Type::Float, .float_val = 5.0f});
        pm.SetSetting(fivem_legit, "aim_smooth", SettingValue{SettingValue::Type::Float, .float_val = 20.0f});
        pm.SetSetting(fivem_legit, "esp_enabled", SettingValue{SettingValue::Type::Bool, .bool_val = true});
        fivem_legit->tags = {"legit", "fivem", "roleplay"};
        
        // Warzone Legit
        auto* wz_legit = pm.CreateProfile("Warzone Legit", GameType::Warzone, ProfileType::Legit);
        wz_legit->description = "Legitimate Warzone settings";
        pm.SetSetting(wz_legit, "aim_fov", SettingValue{SettingValue::Type::Float, .float_val = 3.0f});
        pm.SetSetting(wz_legit, "aim_smooth", SettingValue{SettingValue::Type::Float, .float_val = 10.0f});
        pm.SetSetting(wz_legit, "esp_enabled", SettingValue{SettingValue::Type::Bool, .bool_val = true});
        wz_legit->tags = {"legit", "warzone", "br"};
    }

    const char* GetGameTypeName(GameType game) {
        switch (game) {
            case GameType::FiveM: return "FiveM";
            case GameType::CS2: return "Counter-Strike 2";
            case GameType::Warzone: return "Warzone";
            case GameType::Fortnite: return "Fortnite";
            case GameType::Apex: return "Apex Legends";
            default: return "Unknown";
        }
    }

    const char* GetProfileTypeName(ProfileType type) {
        switch (type) {
            case ProfileType::Legit: return "Legit";
            case ProfileType::SemiLegit: return "Semi-Legit";
            case ProfileType::Competitive: return "Competitive";
            case ProfileType::Rage: return "Rage";
            case ProfileType::Sniper: return "Sniper";
            case ProfileType::Movement: return "Movement";
            default: return "Custom";
        }
    }

    const char* GetFeatureCategoryName(FeatureCategory cat) {
        switch (cat) {
            case FeatureCategory::Aim: return "Aim";
            case FeatureCategory::ESP: return "ESP";
            case FeatureCategory::Triggerbot: return "Triggerbot";
            case FeatureCategory::Movement: return "Movement";
            case FeatureCategory::Recoil: return "Recoil";
            case FeatureCategory::Radar: return "Radar";
            case FeatureCategory::SoundESP: return "Sound ESP";
            case FeatureCategory::Visuals: return "Visuals";
            case FeatureCategory::Misc: return "Misc";
            default: return "Misc";
        }
    }

    std::string SerializeSettingValue(const SettingValue& value) {
        std::ostringstream out;
        out << static_cast<int>(value.type) << "|";
        switch (value.type) {
            case SettingValue::Type::Bool:
                out << (value.bool_val ? "1" : "0");
                break;
            case SettingValue::Type::Int:
                out << value.int_val;
                break;
            case SettingValue::Type::Float:
                out << value.float_val;
                break;
            case SettingValue::Type::String:
                out << value.string_val;
                break;
            case SettingValue::Type::Color:
                out << std::hex << value.color_val << std::dec;
                break;
            case SettingValue::Type::Enum:
                out << value.int_val;
                break;
        }
        return out.str();
    }

    bool DeserializeSettingValue(const std::string& data, SettingValue& value) {
        std::istringstream in(data);
        std::string token;
        
        std::getline(in, token, '|');
        value.type = static_cast<SettingValue::Type>(std::stoi(token));
        
        switch (value.type) {
            case SettingValue::Type::Bool:
                value.bool_val = value == "1";
                break;
            case SettingValue::Type::Int:
                value.int_val = std::stoi(value);
                break;
            case SettingValue::Type::Float:
                value.float_val = std::stof(value);
                break;
            case SettingValue::Type::String:
                value.string_val = value;
                break;
            case SettingValue::Type::Color:
                value.color_val = std::stoul(value, nullptr, 16);
                break;
            case SettingValue::Type::Enum:
                value.int_val = std::stoi(value);
                break;
        }
        return true;
    }

    std::string SerializeProfile(const GameProfile& profile) {
        return ""; // Use ExportProfile
    }

    GameProfile* DeserializeProfile(const std::string& data) {
        return nullptr; // Use ImportProfile
    }

    // ProfileUI implementation
    std::function<void(GameProfile*)> ProfileUI::on_profile_selected = nullptr;
    std::function<void(GameProfile*, const std::string&, const SettingValue&)> ProfileUI::on_setting_changed = nullptr;

    void ProfileUI::DrawProfileList() {
        // Would draw profile list UI
    }

    void ProfileUI::DrawProfileEditor(GameProfile* profile) {
        if (!profile) return;
        
        if (ImGui::Begin("Profile Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Profile: %s", profile->name.c_str());
            ImGui::Text("Game: %s", GetGameTypeName(profile->game));
            ImGui::Text("Type: %s", GetProfileTypeName(profile->type));
            ImGui::Separator();
            
            if (ImGui::BeginTable("##settings", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 200);
                ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthFixed, 150);
                ImGui::TableHeadersRow();
                
                for (auto& [key, setting] : profile->settings) {
                    if (setting.is_advanced) continue; // Skip advanced in basic view
                    
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", setting.name.c_str());
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", setting.description.c_str());
                    
                    ImGui::TableSetColumnIndex(1);
                    // Draw appropriate widget based on type
                    switch (setting.value.type) {
                        case SettingValue::Type::Bool: {
                            bool val = setting.value.bool_val;
                            if (ImGui::Checkbox(("##" + key).c_str(), &val)) {
                                SettingValue new_val{SettingValue::Type::Bool, .bool_val = val};
                                ProfileManager::Instance().SetSetting(profile, key, new_val);
                            }
                            break;
                        }
                        case SettingValue::Type::Float: {
                            float val = setting.value.float_val;
                            if (ImGui::DragFloat(("##" + key).c_str(), &val, 0.1f, 
                                setting.min_value.float_val, setting.max_value.float_val)) {
                                SettingValue new_val{SettingValue::Type::Float, .float_val = val};
                                ProfileManager::Instance().SetSetting(profile, key, new_val);
                            }
                            break;
                        }
                        case SettingValue::Type::Int: {
                            int val = setting.value.int_val;
                            if (ImGui::DragInt(("##" + key).c_str(), &val, 1.0f,
                                setting.min_value.int_val, setting.max_value.int_val)) {
                                SettingValue new_val{SettingValue::Type::Int, .int_val = val};
                                ProfileManager::Instance().SetSetting(profile, key, new_val);
                            }
                            break;
                        }
                        case SettingValue::Type::Enum: {
                            int val = setting.value.int_val;
                            if (ImGui::Combo(("##" + key).c_str(), &val, 
                                [](void* data, int idx, const char** out_text) {
                                    auto& opts = *static_cast<std::vector<std::string>*>(data);
                                    *out_text = opts[idx].c_str();
                                    return true;
                                }, &setting.enum_options, (int)setting.enum_options.size())) {
                                SettingValue new_val{SettingValue::Type::Enum, .int_val = val};
                                ProfileManager::Instance().SetSetting(profile, key, new_val);
                            }
                            break;
                        }
                    }
                    
                    ImGui::TableSetColumnIndex(2);
                    // Show default value
                    switch (setting.default_value.type) {
                        case SettingValue::Type::Bool:
                            ImGui::Text("%s", setting.default_value.bool_val ? "On" : "Off");
                            break;
                        case SettingValue::Type::Float:
                            ImGui::Text("%.1f", setting.default_value.float_val);
                            break;
                        case SettingValue::Type::Int:
                            ImGui::Text("%d", setting.default_value.int_val);
                            break;
                    }
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }

    void ProfileUI::DrawProfileSelector(GameType game) {
        auto profiles = ProfileManager::Instance().GetProfiles(game);
        
        if (ImGui::BeginCombo("##profile_select", "Select Profile")) {
            for (auto* profile : profiles) {
                bool is_active = ProfileManager::Instance().GetActiveProfile(profile->game) == profile;
                if (ImGui::Selectable(profile->name.c_str(), is_active)) {
                    ProfileManager::Instance().SetActiveProfile(game, profile->id);
                    if (on_profile_selected) on_profile_selected(profile);
                }
            }
            ImGui::EndCombo();
        }
    }

    void ProfileUI::DrawSettingsPanel(GameProfile* profile, FeatureCategory category) {
        if (!profile) return;
        
        if (ImGui::BeginTable("##category_settings", 2, ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 200);
            ImGui::TableHeadersRow();
            
            for (auto& [key, setting] : profile->settings) {
                if (setting.category != category) continue;
                
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", setting.name.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", setting.description.c_str());
                
                ImGui::TableSetColumnIndex(1);
                // Same widget logic as DrawProfileEditor
            }
            ImGui::EndTable();
        }
    }

    void ProfileUI::DrawProfileComparison(const GameProfile* a, const GameProfile* b) {
        if (!a || !b) return;
        
        auto diffs = ProfileManager::Instance().CompareProfiles(a, b);
        
        if (ImGui::Begin("Profile Comparison")) {
            ImGui::Text("Comparing: %s vs %s", a->name.c_str(), b->name.c_str());
            ImGui::Separator();
            
            for (const auto& diff : diffs) {
                ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "%s", diff.c_str());
            }
            
            if (diffs.empty()) {
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "Profiles are identical!");
            }
            ImGui::End();
        }
    }

} // namespace Gameplay::ProfileManager
