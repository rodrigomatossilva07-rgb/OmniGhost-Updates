#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <functional>
#include <mutex>
#include <atomic>

namespace Gameplay::Offsets {

    // Offset definition
    struct OffsetDefinition {
        std::string name;
        std::string module;           // Module name (e.g., "client.dll", "engine.dll")
        uintptr_t base_offset = 0;    // Base offset from module
        std::vector<uintptr_t> chain; // Multi-level pointer chain
        bool is_relative = false;     // Relative to instruction pointer
        int relative_size = 4;        // Size of relative offset (4 or 8)
        std::string signature;        // Pattern signature for scanning
        std::string mask;             // Pattern mask
        int extra_offset = 0;         // Extra offset after signature match
        bool is_critical = false;     // Critical for functionality
        std::string description;
        uint64_t last_validated = 0;  // Timestamp
        bool is_valid = false;
        uintptr_t current_value = 0;
        uintptr_t previous_value = 0;
    };

    // Offset validation result
    struct ValidationResult {
        bool success = false;
        uintptr_t found_value = 0;
        std::string error;
        float confidence = 0.0f;      // 0.0 - 1.0
        std::chrono::microseconds scan_time;
        std::string method_used;      // "signature", "pointer_chain", "relative", "manual"
    };

    // Offset update configuration
    struct UpdateConfig {
        bool auto_update = true;
        bool hot_reload = true;           // Reload without restart
        bool validate_on_startup = true;
        bool validate_periodically = true;
        int validation_interval_minutes = 30;
        
        // Scan settings
        bool use_signature_scan = true;
        bool use_pointer_chain = true;
        bool use_relative = true;
        int max_scan_threads = 4;
        int scan_timeout_ms = 5000;
        
        // Fallback
        bool use_fallback_offsets = true;
        bool use_community_offsets = true;
        bool use_cached_offsets = true;
        
        // Validation
        bool cross_validate = true;       // Cross-check with multiple methods
        float min_confidence = 0.7f;      // Minimum confidence to accept
        bool require_multiple_matches = false;
        
        // Callbacks
        std::function<void(const std::string&, uintptr_t, uintptr_t)> on_offset_changed;
        std::function<void(const std::string&, const std::string&)> on_update_failed;
        std::function<void(const std::string&)> on_validation_success;
        std::function<void(float)> on_update_progress;
    };

    // Offset manager for a specific game
    class OffsetManager {
    public:
        OffsetManager();
        ~OffsetManager();
        
        void SetConfig(const UpdateConfig& config) { config_ = config; }
        const UpdateConfig& GetConfig() const { return config_; }
        
        // Register offset
        void RegisterOffset(const OffsetDefinition& offset);
        void UnregisterOffset(const std::string& name);
        OffsetDefinition* GetOffset(const std::string& name);
        const OffsetDefinition* GetOffset(const std::string& name) const;
        
        // Get all offsets
        std::vector<OffsetDefinition*> GetAllOffsets();
        std::vector<OffsetDefinition*> GetCriticalOffsets();
        std::vector<OffsetDefinition*> GetInvalidOffsets();
        
        // Update offsets
        bool UpdateAllOffsets(bool force = false);
        bool UpdateOffset(const std::string& name);
        
        // Hot reload
        void EnableHotReload(bool enable) { hot_reload_enabled_ = enable; }
        bool IsHotReloadEnabled() const { return hot_reload_enabled_; }
        void TriggerHotReload();
        
        // Validation
        ValidationResult ValidateOffset(const std::string& name);
        ValidationResult ValidateOffset(const OffsetDefinition& offset);
        bool ValidateAllOffsets();
        
        // Get offset value (with auto-validation)
        uintptr_t GetOffsetValue(const std::string& name);
        bool TryGetOffsetValue(const std::string& name, uintptr_t& out_value);
        
        // Set offset value manually
        bool SetOffsetValue(const std::string& name, uintptr_t value);
        
        // Callbacks
        void SetOnOffsetChanged(std::function<void(const std::string&, uintptr_t, uintptr_t)> callback) {
            on_offset_changed_ = callback;
        }
        void SetOnUpdateFailed(std::function<void(const std::string&, const std::string&)> callback) {
            on_update_failed_ = callback;
        }
        
        // Import/Export
        std::string ExportOffsets() const;
        bool ImportOffsets(const std::string& data);
        bool ExportToFile(const char* path) const;
        bool ImportFromFile(const char* path);
        
        // Stats
        struct Stats {
            int total_offsets = 0;
            int valid_offsets = 0;
            int critical_offsets = 0;
            int failed_validations = 0;
            std::chrono::steady_clock::time_point last_update;
            std::chrono::steady_clock::time_point last_full_scan;
            float avg_confidence = 0.0f;
        };
        Stats GetStats() const;
        
        // Progress tracking
        float GetUpdateProgress() const { return update_progress_; }
        bool IsUpdating() const { return is_updating_; }
        
    private:
        UpdateConfig config_;
        std::unordered_map<std::string, OffsetDefinition> offsets_;
        std::mutex offsets_mutex_;
        std::atomic<bool> hot_reload_enabled_ = true;
        std::atomic<bool> is_updating_ = false;
        std::atomic<float> update_progress_ = 0.0f;
        
        std::function<void(const std::string&, uintptr_t, uintptr_t)> on_offset_changed_;
        std::function<void(const std::string&, const std::string&)> on_update_failed_;
        
        // Scan methods
        ValidationResult ScanSignature(const OffsetDefinition& offset);
        ValidationResult ScanPointerChain(const OffsetDefinition& offset);
        ValidationResult ScanRelative(const OffsetDefinition& offset);
        ValidationResult ScanManual(const std::string& name, uintptr_t value);
        
        // Module handling
        uintptr_t GetModuleBase(const std::string& module);
        bool IsModuleLoaded(const std::string& module);
        std::vector<uintptr_t> FindPattern(uintptr_t base, size_t size, const std::string& pattern, const std::string& mask);
        
        // Validation helpers
        bool CrossValidate(const OffsetDefinition& offset, uintptr_t value);
        float CalculateConfidence(const ValidationResult& result);
        
        // Background update thread
        void UpdateThread();
        std::thread update_thread_;
        std::atomic<bool> stop_thread_ = false;
    };
    
    // Global offset registry
    class OffsetRegistry {
    public:
        static OffsetRegistry& Instance();
        
        OffsetManager* GetManager(const std::string& game_name);
        OffsetManager* CreateManager(const std::string& game_name);
        void RemoveManager(const std::string& game_name);
        void UpdateAllManagers();
        
        // Shared offsets across games
        void RegisterSharedOffset(const std::string& name, const OffsetDefinition& offset);
        OffsetDefinition* GetSharedOffset(const std::string& name);
        
    private:
        std::unordered_map<std::string, std::unique_ptr<OffsetManager>> managers_;
        std::unordered_map<std::string, OffsetDefinition> shared_offsets_;
        std::mutex registry_mutex_;
    };
    
    // Community offset database
    class CommunityOffsetDB {
    public:
        struct CommunityOffset {
            std::string game;
            std::string offset_name;
            uintptr_t value;
            std::string submitter;
            std::chrono::system_clock::time_point submitted;
            int votes_up = 0;
            int votes_down = 0;
            std::string version;        // Game version
            std::string commit_hash;    // Source commit
            bool verified = false;
        };
        
        static CommunityOffsetDB& Instance();
        
        void SubmitOffset(const CommunityOffset& offset);
        bool VoteOffset(const std::string& game, const std::string& name, bool upvote);
        std::vector<CommunityOffset> GetOffsets(const std::string& game, const std::string& version = "");
        CommunityOffset* GetBestOffset(const std::string& game, const std::string& name, const std::string& version = "");
        
        bool LoadFromServer(const std::string& url);
        bool SaveToServer(const std::string& url);
        bool ExportToFile(const char* path) const;
        bool ImportFromFile(const char* path);
        
    private:
        std::unordered_map<std::string, std::vector<CommunityOffset>> offsets_; // game -> offsets
        std::mutex db_mutex_;
    };
    
    // Version tracking
    struct GameVersionInfo {
        std::string game_name;
        std::string current_version;
        std::string previous_version;
        std::chrono::system_clock::time_point last_checked;
        std::chrono::system_clock::time_point last_updated;
        bool update_available = false;
        std::string update_url;
        std::string changelog;
        std::vector<std::string> changed_offsets;
    };
    
    class VersionTracker {
    public:
        static VersionTracker& Instance();
        
        void RegisterGame(const std::string& game_name, const std::string& version_url);
        bool CheckForUpdates(const std::string& game_name);
        const GameVersionInfo* GetVersionInfo(const std::string& game_name) const;
        std::vector<std::string> GetGamesWithUpdates() const;
        
        void SetUpdateCallback(std::function<void(const std::string&, const GameVersionInfo&)> callback) {
            on_update_ = callback;
        }
        
    private:
        std::unordered_map<std::string, GameVersionInfo> versions_;
        std::function<void(const std::string&, const GameVersionInfo&)> on_update_;
        std::mutex versions_mutex_;
        std::thread check_thread_;
        std::atomic<bool> stop_thread_ = false;
    };

} // namespace Gameplay::Offsets