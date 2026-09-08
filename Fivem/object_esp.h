#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <functional>
#include <memory>
#include <atomic>
#include <set>
#include <thread>
#include "math/math.h"
#include "game/offsets.h"
#include "../DMALibrary/Memory/Memory.h"
#include "object_esp_config.h"

namespace object_esp {

// Forward declarations
class ObjectScanner;
class ObjectCache;
class ObjectDatabase;
class WhitelistManager;
class ObjectRenderer;
class ObjectInspector;

// Object entity representation
struct ObjectEntity {
    uintptr_t address = 0;
    std::string model;
    uint32_t hash = 0;
    Vec3 position{};
    Vec3 rotation{};
    float distance = 0.0f;
    bool is_networked = false;
    uint32_t network_id = 0;
    uintptr_t entity_handle = 0;
    std::chrono::steady_clock::time_point last_update;
    bool valid = false;
    
    ObjectEntity() = default;
    ObjectEntity(uintptr_t addr, const std::string& mdl, uint32_t h, const Vec3& pos)
        : address(addr), model(mdl), hash(h), position(pos), last_update(std::chrono::steady_clock::now()), valid(true) {}
};

// Scanner result for a single model
struct ScanResult {
    std::string model;
    uint32_t hash = 0;
    int count = 0;
    float nearest_distance = 0.0f;
    std::vector<Vec3> sample_positions; // First few positions
    ObjectCategory category = ObjectCategory::Other;
    bool is_custom = false;
    
    ScanResult() = default;
    ScanResult(const std::string& m, uint32_t h) : model(m), hash(h) {}
};

// Tracked object instance (for rendering)
struct TrackedInstance {
    ObjectEntity entity;
    WhitelistEntry config;
    bool visible_this_frame = false;
    Vec2 screen_pos{};
    float screen_distance = 0.0f;
    std::chrono::steady_clock::time_point last_render;
    
    TrackedInstance() = default;
    TrackedInstance(const ObjectEntity& e, const WhitelistEntry& cfg) : entity(e), config(cfg) {}
};

// Main Object ESP Manager
class ObjectESPManager {
public:
    ObjectESPManager();
    ~ObjectESPManager();
    
    // Initialization
    bool Initialize();
    void Shutdown();
    
    // Main update loop (call every frame)
    void Update();
    
    // Scanner control
    void StartScan(float radius = 500.0f);
    void StopScan();
    bool IsScanning() const { return scanner_state_.scanning; }
    float GetScanProgress() const { return scanner_state_.scan_progress; }
    const ScannerState& GetScannerState() const { return scanner_state_; }
    
    // Whitelist management
    bool AddToWhitelist(const std::string& model, uint32_t hash = 0, 
                        const std::string& display_name = "",
                        ObjectCategory category = ObjectCategory::Other,
                        bool is_custom = false);
    bool RemoveFromWhitelist(const std::string& model);
    bool ToggleWhitelistEntry(const std::string& model, bool enabled);
    bool UpdateWhitelistEntry(const std::string& model, const WhitelistEntry& entry);
    const std::vector<WhitelistEntry>& GetWhitelist() const { return whitelist_; }
    WhitelistEntry* GetWhitelistEntry(const std::string& model);
    
    // Scanner results
    const std::vector<ScanResult>& GetScanResults() const { return scan_results_; }
    void ClearScanResults();
    
    // Tracked objects
    const std::vector<TrackedInstance>& GetTrackedObjects() const { return tracked_objects_; }
    
    // Inspector
    void OpenInspector(const std::string& model);
    void CloseInspector();
    const InspectorData* GetInspectorData() const { return inspector_open_ ? &inspector_data_ : nullptr; }
    
    // Categories
    void SetCategoryFilter(ObjectCategory cat) { current_filter_ = cat; }
    ObjectCategory GetCategoryFilter() const { return current_filter_; }
    void ToggleCategoryVisibility(ObjectCategory cat);
    bool IsCategoryVisible(ObjectCategory cat) const;
    
    // Search
    void SetSearchQuery(const std::string& query) { search_query_ = query; }
    const std::string& GetSearchQuery() const { return search_query_; }
    std::vector<ScanResult> GetFilteredResults() const;
    std::vector<WhitelistEntry> GetFilteredWhitelist() const;
    
    // Settings
    const Config& GetConfig() const { return config_; }
    Config& GetMutableConfig() { return config_; }
    void SaveConfig();
    void LoadConfig();
    
    // Statistics
    struct Stats {
        int total_scanned = 0;
        int unique_models = 0;
        int tracked_objects = 0;
        int rendered_objects = 0;
        float last_scan_time_ms = 0.0f;
        float update_time_ms = 0.0f;
        float render_time_ms = 0.0f;
    };
    const Stats& GetStats() const { return stats_; }
    
    // Config persistence
    void SaveAll();
    void LoadAll();

private:
    // Core components
    std::unique_ptr<ObjectScanner> scanner_;
    std::unique_ptr<ObjectCache> cache_;
    std::unique_ptr<ObjectDatabase> database_;
    std::unique_ptr<WhitelistManager> whitelist_manager_;
    std::unique_ptr<ObjectRenderer> renderer_;
    std::unique_ptr<ObjectInspector> inspector_;
    
    // State
    Config config_;
    ScannerState scanner_state_;
    std::vector<ScanResult> scan_results_;
    std::vector<WhitelistEntry> whitelist_;
    std::vector<TrackedInstance> tracked_objects_;
    InspectorData inspector_data_;
    bool inspector_open_ = false;
    ObjectCategory current_filter_ = ObjectCategory::All;
    std::string search_query_;
    Stats stats_;
    
    // Threading
    std::thread scanner_thread_;
    std::atomic<bool> scanner_running_{false};
    std::mutex data_mutex_;
    
    // Internal methods
    void ScannerThread();
    void UpdateTrackedObjects();
    void UpdateInspector();
    void PruneStaleObjects();
    void ApplyDistanceCulling();
    void ApplyFrustumCulling();
    
    // Config persistence
    void SaveWhitelistToDisk();
    void LoadWhitelistFromDisk();
};

// Global instance accessor
ObjectESPManager& GetObjectESPManager();

} // namespace object_esp