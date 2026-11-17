#include "config_profiles.h"

#include "config_core.h"
#include "app_paths.h"
#include "logging.h"

#include <fstream>
#include <algorithm>
#include <filesystem>

namespace OmniGhost::Config {

// =============================================================================
// ProfileMetadata Serialization
// =============================================================================

nlohmann::json ProfileMetadata::ToJson() const {
    nlohmann::json j;
    j["name"] = name;
    j["gameId"] = gameId;
    j["type"] = static_cast<int>(type);
    j["typeStr"] = ProfileTypeToString(type);
    j["description"] = description;
    j["author"] = author;
    j["createdAt"] = std::chrono::duration_cast<std::chrono::seconds>(
        createdAt.time_since_epoch()).count();
    j["updatedAt"] = std::chrono::duration_cast<std::chrono::seconds>(
        updatedAt.time_since_epoch()).count();
    j["version"] = version;
    j["schemaVersion"] = schemaVersion;
    j["builtIn"] = builtIn;
    j["favorite"] = favorite;
    j["tags"] = tags;
    return j;
}

ProfileMetadata ProfileMetadata::FromJson(const nlohmann::json& j) {
    ProfileMetadata m;
    m.name = j.value("name", "");
    m.gameId = j.value("gameId", "");
    m.type = static_cast<ProfileType>(j.value("type", static_cast<int>(ProfileType::Custom)));
    m.description = j.value("description", "");
    m.author = j.value("author", "");
    m.createdAt = std::chrono::system_clock::time_point(
        std::chrono::seconds(j.value("createdAt", 0)));
    m.updatedAt = std::chrono::system_clock::time_point(
        std::chrono::seconds(j.value("updatedAt", 0)));
    m.version = j.value("version", 1);
    m.schemaVersion = j.value("schemaVersion", 1);
    m.builtIn = j.value("builtIn", false);
    m.favorite = j.value("favorite", false);
    m.tags = j.value("tags", std::vector<std::string>{});
    return m;
}

// =============================================================================
// ProfileData Serialization
// =============================================================================

nlohmann::json ProfileData::ToJson() const {
    nlohmann::json j;
    j["metadata"] = metadata.ToJson();
    j["settings"] = settings;
    return j;
}

ProfileData ProfileData::FromJson(const nlohmann::json& j) {
    ProfileData p;
    if (j.contains("metadata")) {
        p.metadata = ProfileMetadata::FromJson(j["metadata"]);
    }
    p.settings = j.value("settings", nlohmann::json::object());
    return p;
}

// =============================================================================
// ProfileManager Implementation
// =============================================================================

bool ProfileManager::Initialize() {
    if (initialized_) {
        return true;
    }

    // Create profile directories
    std::error_code ec;
    std::filesystem::create_directories(GetProfileDirectory(), ec);
    std::filesystem::create_directories(GetBuiltInProfileDirectory(), ec);

    // Load profiles from disk
    LoadProfilesFromDisk();

    // Initialize built-in profiles
    InitializeBuiltInProfiles();

    initialized_ = true;
    OGS_LOG_INFO("[ProfileManager] Initialized with {} game(s) profiles",
                 profiles_.size());
    return true;
}

void ProfileManager::Shutdown() {
    if (!initialized_) {
        return;
    }
    SaveProfilesToDisk();
    initialized_ = false;
    OGS_LOG_INFO("[ProfileManager] Shutdown");
}

bool ProfileManager::CreateProfile(const ProfileData& profile) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto& gameId = profile.metadata.gameId;
    const auto& profileName = profile.metadata.name;

    if (profileName.empty() || gameId.empty()) {
        OGS_LOG_ERROR("[ProfileManager] Invalid profile: empty name or gameId");
        return false;
    }

    auto& gameProfiles = profiles_[gameId];
    if (gameProfiles.contains(profileName)) {
        OGS_LOG_WARN("[ProfileManager] Profile already exists: {} for {}", profileName, gameId);
        return false;
    }

    // Save to disk
    std::filesystem::path filePath = GetProfileFilePath(gameId, profileName);
    nlohmann::json j = profile.ToJson();

    std::ofstream file(filePath);
    if (!file) {
        OGS_LOG_ERROR("[ProfileManager] Failed to save profile: {}", filePath.string());
        return false;
    }
    file << j.dump(4);

    // Add to memory
    gameProfiles[profileName] = profile;
    OGS_LOG_INFO("[ProfileManager] Created profile: {} for {}", profileName, gameId);
    return true;
}

bool ProfileManager::CreateProfileFromCurrent(std::string_view name,
                                               std::string_view gameId,
                                               ProfileType type,
                                               std::string_view description) {
    ProfileData profile;
    profile.metadata.name = name;
    profile.metadata.gameId = gameId;
    profile.metadata.type = type;
    profile.metadata.description = description;
    profile.metadata.author = "User";
    profile.metadata.createdAt = std::chrono::system_clock::now();
    profile.metadata.updatedAt = std::chrono::system_clock::now();
    profile.metadata.version = 1;
    profile.metadata.schemaVersion = 1;
    profile.metadata.builtIn = false;
    profile.metadata.favorite = false;

    // Serialize current settings
    profile.settings = config_manager::SerializeAllToJson();

    return CreateProfile(profile);
}

std::optional<ProfileData> ProfileManager::GetProfile(std::string_view gameId,
                                                       std::string_view profileName) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto gameIt = profiles_.find(gameId);
    if (gameIt == profiles_.end()) {
        return std::nullopt;
    }

    auto profileIt = gameIt->second.find(profileName);
    if (profileIt == gameIt->second.end()) {
        return std::nullopt;
    }

    return profileIt->second;
}

std::vector<ProfileData> ProfileManager::GetProfilesForGame(std::string_view gameId) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<ProfileData> result;
    auto it = profiles_.find(gameId);
    if (it != profiles_.end()) {
        result.reserve(it->second.size());
        for (const auto& [name, profile] : it->second) {
            result.push_back(profile);
        }
    }

    // Sort: favorites first, then by type (Legit, Rage, Default, Custom), then name
    std::sort(result.begin(), result.end(),
        [](const ProfileData& a, const ProfileData& b) {
            if (a.metadata.favorite != b.metadata.favorite) {
                return a.metadata.favorite > b.metadata.favorite;
            }
            if (a.metadata.type != b.metadata.type) {
                return static_cast<int>(a.metadata.type) < static_cast<int>(b.metadata.type);
            }
            return a.metadata.name < b.metadata.name;
        });

    return result;
}

std::vector<ProfileData> ProfileManager::GetAllProfiles() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<ProfileData> result;
    for (const auto& [gameId, gameProfiles] : profiles_) {
        for (const auto& [name, profile] : gameProfiles) {
            result.push_back(profile);
        }
    }
    return result;
}

bool ProfileManager::UpdateProfile(std::string_view gameId,
                                    std::string_view profileName,
                                    const ProfileData& profile) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto gameIt = profiles_.find(gameId);
    if (gameIt == profiles_.end()) {
        return false;
    }

    auto profileIt = gameIt->second.find(profileName);
    if (profileIt == gameIt->second.end()) {
        return false;
    }

    // Update metadata
    ProfileData updated = profile;
    updated.metadata.updatedAt = std::chrono::system_clock::now();
    updated.metadata.version = profileIt->second.metadata.version + 1;

    // Save to disk
    std::filesystem::path filePath = GetProfileFilePath(gameId, profileName);
    nlohmann::json j = updated.ToJson();

    std::ofstream file(filePath);
    if (!file) {
        OGS_LOG_ERROR("[ProfileManager] Failed to save profile: {}", filePath.string());
        return false;
    }
    file << j.dump(4);

    // Update in memory
    profileIt->second = std::move(updated);
    OGS_LOG_INFO("[ProfileManager] Updated profile: {} for {}", profileName, gameId);
    return true;
}

bool ProfileManager::DeleteProfile(std::string_view gameId,
                                    std::string_view profileName) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto gameIt = profiles_.find(gameId);
    if (gameIt == profiles_.end()) {
        return false;
    }

    auto profileIt = gameIt->second.find(profileName);
    if (profileIt == gameIt->second.end()) {
        return false;
    }

    // Don't delete built-in profiles
    if (profileIt->second.metadata.builtIn) {
        OGS_LOG_WARN("[ProfileManager] Cannot delete built-in profile: {}", profileName);
        return false;
    }

    // Remove from disk
    std::filesystem::path filePath = GetProfileFilePath(gameId, profileName);
    std::error_code ec;
    std::filesystem::remove(filePath, ec);

    // Remove from memory
    gameIt->second.erase(profileIt);

    // Clear active profile if it was this one
    auto activeIt = activeProfiles_.find(gameId);
    if (activeIt != activeProfiles_.end() && activeIt->second == profileName) {
        activeProfiles_.erase(activeIt);
    }

    OGS_LOG_INFO("[ProfileManager] Deleted profile: {} for {}", profileName, gameId);
    return true;
}

bool ProfileManager::RenameProfile(std::string_view gameId,
                                    std::string_view oldName,
                                    std::string_view newName) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto gameIt = profiles_.find(gameId);
    if (gameIt == profiles_.end()) {
        return false;
    }

    auto profileIt = gameIt->second.find(oldName);
    if (profileIt == gameIt->second.end()) {
        return false;
    }

    // Check if new name already exists
    if (gameIt->second.contains(newName)) {
        return false;
    }

    // Don't rename built-in profiles
    if (profileIt->second.metadata.builtIn) {
        return false;
    }

    // Create new entry with new name
    ProfileData updated = profileIt->second;
    updated.metadata.name = newName;
    updated.metadata.updatedAt = std::chrono::system_clock::now();

    // Save new file
    std::filesystem::path newFilePath = GetProfileFilePath(gameId, newName);
    nlohmann::json j = updated.ToJson();
    std::ofstream newFile(newFilePath);
    if (!newFile) {
        return false;
    }
    newFile << j.dump(4);

    // Remove old file
    std::filesystem::path oldFilePath = GetProfileFilePath(gameId, oldName);
    std::error_code ec;
    std::filesystem::remove(oldFilePath, ec);

    // Update in memory
    gameIt->second.erase(profileIt);
    gameIt->second[newName] = std::move(updated);

    // Update active profile reference
    auto activeIt = activeProfiles_.find(gameId);
    if (activeIt != activeProfiles_.end() && activeIt->second == oldName) {
        activeIt->second = newName;
    }

    OGS_LOG_INFO("[ProfileManager] Renamed profile: {} -> {} for {}", oldName, newName, gameId);
    return true;
}

bool ProfileManager::DuplicateProfile(std::string_view gameId,
                                       std::string_view sourceName,
                                       std::string_view newName) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto gameIt = profiles_.find(gameId);
    if (gameIt == profiles_.end()) {
        return false;
    }

    auto profileIt = gameIt->second.find(sourceName);
    if (profileIt == gameIt->second.end()) {
        return false;
    }

    if (gameIt->second.contains(newName)) {
        return false;
    }

    // Create duplicate
    ProfileData duplicate = profileIt->second;
    duplicate.metadata.name = newName;
    duplicate.metadata.type = ProfileType::Custom;
    duplicate.metadata.builtIn = false;
    duplicate.metadata.createdAt = std::chrono::system_clock::now();
    duplicate.metadata.updatedAt = std::chrono::system_clock::now();
    duplicate.metadata.version = 1;

    return CreateProfile(duplicate);
}

bool ProfileManager::ApplyProfile(std::string_view gameId,
                                   std::string_view profileName) {
    auto profileOpt = GetProfile(gameId, profileName);
    if (!profileOpt) {
        OGS_LOG_ERROR("[ProfileManager] Profile not found: {} for {}", profileName, gameId);
        return false;
    }

    const auto& profile = *profileOpt;

    // Deserialize settings
    if (!config_manager::DeserializeAllFromJson(profile.settings.dump())) {
        OGS_LOG_ERROR("[ProfileManager] Failed to deserialize profile settings");
        return false;
    }

    // Save as active profile
    SetActiveProfile(gameId, profileName);

    OGS_LOG_INFO("[ProfileManager] Applied profile: {} for {}", profileName, gameId);
    return true;
}

bool ProfileManager::ApplyProfileByType(ProfileType type) {
    // Get current game from config_manager
    std::string currentGame = config_manager::GetCurrentGameName();
    if (currentGame.empty()) {
        return false;
    }

    auto profiles = GetProfilesForGame(currentGame);
    for (const auto& profile : profiles) {
        if (profile.metadata.type == type) {
            return ApplyProfile(currentGame, profile.metadata.name);
        }
    }

    OGS_LOG_WARN("[ProfileManager] No {} profile found for {}", ProfileTypeToString(type), currentGame);
    return false;
}

bool ProfileManager::ToggleFavorite(std::string_view gameId,
                                     std::string_view profileName) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto gameIt = profiles_.find(gameId);
    if (gameIt == profiles_.end()) {
        return false;
    }

    auto profileIt = gameIt->second.find(profileName);
    if (profileIt == gameIt->second.end()) {
        return false;
    }

    profileIt->second.metadata.favorite = !profileIt->second.metadata.favorite;
    profileIt->second.metadata.updatedAt = std::chrono::system_clock::now();

    // Save to disk
    std::filesystem::path filePath = GetProfileFilePath(gameId, profileName);
    nlohmann::json j = profileIt->second.ToJson();
    std::ofstream file(filePath);
    if (!file) {
        return false;
    }
    file << j.dump(4);

    return true;
}

bool ProfileManager::ExportProfile(std::string_view gameId,
                                    std::string_view profileName,
                                    const std::filesystem::path& outputPath) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto gameIt = profiles_.find(gameId);
    if (gameIt == profiles_.end()) {
        return false;
    }

    auto profileIt = gameIt->second.find(profileName);
    if (profileIt == gameIt->second.end()) {
        return false;
    }

    nlohmann::json j = profileIt->second.ToJson();
    std::ofstream file(outputPath);
    if (!file) {
        return false;
    }
    file << j.dump(4);
    return true;
}

bool ProfileManager::ImportProfile(const std::filesystem::path& inputPath,
                                    std::string_view newName) {
    std::ifstream file(inputPath);
    if (!file) {
        return false;
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (...) {
        return false;
    }

    ProfileData profile = ProfileData::FromJson(j);

    // Override name if provided
    if (!newName.empty()) {
        profile.metadata.name = newName;
    }

    // Mark as user profile
    profile.metadata.builtIn = false;
    profile.metadata.updatedAt = std::chrono::system_clock::now();

    return CreateProfile(profile);
}

std::size_t ProfileManager::ImportProfilesFromDirectory(const std::filesystem::path& dirPath) {
    std::size_t count = 0;
    std::error_code ec;

    for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            if (ImportProfile(entry.path())) {
                count++;
            }
        }
    }

    return count;
}

std::filesystem::path ProfileManager::GetProfileDirectory() const {
    return OmniGhost::Paths::ConfigDirectory() / "profiles";
}

std::filesystem::path ProfileManager::GetBuiltInProfileDirectory() const {
    return OmniGhost::Paths::InstallDirectory() / "data" / "profiles";
}

void ProfileManager::InitializeBuiltInProfiles() {
    const std::vector<std::string> games = {"cs2", "rust", "fivem", "fortnite", "warzone", "apex"};

    for (const auto& game : games) {
        // Check if built-in profiles already exist
        bool hasLegit = ProfileExists(game, "Legit");
        bool hasRage = ProfileExists(game, "Rage");
        bool hasDefault = ProfileExists(game, "Default");

        if (!hasLegit) {
            ProfileData legit = CreateLegitProfile(game);
            legit.metadata.builtIn = true;
            CreateProfile(legit);
        }

        if (!hasRage) {
            ProfileData rage = CreateRageProfile(game);
            rage.metadata.builtIn = true;
            CreateProfile(rage);
        }

        if (!hasDefault) {
            ProfileData def = CreateDefaultProfile(game);
            def.metadata.builtIn = true;
            CreateProfile(def);
        }
    }
}

bool ProfileManager::ProfileExists(std::string_view gameId,
                                    std::string_view profileName) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto gameIt = profiles_.find(gameId);
    if (gameIt == profiles_.end()) {
        return false;
    }

    return gameIt->second.contains(profileName);
}

std::optional<std::string> ProfileManager::GetActiveProfile(std::string_view gameId) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = activeProfiles_.find(gameId);
    if (it != activeProfiles_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void ProfileManager::SetActiveProfile(std::string_view gameId,
                                       std::string_view profileName) {
    std::lock_guard<std::mutex> lock(mutex_);
    activeProfiles_[gameId] = profileName;
}

bool ProfileManager::LoadProfilesFromDisk() {
    std::filesystem::path profileDir = GetProfileDirectory();
    if (!std::filesystem::exists(profileDir)) {
        return true;
    }

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(profileDir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            try {
                std::ifstream file(entry.path());
                nlohmann::json j;
                file >> j;

                ProfileData profile = ProfileData::FromJson(j);
                if (!profile.metadata.name.empty() && !profile.metadata.gameId.empty()) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    profiles_[profile.metadata.gameId][profile.metadata.name] = std::move(profile);
                }
            } catch (...) {
                OGS_LOG_WARN("[ProfileManager] Failed to load profile: {}", entry.path().string());
            }
        }
    }

    // Also load built-in profiles
    std::filesystem::path builtInDir = GetBuiltInProfileDirectory();
    if (std::filesystem::exists(builtInDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(builtInDir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                try {
                    std::ifstream file(entry.path());
                    nlohmann::json j;
                    file >> j;

                    ProfileData profile = ProfileData::FromJson(j);
                    if (!profile.metadata.name.empty() && !profile.metadata.gameId.empty()) {
                        std::lock_guard<std::mutex> lock(mutex_);
                        profiles_[profile.metadata.gameId][profile.metadata.name] = std::move(profile);
                    }
                } catch (...) {
                    // Ignore built-in profile load errors
                }
            }
        }
    }

    return true;
}

bool ProfileManager::SaveProfilesToDisk() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::error_code ec;
    std::filesystem::create_directories(GetProfileDirectory(), ec);

    for (const auto& [gameId, gameProfiles] : profiles_) {
        for (const auto& [name, profile] : gameProfiles) {
            // Only save user profiles (not built-in)
            if (profile.metadata.builtIn) {
                continue;
            }

            std::filesystem::path filePath = GetProfileFilePath(gameId, name);
            nlohmann::json j = profile.ToJson();

            std::ofstream file(filePath);
            if (!file) {
                OGS_LOG_ERROR("[ProfileManager] Failed to save profile: {}", filePath.string());
                continue;
            }
            file << j.dump(4);
        }
    }

    return true;
}

std::filesystem::path ProfileManager::GetProfileFilePath(std::string_view gameId,
                                                          std::string_view profileName) const {
    // Sanitize filename
    std::string safeName = profileName;
    std::replace(safeName.begin(), safeName.end(), '/', '_');
    std::replace(safeName.begin(), safeName.end(), '\\', '_');

    return GetProfileDirectory() / gameId / (safeName + ".json");
}

// =============================================================================
// Built-in Profile Templates
// =============================================================================

ProfileData CreateLegitProfile(std::string_view gameId) {
    ProfileData profile;
    profile.metadata.name = "Legit";
    profile.metadata.gameId = gameId;
    profile.metadata.type = ProfileType::Legit;
    profile.metadata.description = "Configuração legítima para jogo competitivo";
    profile.metadata.author = "OmniGhost";
    profile.metadata.createdAt = std::chrono::system_clock::now();
    profile.metadata.updatedAt = std::chrono::system_clock::now();
    profile.metadata.version = 1;
    profile.metadata.schemaVersion = 1;
    profile.metadata.builtIn = true;
    profile.metadata.favorite = false;
    profile.metadata.tags = {"legit", "competitive", "low-fov"};

    // Legit settings (low FOV, smooth aim, minimal visuals)
    profile.settings = nlohmann::json::object({
        {"esp_enabled", true},
        {"esp_box", true},
        {"esp_health", true},
        {"esp_name", true},
        {"esp_distance", true},
        {"esp_skeleton", false},
        {"esp_head_dot", false},
        {"esp_fov", 90.0f},
        {"esp_max_distance", 300.0f},
        {"aim_enabled", true},
        {"aim_fov", 3.0f},
        {"aim_smooth", 15.0f},
        {"aim_bone", 0},  // Head
        {"aim_rcs", true},
        {"aim_rcs_x", 1.0f},
        {"aim_rcs_y", 1.0f},
        {"trigger_enabled", false},
        {"trigger_delay", 50},
        {"radar_enabled", true},
        {"radar_range", 200.0f}
    });

    return profile;
}

ProfileData CreateRageProfile(std::string_view gameId) {
    ProfileData profile;
    profile.metadata.name = "Rage";
    profile.metadata.gameId = gameId;
    profile.metadata.type = ProfileType::Rage;
    profile.metadata.description = "Configuração máxima para dominação total";
    profile.metadata.author = "OmniGhost";
    profile.metadata.createdAt = std::chrono::system_clock::now();
    profile.metadata.updatedAt = std::chrono::system_clock::now();
    profile.metadata.version = 1;
    profile.metadata.schemaVersion = 1;
    profile.metadata.builtIn = true;
    profile.metadata.favorite = false;
    profile.metadata.tags = {"rage", "hvh", "max-features"};

    // Rage settings (high FOV, no smooth, all features)
    profile.settings = nlohmann::json::object({
        {"esp_enabled", true},
        {"esp_box", true},
        {"esp_health", true},
        {"esp_name", true},
        {"esp_distance", true},
        {"esp_skeleton", true},
        {"esp_head_dot", true},
        {"esp_fov", 180.0f},
        {"esp_max_distance", 500.0f},
        {"aim_enabled", true},
        {"aim_fov", 180.0f},
        {"aim_smooth", 1.0f},
        {"aim_bone", 0},
        {"aim_rcs", true},
        {"aim_rcs_x", 2.0f},
        {"aim_rcs_y", 2.0f},
        {"aim_multipoint", true},
        {"aim_resolver", true},
        {"trigger_enabled", true},
        {"trigger_delay", 0},
        {"trigger_autowall", true},
        {"radar_enabled", true},
        {"radar_range", 500.0f},
        {"misc_bhop", true},
        {"misc_autostrafe", true}
    });

    return profile;
}

ProfileData CreateDefaultProfile(std::string_view gameId) {
    ProfileData profile;
    profile.metadata.name = "Default";
    profile.metadata.gameId = gameId;
    profile.metadata.type = ProfileType::Default;
    profile.metadata.description = "Configuração padrão equilibrada";
    profile.metadata.author = "OmniGhost";
    profile.metadata.createdAt = std::chrono::system_clock::now();
    profile.metadata.updatedAt = std::chrono::system_clock::now();
    profile.metadata.version = 1;
    profile.metadata.schemaVersion = 1;
    profile.metadata.builtIn = true;
    profile.metadata.favorite = true;  // Default is favorite
    profile.metadata.tags = {"default", "balanced"};

    // Default settings (balanced)
    profile.settings = nlohmann::json::object({
        {"esp_enabled", true},
        {"esp_box", true},
        {"esp_health", true},
        {"esp_name", true},
        {"esp_distance", false},
        {"esp_skeleton", false},
        {"esp_head_dot", false},
        {"esp_fov", 120.0f},
        {"esp_max_distance", 400.0f},
        {"aim_enabled", true},
        {"aim_fov", 10.0f},
        {"aim_smooth", 8.0f},
        {"aim_bone", 0},
        {"aim_rcs", true},
        {"aim_rcs_x", 1.5f},
        {"aim_rcs_y", 1.5f},
        {"trigger_enabled", false},
        {"trigger_delay", 30},
        {"radar_enabled", true},
        {"radar_range", 300.0f}
    });

    return profile;
}

} // namespace OmniGhost::Config