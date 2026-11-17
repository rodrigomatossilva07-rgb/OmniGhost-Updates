#include "config_schema_validator.h"
#include "platform/expected.h"
#include "platform/logging.h"
#include "platform/app_paths.h"

#include <nlohmann/json.hpp>
#include <nlohmann/json_schema.hpp>
#include <fstream>
#include <shared_mutex>

namespace OmniGhost::Config {

// =============================================================================
// ConfigSchemaValidator Implementation
// =============================================================================

Expected<void> ConfigSchemaValidator::Initialize(const std::filesystem::path& schemaDirectory) {
    std::unique_lock lock(mutex_);
    
    schemaDir_ = schemaDirectory;
    
    if (!std::filesystem::exists(schemaDir_)) {
        OGS_LOG_WARN("[ConfigSchemaValidator] Schema directory does not exist: {}", schemaDir_.string());
        // Try to create schemas if directory doesn't exist
        auto writeResult = SchemaHelpers::WriteAllSchemas(schemaDir_);
        if (!writeResult) {
            return Failure<void>(ErrorCode::IOError, "Failed to create schema directory and default schemas");
        }
    }

    // Initialize validators for each schema
    const std::array<std::pair<SchemaId, std::string_view>, static_cast<size_t>(SchemaId::Count)> schemas = {{
        {SchemaId::GlobalConfig, "global_config.schema.json"},
        {SchemaId::GameConfig, "game_config.schema.json"},
        {SchemaId::ProfileConfig, "profile_config.schema.json"},
        {SchemaId::ThemeConfig, "theme_config.schema.json"},
        {SchemaId::LocaleConfig, "locale_config.schema.json"},
    }};

    for (const auto& [id, filename] : schemas) {
        const auto schemaPath = schemaDir_ / filename;
        if (!std::filesystem::exists(schemaPath)) {
            OGS_LOG_WARN("[ConfigSchemaValidator] Schema file not found: {}", schemaPath.string());
            // Try to generate it
            auto writeResult = SchemaHelpers::WriteAllSchemas(schemaDir_);
            if (!writeResult) {
                OGS_LOG_ERROR("[ConfigSchemaValidator] Failed to generate missing schemas");
            }
            break;
        }

        try {
            std::ifstream file(schemaPath);
            nlohmann::json schemaJson;
            file >> schemaJson;

            auto validator = std::make_unique<JsonSchemaValidator>(schemaJson);
            validators_[static_cast<size_t>(id)] = std::move(validator);
            initialized_[static_cast<size_t>(id)] = true;

            OGS_LOG_INFO("[ConfigSchemaValidator] Loaded schema: {} ({})", filename, SchemaIdToString(id));
        } catch (const std::exception& ex) {
            OGS_LOG_ERROR("[ConfigSchemaValidator] Failed to load schema {}: {}", filename, ex.what());
            return Failure<void>(ErrorCode::ParseError, "Failed to load schema: " + std::string(ex.what()));
        }
    }

    OGS_LOG_INFO("[ConfigSchemaValidator] Initialized with {} schemas", GetAvailableSchemas().size());
    return Success();
}

ConfigSchemaValidator::ValidationResult ConfigSchemaValidator::Validate(
    const nlohmann::json& json, 
    SchemaId schemaId) const {

    ValidationResult result;

    std::shared_lock lock(mutex_);
    const size_t index = static_cast<size_t>(schemaId);

    if (!initialized_[index] || !validators_[index]) {
        result.valid = false;
        result.errors.push_back("Schema not initialized: " + std::string(SchemaIdToString(schemaId)));
        return result;
    }

    try {
        validators_[index]->validate(json);
        result.valid = true;
    } catch (const nlohmann::json_schema::validation_error& ex) {
        result.valid = false;
        result.errors.push_back(ex.what());
    } catch (const std::exception& ex) {
        result.valid = false;
        result.errors.push_back("Validation error: " + std::string(ex.what()));
    }

    return result;
}

Expected<ConfigSchemaValidator::ValidationResult> ConfigSchemaValidator::ValidateFile(
    const std::filesystem::path& filePath, 
    SchemaId schemaId) const {

    if (!std::filesystem::exists(filePath)) {
        return Failure<ValidationResult>(ErrorCode::NotFound, "File not found: " + filePath.string());
    }

    try {
        std::ifstream file(filePath);
        nlohmann::json json;
        file >> json;
        return Success(Validate(json, schemaId));
    } catch (const nlohmann::json::parse_error& ex) {
        ValidationResult result;
        result.valid = false;
        result.errors.push_back("JSON parse error: " + std::string(ex.what()));
        return Success(result);
    } catch (const std::exception& ex) {
        return Failure<ValidationResult>(ErrorCode::IOError, "Failed to read file: " + std::string(ex.what()));
    }
}

std::optional<nlohmann::json> ConfigSchemaValidator::GetSchema(SchemaId id) const noexcept {
    std::shared_lock lock(mutex_);
    const size_t index = static_cast<size_t>(id);
    if (initialized_[index] && validators_[index]) {
        // Return the schema JSON (we'd need to store it separately)
        // For now, return empty
        return std::nullopt;
    }
    return std::nullopt;
}

std::vector<ConfigSchemaValidator::SchemaId> ConfigSchemaValidator::GetAvailableSchemas() const noexcept {
    std::shared_lock lock(mutex_);
    std::vector<SchemaId> schemas;
    for (size_t i = 0; i < initialized_.size(); ++i) {
        if (initialized_[i]) {
            schemas.push_back(static_cast<SchemaId>(i));
        }
    }
    return schemas;
}

// =============================================================================
// Schema Definition Helpers Implementation
// =============================================================================

namespace SchemaHelpers {

nlohmann::json CreateGlobalConfigSchema() {
    return nlohmann::json{
        {"$schema", "http://json-schema.org/draft-07/schema#"},
        {"$id", "https://omnighost.dev/schemas/global_config.schema.json"},
        {"title", "OmniGhost Global Configuration"},
        {"type", "object"},
        {"properties", {
            {"schema_version", {"type", "integer", "minimum", 1, "maximum", 10}},
            {"language", {"type", "integer", "minimum", 0, "maximum", 6}},
            {"start_behavior", {"type", "integer", "minimum", 0, "maximum", 2}},
            {"remember_last_game", {"type", "boolean"}},
            {"minimize_behavior", {"type", "integer", "minimum", 0, "maximum", 1}},
            {"close_behavior", {"type", "integer", "minimum", 0, "maximum", 1}},
            {"auto_save", {"type", "boolean"}},
            {"menu_bind", {"type", "integer", "minimum", 0, "maximum", 255}},
            {"black_background", {"type", "boolean"}},
            {"black_level", {"type", "number", "minimum", 0, "maximum", 100}},
            {"performance_mode", {"type", "boolean"}},
            {"auto_performance", {"type", "boolean"}},
            {"particles", {"type", "boolean"}},
            {"snow_effect", {"type", "boolean"}},
            {"matrix_rain", {"type", "boolean"}},
            {"digital_rain_level", {"type", "integer", "minimum", 0, "maximum", 2}},
            {"animation_intensity", {"type", "integer", "minimum", 0, "maximum", 2}},
            {"ui_sounds", {"type", "boolean"}},
            {"ui_sound_volume", {"type", "number", "minimum", 0, "maximum", 100}},
            {"notification_sounds", {"type", "boolean"}},
            {"startup_sound", {"type", "boolean"}},
            {"high_contrast", {"type", "boolean"}},
            {"ui_scale", {"type", "number", "minimum", 0.5, "maximum", 2.0}},
            {"monitor_index", {"type", "integer", "minimum", -1}},
            {"show_fps", {"type", "boolean"}},
            {"telemetry_log_enabled", {"type", "boolean"}},
            {"show_hotkey_overlay", {"type", "boolean"}},
            {"vsync", {"type", "boolean"}},
            {"overlay_position_mode", {"type", "integer", "minimum", 0}},
            {"overlay_scale", {"type", "number", "minimum", 0.5, "maximum", 3.0}},
            {"overlay_lock_to_monitor", {"type", "boolean"}},
            {"update_channel", {"type", "integer", "minimum", 0, "maximum", 2}},
            {"update_auto_check", {"type", "boolean"}},
            {"update_auto_download", {"type", "boolean"}},
            {"update_install_on_exit", {"type", "boolean"}},
            {"show_advanced", {"type", "boolean"}},
            {"color_primary", {"type", "integer", "minimum", 0, "maximum", 4294967295}},
            {"color_secondary", {"type", "integer", "minimum", 0, "maximum", 4294967295}},
            {"color_fps", {"type", "integer", "minimum", 0, "maximum", 4294967295}},
            {"color_particles", {"type", "integer", "minimum", 0, "maximum", 4294967295}},
            {"theme_mode", {"type", "integer", "minimum", 0, "maximum", 2}},
            {"accent_preset", {"type", "integer", "minimum", 0, "maximum", 99}},
            {"visual_style", {"type", "integer", "minimum", 0, "maximum", 7}},
            {"aura_50", {"type", "boolean"}},
            {"aura_100", {"type", "boolean"}},
            {"onboarding_completed", {"type", "boolean"}},
            {"license_accepted", {"type", "boolean"}},
            {"last_selected_game", {"type", "integer", "minimum", -1}},
            {"changelog_last_viewed", {"type", "string"}}
        }},
        {"required", {
            "schema_version", "language", "start_behavior", "remember_last_game",
            "minimize_behavior", "close_behavior", "auto_save", "menu_bind"
        }},
        {"additionalProperties", true}
    };
}

nlohmann::json CreateGameConfigSchema() {
    return nlohmann::json{
        {"$schema", "http://json-schema.org/draft-07/schema#"},
        {"$id", "https://omnighost.dev/schemas/game_config.schema.json"},
        {"title", "OmniGhost Game Configuration"},
        {"type", "object"},
        {"properties", {
            {"schema_version", {"type", "integer", "minimum", 1, "maximum", 10}},
            {"game_id", {"type", "string"}},
            {"esp", {"type", "object"}},
            {"aimbot", {"type", "object"}},
            {"trigger", {"type", "object"}},
            {"radar", {"type", "object"}},
            {"misc", {"type", "object"}}
        }},
        {"required", {"schema_version", "game_id"}},
        {"additionalProperties", true}
    };
}

nlohmann::json CreateProfileConfigSchema() {
    return nlohmann::json{
        {"$schema", "http://json-schema.org/draft-07/schema#"},
        {"$id", "https://omnighost.dev/schemas/profile_config.schema.json"},
        {"title", "OmniGhost Profile Configuration"},
        {"type", "object"},
        {"properties", {
            {"schema_version", {"type", "integer", "minimum", 1, "maximum", 10}},
            {"name", {"type", "string"}},
            {"game_id", {"type", "string"}},
            {"type", {"type", "integer", "minimum", 0, "maximum", 3}},
            {"description", {"type", "string"}},
            {"author", {"type", "string"}},
            {"version", {"type", "integer", "minimum", 1}},
            {"settings", {"type", "object"}},
            {"created_at", {"type", "string", "format", "date-time"}},
            {"updated_at", {"type", "string", "format", "date-time"}},
            {"favorite", {"type", "boolean"}},
            {"tags", {"type", "array", "items", {"type", "string"}}}
        }},
        {"required", {"schema_version", "name", "game_id", "type"}},
        {"additionalProperties", true}
    };
}

nlohmann::json CreateThemeConfigSchema() {
    return nlohmann::json{
        {"$schema", "http://json-schema.org/draft-07/schema#"},
        {"$id", "https://omnighost.dev/schemas/theme_config.schema.json"},
        {"title", "OmniGhost Theme Configuration"},
        {"type", "object"},
        {"properties", {
            {"schema_version", {"type", "integer", "minimum", 1, "maximum", 10}},
            {"id", {"type", "string"}},
            {"name", {"type", "string"}},
            {"description", {"type", "string"}},
            {"author", {"type", "string"}},
            {"version", {"type", "string"}},
            {"built_in", {"type", "boolean"}},
            {"colors", {"type", "object"}},
            {"metrics", {"type", "object"}},
            {"fonts", {"type", "object"}}
        }},
        {"required", {"schema_version", "id", "name"}},
        {"additionalProperties", true}
    };
}

nlohmann::json CreateLocaleConfigSchema() {
    return nlohmann::json{
        {"$schema", "http://json-schema.org/draft-07/schema#"},
        {"$id", "https://omnighost.dev/schemas/locale_config.schema.json"},
        {"title", "OmniGhost Locale Configuration"},
        {"type", "object"},
        {"patternProperties", {
            {"^.*$", {"type", "string"}}
        }},
        {"additionalProperties", false}
    };
}

Expected<void> WriteAllSchemas(const std::filesystem::path& outputDir) {
    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);
    if (ec) {
        return Failure<void>(ErrorCode::IOError, "Failed to create schema directory: " + ec.message());
    }

    const std::array<std::pair<std::string_view, nlohmann::json>, 5> schemas = {{
        {"global_config.schema.json", CreateGlobalConfigSchema()},
        {"game_config.schema.json", CreateGameConfigSchema()},
        {"profile_config.schema.json", CreateProfileConfigSchema()},
        {"theme_config.schema.json", CreateThemeConfigSchema()},
        {"locale_config.schema.json", CreateLocaleConfigSchema()},
    }};

    for (const auto& [filename, schema] : schemas) {
        const auto filePath = outputDir / filename;
        std::ofstream file(filePath);
        if (!file) {
            return Failure<void>(ErrorCode::IOError, "Failed to write schema: " + filePath.string());
        }
        file << schema.dump(4);
    }

    return Success();
}

} // namespace SchemaHelpers

} // namespace OmniGhost::Config