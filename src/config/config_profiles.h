#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <chrono>
#include <variant>
#include <optional>
#include <nlohmann/json.hpp>

#include "config_core.h"

namespace OmniGhost::Config {

// =============================================================================
// Profile System - Multiple profiles per game (Legit/Rage/Custom)
// =============================================================================

enum class ProfileType : uint8_t {
    Legit = 0,      // Legitimate-looking settings
    Rage = 1,       // Maximum performance/features
    Custom = 2,     // User-defined
    Default = 3     // Built-in default
};

inline constexpr const char* ProfileTypeToString(ProfileType type) {
    switch (type) {
        case ProfileType::Legit: return "Legit";
        case ProfileType::Rage: return "Rage";
        case ProfileType::Custom: return "Custom";
        case ProfileType::Default: return "Default";
    }
    return "Unknown";
}

inline ProfileType ProfileTypeFromString(std::string_view str) {
    if (str == "Legit" || str == "legit") return ProfileType::Legit;
    if (str == "Rage" || str == "rage") return ProfileType::Rage;
    if (str == "Custom" || str == "custom") return ProfileType::Custom;
    return ProfileType::Default;
}

struct ProfileMetadata {
    std::string name;
    std::string gameId;           // e.g., "cs2", "rust", "fivem"
    ProfileType type = ProfileType::Custom;
    std::string description;
    std::string author;
    std::chrono::system_clock::time_point createdAt;
    std::chrono::system_clock::time_point updatedAt;
    std::uint32_t version = 1;
    std::uint32_t schemaVersion = 1;
    bool builtIn = false;
    bool favorite = false;
    std::vector<std::string> tags;

    // Serialization
    nlohmann::json ToJson() const;
    static ProfileMetadata FromJson(const nlohmann::json& j);
};

struct ProfileData {
    ProfileMetadata metadata;
    nlohmann::json settings;  // Full config settings (ESP, Aimbot, etc.)

    // Serialization
    nlohmann::json ToJson() const;
    static ProfileData FromJson(const nlohmann::json& j);
};

class ProfileManager {
public:
    static ProfileManager& Instance() noexcept {
        static ProfileManager instance;
        return instance;
    }

    // Initialize profile system
    bool Initialize();

    // Shutdown
    void Shutdown();

    // Create a new profile
    [[nodiscard]] bool CreateProfile(const ProfileData& profile);

    // Create profile from current settings
    [[nodiscard]] bool CreateProfileFromCurrent(std::string_view name,
                                                 std::string_view gameId,
                                                 ProfileType type,
                                                 std::string_view description = "");

    // Get profile by name and game
    [[nodiscard]] std::optional<ProfileData> GetProfile(std::string_view gameId,
                                                         std::string_view profileName) const;

    // Get all profiles for a game
    [[nodiscard]] std::vector<ProfileData> GetProfilesForGame(std::string_view gameId) const;

    // Get all profiles (all games)
    [[nodiscard]] std::vector<ProfileData> GetAllProfiles() const;

    // Update existing profile
    [[nodiscard]] bool UpdateProfile(std::string_view gameId,
                                      std::string_view profileName,
                                      const ProfileData& profile);

    // Delete profile
    [[nodiscard]] bool DeleteProfile(std::string_view gameId,
                                      std::string_view profileName);

    // Rename profile
    [[nodiscard]] bool RenameProfile(std::string_view gameId,
                                      std::string_view oldName,
                                      std::string_view newName);

    // Duplicate profile
    [[nodiscard]] bool DuplicateProfile(std::string_view gameId,
                                         std::string_view sourceName,
                                         std::string_view newName);

    // Apply profile to current game settings
    [[nodiscard]] bool ApplyProfile(std::string_view gameId,
                                     std::string_view profileName);

    // Apply profile by type (Legit/Rage/Default) for current game
    [[nodiscard]] bool ApplyProfileByType(ProfileType type);

    // Toggle favorite
    [[nodiscard]] bool ToggleFavorite(std::string_view gameId,
                                       std::string_view profileName);

    // Export profile to file
    [[nodiscard]] bool ExportProfile(std::string_view gameId,
                                      std::string_view profileName,
                                      const std::filesystem::path& outputPath) const;

    // Import profile from file
    [[nodiscard]] bool ImportProfile(const std::filesystem::path& inputPath,
                                      std::string_view newName = "");

    // Import multiple profiles from directory
    [[nodiscard]] std::size_t ImportProfilesFromDirectory(const std::filesystem::path& dirPath);

    // Get profile directory
    [[nodiscard]] std::filesystem::path GetProfileDirectory() const;

    // Get built-in profiles directory
    [[nodiscard]] std::filesystem::path GetBuiltInProfileDirectory() const;

    // Initialize built-in profiles (Legit/Rage/Default for each game)
    void InitializeBuiltInProfiles();

    // Check if profile exists
    [[nodiscard]] bool ProfileExists(std::string_view gameId,
                                      std::string_view profileName) const;

    // Get active profile name for game
    [[nodiscard]] std::optional<std::string> GetActiveProfile(std::string_view gameId) const;

    // Set active profile for game
    void SetActiveProfile(std::string_view gameId,
                          std::string_view profileName);

private:
    ProfileManager() = default;
    ~ProfileManager() { Shutdown(); }
    ProfileManager(const ProfileManager&) = delete;
    ProfileManager& operator=(const ProfileManager&) = delete;

    bool LoadProfilesFromDisk();
    bool SaveProfilesToDisk() const;
    std::filesystem::path GetProfileFilePath(std::string_view gameId,
                                              std::string_view profileName) const;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unordered_map<std::string, ProfileData>> profiles_; // gameId -> profileName -> data
    std::unordered_map<std::string, std::string> activeProfiles_; // gameId -> active profile name
    bool initialized_ = false;
};

// =============================================================================
// Convenience Functions
// =============================================================================

inline ProfileManager& Profiles() {
    return ProfileManager::Instance();
}

inline bool InitializeProfiles() {
    return ProfileManager::Instance().Initialize();
}

inline void ShutdownProfiles() {
    ProfileManager::Instance().Shutdown();
}

// =============================================================================
// Built-in Profile Templates
// =============================================================================

// Create default Legit profile for a game
ProfileData CreateLegitProfile(std::string_view gameId);

// Create default Rage profile for a game
ProfileData CreateRageProfile(std::string_view gameId);

// Create default profile for a game
ProfileData CreateDefaultProfile(std::string_view gameId);

} // namespace OmniGhost::Config