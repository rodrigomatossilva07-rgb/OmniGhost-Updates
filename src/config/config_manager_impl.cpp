#include "iconfig_manager.h"
#include "config_manager.h"
#include "config_manager_factory.h"
#include "app_settings.h"
#include "platform/expected.h"
#include "platform/logging.h"

#include <filesystem>

namespace OmniGhost::Config {





class ConfigManagerImpl final : public IConfigManager {
public:
    ConfigManagerImpl() = default;
    ~ConfigManagerImpl() override = default;

    Expected<void> Initialize() override {
        OGS_LOG_INFO("[ConfigManagerImpl] Initializing...");
        config_manager::Initialize();
        OGS_LOG_INFO("[ConfigManagerImpl] Initialized");
        return Success();
    }

    Expected<void> Shutdown() override {
        OGS_LOG_INFO("[ConfigManagerImpl] Shutting down...");

        config_manager::FlushActiveConfig();
        OGS_LOG_INFO("[ConfigManagerImpl] Shutdown complete");
        return Success();
    }

    Expected<void> LoadGlobal() override {
        OGS_LOG_INFO("[ConfigManagerImpl] Loading global config...");
        std::string error;
        if (!app_settings::LoadGlobal(&error)) {
            return Failure<void>(ErrorCode::IOError, error);
        }
        return Success();
    }

    Expected<void> SaveGlobal() override {
        OGS_LOG_INFO("[ConfigManagerImpl] Saving global config...");
        std::string error;
        if (!app_settings::SaveGlobal(&error)) {
            return Failure<void>(ErrorCode::IOError, error);
        }
        return Success();
    }

    Expected<void> ExportGlobal(const std::filesystem::path& path) override {
        OGS_LOG_INFO("[ConfigManagerImpl] Exporting global config to: {}", path.string());
        std::string error;
        if (!app_settings::ExportGlobal(path, &error)) {
            return Failure<void>(ErrorCode::IOError, error);
        }
        return Success();
    }

    Expected<void> ImportGlobal(const std::filesystem::path& path) override {
        OGS_LOG_INFO("[ConfigManagerImpl] Importing global config from: {}", path.string());
        std::string error;
        if (!app_settings::ImportGlobal(path, &error)) {
            return Failure<void>(ErrorCode::IOError, error);
        }
        return Success();
    }

    Expected<void> ActivateCurrentGameConfig() override {
        OGS_LOG_INFO("[ConfigManagerImpl] Activating current game config...");
        config_manager::ActivateCurrentGameConfig();
        return Success();
    }

    Expected<void> LoadGameConfig(std::string_view gameId) override {
        OGS_LOG_INFO("[ConfigManagerImpl] Loading game config for: {}", gameId);


        config_manager::ActivateCurrentGameConfig();
        return Success();
    }

    Expected<void> SaveGameConfig(std::string_view gameId) override {
        OGS_LOG_INFO("[ConfigManagerImpl] Saving game config for: {}", gameId);
        config_manager::FlushActiveConfig();
        return Success();
    }

    Expected<void> ApplyProfile(std::string_view gameId, std::string_view profileName) override {
        OGS_LOG_INFO("[ConfigManagerImpl] Applying profile '{}' for game: {}", profileName, gameId);
        config_manager::ApplyNamedProfile(profileName.data());
        return Success();
    }

    Expected<std::string> GetActiveProfile(std::string_view gameId) const override {
        const char* profile = config_manager::CurrentGameProfileName();
        return Success(profile ? std::string(profile) : std::string("Default"));
    }

    Expected<std::string> SerializeAll() const override {
        OGS_LOG_INFO("[ConfigManagerImpl] Serializing all config...");
        return Success(config_manager::SerializeAll());
    }

    Expected<void> DeserializeAll(std::string_view json) override {
        OGS_LOG_INFO("[ConfigManagerImpl] Deserializing config...");
        if (!config_manager::DeserializeAll(std::string(json))) {
            return Failure<void>(ErrorCode::ParseError, "Failed to deserialize config");
        }
        return Success();
    }

    std::filesystem::path GetConfigDirectory() const noexcept override {
        return config_manager::GetConfigFolder();
    }

    const char* ActiveConfigName() const noexcept override {
        return config_manager::ActiveConfigName();
    }

    Expected<bool> IsDevConfigAvailable() const override {
        return Success(config_manager::IsDevConfigAvailable());
    }

    Expected<std::vector<std::string>> ListDevConfigs() const override {
        return Success(config_manager::ListDevConfigs());
    }

    Expected<void> DownloadDevConfig(std::string_view configName) override {
        if (config_manager::DownloadDevConfig(std::string(configName))) {
            return Success();
        }
        return Failure<void>(ErrorCode::PermissionDenied, config_manager::GetLastStatus());
    }

    Expected<void> UploadDevConfig(std::string_view configName) override {
        if (config_manager::UploadDevConfig(std::string(configName))) {
            return Success();
        }
        return Failure<void>(ErrorCode::PermissionDenied, config_manager::GetLastStatus());
    }

    Expected<void> DeleteDevConfig(std::string_view configName) override {
        if (config_manager::DeleteDevConfig(std::string(configName))) {
            return Success();
        }
        return Failure<void>(ErrorCode::PermissionDenied, config_manager::GetLastStatus());
    }
};





Expected<std::unique_ptr<IConfigManager>> ConfigManagerFactory::Create() {
    std::unique_ptr<IConfigManager> manager = std::make_unique<ConfigManagerImpl>();
    return Success(std::move(manager));
}

}
