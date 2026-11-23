#pragma once

#include "platform/expected.h"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace OmniGhost::Config {





class IConfigManager {
public:
    virtual ~IConfigManager() = default;


    virtual Expected<void> Initialize() = 0;
    virtual Expected<void> Shutdown() = 0;


    virtual Expected<void> LoadGlobal() = 0;
    virtual Expected<void> SaveGlobal() = 0;
    virtual Expected<void> ExportGlobal(const std::filesystem::path& path) = 0;
    virtual Expected<void> ImportGlobal(const std::filesystem::path& path) = 0;


    virtual Expected<void> ActivateCurrentGameConfig() = 0;
    virtual Expected<void> LoadGameConfig(std::string_view gameId) = 0;
    virtual Expected<void> SaveGameConfig(std::string_view gameId) = 0;


    virtual Expected<void> ApplyProfile(std::string_view gameId, std::string_view profileName) = 0;
    virtual Expected<std::string> GetActiveProfile(std::string_view gameId) const = 0;


    virtual Expected<std::string> SerializeAll() const = 0;
    virtual Expected<void> DeserializeAll(std::string_view json) = 0;


    virtual std::filesystem::path GetConfigDirectory() const noexcept = 0;
    [[nodiscard]] virtual const char* ActiveConfigName() const noexcept = 0;


    virtual Expected<bool> IsDevConfigAvailable() const = 0;
    virtual Expected<std::vector<std::string>> ListDevConfigs() const = 0;
    virtual Expected<void> DownloadDevConfig(std::string_view configName) = 0;
    virtual Expected<void> UploadDevConfig(std::string_view configName) = 0;
    virtual Expected<void> DeleteDevConfig(std::string_view configName) = 0;
};





class IConfigManagerFactory {
public:
    virtual ~IConfigManagerFactory() = default;
    virtual Expected<std::unique_ptr<IConfigManager>> Create() = 0;
};

}