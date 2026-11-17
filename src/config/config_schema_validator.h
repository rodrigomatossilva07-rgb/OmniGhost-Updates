#pragma once

#include "platform/expected.h"
#include <nlohmann/json.hpp>
#include <nlohmann/json_schema.hpp>
#include <string>
#include <string_view>
#include <filesystem>
#include <optional>

namespace OmniGhost::Config {

// =============================================================================
// ConfigSchemaValidator - JSON Schema validation for configuration files
// =============================================================================

class ConfigSchemaValidator {
public:
    struct ValidationResult {
        bool valid = true;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };

    // Schema IDs
    enum class SchemaId {
        GlobalConfig,
        GameConfig,
        ProfileConfig,
        ThemeConfig,
        LocaleConfig,
        Count
    };

    static constexpr std::string_view SchemaIdToString(SchemaId id) noexcept {
        switch (id) {
            case SchemaId::GlobalConfig: return "global_config";
            case SchemaId::GameConfig: return "game_config";
            case SchemaId::ProfileConfig: return "profile_config";
            case SchemaId::ThemeConfig: return "theme_config";
            case SchemaId::LocaleConfig: return "locale_config";
            default: return "unknown";
        }
    }

    // Initialize validator with schema files
    Expected<void> Initialize(const std::filesystem::path& schemaDirectory);

    // Validate JSON against a specific schema
    ValidationResult Validate(const nlohmann::json& json, SchemaId schemaId) const;

    // Validate JSON file against schema
    Expected<ValidationResult> ValidateFile(const std::filesystem::path& filePath, SchemaId schemaId) const;

    // Get schema for a given ID
    std::optional<nlohmann::json> GetSchema(SchemaId id) const noexcept;

    // List available schemas
    std::vector<SchemaId> GetAvailableSchemas() const noexcept;

private:
    using JsonSchemaValidator = nlohmann::json_schema::json_validator;

    std::filesystem::path schemaDir_;
    std::array<std::unique_ptr<JsonSchemaValidator>, static_cast<size_t>(SchemaId::Count)> validators_;
    std::array<bool, static_cast<size_t>(SchemaId::Count)> initialized_{false};
    mutable std::shared_mutex mutex_;
};

// =============================================================================
// Schema Definition Helpers
// =============================================================================

namespace SchemaHelpers {

// Create schema for global config
nlohmann::json CreateGlobalConfigSchema();

// Create schema for game config
nlohmann::json CreateGameConfigSchema();

// Create schema for profile config
nlohmann::json CreateProfileConfigSchema();

// Create schema for theme config
nlohmann::json CreateThemeConfigSchema();

// Create schema for locale config
nlohmann::json CreateLocaleConfigSchema();

// Write all schemas to directory
Expected<void> WriteAllSchemas(const std::filesystem::path& outputDir);

} // namespace SchemaHelpers

} // namespace OmniGhost::Config