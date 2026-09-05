#pragma once

#include "result.h"
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <functional>
#include <optional>
#include <variant>

namespace OmniGhost::Platform {

// Items 66-71: Config schema versioning, migrations, validation

// ============================================================
// ConfigValue - Type-safe config value with validation
// ============================================================

class ConfigValue {
public:
    using Value = std::variant<
        bool,
        int64_t,
        double,
        std::string,
        std::vector<ConfigValue>,  // array
        std::unordered_map<std::string, ConfigValue>  // object
    >;

    ConfigValue() = default;
    ConfigValue(bool v) : value_(v) {}
    ConfigValue(int64_t v) : value_(v) {}
    ConfigValue(double v) : value_(v) {}
    ConfigValue(const std::string& v) : value_(v) {}
    ConfigValue(std::string&& v) : value_(std::move(v)) {}
    ConfigValue(const char* v) : value_(std::string(v)) {}
    ConfigValue(const std::vector<ConfigValue>& v) : value_(v) {}
    ConfigValue(std::vector<ConfigValue>&& v) : value_(std::move(v)) {}
    ConfigValue(const std::unordered_map<std::string, ConfigValue>& v) : value_(v) {}
    ConfigValue(std::unordered_map<std::string, ConfigValue>&& v) : value_(std::move(v)) {}

    // Type checks
    [[nodiscard]] bool IsBool() const noexcept { return std::holds_alternative<bool>(value_); }
    [[nodiscard]] bool IsInt() const noexcept { return std::holds_alternative<int64_t>(value_); }
    [[nodiscard]] bool IsDouble() const noexcept { return std::holds_alternative<double>(value_); }
    [[nodiscard]] bool IsString() const noexcept { return std::holds_alternative<std::string>(value_); }
    [[nodiscard]] bool IsArray() const noexcept { return std::holds_alternative<std::vector<ConfigValue>>(value_); }
    [[nodiscard]] bool IsObject() const noexcept { return std::holds_alternative<std::unordered_map<std::string, ConfigValue>>(value_); }
    [[nodiscard]] bool IsNull() const noexcept { return std::holds_alternative<std::monostate>(value_); }

    // Getters with type checking
    [[nodiscard]] bool AsBool(bool defaultValue = false) const noexcept {
        if (auto* v = std::get_if<bool>(&value_)) return *v;
        return defaultValue;
    }

    [[nodiscard]] int64_t AsInt(int64_t defaultValue = 0) const noexcept {
        if (auto* v = std::get_if<int64_t>(&value_)) return *v;
        if (auto* v = std::get_if<double>(&value_)) return static_cast<int64_t>(*v);
        return defaultValue;
    }

    [[nodiscard]] double AsDouble(double defaultValue = 0.0) const noexcept {
        if (auto* v = std::get_if<double>(&value_)) return *v;
        if (auto* v = std::get_if<int64_t>(&value_)) return static_cast<double>(*v);
        return defaultValue;
    }

    [[nodiscard]] std::string AsString(std::string_view defaultValue = "") const noexcept {
        if (auto* v = std::get_if<std::string>(&value_)) return *v;
        return std::string(defaultValue);
    }

    [[nodiscard]] std::vector<ConfigValue> AsArray(std::vector<ConfigValue> defaultValue = {}) const noexcept {
        if (auto* v = std::get_if<std::vector<ConfigValue>>(&value_)) return *v;
        return defaultValue;
    }

    [[nodiscard]] std::unordered_map<std::string, ConfigValue> AsObject(
        std::unordered_map<std::string, ConfigValue> defaultValue = {}) const noexcept {
        if (auto* v = std::get_if<std::unordered_map<std::string, ConfigValue>>(&value_)) return *v;
        return defaultValue;
    }

    // Type-safe access with default
    // template <typename T>
    [[nodiscard]] int64_t GetInt(const std::string& key, T defaultValue) const noexcept {
        if (auto* obj = std::get_if<std::unordered_map<std::string, ConfigValue>>(&value_)) {
            auto it = obj->find(key);
            if (it != obj->end()) {
                return it->second.template Get<T>(defaultValue);
            }
        }
        return defaultValue;
    }

    // Array access
    [[nodiscard]] ConfigValue operator[](size_t index) const noexcept {
        if (auto* arr = std::get_if<std::vector<ConfigValue>>(&value_)) {
            if (index < arr->size()) return (*arr)[index];
        }
        return ConfigValue();
    }

    // Object access
    [[nodiscard]] ConfigValue operator[](const std::string& key) const noexcept {
        if (auto* obj = std::get_if<std::unordered_map<std::string, ConfigValue>>(&value_)) {
            auto it = obj->find(key);
            if (it != obj->end()) return it->second;
        }
        return ConfigValue();
    }

    [[nodiscard]] bool HasKey(const std::string& key) const noexcept {
        if (auto* obj = std::get_if<std::unordered_map<std::string, ConfigValue>>(&value_)) {
            return obj->find(key) != obj->end();
        }
        return false;
    }

    [[nodiscard]] size_t ArraySize() const noexcept {
        if (auto* arr = std::get_if<std::vector<ConfigValue>>(&value_)) return arr->size();
        return 0;
    }

    [[nodiscard]] size_t ObjectSize() const noexcept {
        if (auto* obj = std::get_if<std::unordered_map<std::string, ConfigValue>>(&value_)) return obj->size();
        return 0;
    }

    // Convert to JSON
    [[nodiscard]] nlohmann::json ToJson() const {
        if (std::holds_alternative<bool>(value_)) return std::get<bool>(value_);
        if (std::holds_alternative<int64_t>(value_)) return std::get<int64_t>(value_);
        if (std::holds_alternative<double>(value_)) return std::get<double>(value_);
        if (std::holds_alternative<std::string>(value_)) return std::get<std::string>(value_);
        if (std::holds_alternative<std::vector<ConfigValue>>(value_)) {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& v : std::get<std::vector<ConfigValue>>(value_)) {
                arr.push_back(v.ToJson());
            }
            return arr;
        }
        if (std::holds_alternative<std::unordered_map<std::string, ConfigValue>>(value_)) {
            nlohmann::json obj = nlohmann::json::object();
            for (const auto& [k, v] : std::get<std::unordered_map<std::string, ConfigValue>>(value_)) {
                obj[k] = v.ToJson();
            }
            return obj;
        }
        return nullptr;
    }

    static ConfigValue FromJson(const nlohmann::json& j) {
        ConfigValue val;
        if (j.is_boolean()) val.value_ = j.get<bool>();
        else if (j.is_number_integer()) val.value_ = j.get<int64_t>();
        else if (j.is_number_float()) val.value_ = j.get<double>();
        else if (j.is_string()) val.value_ = j.get<std::string>();
        else if (j.is_array()) {
            std::vector<ConfigValue> arr;
            for (const auto& item : j) arr.push_back(ConfigValue::FromJson(item));
            val.value_ = std::move(arr);
        } else if (j.is_object()) {
            std::unordered_map<std::string, ConfigValue> obj;
            for (auto& [k, v] : j.items()) obj[k] = ConfigValue::FromJson(v);
            val.value_ = std::move(obj);
        } else {
            val.value_ = std::monostate{};
        }
        return val;
    }

private:
    Value value_ = std::monostate{};
};

// ============================================================
// ConfigSchema - Schema definition with validation rules
// Item 66: Clear schema version for all configs
// ============================================================

struct ConfigField {
    std::string name;
    enum class Type { Bool, Int, Double, String, Array, Object } type;
    bool required = false;
    std::string description;

    // Validation rules
    std::optional<double> min;        // For numeric types
    std::optional<double> max;        // For numeric types
    std::optional<size_t> minLength;  // For strings/arrays
    std::optional<size_t> maxLength;  // For strings/arrays
    std::vector<std::string> enumValues;  // Allowed values
    std::string pattern;              // Regex pattern for strings

    // Default value
    ConfigValue defaultValue;

    // Custom validator
    std::function<Result<void>(const ConfigValue&)> customValidator;

    // Deprecated
    bool deprecated = false;
    std::string deprecationMessage;
};

class ConfigSchema {
public:
    uint32_t version = 1;  // Schema version
    std::string name;      // Config name (e.g., "app_settings", "rust_config")
    std::string description;
    std::vector<ConfigField> fields;

    // Add field
    ConfigSchema& AddField(ConfigField field) {
        fields.push_back(std::move(field));
        return *this;
    }

    // Get field by name
    [[nodiscard]] const ConfigField* GetField(const std::string& name) const noexcept {
        for (const auto& f : fields) {
            if (f.name == name) return &f;
        }
        return nullptr;
    }

    // Validate a config object against this schema
    Result<void> Validate(const std::unordered_map<std::string, ConfigValue>& config) const noexcept {
        std::vector<std::string> errors;

        // Check required fields
        for (const auto& field : fields) {
            if (field.required && !config.contains(field.name)) {
                errors.push_back("Missing required field: " + field.name);
                continue;
            }

            auto it = config.find(field.name);
            if (it == config.end()) continue;

            const ConfigValue& value = it->second;

            // Type check
            bool typeOk = false;
            switch (field.type) {
                case ConfigField::Type::Bool: typeOk = it->second.IsBool(); break;
                case ConfigField::Type::Int: typeOk = it->second.IsInt(); break;
                case ConfigField::Type::Double: typeOk = it->second.IsDouble() || it->second.IsInt(); break;
                case ConfigField::Type::String: typeOk = it->second.IsString(); break;
                case ConfigField::Type::Array: typeOk = it->second.IsArray(); break;
                case ConfigField::Type::Object: typeOk = it->second.IsObject(); break;
            }

            if (!typeOk) {
                errors.push_back("Field '" + field.name + "': type mismatch (expected " +
                    std::to_string(static_cast<int>(field.type)) + ")");
                continue;
            }

            // Range checks for numeric
            if (field.type == ConfigField::Type::Int || field.type == ConfigField::Type::Double) {
                double val = it->second.IsInt() ? it->second.AsInt() : it->second.AsDouble();
                if (field.min && val < *field.min) {
                    errors.push_back("Field '" + field.name + "': value " + std::to_string(val) +
                        " below minimum " + std::to_string(*field.min));
                }
                if (field.max && val > *field.max) {
                    errors.push_back("Field '" + field.name + "': value " + std::to_string(val) +
                        " above maximum " + std::to_string(*field.max));
                }
            }

            // Length checks for strings/arrays
            if (field.type == ConfigField::Type::String) {
                size_t len = it->second.AsString().length();
                if (field.minLength && len < *field.minLength) {
                    errors.push_back("Field '" + field.name + "': length " + std::to_string(len) +
                        " below minimum " + std::to_string(*field.minLength));
                }
                if (field.maxLength && len > *field.maxLength) {
                    errors.push_back("Field '" + field.name + "': length " + std::to_string(len) +
                        " above maximum " + std::to_string(*field.maxLength));
                }
                // Pattern match
                if (!field.pattern.empty()) {
                    std::regex re(field.pattern);
                    if (!std::regex_match(it->second.AsString(), re)) {
                        errors.push_back("Field '" + field.name + "': does not match pattern " + field.pattern);
                    }
                }
            }

            if (field.type == ConfigField::Type::Array) {
                size_t len = it->second.AsArray().size();
                if (field.minLength && len < *field.minLength) {
                    errors.push_back("Field '" + field.name + "': array size " + std::to_string(len) +
                        " below minimum " + std::to_string(*field.minLength));
                }
                if (field.maxLength && len > *field.maxLength) {
                    errors.push_back("Field '" + field.name + "": array size " + std::to_string(len) +
                        " above maximum " + std::to_string(*field.maxLength));
                }
            }

            // Enum validation
            if (!field.enumValues.empty()) {
                std::string val = it->second.AsString();
                bool found = false;
                for (const auto& ev : field.enumValues) {
                    if (ev == val) { found = true; break; }
                }
                if (!found) {
                    errors.push_back("Field '" + field.name + "': value '" + val +
                        "' not in allowed values");
                }
            }

            // Custom validator
            if (field.customValidator) {
                auto result = field.customValidator(it->second);
                if (result.IsErr()) {
                    errors.push_back("Field '" + field.name + "': " + result.UnwrapErr().message);
                }
            }

            // Deprecation warning
            if (field.deprecated) {
                SessionLog::Write(
                    SessionLog::Severity::Warning,
                    SessionLog::Subsystem::Config,
                    "Config field deprecated: " + field.name,
                    {{"field", field.name}, {"message", field.deprecationMessage}}
                );
            }
        }

        // Check for unknown keys
        for (const auto& [key, _] : config) {
            bool known = false;
            for (const auto& f : fields) {
                if (f.name == key) { known = true; break; }
            }
            if (!known) {
                errors.push_back("Unknown config field: " + key);
            }
        }

        if (!errors.empty()) {
            return Err<void>(JoinErrors(errors));
        }
        return Ok();
    }

    // Apply defaults for missing optional fields
    void ApplyDefaults(std::unordered_map<std::string, ConfigValue>& config) const noexcept {
        for (const auto& field : fields) {
            if (!config.contains(field.name) && !field.defaultValue.IsNull()) {
                config[field.name] = field.defaultValue;
            }
        }
    }

    // Convert to JSON schema for documentation
    [[nodiscard]] nlohmann::json ToJsonSchema() const noexcept;

private:
    [[nodiscard]] std::string JoinErrors(const std::vector<std::string>& errors) const noexcept {
        std::string result;
        for (size_t i = 0; i < errors.size(); ++i) {
            if (i > 0) result += "; ";
            result += errors[i];
        }
        return result;
    }
};

// ============================================================
// ConfigManager - Manages config with schema, migrations
// Item 67: Incremental migrations between versions
// ============================================================

using MigrationFn = std::function<Result<void>(nlohmann::json&)>;

struct Migration {
    uint32_t fromVersion;
    uint32_t toVersion;
    std::string description;
    MigrationFn migrate;
};

class ConfigManager {
public:
    ConfigManager(const ConfigSchema& schema, const std::filesystem::path& configPath)
        : schema_(schema), configPath_(configPath) {}

    // Load config with migration
    Result<std::unordered_map<std::string, ConfigValue>> Load();

    // Save config (with atomic write)
    Result<void> Save(const std::unordered_map<std::string, ConfigValue>& config);

    // Get current schema version
    [[nodiscard]] uint32_int64_t GetIntSchemaVersion() const noexcept { return schema_.version; }

    // Get config path
    [[nodiscard]] const std::filesystem::path& GetPath() const noexcept { return configPath_; }

    // Register migration
    void AddMigration(Migration migration) {
        migrations_.push_back(std::move(migration));
        // Sort by fromVersion
        std::sort(migrations_.begin(), migrations_.end(),
            [](const Migration& a, const Migration& b) { return a.fromVersion < b.fromVersion; });
    }

    // Validate current config
    Result<void> Validate() const noexcept;

    // Get value with type safety
    // template <typename T>
    int64_t GetInt(std::string_view key, T defaultValue = T{}) const {
        auto it = config_.find(std::string(key));
        if (it != config_.end()) {
            return it->second.template Get<T>(T{});
        }
        return T{};
    }

    // Set value (marks dirty)
    void Set(std::string_view key, const ConfigValue& value) {
        config_[std::string(key)] = value;
        dirty_ = true;
    }

    // Check if dirty
    [[nodiscard]] bool IsDirty() const noexcept { return dirty_; }

private:
    const ConfigSchema& schema_;
    std::filesystem::path configPath_;
    std::unordered_map<std::string, ConfigValue> config_;
    std::vector<Migration> migrations_;
    bool dirty_ = false;
    mutable std::mutex mutex_;

    Result<void> RunMigrations(nlohmann::json& json);
};

// ============================================================
// Validation helpers (Item 69, 70)
// ============================================================

namespace ConfigValidation {

// Reject NaN, infinity
[[nodiscard]] inline Result<void> ValidateFinite(double value, std::string_view fieldName) {
    if (!std::isfinite(value)) {
        return Err<void>(std::string(fieldName) + ": value must be finite (not NaN or infinity)");
    }
    return Ok();
}

// Semantic clamping with logging
// template <typename T>
T ClampWithLog(T value, T min, T max, std::string_view fieldName) {
    if (value < min) {
        SessionLog::Write(SessionLog::Severity::Warning, SessionLog::Subsystem::Config,
            "Config value clamped",
            {{"field", std::string(fieldName)}, {"original", std::to_string(value)},
             {"clamped", std::to_string(min)}, {"action", "clamped_to_min"}});
        return min;
    }
    if (value > max) {
        SessionLog::Write(SessionLog::Severity::Warning, SessionLog::Subsystem::Config,
            "Config value clamped",
            {{"field", std::string(fieldName)}, {"original", std::to_string(value)},
             {"clamped", std::to_string(max)}, {"action", "clamped_to_max"}});
        return max;
    }
    return value;
}

// Reject NaN/infinity/out-of-range for config values
// template <typename T>
Result<T> ValidateAndClamp(T value, T min, T max, std::string_view fieldName) {
    if (!std::isfinite(static_cast<double>(value))) {
        return Err<T>(std::string(fieldName) + ": value must be finite (not NaN or infinity)");
    }
    if (value < min || value > max) {
        return Err<T>(std::string(fieldName) + ": value " + std::to_string(value) +
            " out of range [" + std::to_string(min) + ", " + std::to_string(max) + "]");
    }
    return Ok(value);
}

} // namespace ConfigValidation

} // namespace OmniGhost::Platform
