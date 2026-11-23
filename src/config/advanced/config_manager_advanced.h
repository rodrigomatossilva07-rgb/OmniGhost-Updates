#pragma once

// Advanced Config Manager
// Backup/Restore, Cloud Sync, Game Profiles, Migration

#ifndef OMNIGHOST_CONFIG_MANAGER_ADVANCED_H
#define OMNIGHOST_CONFIG_MANAGER_ADVANCED_H

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <filesystem>
#include <optional>
#include <functional>
#include <mutex>
#include <atomic>
#include <nlohmann/json.hpp>
#include "platform/secure_annotations.h"

namespace omnighost::config {

// =============================================================================
// Forward Declarations
// =============================================================================

class ConfigBackupManager;
class ConfigCloudSync;
class GameProfileManager;
class ConfigMigrationManager;

// =============================================================================
// Game Types
// =============================================================================

enum class SupportedGame : uint8_t {
    None = 0,
    CS2 = 1,
    FiveM = 2,
    Warzone = 3,
    Apex = 4,
    Rust = 5,
    Fortnite = 6,
    Valorant = 7,
    Custom = 255
};

inline std::string_view game_to_string(SupportedGame game) {
    switch (game) {
        case SupportedGame::CS2: return "cs2";
        case SupportedGame::FiveM: return "fivem";
        case SupportedGame::Warzone: return "warzone";
        case SupportedGame::Apex: return "apex";
        case SupportedGame::Rust: return "rust";
        case SupportedGame::Fortnite: return "fortnite";
        case SupportedGame::Valorant: return "valorant";
        default: return "unknown";
    }
}

inline SupportedGame string_to_game(std::string_view str) {
    if (str == "cs2") return SupportedGame::CS2;
    if (str == "fivem") return SupportedGame::FiveM;
    if (str == "warzone") return SupportedGame::Warzone;
    if (str == "apex") return SupportedGame::Apex;
    if (str == "rust") return SupportedGame::Rust;
    if (str == "fortnite") return SupportedGame::Fortnite;
    if (str == "valorant") return SupportedGame::Valorant;
    return SupportedGame::None;
}

// =============================================================================
// Config Version & Schema
// =============================================================================

struct ConfigSchema {
    uint32_t version = 1;
    uint32_t min_compatible_version = 1;
    std::string schema_json;
    std::chrono::system_clock::time_point created_at;
    std::string description;
};

struct ConfigMetadata {
    std::string id;
    std::string name;
    SupportedGame game = SupportedGame::None;
    ConfigSchema schema;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point modified_at;
    std::chrono::system_clock::time_point last_synced;
    uint64_t size_bytes = 0;
    std::string checksum_sha256;
    std::string author;
    std::vector<std::string> tags;
    bool is_favorite = false;
    bool is_cloud_synced = false;
    bool is_backup = false;
    uint32_t version = 1;
};

// =============================================================================
// Game Profile
// =============================================================================

struct GameProfile {
    std::string id;
    std::string name;
    SupportedGame game = SupportedGame::None;
    nlohmann::json config_data;
    ConfigMetadata metadata;

    // Game-specific settings
    struct AimSettings {
        bool enabled = false;
        float fov = 90.0f;
        float smooth = 1.0f;
        float max_distance = 200.0f;
        std::vector<int> target_bones = {0}; // Head = 0
        bool visibility_check = true;
        bool team_check = true;
        float prediction = 0.0f;
    } aim;

    struct ESPsSettings {
        bool enabled = false;
        bool box = true;
        bool skeleton = false;
        bool health = true;
        bool name = true;
        bool weapon = false;
        bool distance = false;
        float max_distance = 300.0f;
        struct Color {
            float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
        };
        Color enemy_color = {1.0f, 0.0f, 0.0f, 1.0f};
        Color friendly_color = {0.0f, 1.0f, 0.0f, 1.0f};
    } esp;

    struct MiscSettings {
        bool bhop = false;
        bool triggerbot = false;
        bool radar = false;
        bool no_flash = false;
        bool no_smoke = false;
        bool third_person = false;
        float fov_override = 0.0f;
    } misc;
};

// =============================================================================
// Backup/Restore Types
// =============================================================================

enum class BackupType : uint8_t {
    Manual = 0,
    Automatic = 1,
    PreUpdate = 2,
    PreGameLaunch = 3,
    Periodic = 4,
    CloudSync = 5
};

enum class BackupStatus : uint8_t {
    Pending = 0,
    InProgress = 1,
    Completed = 2,
    Failed = 3,
    Corrupted = 4
};

struct BackupInfo {
    std::string id;
    std::string config_id;
    std::string name;
    BackupType type = BackupType::Manual;
    BackupStatus status = BackupStatus::Pending;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point completed_at;
    std::filesystem::path local_path;
    std::optional<std::string> cloud_url;
    uint64_t size_bytes = 0;
    std::string checksum_sha256;
    std::string error_message;
    nlohmann::json metadata;
};

struct RestoreOptions {
    bool overwrite_existing = true;
    bool preserve_local_changes = false;
    bool create_backup_before_restore = true;
    std::vector<std::string> excluded_keys;
    bool verify_checksum = true;
    bool dry_run = false;
};

struct RestoreResult {
    bool success = false;
    std::string restored_config_id;
    std::vector<std::string> restored_keys;
    std::vector<std::string> skipped_keys;
    std::vector<std::string> conflicts;
    std::string error_message;
    uint32_t restored_count = 0;
    uint32_t failed_count = 0;
    std::chrono::milliseconds duration;
};

// =============================================================================
// Cloud Sync Types
// =============================================================================

enum class SyncDirection : uint8_t {
    Upload = 0,
    Download = 1,
    Bidirectional = 2
};

enum class SyncConflictResolution : uint8_t {
    LocalWins = 0,
    RemoteWins = 1,
    NewerWins = 1,
    Manual = 2,
    Merge = 3
};

enum class SyncStatus : uint8_t {
    Idle = 0,
    Checking = 1,
    Downloading = 2,
    Uploading = 3,
    Merging = 4,
    Completed = 5,
    Failed = 5,
    Conflict = 6
};

struct SyncConfig {
    std::string provider; // "github", "gist", "custom"
    std::string api_endpoint;
    std::string auth_token;
    std::string repository;
    std::string branch = "main";
    std::string config_path = "configs/";
    SyncDirection direction = SyncDirection::Bidirectional;
    SyncConflictResolution conflict_resolution = SyncConflictResolution::NewerWins;
    bool auto_sync = true;
    uint32_t sync_interval_minutes = 30;
    bool encrypt_data = true;
    std::string encryption_key;
    uint32_t max_retries = 3;
    uint32_t timeout_seconds = 30;
};

struct SyncStatusInfo {
    SyncStatus status = SyncStatus::Idle;
    std::chrono::system_clock::time_point last_sync;
    std::chrono::system_clock::time_point next_sync;
    uint32_t pending_uploads = 0;
    uint32_t pending_downloads = 0;
    uint32_t conflicts = 0;
    std::string last_error;
    uint64_t bytes_uploaded = 0;
    uint64_t bytes_downloaded = 0;
    std::chrono::milliseconds last_sync_duration;
};

struct SyncConflict {
    std::string config_id;
    std::string key;
    nlohmann::json local_value;
    nlohmann::json remote_value;
    std::chrono::system_clock::time_point local_modified;
    std::chrono::system_clock::time_point remote_modified;
    SyncConflictResolution resolution = SyncConflictResolution::NewerWins;
    nlohmann::json resolved_value;
};

struct SyncResult {
    bool success = false;
    uint32_t uploaded = 0;
    uint32_t downloaded = 0;
    uint32_t conflicts = 0;
    uint32_t resolved = 0;
    std::chrono::milliseconds duration;
    std::string error_message;
    std::vector<SyncConflict> conflicts_list;
};

// =============================================================================
// Migration Types
// =============================================================================

struct MigrationStep {
    uint32_t from_version;
    uint32_t to_version;
    std::string description;
    std::function<bool(nlohmann::json&)> migrator;
    bool required = true;
};

struct MigrationResult {
    bool success = false;
    uint32_t from_version = 0;
    uint32_t to_version = 0;
    std::vector<std::string> applied_steps;
    std::vector<std::string> failed_steps;
    std::string error_message;
    uint32_t applied_count = 0;
    uint32_t skipped_count = 0;
};

// =============================================================================
// Main Config Manager Advanced
// =============================================================================

class ConfigManagerAdvanced {
public:
    // Singleton
    static ConfigManagerAdvanced& instance() {
        static ConfigManagerAdvanced instance;
        return instance;
    }

    ConfigManagerAdvanced(const ConfigManagerAdvanced&) = delete;
    ConfigManagerAdvanced& operator=(const ConfigManagerAdvanced&) = delete;
    ConfigManagerAdvanced(ConfigManagerAdvanced&&) = delete;
    ConfigManagerAdvanced& operator=(ConfigManagerAdvanced&&) = delete;

    // Initialization
    OMNIGHOST_SECURE_FUNC bool initialize();
    OMNIGHOST_SECURE_FUNC void shutdown();

    // Config Management
    OMNIGHOST_SECURE_FUNC std::optional<ConfigMetadata> create_config(const std::string& name, SupportedGame game);
    OMNIGHOST_SECURE_FUNC bool delete_config(const std::string& config_id);
    OMNIGHOST_SECURE_FUNC std::optional<ConfigMetadata> get_config(const std::string& config_id);
    OMNIGHOST_SECURE_FUNC std::vector<ConfigMetadata> list_configs(SupportedGame game = SupportedGame::None);
    OMNIGHOST_SECURE_FUNC bool update_config(const std::string& config_id, const nlohmann::json& data);
    OMNIGHOST_SECURE_FUNC nlohmann::json get_config_data(const std::string& config_id);

    // Game Profiles
    OMNIGHOST_SECURE_FUNC std::optional<GameProfile> create_game_profile(const std::string& name, SupportedGame game);
    OMNIGHOST_SECURE_FUNC bool delete_game_profile(const std::string& profile_id);
    OMNIGHOST_SECURE_FUNC std::optional<GameProfile> get_game_profile(const std::string& profile_id);
    OMNIGHOST_SECURE_FUNC std::vector<GameProfile> list_game_profiles(SupportedGame game = SupportedGame::None);
    OMNIGHOST_SECURE_FUNC bool apply_game_profile(const std::string& profile_id);
    OMNIGHOST_SECURE_FUNC std::optional<GameProfile> get_active_profile(SupportedGame game);

    // Backup/Restore
    OMNIGHOST_SECURE_FUNC std::optional<BackupInfo> create_backup(const std::string& config_id, BackupType type, const std::string& name = "");
    OMNIGHOST_SECURE_FUNC std::vector<BackupInfo> list_backups(const std::string& config_id = "");
    OMNIGHOST_SECURE_FUNC RestoreResult restore_backup(const std::string& backup_id, const RestoreOptions& options = {});
    OMNIGHOST_SECURE_FUNC bool delete_backup(const std::string& backup_id);
    OMNIGHOST_SECURE_FUNC std::optional<BackupInfo> get_backup(const std::string& backup_id);

    // Cloud Sync
    OMNIGHOST_SECURE_FUNC bool configure_cloud_sync(const SyncConfig& config);
    OMNIGHOST_SECURE_FUNC std::optional<SyncConfig> get_sync_config();
    OMNIGHOST_SECURE_FUNC SyncResult sync_now(SyncDirection direction = SyncDirection::Bidirectional);
    OMNIGHOST_SECURE_FUNC SyncStatusInfo get_sync_status();
    OMNIGHOST_SECURE_FUNC std::vector<SyncConflict> get_pending_conflicts();
    OMNIGHOST_SECURE_FUNC bool resolve_conflict(const std::string& config_id, const std::string& key, const nlohmann::json& resolved_value);
    OMNIGHOST_SECURE_FUNC void start_auto_sync();
    OMNIGHOST_SECURE_FUNC void stop_auto_sync();

    // Migration
    OMNIGHOST_SECURE_FUNC MigrationResult migrate_config(const std::string& config_id, uint32_t target_version);
    OMNIGHOST_SECURE_FUNC std::vector<uint32_t> get_available_versions(const std::string& config_id);
    OMNIGHOST_SECURE_FUNC uint32_t get_current_schema_version();

    // Import/Export
    OMNIGHOST_SECURE_FUNC std::string export_config(const std::string& config_id, bool include_metadata = true);
    OMNIGHOST_SECURE_FUNC std::optional<std::string> import_config(const std::string& json_data, const std::string& new_name = "");

    // Status
    OMNIGHOST_SECURE_INLINE bool is_initialized() const { return initialized_; }

private:
    ConfigManagerAdvanced() = default;
    ~ConfigManagerAdvanced() = default;

    std::string config_dir_;
    std::string backup_dir_;
    std::string profiles_dir_;
    std::string cloud_sync_dir_;

    // These implementation classes live in separate compatibility units.
    // shared_ptr permits the public manager declaration to remain complete
    // without leaking their private definitions into this header.
    std::shared_ptr<ConfigBackupManager> backup_manager_;
    std::shared_ptr<ConfigCloudSync> cloud_sync_;
    std::shared_ptr<GameProfileManager> profile_manager_;
    std::shared_ptr<ConfigMigrationManager> migration_manager_;

    std::map<std::string, ConfigMetadata> configs_;
    std::map<std::string, GameProfile> game_profiles_;
    std::string active_profile_per_game_[static_cast<size_t>(SupportedGame::Valorant) + 1];

    std::mutex mutex_;
    std::atomic<bool> initialized_{false};

    OMNIGHOST_SECURE_FUNC bool load_configs();
    OMNIGHOST_SECURE_FUNC bool save_config(const std::string& config_id);
    OMNIGHOST_SECURE_FUNC std::string generate_id();
    OMNIGHOST_SECURE_FUNC std::string compute_checksum(const nlohmann::json& data);
    OMNIGHOST_SECURE_FUNC bool ensure_directories();
};

// =============================================================================
// Global Access
// =============================================================================

inline ConfigManagerAdvanced& config_advanced() {
    return ConfigManagerAdvanced::instance();
}

// =============================================================================
// Convenience Macros
// =============================================================================

#define OMNIGHOST_CREATE_CONFIG(name, game) config_advanced().create_config(name, game)
#define OMNIGHOST_GET_CONFIG(id) config_advanced().get_config(id)
#define OMNIGHOST_CREATE_PROFILE(name, game) config_advanced().create_game_profile(name, game)
#define OMNIGHOST_APPLY_PROFILE(id) config_advanced().apply_game_profile(id)
#define OMNIGHOST_CREATE_BACKUP(id, type, name) config_advanced().create_backup(id, type, name)
#define OMNIGHOST_RESTORE_BACKUP(id, opts) config_advanced().restore_backup(id, opts)
#define OMNIGHOST_SYNC_NOW(dir) config_advanced().sync_now(dir)

} // namespace omnighost::config

#endif // OMNIGHOST_CONFIG_MANAGER_ADVANCED_H
