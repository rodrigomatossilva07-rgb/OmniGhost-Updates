#pragma once

// Auto-Updater Module
// GitHub Releases auto-updater with signature verification

#ifndef OMNIGHOST_AUTO_UPDATER_H
#define OMNIGHOST_AUTO_UPDATER_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>
#include <atomic>
#include <mutex>
#include <optional>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "platform/secure_annotations.h"

namespace omnighost::updater {

// =============================================================================
// Types
// =============================================================================

enum class UpdateChannel : uint8_t {
    Stable = 0,
    Beta = 1,
    Development = 2,
    DevConfig = 3
};

enum class UpdateStatus : uint8_t {
    Idle = 0,
    Checking = 1,
    UpdateAvailable = 2,
    Downloading = 3,
    Verifying = 4,
    Installing = 5,
    Completed = 6,
    Failed = 7,
    UpToDate = 8
};

enum class UpdateType : uint8_t {
    Full = 0,
    Delta = 1,
    Hotfix = 2
};

struct ReleaseInfo {
    std::string version;
    std::string tag_name;
    std::string name;
    std::string body;
    std::string html_url;
    std::string tarball_url;
    std::string zipball_url;
    std::chrono::system_clock::time_point published_at;
    std::chrono::system_clock::time_point created_at;
    bool draft = false;
    bool prerelease = false;
    std::string target_commitish;
    std::vector<std::string> assets;
    std::string sha256;
    std::string signature;
    UpdateType type = UpdateType::Full;
    uint64_t size_bytes = 0;
};

struct UpdateInfo {
    bool update_available = false;
    std::string current_version;
    std::string latest_version;
    ReleaseInfo release;
    UpdateType update_type = UpdateType::Full;
    bool mandatory = false;
    std::string mandatory_reason;
};

struct DownloadProgress {
    uint64_t bytes_downloaded = 0;
    uint64_t total_bytes = 0;
    double percentage = 0.0;
    double speed_bps = 0.0;
    std::chrono::milliseconds eta;
};

struct InstallProgress {
    enum class Stage {
        Preparing,
        Extracting,
        Installing,
        Configuring,
        Finalizing,
        Completed
    };

    Stage stage = Stage::Preparing;
    double percentage = 0.0;
    std::string current_file;
    uint64_t files_processed = 0;
    uint64_t total_files = 0;
};

// =============================================================================
// Configuration
// =============================================================================

struct AutoUpdaterConfig {
    std::string github_owner = "rodrigomatossilva07-rgb";
    std::string github_repo = "OmniGhost-Updates";
    std::string github_token; // Optional, for private repos / higher rate limits
    UpdateChannel channel = UpdateChannel::Stable;
    std::string current_version = "1.0.0";

    // Check settings
    bool auto_check = true;
    uint32_t check_interval_hours = 6;
    bool check_on_startup = true;

    // Download settings
    std::string download_dir;
    uint32_t max_concurrent_downloads = 2;
    uint32_t download_timeout_seconds = 300;
    bool verify_signature = true;
    std::string public_key_pem; // For signature verification

    // Install settings
    bool auto_install = false;
    bool install_on_exit = false;
    bool create_backup_before_install = true;
    bool require_admin = true;

    // Notifications
    bool notify_on_update = true;
    bool silent_mode = false;

    // Proxy
    std::string proxy_url;
    std::string proxy_user;
    std::string proxy_pass;
};

// =============================================================================
// Callbacks
// =============================================================================

using UpdateCheckCallback = std::function<void(const UpdateInfo&)>;
using DownloadProgressCallback = std::function<void(const DownloadProgress&)>;
using InstallProgressCallback = std::function<void(const InstallProgress&)>;
using UpdateCompleteCallback = std::function<void(bool success, const std::string& error)>;

// =============================================================================
// Auto Updater Class
// =============================================================================

class AutoUpdater {
public:
    // Singleton
    static AutoUpdater& instance() {
        static AutoUpdater instance;
        return instance;
    }

    AutoUpdater(const AutoUpdater&) = delete;
    AutoUpdater& operator=(const AutoUpdater&) = delete;
    AutoUpdater(AutoUpdater&&) = delete;
    AutoUpdater& operator=(AutoUpdater&&) = delete;

    // Initialization
    OMNIGHOST_SECURE_FUNC bool initialize(const AutoUpdaterConfig& config = {});
    OMNIGHOST_SECURE_FUNC void shutdown();

    // Configuration
    OMNIGHOST_SECURE_FUNC void set_config(const AutoUpdaterConfig& config);
    OMNIGHOST_SECURE_FUNC const AutoUpdaterConfig& get_config() const;

    // Update checking
    OMNIGHOST_SECURE_FUNC std::optional<UpdateInfo> check_for_updates(bool force = false);
    OMNIGHOST_SECURE_FUNC void check_for_updates_async(UpdateCheckCallback callback);

    // Download
    OMNIGHOST_SECURE_FUNC bool download_update(const ReleaseInfo& release, DownloadProgressCallback progress_cb = nullptr);
    OMNIGHOST_SECURE_FUNC void download_update_async(const ReleaseInfo& release, DownloadProgressCallback progress_cb, UpdateCompleteCallback complete_cb);

    // Installation
    OMNIGHOST_SECURE_FUNC bool install_update(InstallProgressCallback progress_cb = nullptr);
    OMNIGHOST_SECURE_FUNC void install_update_async(InstallProgressCallback progress_cb, UpdateCompleteCallback complete_cb);

    // Rollback
    OMNIGHOST_SECURE_FUNC bool rollback_update();
    OMNIGHOST_SECURE_FUNC bool can_rollback() const;

    // Status
    OMNIGHOST_SECURE_FUNC bool is_initialized() const;
    OMNIGHOST_SECURE_FUNC UpdateStatus get_status() const;
    OMNIGHOST_SECURE_FUNC const UpdateInfo& get_last_update_info() const;
    OMNIGHOST_SECURE_FUNC std::chrono::system_clock::time_point get_last_check_time() const;

    // Scheduling
    OMNIGHOST_SECURE_FUNC void start_auto_check();
    OMNIGHOST_SECURE_FUNC void stop_auto_check();
    OMNIGHOST_SECURE_FUNC void set_check_interval(uint32_t hours);

    // Manual trigger
    OMNIGHOST_SECURE_FUNC void force_check();

private:
    AutoUpdater();
    ~AutoUpdater();

    AutoUpdaterConfig config_;
    bool initialized_ = false;
    std::atomic<UpdateStatus> status_{UpdateStatus::Idle};
    UpdateInfo last_update_info_;
    std::chrono::system_clock::time_point last_check_time_;
    std::mutex mutex_;

    std::thread check_thread_;
    std::thread download_thread_;
    std::thread install_thread_;
    std::atomic<bool> auto_check_running_{false};
    std::thread auto_check_thread_;

    // Callbacks
    UpdateCheckCallback check_callback_;
    DownloadProgressCallback download_callback_;
    InstallProgressCallback install_callback_;
    UpdateCompleteCallback complete_callback_;

    // Workers
    OMNIGHOST_SECURE_FUNC std::optional<UpdateInfo> fetch_latest_release();
    OMNIGHOST_SECURE_FUNC bool verify_signature(const std::string& file_path, const std::string& signature);
    OMNIGHOST_SECURE_FUNC bool verify_checksum(const std::string& file_path, const std::string& expected_sha256);
    OMNIGHOST_SECURE_FUNC std::string download_file(const std::string& url, const std::filesystem::path& dest, std::function<void(uint64_t, uint64_t)> progress);
    OMNIGHOST_SECURE_FUNC bool extract_archive(const std::filesystem::path& archive, const std::filesystem::path& dest);
    OMNIGHOST_SECURE_FUNC bool run_installer(const std::filesystem::path& installer_path);
    OMNIGHOST_SECURE_FUNC bool create_backup();
    OMNIGHOST_SECURE_FUNC bool restore_backup();

    OMNIGHOST_SECURE_FUNC void check_worker();
    OMNIGHOST_SECURE_FUNC void download_worker(const ReleaseInfo& release);
    OMNIGHOST_SECURE_FUNC void install_worker();

    // GitHub API
    OMNIGHOST_SECURE_FUNC std::optional<nlohmann::json> github_api_request(const std::string& endpoint);
    OMNIGHOST_SECURE_FUNC std::vector<ReleaseInfo> parse_releases(const nlohmann::json& json);
    OMNIGHOST_SECURE_FUNC std::optional<ReleaseInfo> get_latest_release(const std::vector<ReleaseInfo>& releases);
};

} // namespace omnighost::updater

#endif // OMNIGHOST_AUTO_UPDATER_H
