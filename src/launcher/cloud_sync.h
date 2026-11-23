#pragma once

// Cloud Sync for Configs (Launcher-side)
// GitHub Gist / Repository sync with conflict resolution

#ifndef OMNIGHOST_CLOUD_SYNC_H
#define OMNIGHOST_CLOUD_SYNC_H

#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <nlohmann/json.hpp>

namespace omnighost::launcher {

// =============================================================================
// Types
// =============================================================================

enum class SyncProvider : uint8_t {
    GitHubGist = 0,
    GitHubRepo = 1,
    CustomAPI = 2
};

enum class SyncDirection : uint8_t {
    Upload = 0,
    Download = 1,
    Bidirectional = 2
};

enum class ConflictResolution : uint8_t {
    LocalWins = 0,
    RemoteWins = 1,
    NewerWins = 2,
    Manual = 3,
    Merge = 4
};

struct CloudSyncConfig {
    SyncProvider provider = SyncProvider::GitHubGist;
    std::string api_endpoint = "https://api.github.com";
    std::string auth_token;
    std::string repository;
    std::string gist_id;
    std::string branch = "main";
    std::string config_path = "configs/";
    SyncDirection direction = SyncDirection::Bidirectional;
    uint8_t conflict_resolution = 2; // NewerWins
    bool auto_sync = true;
    uint32_t sync_interval_minutes = 30;
    bool encrypt_data = true;
    std::string encryption_key;
    uint32_t max_retries = 3;
    uint32_t timeout_seconds = 30;
};

enum class SyncStatus : uint8_t {
    Idle = 0,
    Checking = 1,
    Downloading = 2,
    Uploading = 3,
    Merging = 4,
    Completed = 5,
    Failed = 6,
    Conflict = 7
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
    uint8_t resolution = 2; // NewerWins
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
    std::vector<std::string> conflict_ids;
};

// =============================================================================
// Cloud Sync Class
// =============================================================================

class CloudSync {
public:
    // Singleton
    static CloudSync& instance() {
        static CloudSync instance;
        return instance;
    }

    CloudSync(const CloudSync&) = delete;
    CloudSync& operator=(const CloudSync&) = delete;
    CloudSync(CloudSync&&) = delete;
    CloudSync& operator=(CloudSync&&) = delete;

    // Initialization
    bool initialize(const CloudSyncConfig& config = {});
    void shutdown();

    void set_config(const CloudSyncConfig& config);
    const CloudSyncConfig& get_config() const;

    // Main sync operations
    bool sync_now(uint8_t direction = 2); // 0=up, 1=down, 2=bi
    void sync_now_async(uint8_t direction, std::function<void(bool, const std::string&)> callback);

    // Status
    uint8_t get_status() const;
    std::string get_status_string() const;
    uint32_t get_pending_uploads() const;
    uint32_t get_pending_downloads() const;
    uint32_t get_conflicts_count() const;
    std::string get_last_error() const;
    uint64_t get_last_sync_time() const;

    // Conflicts
    std::vector<std::string> get_conflict_ids() const;
    bool resolve_conflict(const std::string& config_id, const std::string& key, const nlohmann::json& resolved_value);

    // Auto-sync
    void start_auto_sync();
    void stop_auto_sync();
    void set_sync_interval(uint32_t minutes);

    // Manual trigger
    void force_sync();

private:
    CloudSync();
    ~CloudSync();

    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

// Global access
inline CloudSync& cloud_sync() {
    return CloudSync::instance();
}

} // namespace omnighost::launcher

#endif // OMNIGHOST_CLOUD_SYNC_H
