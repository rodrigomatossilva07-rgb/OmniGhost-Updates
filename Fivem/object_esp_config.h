#pragma once
#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace object_esp {

// Object Categories
enum class ObjectCategory : uint8_t {
    All = 0,
    Loot,
    Mission,
    Interaction,
    Police,
    Medical,
    Vehicle,
    Crafting,
    Workshop,
    Job,
    Container,
    Custom,
    Other,
    Count
};

inline const char* ObjectCategoryToString(ObjectCategory cat) {
    static const char* names[] = {
        "All", "Loot", "Mission", "Interaction", "Police", "Medical",
        "Vehicle", "Crafting", "Workshop", "Job", "Container", "Custom", "Other"
    };
    return (static_cast<uint8_t>(cat) < static_cast<uint8_t>(ObjectCategory::Count))
        ? names[static_cast<uint8_t>(cat)] : "Other";
}

inline ObjectCategory StringToObjectCategory(const std::string& str) {
    static const std::unordered_map<std::string, ObjectCategory> map = {
        {"all", ObjectCategory::All}, {"loot", ObjectCategory::Loot},
        {"mission", ObjectCategory::Mission}, {"interaction", ObjectCategory::Interaction},
        {"police", ObjectCategory::Police}, {"medical", ObjectCategory::Medical},
        {"vehicle", ObjectCategory::Vehicle}, {"crafting", ObjectCategory::Crafting},
        {"workshop", ObjectCategory::Workshop}, {"job", ObjectCategory::Job},
        {"container", ObjectCategory::Container}, {"custom", ObjectCategory::Custom},
        {"other", ObjectCategory::Other}
    };
    auto it = map.find(str);
    return it != map.end() ? it->second : ObjectCategory::Other;
}

// Whitelist Entry - persistent object configuration
struct WhitelistEntry {
    std::string model;           // Model name (e.g., "prop_ld_int_safe")
    uint32_t hash = 0;           // Model hash
    std::string display_name;    // User-friendly name
    ObjectCategory category = ObjectCategory::Other;
    bool enabled = true;
    float max_distance = 300.0f;
    bool show_name = true;
    bool show_distance = true;
    bool show_category = false;
    bool show_box = false;
    bool show_marker = false;
    ImU32 color = IM_COL32(255, 255, 0, 255); // Default yellow
    bool is_custom = false;      // Whether this is a custom server object
    
    WhitelistEntry() = default;
    WhitelistEntry(const std::string& model_) : model(model_), display_name(model_) {}
};

// Scanned Object - temporary entry during scanning
struct ScannedObject {
    std::string model;
    uint32_t hash = 0;
    int count = 0;
    float nearest_distance = 0.0f;
    std::vector<Vec3> positions; // First few positions for reference
    
    ScannedObject() = default;
    ScannedObject(const std::string& m, uint32_t h) : model(m), hash(h) {}
};

// Tracked Object - runtime object being tracked
struct TrackedObject {
    WhitelistEntry entry;
    std::vector<Vec3> positions;
    float nearest_distance = 0.0f;
    int count = 0;
    bool visible_this_frame = false;
    std::chrono::steady_clock::time_point last_seen;
    
    TrackedObject() = default;
    TrackedObject(const WhitelistEntry& e) : entry(e), last_seen(std::chrono::steady_clock::now()) {}
};

// Object Inspector Data - detailed info for selected object
struct InspectorData {
    std::string model;
    uint32_t hash = 0;
    std::string display_name;
    uintptr_t entity_handle = 0;
    uint32_t network_id = 0;
    Vec3 position{};
    Vec3 rotation{};
    float distance = 0.0f;
    bool networked = false;
    bool is_entity = false;
    std::vector<std::pair<std::string, std::string>> extra_props; // Additional properties
    
    InspectorData() = default;
};

// Scanner State
struct ScannerState {
    bool scanning = false;
    bool scan_complete = false;
    float scan_progress = 0.0f;
    std::string status_message;
    int total_entities_scanned = 0;
    int unique_models_found = 0;
    int total_objects_found = 0;
    std::chrono::steady_clock::time_point scan_start_time;
    std::string error_message;
    
    void Reset() {
        scanning = false;
        scan_complete = false;
        scan_progress = 0.0f;
        status_message.clear();
        total_entities_scanned = 0;
        unique_models_found = 0;
        total_objects_found = 0;
        error_message.clear();
    }
};

// Object ESP Configuration
struct Config {
    // Main toggle
    bool enabled = false;
    
    // Scanner settings
    float scan_radius = 500.0f;
    int scan_interval_ms = 5000; // Time between auto-scans
    bool auto_scan = false;
    
    // Rendering settings
    float max_distance = 300.0f;
    bool show_name = true;
    bool show_distance = true;
    bool show_category = false;
    bool show_box = false;
    bool show_marker = true;
    float text_scale = 1.0f;
    float box_thickness = 2.0f;
    
    // Scanner settings
    int scan_interval_frames = 600; // Scan every N frames (at 60fps = 10s)
    int max_scan_results = 500;
    bool group_by_model = true;
    
    // Performance
    int update_interval_ms = 100; // Tracked object update interval
    int max_tracked_objects = 200;
    bool distance_culling = true;
    bool frustum_culling = true;
    
    // Scanner
    bool auto_scan_enabled = false;
    int auto_scan_interval_seconds = 30;
    
    // UI state (not persisted)
    bool show_scanner = true;
    bool show_whitelist = true;
    bool show_categories = true;
    bool show_settings = true;
    ObjectCategory current_filter = ObjectCategory::All;
    std::string search_query;
    
    // Inspector
    bool show_inspector = false;
    std::string selected_model;
    
    // Categories
    bool category_visible[static_cast<int>(ObjectCategory::Count)] = {true};
    
    Config() {
        // Initialize category visibility
        for (int i = 0; i < static_cast<int>(ObjectCategory::Count); ++i) {
            category_visible[i] = true;
        }
    }
};

// Global config instance
extern Config config;

// Whitelist management
extern std::vector<WhitelistEntry> g_whitelist;
extern std::vector<ScannedObject> g_scanned_objects;
extern std::vector<TrackedObject> g_tracked_objects;
extern ScannerState g_scanner_state;
extern InspectorData g_inspector_data;
extern std::mutex g_object_esp_mutex;

// Config persistence
bool SaveConfig(const std::string& filename = "object_esp");
bool LoadConfig(const std::string& filename = "object_esp");
bool SaveWhitelist(const std::string& filename = "object_esp_whitelist");
bool LoadWhitelist(const std::string& filename = "object_esp_whitelist");

// Whitelist management functions
bool AddToWhitelist(const std::string& model, uint32_t hash = 0, const std::string& display_name = "", ObjectCategory category = ObjectCategory::Other, bool is_custom = false);
bool RemoveFromWhitelist(const std::string& model);
WhitelistEntry* FindWhitelistEntry(const std::string& model);
bool ToggleWhitelistEntry(const std::string& model, bool enabled);
bool UpdateWhitelistEntry(const std::string& model, const WhitelistEntry& entry);

// Scanner functions
void StartObjectScan(float radius = 500.0f);
void StopObjectScan();
void UpdateScanner();
void ClearScanResults();

// Tracking functions
void UpdateTrackedObjects();
void ClearTrackedObjects();
TrackedObject* FindTrackedObject(const std::string& model);

// Inspector
void OpenInspector(const std::string& model);
void CloseInspector();
void UpdateInspector(const std::string& model);

// Categories
void SetCategoryFilter(ObjectCategory cat);
void ToggleCategoryVisibility(ObjectCategory cat);
bool IsCategoryVisible(ObjectCategory cat);

// Config persistence
void SaveAllConfig();
void LoadAllConfig();

// Scanner worker thread
void ScannerThread();
void StartScannerThread();
void StopScannerThread();

} // namespace object_esp