#pragma once

#include "platform/expected.h"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace OmniGhost::Config {

// =============================================================================
// IConfigManager - Interface for configuration management
// =============================================================================

class IConfigManager {
public:
    virtual ~IConfigManager() = default;

    // Initialization
    virtual Expected<void> Initialize() = 0;
    virtual Expected<void> Shutdown() = 0;

    // Global config
    virtual Expected<void> LoadGlobal() = 0;
    virtual Expected<void> SaveGlobal() = 0;
    virtual Expected<void> ExportGlobal(const std::filesystem::path& path) = 0;
    virtual Expected<void> ImportGlobal(const std::filesystem::path& path) = 0;

    // Game-specific config
    virtual Expected<void> ActivateCurrentGameConfig() = 0;
    virtual Expected<void> LoadGameConfig(std::string_view gameId) = 0;
    virtual Expected<void> SaveGameConfig(std::string_view gameId) = 0;

    // Profile management
    virtual Expected<void> ApplyProfile(std::string_view gameId, std::string_view profileName) = 0;
    virtual Expected<std::string> GetActiveProfile(std::string_view gameId) const = 0;

    // Serialization
    virtual Expected<std::string> SerializeAll() const = 0;
    virtual Expected<void> DeserializeAll(std::string_view json) = 0;

    // Utility
    virtual std::filesystem::path GetConfigDirectory() const noexcept = 0;
    [[nodiscard]] virtual const char* ActiveConfigName() const noexcept = 0;
};

// =============================================================================
// Factory for DI registration
// =============================================================================

class IConfigManagerFactory {
public:
    virtual ~IConfigManagerFactory() = default;
    virtual Expected<std::unique_ptr<IConfigManager>> Create() = 0;
};

} // namespace OmniGhost::Config