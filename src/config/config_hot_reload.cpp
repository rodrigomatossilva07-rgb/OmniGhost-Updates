#include "config_hot_reload.h"

#include "app_paths.h"
#include "logging.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

namespace OmniGhost::Config {

// =============================================================================
// ConfigHotReloader Implementation
// =============================================================================

ConfigHotReloader::ConfigHotReloader(HotReloadConfig config)
    : config_(std::move(config)) {}

ConfigHotReloader::~ConfigHotReloader() {
    Stop();
}

void ConfigHotReloader::Start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return; // Already running
    }
    stopRequested_.store(false, std::memory_order_release);
    reloadThread_ = std::thread(&ConfigHotReloader::ReloadThread, this);
    OGS_LOG_INFO("[ConfigHotReload] Started watching {} file(s)", watchedFiles_.size());
}

void ConfigHotReloader::Stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        return; // Not running
    }
    stopRequested_.store(true, std::memory_order_release);
    if (reloadThread_.joinable()) {
        reloadThread_.join();
    }
    OGS_LOG_INFO("[ConfigHotReload] Stopped");
}

std::size_t ConfigHotReloader::WatchFile(std::filesystem::path path,
                                         std::function<void()> reloadCallback) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::error_code ec;
    ConfigFileState state;
    state.path = path;
    state.exists = std::filesystem::exists(path, ec);
    if (state.exists) {
        state.lastWriteTime = std::filesystem::last_write_time(path, ec);
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (file) {
            state.lastSize = static_cast<std::size_t>(file.tellg());
        }
    }

    const std::size_t id = nextWatchId_++;
    watchedFiles_.push_back({id, std::move(path), std::move(state), std::move(reloadCallback)});

    OGS_LOG_DEBUG("[ConfigHotReload] Watching file: {} (id={})", watchedFiles_.back().path.string(), id);
    return id;
}

bool ConfigHotReloader::UnwatchFile(std::size_t watchId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = std::find_if(watchedFiles_.begin(), watchedFiles_.end(),
                           [watchId](const auto& wf) { return wf.id == watchId; });

    if (it != watchedFiles_.end()) {
        OGS_LOG_DEBUG("[ConfigHotReload] Unwatching file: {} (id={})", it->path.string(), watchId);
        watchedFiles_.erase(it);
        return true;
    }
    return false;
}

void ConfigHotReloader::TriggerReload() {
    CheckAndReload();
}

std::size_t ConfigHotReloader::WatchedFileCount() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return watchedFiles_.size();
}

ConfigHotReloader::Status ConfigHotReloader::GetStatus() const noexcept {
    return status_;
}

void ConfigHotReloader::ReloadThread() {
    while (!stopRequested_.load(std::memory_order_acquire)) {
        CheckAndReload();
        std::this_thread::sleep_for(config_.pollInterval);
    }
}

void ConfigHotReloader::CheckAndReload() {
    std::vector<WatchedFile*> toReload;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        for (auto& watched : watchedFiles_) {
            std::error_code ec;
            bool exists = std::filesystem::exists(watched.path, ec);

            if (!exists) {
                if (watched.state.exists) {
                    // File was deleted
                    watched.state.exists = false;
                    OGS_LOG_WARN("[ConfigHotReload] Config file deleted: {}", watched.path.string());
                    if (config_.onReloadFailure) {
                        config_.onReloadFailure("File deleted: " + watched.path.string());
                    }
                }
                continue;
            }

            auto currentWriteTime = std::filesystem::last_write_time(watched.path, ec);
            if (ec) {
                continue;
            }

            // Check if file size changed (quick check)
            std::ifstream file(watched.path, std::ios::binary | std::ios::ate);
            std::size_t currentSize = file ? static_cast<std::size_t>(file.tellg()) : 0;

            if (currentWriteTime != watched.state.lastWriteTime ||
                currentSize != watched.state.lastSize) {

                // File changed
                watched.state.lastWriteTime = currentWriteTime;
                watched.state.lastSize = currentSize;
                watched.state.exists = true;

                toReload.push_back(&watched);
            } else if (!watched.state.exists) {
                // File was recreated
                watched.state.exists = true;
                watched.state.lastWriteTime = currentWriteTime;
                watched.state.lastSize = currentSize;
                toReload.push_back(&watched);
            }
        }
    }

    // Reload changed files (outside lock to avoid deadlock)
    for (auto* watched : toReload) {
        OGS_LOG_INFO("[ConfigHotReload] Reloading config: {}", watched->path.string());

        bool success = false;
        std::string error;

        try {
            // Custom validation if provided
            if (config_.customValidator) {
                std::ifstream file(watched->path);
                std::stringstream buffer;
                buffer << file.rdbuf();
                std::string content = buffer.str();

                if (!config_.customValidator(content)) {
                    error = "Custom validation failed";
                    success = false;
                } else {
                    success = true;
                }
            }

            if (success || !config_.customValidator) {
                // Call the reload callback
                if (watched->callback) {
                    watched->callback();
                }
                success = true;
            }
        } catch (const std::exception& ex) {
            error = ex.what();
            success = false;
        } catch (...) {
            error = "Unknown error";
            success = false;
        }

        // Update status
        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.lastReloadTime = std::chrono::system_clock::now();
            status_.totalReloads++;
            if (success) {
                status_.lastReloadSuccess = true;
                status_.lastError.clear();
                if (config_.onReloadSuccess) {
                    config_.onReloadSuccess(watched->path.string());
                }
                OGS_LOG_INFO("[ConfigHotReload] Successfully reloaded: {}", watched->path.string());
            } else {
                status_.lastReloadSuccess = false;
                status_.lastError = error;
                status_.failedReloads++;
                if (config_.onReloadFailure) {
                    config_.onReloadFailure(error);
                }
                OGS_LOG_ERROR("[ConfigHotReload] Failed to reload {}: {}", watched->path.string(), error);
            }
        }
    }
}

// =============================================================================
// ConfigHotReloadManager Implementation
// =============================================================================

bool ConfigHotReloadManager::Initialize() {
    if (initialized_.load(std::memory_order_acquire)) {
        return true;
    }

    HotReloadConfig config;
    config.pollInterval = std::chrono::seconds(2);
    config.enabled = true;
    config.requireValidation = true;
    config.onReloadSuccess = [](std::string_view path) {
        OGS_LOG_INFO("[ConfigHotReload] Config reloaded: {}", path);
    };
    config.onReloadFailure = [](std::string_view error) {
        OGS_LOG_ERROR("[ConfigHotReload] Reload failed: {}", error);
    };

    reloader_ = ConfigHotReloader(std::move(config));

    // Register default config files
    RegisterDefaultConfigFiles();

    initialized_.store(true, std::memory_order_release);
    OGS_LOG_INFO("[ConfigHotReload] Manager initialized");
    return true;
}

void ConfigHotReloadManager::Shutdown() {
    if (!initialized_.load(std::memory_order_acquire)) {
        return;
    }

    reloader_.Stop();
    initialized_.store(false, std::memory_order_release);
    OGS_LOG_INFO("[ConfigHotReload] Manager shutdown");
}

void ConfigHotReloadManager::StartAll() {
    if (initialized_.load(std::memory_order_acquire)) {
        reloader_.Start();
    }
}

void ConfigHotReloadManager::StopAll() {
    reloader_.Stop();
}

std::size_t ConfigHotReloadManager::WatchConfig(std::filesystem::path path,
                                                std::function<void()> reloadCallback) {
    if (!initialized_.load(std::memory_order_acquire)) {
        Initialize();
    }
    return reloader_.WatchFile(std::move(path), std::move(reloadCallback));
}

bool ConfigHotReloadManager::UnwatchConfig(std::size_t watchId) {
    return reloader_.UnwatchFile(watchId);
}

// =============================================================================
// Integration Functions
// =============================================================================

void OnGlobalConfigChanged() {
    OGS_LOG_INFO("[ConfigHotReload] Global config changed, reloading...");
    app_settings::LoadGlobal();
}

void OnGameConfigChanged(std::string_view gameName) {
    OGS_LOG_INFO("[ConfigHotReload] Game config changed for: {}", gameName);
    // The config_manager will handle game-specific reloads
    config_manager::ActivateCurrentGameConfig();
}

void RegisterDefaultConfigFiles() {
    auto& manager = ConfigHotReloadManager::Instance();
    auto& reloader = *manager.GetReloader();

    // Global config
    std::filesystem::path globalConfig = OmniGhost::Paths::ConfigDirectory() / "global.json";
    reloader.WatchFile(globalConfig, OnGlobalConfigChanged);

    // Game configs directory
    std::filesystem::path gameConfigsDir = OmniGhost::Paths::ConfigDirectory() / "games";
    if (std::filesystem::exists(gameConfigsDir)) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(gameConfigsDir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::string gameName = entry.path().stem().string();
                reloader.WatchFile(entry.path(),
                    [gameName]() { OnGameConfigChanged(gameName); });
            }
        }
    }

    OGS_LOG_INFO("[ConfigHotReload] Registered {} config files for hot-reload", reloader.WatchedFileCount());
}

} // namespace OmniGhost::Config