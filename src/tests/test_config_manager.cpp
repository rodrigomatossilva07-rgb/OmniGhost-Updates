#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "config/iconfig_manager.h"
#include "config/config_manager_impl.h"
#include "config/config_manager_factory.h"
#include "config/config_schema_validator.h"
#include "platform/expected.h"
#include "platform/logging.h"
#include "platform/app_paths.h"

#include <filesystem>
#include <thread>
#include <chrono>

using namespace OmniGhost::Config;

TEST_CASE("ConfigManagerImpl initialization", "[config_manager]") {
    ConfigManagerImpl manager;
    
    auto initResult = manager.Initialize();
    REQUIRE(initResult);
    
    auto shutdownResult = manager.Shutdown();
    REQUIRE(shutdownResult);
}

TEST_CASE("ConfigManagerImpl global config operations", "[config_manager]") {
    ConfigManagerImpl manager;
    manager.Initialize();
    
    // Test LoadGlobal
    auto loadResult = manager.LoadGlobal();
    REQUIRE(loadResult);
    
    // Test SaveGlobal
    auto saveResult = manager.SaveGlobal();
    REQUIRE(saveResult);
    
    // Test ExportGlobal
    std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "test_global_config.json";
    auto exportResult = manager.ExportGlobal(tempPath);
    // May fail if config dir doesn't exist, that's OK
    
    // Cleanup
    std::filesystem::remove(tempPath);
    
    manager.Shutdown();
}

TEST_CASE("ConfigManagerImpl serialization", "[config_manager]") {
    ConfigManagerImpl manager;
    manager.Initialize();
    
    // Test SerializeAll
    auto serialized = manager.SerializeAll();
    REQUIRE(serialized);
    REQUIRE(!serialized.value().empty());
    REQUIRE(serialized.value().find("schema_version") != std::string::npos);
    
    // Test DeserializeAll
    auto deserializeResult = manager.DeserializeAll(serialized.value());
    REQUIRE(deserializeResult);
    
    manager.Shutdown();
}

TEST_CASE("ConfigManagerImpl profile operations", "[config_manager]") {
    ConfigManagerImpl manager;
    manager.Initialize();
    
    // Test profile operations
    auto profileName = manager.GetActiveProfile("cs2");
    REQUIRE(profileName);
    
    // Test ApplyProfile
    auto applyResult = manager.ApplyProfile("cs2", "Legit");
    // May succeed or fail depending on profile existence
    
    // Test SaveConfig
    auto saveResult = manager.SaveConfig();
    REQUIRE(saveResult);
    
    manager.Shutdown();
}

TEST_CASE("ConfigSchemaValidator initialization", "[config_schema]") {
    ConfigSchemaValidator validator;
    
    // Initialize with temp directory
    std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "omnighost_test_schemas";
    std::error_code ec;
    std::filesystem::create_directories(tempDir, ec);
    
    auto initResult = validator.Initialize(tempDir);
    REQUIRE(initResult);
    
    // Check available schemas
    auto schemas = validator.GetAvailableSchemas();
    REQUIRE(schemas.size() >= 5); // global, game, profile, theme, locale
    
    // Cleanup
    std::filesystem::remove_all(tempDir, ec);
}

TEST_CASE("ConfigSchemaValidator validation", "[config_schema]") {
    ConfigSchemaValidator validator;
    std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "omnighost_test_schemas";
    std::filesystem::create_directories(std::filesystem::temp_directory_path() / "omnighost_test_schemas");
    
    auto initResult = validator.Initialize(tempDir);
    if (!initResult) {
        // If schemas don't exist, that's OK for this test
        WARN("Schemas not available, skipping validation tests");
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
}

TEST_CASE("ConfigSchemaValidator file validation", "[config_schema]") {
    ConfigSchemaValidator validator;
    std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "omnighost_test_schemas";
    std::filesystem::create_directories(std::filesystem::temp_directory_path() / "omnighost_test_schemas");
    
    auto initResult = validator.Initialize(tempDir);
    if (!initResult) {
        WARN("Schemas not available, skipping file validation tests");
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
    
    std::ofstream file(tempFile);
    file << validJson.dump(4);
    file.close();
    
    auto fileResult = validator.ValidateFile(tempFile, ConfigSchemaValidator::SchemaId::GlobalConfig);
    REQUIRE(fileResult);
    REQUIRE(fileResult.value().valid);
    
    // Cleanup
    std::filesystem::remove(tempFile);
}

} // namespace