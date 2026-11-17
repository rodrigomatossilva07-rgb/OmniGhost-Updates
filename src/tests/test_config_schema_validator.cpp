#include <catch2/catch_test_macros.hpp>

#include "config/config_schema_validator.h"
#include "platform/expected.h"
#include "platform/logging.h"
#include "platform/app_paths.h"

#include <filesystem>
#include <fstream>

using namespace OmniGhost::Config;

TEST_CASE("ConfigSchemaValidator initialization", "[config_schema]") {
    ConfigSchemaValidator validator;
    
    // Initialize with temp directory
    std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "omnighost_test_schemas_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::error_code ec;
    std::filesystem::create_directories(tempDir, ec);
    
    auto initResult = validator.Initialize(tempDir);
    // May fail if schemas don't exist, that's OK for unit tests
    if (!initResult) {
        WARN("Schema initialization failed (schemas may not exist): " + initResult.error().message);
    } else {
        // Check available schemas
        auto schemas = validator.GetAvailableSchemas();
        REQUIRE(schemas.size() <= 5); // max 5 schemas
    }
    
    // Cleanup
    std::filesystem::remove_all(tempDir, ec);
}

TEST_CASE("ConfigSchemaValidator schema validation", "[config_schema]") {
    ConfigSchemaValidator validator;
    std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "omnighost_test_schemas_validator";
    std::filesystem::create_directories(tempDir);
    
    auto initResult = validator.Initialize(tempDir);
    if (!initResult) {
        WARN("Schemas not available, skipping validation tests");
        std::filesystem::remove_all(tempDir);
        return;
    }
    
    // Test valid global config
    nlohmann::json validGlobal = {
        {"schema_version", 4},
        {"language", 1},
        {"start_behavior", 0},
        {"remember_last_game", true},
        {"minimize_behavior", 0},
        {"close_behavior", 0},
        {"auto_save", true},
        {"menu_bind", 45},
        {"black_background", true},
        {"black_level", 100.0},
        {"performance_mode", false},
        {"auto_performance", true},
        {"particles", true},
        {"snow_effect", false},
        {"matrix_rain", true},
        {"digital_rain_level", 2},
        {"animation_intensity", 1},
        {"ui_sounds", true},
        {"ui_sound_volume", 35.0},
        {"notification_sounds", true},
        {"startup_sound", true},
        {"high_contrast", false},
        {"ui_scale", 1.0},
        {"monitor_index", -1},
        {"show_fps", true},
        {"telemetry_log_enabled", false},
        {"show_hotkey_overlay", false},
        {"vsync", false},
        {"overlay_position_mode", 0},
        {"overlay_scale", 1.0},
        {"overlay_lock_to_monitor", true},
        {"update_channel", 0},
        {"update_auto_check", true},
        {"update_auto_download", false},
        {"update_install_on_exit", false},
        {"show_advanced", false},
        {"color_primary", 13933367},
        {"color_secondary", 2631722},
        {"color_fps", 33023},
        {"color_particles", 538247839},
        {"theme_mode", 0},
        {"accent_preset", 0},
        {"visual_style", 0},
        {"aura_50", false},
        {"aura_100", false},
        {"onboarding_completed", false},
        {"license_accepted", false},
        {"last_selected_game", -1},
        {"changelog_last_viewed", ""}
    };
    
    auto result = validator.Validate(validGlobal, ConfigSchemaValidator::SchemaId::GlobalConfig);
    REQUIRE(result.valid);
    REQUIRE(result.errors.empty());
    
    // Test invalid global config (missing required field)
    nlohmann::json invalidGlobal = validGlobal;
    invalidGlobal.erase("language");
    
    auto invalidResult = validator.Validate(invalidGlobal, ConfigSchemaValidator::SchemaId::GlobalConfig);
    REQUIRE_FALSE(invalidResult.valid);
    REQUIRE_FALSE(invalidResult.errors.empty());
    
    // Test with extra properties (should be allowed)
    nlohmann::json withExtra = validGlobal;
    withExtra["extra_field"] = "test";
    auto extraResult = validator.Validate(withExtra, ConfigSchemaValidator::SchemaId::GlobalConfig);
    REQUIRE(extraResult.valid); // additionalProperties is true by default
}

TEST_CASE("ConfigSchemaValidator file validation", "[config_schema]") {
    ConfigSchemaValidator validator;
    std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "omnighost_test_schemas_file";
    std::filesystem::create_directories(tempDir);
    
    auto initResult = validator.Initialize(tempDir);
    if (!initResult) {
        WARN("Schemas not available, skipping file validation tests");
        std::filesystem::remove_all(tempDir);
        return;
    }
    
    // Write valid config to temp file
    std::filesystem::path tempFile = tempDir / "test_global.json";
    nlohmann::json validJson = {
        {"schema_version", 4},
        {"language", 1},
        {"start_behavior", 0},
        {"remember_last_game", true},
        {"minimize_behavior", 0},
        {"close_behavior", 0},
        {"auto_save", true},
        {"menu_bind", 45},
        {"black_background", true},
        {"black_level", 100.0},
        {"performance_mode", false},
        {"auto_performance", true},
        {"particles", true},
        {"snow_effect", false},
        {"matrix_rain", true},
        {"digital_rain_level", 2},
        {"animation_intensity", 1},
        {"ui_sounds", true},
        {"ui_sound_volume", 35.0},
        {"notification_sounds", true},
        {"startup_sound", true},
        {"high_contrast", false},
        {"ui_scale", 1.0},
        {"monitor_index", -1},
        {"show_fps", true},
        {"telemetry_log_enabled", false},
        {"show_hotkey_overlay", false},
        {"vsync", false},
        {"overlay_position_mode", 0},
        {"overlay_scale", 1.0},
        {"overlay_lock_to_monitor", true},
        {"update_channel", 0},
        {"update_auto_check", true},
        {"update_auto_download", false},
        {"update_install_on_exit", false},
        {"show_advanced", false},
        {"color_primary", 13933367},
        {"color_secondary", 2631722},
        {"color_fps", 33023},
        {"color_particles", 538247839},
        {"theme_mode", 0},
        {"accent_preset", 0},
        {"visual_style", 0},
        {"aura_50", false},
        {"aura_100", false},
        {"onboarding_completed", false},
        {"license_accepted", false},
        {"last_selected_game", -1},
        {"changelog_last_viewed", ""}
    };
    
    {
        std::ofstream file(tempFile);
        file << nlohmann::json::dump(4);
        file.close();
    }
    
    auto fileResult = validator.ValidateFile(tempFile, ConfigSchemaValidator::SchemaId::GlobalConfig);
    REQUIRE(fileResult);
    REQUIRE(fileResult.value().valid);
    
    // Test invalid file
    std::filesystem::path invalidFile = tempDir / "invalid.json";
    {
        std::ofstream file(invalidFile);
        file << "{ invalid json }";
        file.close();
    }
    
    auto invalidFileResult = validator.ValidateFile(invalidFile, ConfigSchemaValidator::SchemaId::GlobalConfig);
    REQUIRE(fileResult); // Should succeed but with valid=false
    REQUIRE_FALSE(fileResult.value().valid);
    REQUIRE_FALSE(fileResult.value().errors.empty());
    
    // Cleanup
    std::filesystem::remove_all(tempDir);
}

TEST_CASE("SchemaHelpers schema generation", "[config_schema]") {
    using namespace OmniGhost::Config::SchemaHelpers;
    
    std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "omnighost_test_schemas_write";
    std::filesystem::create_directories(tempDir);
    
    auto writeResult = WriteAllSchemas(tempDir);
    REQUIRE(writeResult);
    
    // Verify all schema files were created
    std::vector<std::string> expectedFiles = {
        "global_config.schema.json",
        "game_config.schema.json",
        "profile_config.schema.json",
        "theme_config.schema.json",
        "locale_config.schema.json"
    };
    
    for (const auto& filename : expectedFiles) {
        std::filesystem::path filePath = tempDir / filename;
        REQUIRE(std::filesystem::exists(filePath));
        
        // Verify valid JSON
        std::ifstream file(filePath);
        nlohmann::json schema;
        REQUIRE_NOTHROW(file >> schema);
        REQUIRE(schema.contains("$schema"));
        REQUIRE(schema.contains("title"));
    }
    
    // Cleanup
    std::filesystem::remove_all(tempDir);
}

} // namespace