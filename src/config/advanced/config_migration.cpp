// Config Migration Manager Implementation
// Schema version migration with backward compatibility

#include "config_manager_advanced.h"
#include <functional>

namespace omnighost::config {

// =============================================================================
// ConfigMigrationManager Implementation
// =============================================================================

class ConfigMigrationManager {
public:
    ConfigMigrationManager() {
        register_default_migrations();
    }

    MigrationResult migrate_config(nlohmann::json& config_data, uint32_t current_version, uint32_t target_version) {
        MigrationResult result;
        result.from_version = current_version;
        result.to_version = target_version;

        if (current_version >= target_version) {
            result.success = true;
            return result;
        }

        if (current_version == 0) {
            // Fresh config, initialize with current schema
            return migrate_from_zero(config_data, target_version);
        }

        // Find migration path
        auto path = find_migration_path(current_version, target_version);
        if (path.empty()) {
            result.error_message = "No migration path from v" + std::to_string(current_version) + " to v" + std::to_string(target_version);
            return result;
        }

        // Apply migrations sequentially
        for (const auto& step : path) {
            result.applied_steps.push_back(step.description);
            if (!step.migrator(config_data)) {
                result.failed_steps.push_back(step.description);
                result.success = false;
                result.error_message = "Migration step failed: " + step.description;
                return result;
            }
            result.applied_count++;
        }

        result.success = true;
        result.to_version = target_version;
        return result;
    }

    MigrationResult migrate_from_zero(nlohmann::json& config_data, uint32_t target_version) {
        MigrationResult result;
        result.from_version = 0;
        result.to_version = target_version;

        // Initialize with default schema
        nlohmann::json defaults = get_default_config();
        config_data = defaults;
        config_data["schema_version"] = target_version;

        result.success = true;
        result.applied_count = 1;
        result.applied_steps.push_back("Initialized with default schema v" + std::to_string(target_version));
        return result;
    }

    std::vector<uint32_t> get_available_versions() const {
        std::vector<uint32_t> versions;
        for (const auto& step : migration_steps_) {
            if (std::find(versions.begin(), versions.end(), step.from_version) == versions.end()) {
                versions.push_back(step.from_version);
            }
            if (std::find(versions.begin(), versions.end(), step.to_version) == versions.end()) {
                versions.push_back(step.to_version);
            }
        }
        std::sort(versions.begin(), versions.end());
        versions.erase(std::unique(versions.begin(), versions.end()), versions.end());
        return versions;
    }

    uint32_t get_latest_version() const {
        uint32_t latest = 0;
        for (const auto& step : migration_steps_) {
            latest = std::max(latest, step.to_version);
        }
        return latest;
    }

    bool validate_config(const nlohmann::json& config, uint32_t version) {
        // Validate config against schema for version
        return true; // Simplified
    }

    nlohmann::json get_default_config() {
        return nlohmann::json{
            {"schema_version", 1},
            {"aim", {
                {"enabled", false},
                {"fov", 90.0},
                {"smooth", 1.0},
                {"max_distance", 200.0},
                {"target_bones", std::vector<int>{0}},
                {"visibility_check", true},
                {"team_check", true},
                {"prediction", 0.0}
            }},
            {"esp", {
                {"enabled", false},
                {"box", true},
                {"skeleton", false},
                {"health", true},
                {"name", true},
                {"weapon", false},
                {"distance", false},
                {"max_distance", 300.0},
                {"enemy_color", {{"r", 1.0}, {"g", 0.0}, {"b", 0.0}, {"a", 1.0}}},
                {"friendly_color", {{"r", 0.0}, {"g", 1.0}, {"b", 0.0}, {"a", 1.0}}}
            }},
            {"misc", {
                {"bhop", false},
                {"triggerbot", false},
                {"radar", false},
                {"no_flash", false},
                {"no_smoke", false},
                {"third_person", false},
                {"fov_override", 0.0}
            }}
        };
    }

private:
    struct MigrationStepInternal {
        uint32_t from_version;
        uint32_t to_version;
        std::string description;
        std::function<bool(nlohmann::json&)> migrator;
    };

    std::vector<MigrationStepInternal> migration_steps_;

    void register_default_migrations() {
        // v0 -> v1: Initial schema
        migration_steps_.push_back({0, 1, "Initialize default schema",
            [this](nlohmann::json& config) {
                auto defaults = get_default_config();
                config = defaults;
                config["schema_version"] = 1;
                return true;
            }});

        // v1 -> v2: Add prediction field to aim
        migration_steps_.push_back({1, 2, "Add prediction to aim config",
            [](nlohmann::json& config) {
                if (config.contains("aim")) {
                    config["aim"]["prediction"] = 0.0;
                }
                config["schema_version"] = 2;
                return true;
            }});

        // v2 -> v3: Add ESP colors and max_distance
        migration_steps_.push_back({2, 3, "Add ESP colors and max distance",
            [](nlohmann::json& config) {
                if (config.contains("esp")) {
                    if (!config["esp"].contains("enemy_color")) {
                        config["esp"]["enemy_color"] = {{"r", 1.0}, {"g", 0.0}, {"b", 0.0}, {"a", 1.0}};
                    }
                    if (!config["esp"].contains("friendly_color")) {
                        config["esp"]["friendly_color"] = {{"r", 0.0}, {"g", 1.0}, {"b", 0.0}, {"a", 1.0}};
                    }
                    if (!config["esp"].contains("max_distance")) {
                        config["esp"]["max_distance"] = 300.0;
                    }
                }
                config["schema_version"] = 3;
                return true;
            }});

        // v3 -> v4: Add misc settings (bhop, triggerbot, radar, etc.)
        migration_steps_.push_back({3, 4, "Add misc settings",
            [](nlohmann::json& config) {
                if (!config.contains("misc")) {
                    config["misc"] = {
                        {"bhop", false},
                        {"triggerbot", false},
                        {"radar", false},
                        {"no_flash", false},
                        {"no_smoke", false},
                        {"third_person", false},
                        {"fov_override", 0.0}
                    };
                }
                config["schema_version"] = 4;
                return true;
            }});

        // v4 -> v5: Add weapon ESP and distance
        migration_steps_.push_back({4, 5, "Add weapon ESP and distance",
            [](nlohmann::json& config) {
                if (config.contains("esp")) {
                    if (!config["esp"].contains("weapon")) {
                        config["esp"]["weapon"] = false;
                    }
                    if (!config["esp"].contains("distance")) {
                        config["esp"]["distance"] = false;
                    }
                }
                config["schema_version"] = 5;
                return true;
            }});

        // v5 -> v6: Add prediction to aim
        migration_steps_.push_back({5, 6, "Add aim prediction",
            [](nlohmann::json& config) {
                if (config.contains("aim")) {
                    if (!config["aim"].contains("prediction")) {
                        config["aim"]["prediction"] = 0.0;
                    }
                }
                config["schema_version"] = 6;
                return true;
            }});
    }

    std::vector<MigrationStepInternal> find_migration_path(uint32_t from, uint32_t to) {
        std::vector<MigrationStepInternal> path;

        // Simple linear path for now (can be enhanced with graph search)
        uint32_t current = from;
        while (current < to) {
            bool found = false;
            for (const auto& step : migration_steps_) {
                if (step.from_version == current && step.to_version > current && step.to_version <= to) {
                    path.push_back(step);
                    current = step.to_version;
                    found = true;
                    break;
                }
            }
            if (!found) {
                // No direct path, return empty
                return {};
            }
        }
        return path;
    }
};

} // namespace omnighost::config
