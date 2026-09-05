#include "config_schema.h"
#include "session_log.h"
#include <fstream>
#include <filesystem>

namespace OmniGhost::Platform {

Result<std::unordered_map<std::string, ConfigValue>> ConfigManager::Load() {
    std::lock_guard lock(mutex_);

    if (!std::filesystem::exists(configPath_)) {
        config_.clear();
        dirty_ = false;
        return Ok(std::unordered_map<std::string, ConfigValue>{});
    }

    try {
        std::ifstream file(configPath_);
        if (!file) return Err<std::unordered_map<std::string, ConfigValue>>("Failed to open config file");

        nlohmann::json json;
        file >> json;

        uint32_t fileVersion = json.value("schemaVersion", 0);
        if (fileVersion < schema_.version) {
            auto migrationResult = RunMigrations(json);
            if (migrationResult.IsErr()) return Err<std::unordered_map<std::string, ConfigValue>>(migrationResult.UnwrapErr().message);
        }

        std::unordered_map<std::string, ConfigValue> config;
        for (auto& [key, value] : json.items()) {
            if (key == "schemaVersion") continue;
            config[key] = ConfigValue::FromJson(value);
        }
        schema_.ApplyDefaults(config);

        auto validationResult = schema_.Validate(config);
        if (validationResult.IsErr()) {
            SessionLog::Write(SessionLog::Severity::Warning, SessionLog::Subsystem::Config,
                "Config validation warnings (non-fatal)",
                {{"errors", validationResult.UnwrapErr().message}});
        }

        config_ = std::move(config);
        dirty_ = false;
        return Ok(std::move(config_));

    } catch (const std::exception& e) {
        return Err<std::unordered_map<std::string, ConfigValue>>(std::string("Config parse error: ") + e.what());
    }
}

Result<void> ConfigManager::Save(const std::unordered_map<std::string, ConfigValue>& config) {
    std::lock_guard lock(mutex_);
    try {
        nlohmann::json json;
        json["schemaVersion"] = schema_.version;
        for (const auto& [key, value] : config) json[key] = value.ToJson();

        std::filesystem::path tempPath = configPath_.string() + ".tmp";
        std::ofstream out(tempPath);
        if (!out) return Err<void>("Failed to open temp config file");
        out << json.dump(2);
        out.flush();
        if (!out) return Err<void>("Failed to write config file");
        out.close();

        if (!MoveFileExW(tempPath.wstring().c_str(), configPath_.wstring().c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return Err<void>("Failed to atomically replace config file");
        }

        config_ = config;
        dirty_ = false;
        return Ok();
    } catch (const std::exception& e) {
        return Err<void>(std::string("Config save error: ") + e.what());
    }
}

Result<void> ConfigManager::Load() { return Load(); }
Result<void> ConfigManager::Save(const std::unordered_map<std::string, ConfigValue>& config) { return Save(config); }
Result<void> ConfigManager::Validate() const noexcept { return schema_.Validate(config_); }
[[nodiscard]] uint32_t ConfigManager::GetSchemaVersion() const noexcept { return schema_.version; }
[[nodiscard]] const std::filesystem::path& ConfigManager::GetPath() const noexcept { return configPath_; }
void ConfigManager::AddMigration(Migration m) {
    migrations_.push_back(std::move(m));
    std::sort(migrations_.begin(), migrations_.end(),
        [](const Migration& a, const Migration& b) { return a.fromVersion < b.fromVersion; });
}
Result<void> ConfigManager::Validate() const noexcept { return schema_.Validate(config_); }
bool ConfigManager::IsDirty() const noexcept { return dirty_; }

} // namespace OmniGhost::Platform