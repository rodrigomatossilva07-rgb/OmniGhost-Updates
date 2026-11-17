#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <vector>

#include "app_settings.h"
#include "config_manager.h"

namespace OmniGhost::Config {

// =============================================================================
// Configuration Hot-Reload
// =============================================================================
// Uses std::filesystem::file_time_type for cross-platform file watching
// (C++20 std::filesystem::watch would be ideal but not yet widely supported)

struct HotReloadConfig {
    std::chrono::milliseconds pollInterval{1000};  // How often to check for changes
    bool enabled{true};
    bool requireValidation{true};
    std::function<void(std::string_view)> onReloadSuccess;
    std::function<void(std::string_view)> onReloadFailure;
    std::function<bool(const std::string&)> customValidator;
};

struct ConfigFileState {
    std::filesystem::path path;
    std::filesystem::file_time_type lastWriteTime;
    std::size_t lastSize = 0;
    bool exists = false;
};

class ConfigHotReloader {
public:
    explicit ConfigHotReloader(HotReloadConfig config = {});
    ~ConfigHotReloader();

    // Start/stop the hot-reload thread
    void Start();
    void Stop();

    // Add a config file to watch
    // Returns watch ID for later reference
    [[nodiscard]] std::size_t WatchFile(std::filesystem::path path,
                                        std::function<void()> reloadCallback);

    // Remove a watched file
    bool UnwatchFile(std::size_t watchId);

    // Manually trigger reload for all watched files
    void TriggerReload();

    // Get current state
    [[nodiscard]] bool IsRunning() const noexcept { return running_.load(std::memory_order_acquire); }
    [[nodiscard]] std::size_t WatchedFileCount() const noexcept;

    // Get last reload status
    struct Status {
        bool lastReloadSuccess = false;
        std::string lastError;
        std::chrono::system_clock::time_point lastReloadTime;
        std::size_t totalReloads = 0;
        std::size_t failedReloads = 0;
    };
    [[nodiscard]] Status GetStatus() const noexcept;

private:
    void ReloadThread();
    void CheckAndReload();

    HotReloadConfig config_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    std::thread reloadThread_;
    std::mutex mutex_;

    struct WatchedFile {
        std::size_t id;
        std::filesystem::path path;
        ConfigFileState state;
        std::function<void()> callback;
    };
    std::vector<WatchedFile> watchedFiles_;
    std::size_t nextWatchId_ = 1;

    Status status_;
};

// =============================================================================
// Global Hot-Reload Manager
// =============================================================================

class ConfigHotReloadManager {
public:
    static ConfigHotReloadManager& Instance() noexcept {
        static ConfigHotReloadManager instance;
        return instance;
    }

    // Initialize with default config files
    bool Initialize();

    // Shutdown
    void Shutdown();

    // Start/stop all
    void StartAll();
    void StopAll();

    // Add custom config file to watch
    [[nodiscard]] std::size_t WatchConfig(std::filesystem::path path,
                                          std::function<void()> reloadCallback);

    // Remove watched config
    bool UnwatchConfig(std::size_t watchId);

    // Get reloader instance
    [[nodiscard]] ConfigHotReloader* GetReloader() noexcept { return &reloader_; }

    // Check if initialized
    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_; }

private:
    ConfigHotReloadManager() = default;
    ~ConfigHotReloadManager() { Shutdown(); }

    ConfigHotReloader reloader_;
    std::atomic<bool> initialized_{false};
};

// =============================================================================
// Convenience Functions
// =============================================================================

// Initialize default config hot-reload (watches global.json, game configs, etc.)
inline bool InitializeConfigHotReload() {
    return ConfigHotReloadManager::Instance().Initialize();
}

// Shutdown config hot-reload
inline void ShutdownConfigHotReload() {
    ConfigHotReloadManager::Instance().Shutdown();
}

// =============================================================================
// Integration with existing config system
// =============================================================================

// Called when a config file changes - reloads the appropriate config
void OnGlobalConfigChanged();
void OnGameConfigChanged(std::string_view gameName);

// Register default config files for hot-reload
void RegisterDefaultConfigFiles();

} // namespace OmniGhost::Config