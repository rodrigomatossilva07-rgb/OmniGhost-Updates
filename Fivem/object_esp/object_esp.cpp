#include "object_esp.h"
#include "object_esp_renderer.h"
#include "math/math.h"
#include "game/offsets.h"
#include "game/esp_manager.h"
#include "game/game.h"
#include "config/app_settings.h"
#include "../../DMALibrary/Memory/Memory.h"
#include "../../src/platform/app_paths.h"
#include <iostream>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cctype>
#include <filesystem>
#include <unordered_map>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <format>
#include <iomanip>

namespace fs = std::filesystem;

namespace object_esp {

// ============================================================================
// Logging Helper (fixed to properly format arguments)
// ============================================================================
template<typename... Args>
static void LogImpl(const char* level, const char* fmt, Args&&... args) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    char timebuf[32];
    std::strftime(timebuf, sizeof(timebuf), "%H:%M:%S", std::localtime(&time));
    std::cout << "[" << timebuf << "] [ObjectESP][" << level << "] " 
              << std::vformat(fmt, std::make_format_args(args...)) << "\n";
}

#define OBJESP_LOG_DEBUG(fmt, ...) LogImpl("DEBUG", fmt, ##__VA_ARGS__)
#define OBJESP_LOG_INFO(fmt, ...)  LogImpl("INFO", fmt, ##__VA_ARGS__)
#define OBJESP_LOG_WARN(fmt, ...)  LogImpl("WARN", fmt, ##__VA_ARGS__)
#define OBJESP_LOG_ERROR(fmt, ...) LogImpl("ERROR", fmt, ##__VA_ARGS__)

static void LogCrashContext(const char* context) {
    using namespace FiveM::offset;
    OBJESP_LOG_ERROR("CRASH CONTEXT: {}", context);
    OBJESP_LOG_ERROR("  base=0x{:X}, object_pool=0x{:X}, localplayer=0x{:X}, replay=0x{:X}",
        (unsigned long long)base, (unsigned long long)object_pool,
        (unsigned long long)localplayer, (unsigned long long)replay);
    OBJESP_LOG_ERROR("  viewport=0x{:X}, world=0x{:X}", 
        (unsigned long long)viewport, (unsigned long long)world);
}

// ============================================================================
// Pointer Validation Helpers (now defined in header's namespace scope for class method access)
// ============================================================================

// Global instance
static ObjectESPManager* g_manager = nullptr;

ObjectESPManager& GetObjectESPManager() {
    if (!g_manager) {
        OBJESP_LOG_INFO("Creating ObjectESPManager instance");
        g_manager = new ObjectESPManager();
    }
    return *g_manager;
}

// ============================================================================
// ObjectESPManager Implementation
// ============================================================================

ObjectESPManager::ObjectESPManager() 
    : scanner_state_(), stats_(), inspector_open_(false), current_filter_(ObjectCategory::All), selected_model_() {
    OBJESP_LOG_INFO("ObjectESPManager constructor");
    for (int i = 0; i < static_cast<int>(ObjectCategory::Count); ++i) {
        config_.category_visible[i] = true;
    }
    
    OBJESP_LOG_DEBUG("Creating ObjectRenderer");
    try {
        renderer_ = std::make_unique<ObjectRenderer>();
        renderer_->Initialize();
        OBJESP_LOG_INFO("ObjectRenderer initialized successfully");
    } catch (const std::exception&) {
        OBJESP_LOG_ERROR("Failed to initialize ObjectRenderer");
    } catch (...) {
        OBJESP_LOG_ERROR("Unknown exception during ObjectRenderer initialization");
    }
}

ObjectESPManager::~ObjectESPManager() {
    OBJESP_LOG_INFO("ObjectESPManager destructor");
    Shutdown();
    g_manager = nullptr;
}

bool ObjectESPManager::Initialize() {
    OBJESP_LOG_INFO("Initialize() called, initialized_=%s", initialized_ ? "true" : "false");
    
    if (initialized_) {
        OBJESP_LOG_INFO("Already initialized");
        return true;
    }

    try {
        if (!renderer_) {
            OBJESP_LOG_DEBUG("Creating ObjectRenderer");
            renderer_ = std::make_unique<ObjectRenderer>();
            renderer_->Initialize();
            OBJESP_LOG_INFO("ObjectRenderer initialized");
        }
        LoadAll();
        initialized_ = true;
        OBJESP_LOG_INFO("ObjectESP initialized successfully (frame-snapshot scanner)");
    } catch (const std::exception&) {
        OBJESP_LOG_ERROR("Initialize failed");
        return false;
    } catch (...) {
        OBJESP_LOG_ERROR("Initialize failed with unknown exception");
        return false;
    }
    return true;
}

void ObjectESPManager::Shutdown() {
    OBJESP_LOG_INFO("Shutdown() called, initialized_=%s", initialized_ ? "true" : "false");
    
    if (!initialized_)
        return;

    scanner_state_.scanning = false;
    scanner_state_.scan_complete = true;
    renderer_.reset();
    SaveAll();
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        tracked_objects_.clear();
        scan_results_.clear();
    }
    initialized_ = false;
    OBJESP_LOG_INFO("Shutdown complete");
}

void ObjectESPManager::Update() {
    static int frame_counter = 0;
    if (frame_counter++ % 300 == 0) { // Log every 300 frames (~5 seconds at 60fps)
        OBJESP_LOG_DEBUG("Update: initialized_=%s, enabled=%s, scanning=%s, tracked=%d, scan_results=%d",
            initialized_ ? "Y" : "N", config_.enabled ? "Y" : "N", 
            scanner_state_.scanning ? "Y" : "N",
            (int)tracked_objects_.size(), (int)scan_results_.size());
    }
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();

        if (initialized_ && scanner_state_.scanning) {
            OBJESP_LOG_DEBUG("Starting PerformScanIncremental");
            const auto scan_start = std::chrono::high_resolution_clock::now();
            PerformScanIncremental();
            const auto scan_end = std::chrono::high_resolution_clock::now();
            stats_.last_scan_time_ms = std::chrono::duration<float, std::milli>(scan_end - scan_start).count();
            OBJESP_LOG_DEBUG("PerformScanIncremental completed in %.2f ms", stats_.last_scan_time_ms);
        }
        
        if (!config_.enabled) {
            return;
        }
        
        std::lock_guard<std::mutex> lock(data_mutex_);
        
        // Update tracked objects from current scan results and whitelist
        {
            static auto s_lastTrackRebuild = std::chrono::steady_clock::time_point{};
            const auto nowTr = std::chrono::steady_clock::now();
            if (s_lastTrackRebuild.time_since_epoch().count() == 0 ||
                nowTr - s_lastTrackRebuild > std::chrono::milliseconds(300)) {
                UpdateTrackedObjects();
                s_lastTrackRebuild = nowTr;
            }
        }
        
        // Apply culling
        if (config_.distance_culling) ApplyDistanceCulling();
        if (config_.frustum_culling) ApplyFrustumCulling();
        
        // Prune stale objects
        PruneStaleObjects();
        
        // Update stats
        stats_.tracked_objects = static_cast<int>(tracked_objects_.size());
        stats_.rendered_objects = 0; // Will be updated by renderer
        
        auto end_time = std::chrono::high_resolution_clock::now();
        stats_.update_time_ms = std::chrono::duration<float, std::milli>(end_time - start_time).count();
    } catch (const std::exception&) {
        OBJESP_LOG_ERROR("Update failed");
        scanner_state_.scanning = false;
        scanner_state_.scan_complete = true;
        scanner_state_.error_message = "Object ESP update failed safely";
        scanner_state_.status_message = scanner_state_.error_message;
    } catch (...) {
        OBJESP_LOG_ERROR("Update failed with unknown exception");
        scanner_state_.scanning = false;
        scanner_state_.scan_complete = true;
        scanner_state_.error_message = "Object ESP update failed safely";
        scanner_state_.status_message = scanner_state_.error_message;
    }
}

void ObjectESPManager::StartScan(float radius) {
    OBJESP_LOG_INFO("StartScan called, radius=%.1f", radius);
    if (scanner_state_.scanning) {
        OBJESP_LOG_WARN("Scan already in progress");
        return;
    }
    if (!initialized_) {
        scanner_state_.Reset();
        scanner_state_.scan_complete = true;
        scanner_state_.status_message = "Start a FiveM session before scanning";
        OBJESP_LOG_WARN("Scan rejected: FiveM session not initialized");
        LogCrashContext("StartScan - not initialized");
        return;
    }
    if (!HasValidatedDiscoverySource()) {
        scanner_state_.Reset();
        scanner_state_.scan_complete = true;
        scanner_state_.status_message = "Object discovery is not validated for this FiveM build";
        scanner_state_.error_message = scanner_state_.status_message;
        OBJESP_LOG_WARN("Scan rejected: no validated object discovery source");
        LogCrashContext("StartScan - no validated discovery source");
        return;
    }
    
    scanner_state_.Reset();
    scanner_state_.scanning = true;
    scanner_state_.scan_start_time = std::chrono::steady_clock::now();
    scanner_state_.status_message = "Scanning objects...";
    config_.scan_radius = radius;
    
    OBJESP_LOG_INFO("Scan started, radius: %.1fm", radius);
}

void ObjectESPManager::StopScan() {
    OBJESP_LOG_INFO("StopScan called");
    scanner_state_.scanning = false;
    scanner_state_.scan_complete = true;
    scanner_state_.status_message = "Scan complete";
    OBJESP_LOG_INFO("Scan stopped");
}

void ObjectESPManager::ClearScanResults() {
    OBJESP_LOG_DEBUG("ClearScanResults");
    std::lock_guard<std::mutex> lock(data_mutex_);
    scan_results_.clear();
    scanner_state_.unique_models_found = 0;
    scanner_state_.total_objects_found = 0;
}

std::vector<ScanResult> ObjectESPManager::GetFilteredResults() const {
    std::vector<ScanResult> results;
    std::lock_guard<std::mutex> lock(data_mutex_);
    
    for (const auto& result : scan_results_) {
        // Category filter
        if (current_filter_ != ObjectCategory::All && result.category != current_filter_) {
            continue;
        }
        
        // Search query filter
        if (!search_query_.empty()) {
            std::string query = search_query_;
            std::transform(query.begin(), query.end(), query.begin(), ::tolower);
            std::string model = result.model;
            std::transform(model.begin(), model.end(), model.begin(), ::tolower);
            
            if (model.find(query) == std::string::npos) {
                continue;
            }
        }
        
        results.push_back(result);
    }
    
    return results;
}

std::vector<WhitelistEntry> ObjectESPManager::GetFilteredWhitelist() const {
    std::vector<WhitelistEntry> results;
    std::lock_guard<std::mutex> lock(data_mutex_);
    
    for (const auto& entry : whitelist_) {
        if (current_filter_ != ObjectCategory::All && entry.category != current_filter_) {
            continue;
        }
        
        if (!search_query_.empty()) {
            std::string query = search_query_;
            std::transform(query.begin(), query.end(), query.begin(), ::tolower);
            std::string model = entry.model;
            std::transform(model.begin(), model.end(), model.begin(), ::tolower);
            
            if (model.find(query) == std::string::npos &&
                entry.display_name.find(query) == std::string::npos) {
                continue;
            }
        }
        
        results.push_back(entry);
    }
    
    return results;
}

void ObjectESPManager::OpenInspector(const std::string& model) {
    OBJESP_LOG_INFO("Opening inspector for: %s", model.c_str());
    std::lock_guard<std::mutex> lock(data_mutex_);
    inspector_open_ = true;
    inspector_data_ = InspectorData();
    inspector_data_.model = model;
    selected_model_ = model;
    
    // Find in whitelist
    auto it = std::find_if(whitelist_.begin(), whitelist_.end(),
        [&model](const WhitelistEntry& e) { return e.model == model; });
    if (it != whitelist_.end()) {
        inspector_data_.display_name = it->display_name;
        inspector_data_.hash = it->hash;
        inspector_data_.category = it->category;
    }
    
    // Find in tracked objects for detailed info
    auto tracked_it = std::find_if(tracked_objects_.begin(), tracked_objects_.end(),
        [&model](const TrackedInstance& t) { return t.entity.model == model; });
    if (tracked_it != tracked_objects_.end()) {
        inspector_data_.position = tracked_it->entity.position;
        inspector_data_.distance = tracked_it->entity.distance;
        inspector_data_.entity_handle = tracked_it->entity.entity_handle;
        inspector_data_.network_id = tracked_it->entity.network_id;
        inspector_data_.is_networked = tracked_it->entity.is_networked;
        inspector_data_.category = tracked_it->config.category;
        
        // Add extra properties
        inspector_data_.extra_props.clear();
        inspector_data_.extra_props.emplace_back("Entity Handle", "0x" + std::to_string(tracked_it->entity.entity_handle));
        inspector_data_.extra_props.emplace_back("Network ID", std::to_string(tracked_it->entity.network_id));
        inspector_data_.extra_props.emplace_back("Networked", tracked_it->entity.is_networked ? "Yes" : "No");
        inspector_data_.extra_props.emplace_back("Position X", std::to_string(tracked_it->entity.position.x));
        inspector_data_.extra_props.emplace_back("Position Y", std::to_string(tracked_it->entity.position.y));
        inspector_data_.extra_props.emplace_back("Position Z", std::to_string(tracked_it->entity.position.z));
        inspector_data_.extra_props.emplace_back("Distance", std::to_string(tracked_it->entity.distance) + "m");
        inspector_data_.extra_props.emplace_back("Category", ObjectCategoryToString(tracked_it->config.category));
        inspector_data_.extra_props.emplace_back("Max Distance", std::to_string(tracked_it->config.max_distance) + "m");
        inspector_data_.extra_props.emplace_back("Show Name", tracked_it->config.show_name ? "Yes" : "No");
        inspector_data_.extra_props.emplace_back("Show Distance", tracked_it->config.show_distance ? "Yes" : "No");
        inspector_data_.extra_props.emplace_back("Show Box", tracked_it->config.show_box ? "Yes" : "No");
        inspector_data_.extra_props.emplace_back("Show Marker", tracked_it->config.show_marker ? "Yes" : "No");
    }
    
    // Add scan result info if available
    for (const auto& result : scan_results_) {
        if (result.model == model) {
            inspector_data_.extra_props.emplace_back("Total Count", std::to_string(result.count));
            inspector_data_.extra_props.emplace_back("Nearest Distance", std::to_string(result.nearest_distance) + "m");
            inspector_data_.extra_props.emplace_back("Is Custom", result.is_custom ? "Yes" : "No");
            inspector_data_.extra_props.emplace_back("Hash", "0x" + std::to_string(result.hash));
            break;
        }
    }
    
    OBJESP_LOG_INFO("Inspector opened for: %s", model.c_str());
}

void ObjectESPManager::CloseInspector() {
    OBJESP_LOG_DEBUG("Closing inspector");
    inspector_open_ = false;
    selected_model_.clear();
}

void ObjectESPManager::SetCustomDisplayName(const std::string& model, const std::string& display_name) {
    OBJESP_LOG_INFO("Setting custom display name for %s: %s", model.c_str(), display_name.c_str());
    std::lock_guard<std::mutex> lock(data_mutex_);
    config_.custom_display_names[model] = display_name;
    
    // Also update whitelist entry if it exists
    auto it = std::find_if(whitelist_.begin(), whitelist_.end(),
        [&model](const WhitelistEntry& e) { return e.model == model; });
    if (it != whitelist_.end()) {
        it->display_name = display_name;
    }
    SaveWhitelistToDisk();
}

void ObjectESPManager::ToggleCategoryVisibility(ObjectCategory cat) {
    if (static_cast<int>(cat) < static_cast<int>(ObjectCategory::Count)) {
        config_.category_visible[static_cast<int>(cat)] = !config_.category_visible[static_cast<int>(cat)];
    }
}

bool ObjectESPManager::IsCategoryVisible(ObjectCategory cat) const {
    if (static_cast<int>(cat) < static_cast<int>(ObjectCategory::Count)) {
        return config_.category_visible[static_cast<int>(cat)];
    }
    return true;
}



bool ObjectESPManager::HasValidatedDiscoverySource() const noexcept {
    using namespace FiveM::offset;
    // Validate that we have a valid base address and object_pool pointer
    // Also verify the object_pool points to a valid pool structure
    if (base == 0 || object_pool == 0) return false;
    
    // Try to validate the pool structure
    uintptr_t pool = 0;
    if (!ReadU64Safe(object_pool, pool)) return false;
    if (pool == 0) return false;
    
    // Try to read pool structure
    uintptr_t items = 0, flags = 0;
    uint32_t size = 0, itemSize = 0;
    if (!ReadU64Safe(pool + 0x0, items) || items == 0) return false;
    if (!ReadU64Safe(pool + 0x8, flags)) return false;
    if (!mem.Read(pool + 0x10, &size, sizeof(size)) || size == 0) return false;
    if (!mem.Read(pool + 0x14, &itemSize, sizeof(itemSize)) || itemSize == 0) return false;
    
    return true;
}

void ObjectESPManager::PerformScan() {
    OBJESP_LOG_DEBUG("PerformScan started (incremental)");
    // Start incremental scan
    PerformScanIncremental();
}

void ObjectESPManager::PerformScanIncremental() {
    OBJESP_LOG_DEBUG("PerformScanIncremental started");
    try {
        using namespace FiveM::offset;
        std::lock_guard<std::mutex> lock(data_mutex_);

        // Initialize scan progress if first frame
        if (!scan_progress_.has_value()) {
            if (!HasValidatedDiscoverySource()) {
                OBJESP_LOG_WARN("No validated discovery source");
                scanner_state_.scanning = false;
                scanner_state_.scan_complete = true;
                scanner_state_.status_message = "Object pool unavailable";
                scanner_state_.scan_progress = 1.0f;
                LogCrashContext("PerformScanIncremental - no validated discovery source");
                return;
            }

            Vec3 localPos{};
            if (localplayer)
                ReadVec3Safe(localplayer + playerPosition, localPos);

            // Resolve pool
            uintptr_t pool = 0;
            if (!ReadU64Safe(object_pool, pool))
                pool = object_pool;

            uintptr_t items = 0;
            uintptr_t flags = 0;
            uint32_t size = 0;
            uint32_t itemSize = 0;

            ReadU64Safe(pool + 0x0, items);
            ReadU64Safe(pool + 0x8, flags);
            mem.Read(pool + 0x10, &size, sizeof(size));
            mem.Read(pool + 0x14, &itemSize, sizeof(itemSize));

            // Fallback pool resolution
            if ((!items || !size || size > 300000 || itemSize == 0 || itemSize > 0x4000) && pool) {
                uintptr_t pool2 = 0;
                if (ReadU64Safe(pool, pool2)) {
                    ReadU64Safe(pool2 + 0x0, items);
                    ReadU64Safe(pool2 + 0x8, flags);
                    mem.Read(pool2 + 0x10, &size, sizeof(size));
                    mem.Read(pool2 + 0x14, &itemSize, sizeof(itemSize));
                    pool = pool2;
                }
            }

            if (!items || size == 0 || size > 300000) {
                OBJESP_LOG_ERROR("Pool unreadable");
                scanner_state_.scanning = false;
                scanner_state_.scan_complete = true;
                scanner_state_.status_message = "Object pool layout not readable";
                scanner_state_.error_message = scanner_state_.status_message;
                scanner_state_.scan_progress = 1.0f;
                LogCrashContext("PerformScanIncremental - pool unreadable");
                return;
            }
            if (itemSize == 0 || itemSize > 0x4000)
                itemSize = 0x10;

            const float maxR = config_.scan_radius > 1.f ? config_.scan_radius : 500.f;
            const float maxR2 = maxR * maxR;

            ScanProgress progress;
            progress.current_index = 0;
            progress.total_slots = size;
            progress.pool_address = pool;
            progress.items_address = items;
            progress.flags_address = flags;
            progress.pool_size = size;
            progress.item_size = itemSize;
            progress.local_position = localPos;
            progress.max_radius_sq = maxR2;
            progress.by_hash.reserve(256);
            progress.pool_validated = true;

            scan_progress_ = std::move(progress);
            scan_results_.clear();
            scanner_state_.total_entities_scanned = 0;
            scanner_state_.unique_models_found = 0;
            scanner_state_.total_objects_found = 0;
            scanner_state_.scan_progress = 0.05f;
            scanner_state_.scanning = true;
            scanner_state_.scan_complete = false;
            scanner_state_.status_message = "Scanning objects incrementally...";
        }

        // Perform a single pass of the incremental scan
        PerformScanSinglePass();

    } catch (const std::exception& ex) {
        OBJESP_LOG_ERROR("PerformScanIncremental exception: %s", ex.what());
        scanner_state_.scanning = false;
        scanner_state_.scan_complete = true;
        scanner_state_.status_message = "Scan failed";
        scanner_state_.error_message = ex.what();
    } catch (...) {
        OBJESP_LOG_ERROR("PerformScanIncremental unknown exception");
        scanner_state_.scanning = false;
        scanner_state_.scan_complete = true;
        scanner_state_.status_message = "Scan failed";
        scanner_state_.error_message = "unknown";
    }
}

void ObjectESPManager::PerformScanSinglePass() {
    if (!scan_progress_.has_value()) return;

    auto& progress = *scan_progress_;
    const uint32_t slotsPerPass = 256; // Process 256 slots per acquisition frame
    const auto scanDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(8);

    for (uint32_t processed = 0; processed < slotsPerPass && progress.current_index < progress.total_slots; ++processed, ++progress.current_index) {
        if (std::chrono::steady_clock::now() >= scanDeadline) {
            break;
        }

        const uint32_t i = progress.current_index;

        // Update progress
        if ((i & 0xFF) == 0)
            scanner_state_.scan_progress = 0.05f + 0.9f * (float)i / (float)progress.total_slots;

        // Stage 1: Check flags (free slot)
        if (progress.flags_address) {
            uint8_t bit = 0;
            if (mem.Read(progress.flags_address + i, &bit, 1) && (bit & 0x80))
                continue; // free slot
        }

        // Stage 2: Read entity pointer
        uintptr_t ent = 0;
        if (progress.item_size <= 0x20) {
            if (!ReadU64Safe(progress.items_address + (uintptr_t)i * progress.item_size, ent))
                continue;
        } else {
            ent = progress.items_address + (uintptr_t)i * progress.item_size;
        }
        if (!ent || ent < 0x10000ULL)
            continue;

        ++scanner_state_.total_entities_scanned;

        // Stage 3: Read position
        Vec3 pos{};
        if (!ReadVec3Safe(ent + FiveM::offset::playerPosition, pos) || !LooksFinite(pos))
            continue;

        float dist2 = 0.f;
        if (!progress.local_position.IsZero()) {
            const float dx = pos.x - progress.local_position.x;
            const float dy = pos.y - progress.local_position.y;
            const float dz = pos.z - progress.local_position.z;
            dist2 = dx * dx + dy * dy + dz * dz;
            if (dist2 > progress.max_radius_sq)
                continue;
        }

        // Stage 4: Read model hash
        uint32_t hash = 0;
        uintptr_t modelInfo = 0;
        if (ReadU64Safe(ent + 0x20, modelInfo) && modelInfo) {
            mem.Read(modelInfo + 0x18, &hash, sizeof(hash));
        }
        if (!hash)
            mem.Read(ent + 0x18, &hash, sizeof(hash));
        if (!hash)
            continue;

        const float dist = progress.local_position.IsZero() ? 0.f : std::sqrt(dist2);
        auto& acc = progress.by_hash[hash];
        if (acc.hash == 0) {
            acc.hash = hash;
            acc.model = std::format("0x{:08X}", hash);
            acc.category = ObjectCategory::Other;
            acc.is_custom = (hash > 0x10000000u);
        }
        acc.count++;
        if (acc.sample_positions.size() < 8)
            acc.sample_positions.push_back(pos);
        if (acc.count == 1 || dist < acc.nearest_distance)
            acc.nearest_distance = dist;
        acc.entity_address = ent;
    }

    // Check if scan is complete
    if (progress.current_index >= progress.total_slots) {
        // Scan complete - finalize results
        scan_results_.clear();
        scan_results_.reserve(progress.by_hash.size());
        for (auto& kv : progress.by_hash)
            scan_results_.push_back(std::move(kv.second));

        std::sort(scan_results_.begin(), scan_results_.end(),
            [](const ScanResult& a, const ScanResult& b) { return a.count > b.count; });

        scanner_state_.unique_models_found = static_cast<int>(scan_results_.size());
        scanner_state_.total_objects_found = 0;
        for (const auto& r : scan_results_)
            scanner_state_.total_objects_found += r.count;

        scanner_state_.scanning = false;
        scanner_state_.scan_complete = true;
        scanner_state_.scan_progress = 1.0f;
        char msg[128];
        std::snprintf(msg, sizeof(msg), "Scan completo: %d modelos, %d objetos",
            scanner_state_.unique_models_found, scanner_state_.total_objects_found);
        scanner_state_.status_message = msg;
        OBJESP_LOG_INFO("%s", msg);

        scan_progress_.reset();
    }
}

void ObjectESPManager::UpdateTrackedObjects() {
    tracked_objects_.clear();
    
    // For each whitelist entry, find matching entities
    for (const auto& entry : whitelist_) {
        if (!entry.enabled) continue;
        
        // Find entities matching this model
        // This would scan the current entity list
        // For now, we'll use the scan results
        
        for (const auto& result : scan_results_) {
            if (result.model == entry.model ||
                (entry.hash != 0 && result.hash != 0 && result.hash == entry.hash)) {
                // Create tracked instances for each position
                for (const auto& pos : result.sample_positions) {
                    ObjectEntity entity;
                    entity.model = result.model;
                    entity.hash = result.hash;
                    entity.position = pos;
                    entity.distance = result.nearest_distance;
                    entity.address = result.entity_address; // Preserve entity address
                    
                    tracked_objects_.emplace_back(entity, entry);
                }
                break; // Only need one match per whitelist entry
            }
        }
    }
    
    stats_.tracked_objects = static_cast<int>(tracked_objects_.size());
}

void ObjectESPManager::ApplyDistanceCulling() {
    if (!config_.distance_culling) return;
    
    auto it = tracked_objects_.begin();
    while (it != tracked_objects_.end()) {
        if (it->entity.distance > it->config.max_distance) {
            it = tracked_objects_.erase(it);
        } else {
            ++it;
        }
    }
}

void ObjectESPManager::ApplyFrustumCulling() {
    if (!config_.frustum_culling) return;
    
    using namespace FiveM;
    
    // Get view matrix from ESP frame cache
    if (!ESP::FrameCacheValid()) return;
    const Matrix& viewMatrix = ESP::GetFrameViewMatrix();
    
    // Extract frustum planes from view-projection matrix
    // We'll use a simple frustum culling: check if object's screen position is within extended viewport
    static float s_displayWidth = 1920.f;
    static float s_displayHeight = 1080.f;
    const float margin = 100.f; // Extended margin for off-screen objects
    
    auto it = tracked_objects_.begin();
    while (it != tracked_objects_.end()) {
        Vec2 screenPos;
        bool onScreen = it->entity.position.world_to_screen(const_cast<Matrix&>(viewMatrix), screenPos);
        
        // Keep object if on screen or near screen edge (for LOD transition)
        bool keep = onScreen;
        if (!keep) {
            // Check if object is near screen bounds (for smooth LOD)
            if (screenPos.x < -margin || screenPos.x > s_displayWidth + margin ||
                screenPos.y < -margin || screenPos.y > s_displayHeight + margin) {
                keep = false;
            } else {
                keep = true; // Near screen edge, keep for LOD
            }
        }
        
        if (!keep) {
            it = tracked_objects_.erase(it);
        } else {
            ++it;
        }
    }
}

void ObjectESPManager::PruneStaleObjects() {
    auto now = std::chrono::steady_clock::now();
    const auto max_age = std::chrono::seconds(10);
    
    auto it = tracked_objects_.begin();
    while (it != tracked_objects_.end()) {
        if (now - it->last_render > max_age) {
            it = tracked_objects_.erase(it);
        } else {
            ++it;
        }
    }
}

// Whitelist management
bool ObjectESPManager::AddToWhitelist(const std::string& model, uint32_t hash,
                                      const std::string& display_name,
                                      ObjectCategory category, bool is_custom) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    
    // Check if already exists
    auto it = std::find_if(whitelist_.begin(), whitelist_.end(),
        [&model](const WhitelistEntry& e) { return e.model == model; });
    
    if (it != whitelist_.end()) {
        // Update existing
        it->hash = hash ? hash : it->hash;
        if (!display_name.empty()) it->display_name = display_name;
        it->category = category;
        it->is_custom = is_custom;
        return true;
    }
    
    WhitelistEntry entry(model);
    entry.hash = hash;
    entry.display_name = display_name.empty() ? model : display_name;
    entry.category = category;
    entry.is_custom = is_custom;
    entry.enabled = true;
    
    whitelist_.push_back(std::move(entry));
    SaveWhitelistToDisk();
    
    std::cout << "[ObjectESP] Added to whitelist: " << model << std::endl;
    return true;
}

bool ObjectESPManager::RemoveFromWhitelist(const std::string& model) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    
    auto it = std::find_if(whitelist_.begin(), whitelist_.end(),
        [&model](const WhitelistEntry& e) { return e.model == model; });
    
    if (it != whitelist_.end()) {
        whitelist_.erase(it);
        SaveWhitelistToDisk();
        std::cout << "[ObjectESP] Removed from whitelist: " << model << std::endl;
        return true;
    }
    return false;
}

bool ObjectESPManager::ToggleWhitelistEntry(const std::string& model, bool enabled) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    
    auto it = std::find_if(whitelist_.begin(), whitelist_.end(),
        [&model](const WhitelistEntry& e) { return e.model == model; });
    
    if (it != whitelist_.end()) {
        it->enabled = enabled;
        SaveWhitelistToDisk();
        return true;
    }
    return false;
}

bool ObjectESPManager::UpdateWhitelistEntry(const std::string& model, const WhitelistEntry& entry) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    
    auto it = std::find_if(whitelist_.begin(), whitelist_.end(),
        [&model](const WhitelistEntry& e) { return e.model == model; });
    
    if (it != whitelist_.end()) {
        *it = entry;
        SaveWhitelistToDisk();
        return true;
    }
    return false;
}

WhitelistEntry* ObjectESPManager::GetWhitelistEntry(const std::string& model) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    
    auto it = std::find_if(whitelist_.begin(), whitelist_.end(),
        [&model](const WhitelistEntry& e) { return e.model == model; });
    
    return it != whitelist_.end() ? &(*it) : nullptr;
}

// Config persistence
void ObjectESPManager::SaveConfig() {
    std::string path = (OmniGhost::Paths::Configs() / "object_esp.cfg").string();
    std::ofstream out(path);
    if (!out) return;
    
    out << "enabled=" << (config_.enabled ? "1" : "0") << "\n";
    out << "max_distance=" << config_.max_distance << "\n";
    out << "scan_radius=" << config_.scan_radius << "\n";
    out << "scan_interval_ms=" << config_.scan_interval_ms << "\n";
    out << "auto_scan=" << (config_.auto_scan ? "1" : "0") << "\n";
    out << "show_name=" << (config_.show_name ? "1" : "0") << "\n";
    out << "show_distance=" << (config_.show_distance ? "1" : "0") << "\n";
    out << "show_category=" << (config_.show_category ? "1" : "0") << "\n";
    out << "show_box=" << (config_.show_box ? "1" : "0") << "\n";
    out << "show_marker=" << (config_.show_marker ? "1" : "0") << "\n";
    out << "distance_culling=" << (config_.distance_culling ? "1" : "0") << "\n";
    out << "frustum_culling=" << (config_.frustum_culling ? "1" : "0") << "\n";
    out << "max_scan_results=" << config_.max_scan_results << "\n";
    out << "scan_interval_ms=" << config_.scan_interval_ms << "\n";
    out << "text_scale=" << config_.text_scale << "\n";
    out << "box_thickness=" << config_.box_thickness << "\n";
    out << "distance_culling=" << (config_.distance_culling ? "1" : "0") << "\n";
    out << "frustum_culling=" << (config_.frustum_culling ? "1" : "0") << "\n";
}

void ObjectESPManager::LoadConfig() {
    std::string path = (OmniGhost::Paths::Configs() / "object_esp.cfg").string();
    std::ifstream in(path);
    if (!in) return;
    
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        
        try {
            if (key == "enabled") config_.enabled = (val == "1");
            else if (key == "max_distance") config_.max_distance = std::clamp(std::stof(val), 50.0f, 5000.0f);
            else if (key == "scan_radius") config_.scan_radius = std::clamp(std::stof(val), 50.0f, 2000.0f);
            else if (key == "scan_interval_ms") config_.scan_interval_ms = std::clamp(std::stoi(val), 250, 60000);
            else if (key == "auto_scan") config_.auto_scan = (val == "1");
            else if (key == "show_name") config_.show_name = (val == "1");
            else if (key == "show_distance") config_.show_distance = (val == "1");
            else if (key == "show_category") config_.show_category = (val == "1");
            else if (key == "show_box") config_.show_box = (val == "1");
            else if (key == "show_marker") config_.show_marker = (val == "1");
            else if (key == "text_scale") config_.text_scale = std::clamp(std::stof(val), 0.5f, 2.5f);
            else if (key == "box_thickness") config_.box_thickness = std::clamp(std::stof(val), 1.0f, 5.0f);
            else if (key == "distance_culling") config_.distance_culling = (val == "1");
            else if (key == "frustum_culling") config_.frustum_culling = (val == "1");
            else if (key == "max_scan_results") config_.max_scan_results = std::clamp(std::stoi(val), 1, 500);
        } catch (const std::exception&) {
            std::cerr << "[ObjectESP] Ignored invalid config value for " << key << std::endl;
        }
    }
}

void ObjectESPManager::SaveWhitelistToDisk() {
    std::string path = (OmniGhost::Paths::Configs() / "object_esp_whitelist.cfg").string();
    std::ofstream out(path);
    if (!out) return;
    
    for (const auto& entry : whitelist_) {
        out << entry.model << "|"
            << entry.hash << "|"
            << entry.display_name << "|"
            << static_cast<int>(entry.category) << "|"
            << (entry.enabled ? "1" : "0") << "|"
            << entry.max_distance << "|"
            << (entry.show_name ? "1" : "0") << "|"
            << (entry.show_distance ? "1" : "0") << "|"
            << (entry.show_category ? "1" : "0") << "|"
            << (entry.show_box ? "1" : "0") << "|"
            << (entry.show_marker ? "1" : "0") << "|"
            << std::hex << entry.color << std::dec << "|"
            << (entry.is_custom ? "1" : "0") << "\n";
    }
}

void ObjectESPManager::LoadWhitelistFromDisk() {
    std::string path = (OmniGhost::Paths::Configs() / "object_esp_whitelist.cfg").string();
    std::ifstream in(path);
    if (!in) return;
    
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        
        std::vector<std::string> parts;
        std::stringstream ss(line);
        std::string part;
        while (std::getline(ss, part, '|')) {
            parts.push_back(part);
        }
        
        if (parts.size() < 11) continue;
        
        try {
            WhitelistEntry entry;
            entry.model = parts[0];
            if (entry.model.empty()) continue;
            entry.hash = std::stoul(parts[1], nullptr, 10);
            entry.display_name = parts[2].empty() ? entry.model : parts[2];
            const int category = std::stoi(parts[3]);
            entry.category = category >= 0 && category < static_cast<int>(ObjectCategory::Count)
                ? static_cast<ObjectCategory>(category) : ObjectCategory::Other;
            entry.enabled = (parts[4] == "1");
            entry.max_distance = std::clamp(std::stof(parts[5]), 10.0f, 5000.0f);
            entry.show_name = (parts[6] == "1");
            entry.show_distance = (parts[7] == "1");
            entry.show_category = (parts[8] == "1");
            entry.show_box = (parts[9] == "1");
            entry.show_marker = (parts[10] == "1");
            if (parts.size() > 11) entry.color = std::stoul(parts[11], nullptr, 16);
            if (parts.size() > 12) entry.is_custom = (parts[12] == "1");
            whitelist_.push_back(std::move(entry));
        } catch (const std::exception&) {
            std::cerr << "[ObjectESP] Ignored invalid whitelist entry" << std::endl;
        }
    }
}

void ObjectESPManager::SaveAll() {
    SaveConfig();
    SaveWhitelistToDisk();
}

void ObjectESPManager::LoadAll() {
    LoadConfig();
    whitelist_.clear();
    LoadWhitelistFromDisk();
}

} // namespace object_esp
