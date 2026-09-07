#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <variant>
#include <optional>
#include <cstdint>
#include <climits>
#include <cfloat>
#include <functional>
#include <memory>
#include "result.h"
#include "config_serializer.h"

namespace OmniGhost::Platform {

// ============================================================
// Configuration Schema Definition
// ============================================================

enum class ConfigType : uint8_t {
    Bool,
    Int,
    Float,
    String,
    IntArray,
    FloatArray,
    StringArray
};

struct ConfigField {
    std::string name;
    ConfigType type;
    bool required = false;
    std::string defaultValue;
    std::string description;
    int minInt = INT32_MIN;
    int maxInt = INT32_MAX;
    float minFloat = -FLT_MAX;
    float maxFloat = FLT_MAX;
    std::vector<std::string> allowedValues; // For enum-like validation

    ConfigField() = default;

    ConfigField(std::string n, ConfigType t, bool req, std::string def, std::string desc)
        : name(std::move(n)), type(t), required(req), defaultValue(std::move(def)), description(std::move(desc)) {}

    ConfigField(std::string n, ConfigType t, bool req, std::string def, std::string desc, int minI, int maxI)
        : name(std::move(n)), type(t), required(req), defaultValue(std::move(def)), description(std::move(desc)),
          minInt(minI), maxInt(maxI) {}

    ConfigField(std::string n, ConfigType t, bool req, std::string def, std::string desc, float minF, float maxF)
        : name(std::move(n)), type(t), required(req), defaultValue(std::move(def)), description(std::move(desc)),
          minFloat(minF), maxFloat(maxF) {}
};

struct ConfigSection {
    std::string name;
    std::vector<ConfigField> fields;
    std::string description;
};

class ConfigSchema {
public:
    using ValidationResult = Result<void>;
    
    void AddSection(ConfigSection section) {
        sections_[section.name] = std::move(section);
    }
    
    ValidationResult Validate(const ConfigSerializer& config) const noexcept {
        for (const auto& [sectionName, section] : sections_) {
            for (const auto& field : section.fields) {
                std::string fullKey = sectionName + "." + field.name;
                
                if (field.required && !config.HasKey(fullKey)) {
                    return Err<void>("Required field missing: " + fullKey);
                }
                
                if (!config.HasKey(fullKey)) continue;
                
                auto result = ValidateField(field, fullKey, config);
                if (!result) return result;
            }
        }
        return Ok();
    }
    
    std::vector<ConfigSection> GetSections() const noexcept {
        std::vector<ConfigSection> result;
        result.reserve(sections_.size());
        for (const auto& [_, section] : sections_) {
            result.push_back(section);
        }
        return result;
    }
    
    static ConfigSchema CreateDefault() {
        ConfigSchema schema;
        
        // ESP Section
        ConfigSection espSection;
        espSection.name = "esp";
        espSection.description = "ESP visualization settings";
        espSection.fields = {
            {"enabled", ConfigType::Bool, true, "true", "Enable ESP"},
            {"max_distance", ConfigType::Float, true, "500.0", "Maximum ESP distance", 0.0f, 2000.0f},
            {"box_2d", ConfigType::Bool, true, "true", "2D Boxes"},
            {"skeleton", ConfigType::Bool, true, "true", "Skeleton"},
            {"health_bar", ConfigType::Bool, true, "true", "Health Bar"},
            {"max_entities", ConfigType::Int, true, "128", "Max entities to render", 1, 512},
            {"color_visible", ConfigType::Int, true, "0xFF00FF00", "Visible color"},
            {"color_invisible", ConfigType::Int, true, "0xFFFF0000", "Invisible color"},
        };
        schema.AddSection(std::move(espSection));
        
        // Aim Section
        ConfigSection aimSection;
        aimSection.name = "aim";
        aimSection.description = "Aimbot settings";
        aimSection.fields = {
            {"aimbot_enabled", ConfigType::Bool, true, "false", "Enable aimbot"},
            {"fov_size", ConfigType::Float, true, "90.0", "FOV size", 1.0f, 360.0f},
            {"smooth_x", ConfigType::Float, true, "1.0", "Horizontal smoothing", 0.0f, 100.0f},
            {"smooth_y", ConfigType::Float, true, "1.0", "Vertical smoothing", 0.0f, 100.0f},
            {"max_distance", ConfigType::Float, true, "500.0", "Max aim distance", 0.0f, 2000.0f},
            {"hitbox", ConfigType::Int, true, "0", "Target hitbox", 0, 8},
            {"fov_style", ConfigType::Int, true, "0", "FOV style", 0, 3},
            {"aimbot_bind", ConfigType::Int, true, "0x02", "Aim key"},
            {"aimbot_bind2", ConfigType::Int, true, "0", "Secondary aim key"},
            {"visible_check", ConfigType::Bool, true, "true", "Visible check"},
            {"prediction", ConfigType::Bool, true, "true", "Velocity prediction"},
            {"trigger_enabled", ConfigType::Bool, true, "false", "Enable triggerbot"},
            {"trigger_fov", ConfigType::Float, true, "5.0", "Trigger FOV", 0.1f, 30.0f},
            {"trigger_delay", ConfigType::Float, true, "0.0", "Trigger delay", 0.0f, 1000.0f},
        };
        schema.AddSection(std::move(aimSection));
        
        // Vehicle ESP Section
        ConfigSection vehicleSection;
        vehicleSection.name = "veh";
        vehicleSection.description = "Vehicle ESP settings";
        vehicleSection.fields = {
            {"enabled", ConfigType::Bool, true, "false", "Enable vehicle ESP"},
            {"max_distance", ConfigType::Float, true, "300.0", "Max distance", 0.0f, 2000.0f},
            {"box_3d", ConfigType::Bool, true, "true", "3D Boxes"},
            {"show_speed", ConfigType::Bool, true, "true", "Show speed"},
            {"show_occupants", ConfigType::Bool, true, "true", "Show occupants"},
        };
        schema.AddSection(std::move(vehicleSection));
        
        // Friends Section
        ConfigSection friendsSection;
        friendsSection.name = "friends";
        friendsSection.description = "Friends list settings";
        friendsSection.fields = {
            {"ignore_aim", ConfigType::Bool, true, "true", "Ignore friends in aim"},
            {"show_friend_esp", ConfigType::Bool, true, "true", "Show friend ESP"},
            {"friend_color", ConfigType::Int, true, "0xFF00FF00", "Friend color"},
        };
        schema.AddSection(std::move(friendsSection));
        
        return schema;
    }

private:
    std::unordered_map<std::string, ConfigSection> sections_;
    
    Result<void> ValidateField(const ConfigField& field, std::string_view fullKey, const ConfigSerializer& config) const noexcept {
        // Type-specific validation
        switch (field.type) {
            case ConfigType::Bool: {
                bool val;
                if (!config.Get(fullKey, val)) return Err<void>("Invalid bool: " + std::string(fullKey));
                break;
            }
            case ConfigType::Int: {
                int val;
                if (!config.Get(fullKey, val)) return Err<void>("Invalid int: " + std::string(fullKey));
                if (val < field.minInt || val > field.maxInt) {
                    return Err<void>("Value out of range for " + std::string(fullKey) + 
                                   ": " + std::to_string(val) + " not in [" + 
                                   std::to_string(field.minInt) + ", " + std::to_string(field.maxInt) + "]");
                }
                break;
            }
            case ConfigType::Float: {
                float val;
                if (!config.Get(fullKey, val)) return Err<void>("Invalid float: " + std::string(fullKey));
                if (val < field.minFloat || val > field.maxFloat) {
                    return Err<void>("Value out of range for " + std::string(fullKey));
                }
                break;
            }
            case ConfigType::String: {
                std::string val;
                if (!config.Get(fullKey, val)) return Err<void>("Invalid string: " + std::string(fullKey));
                if (!field.allowedValues.empty()) {
                    if (std::find(field.allowedValues.begin(), field.allowedValues.end(), val) == field.allowedValues.end()) {
                        return Err<void>("Invalid value for " + std::string(fullKey) + ": " + val);
                    }
                }
                break;
            }
            case ConfigType::IntArray: {
                std::vector<int> val;
                if (!config.Get(fullKey, val)) return Err<void>("Invalid int array: " + std::string(fullKey));
                break;
            }
            case ConfigType::FloatArray: {
                std::vector<float> val;
                if (!config.Get(fullKey, val)) return Err<void>("Invalid float array: " + std::string(fullKey));
                break;
            }
            case ConfigType::StringArray: {
                std::vector<std::string> val;
                if (!config.Get(fullKey, val)) return Err<void>("Invalid string array: " + std::string(fullKey));
                break;
            }
        }
        return Ok();
    }
};

} // namespace OmniGhost::Platform