#include "object_esp.h"
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

namespace fs = std::filesystem;

namespace object_esp {

// Global instance
static ObjectESPManager* g_manager = nullptr;

ObjectESPManager& GetObjectESPManager() {
    if (!g_manager) {
        g_manager = new ObjectESPManager();
    }
    return *g_manager;
}

// ============================================================================
// ObjectESPManager Implementation
// ============================================================================

ObjectESPManager::ObjectESPManager() 
    : scanner_state_(), stats_(), inspector_open_(false), current_filter_(ObjectCategory::All) {
    // Initialize category visibility
    for (int i = 0; i < static_cast<int>(ObjectCategory::Count); ++i) {
        config_.category_visible[i] = true;
    }
}

ObjectESPManager::~ObjectESPManager() {
    Shutdown();
    delete g_manager;
    g_manager = nullptr;
}

bool ObjectESPManager::Initialize() {
    LoadAll();
    
    // Start scanner thread
    scanner_running_ = true;
    scanner_thread_ = std::thread(&ObjectESPManager::ScannerThread, this);
    
    std::cout << "[ObjectESP] Initialized" << std::endl;
    return true;
}

void ObjectESPManager::Shutdown() {
    scanner_running_ = false;
    if (scanner_thread_.joinable()) {
        scanner_thread_.join();
    }
    
    SaveAll();
    
    std::lock_guard<std::mutex> lock(data_mutex_);
    tracked_objects_.clear();
    scan_results_.clear();
    
    std::cout << "[ObjectESP] Shutdown complete" << std::endl;
}

void ObjectESPManager::Update() {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    if (!config_.enabled) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(data_mutex_);
    
    // Update tracked objects from current scan results and whitelist
    UpdateTrackedObjects();
    
    // Apply culling
    if (config_.distance_culling) ApplyDistanceCulling();
    if (config_.frustum_culling) ApplyFrustumCulling();
    
    // Prune stale objects
    PruneStaleObjects();
    
    // Update inspector if open
    if (inspector_open_) {
        UpdateInspector();
    }
    
    // Update stats
    stats_.tracked_objects = static_cast<int>(tracked_objects_.size());
    stats_.rendered_objects = 0; // Will be updated by renderer
    
    auto end_time = std::chrono::high_resolution_clock::now();
    stats_.update_time_ms = std::chrono::duration<float, std::milli>(end_time - start_time).count();
}

void ObjectESPManager::StartScan(float radius) {
    if (scanner_state_.scanning) return;
    
    scanner_state_.Reset();
    scanner_state_.scanning = true;
    scanner_state_.scan_start_time = std::chrono::steady_clock::now();
    scanner_state_.status_message = "Scanning objects...";
    config_.scan_radius = radius;
    
    std::cout << "[ObjectESP] Scan started, radius: " << radius << "m" << std::endl;
}

void ObjectESPManager::StopScan() {
    scanner_state_.scanning = false;
    scanner_state_.scan_complete = true;
    scanner_state_.status_message = "Scan complete";
    std::cout << "[ObjectESP] Scan stopped" << std::endl;
}

void ObjectESPManager::ClearScanResults() {
    std::lock_guard<std::mutex> lock(data_mutex_);
    scan_results_.clear();
    scanner_state_.unique_models_found = 0;
    scanner_state_.total_objects_found = 0;
}

const std::vector<ScanResult> ObjectESPManager::GetFilteredResults() const {
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
    }
    
    std::cout << "[ObjectESP] Inspector opened for: " << model << std::endl;
}

void ObjectESPManager::CloseInspector() {
    inspector_open_ = false;
    selected_model_.clear();
}

void ObjectESPManager::SetCategoryFilter(ObjectCategory cat) {
    current_filter_ = cat;
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

void ObjectESPManager::SetSearchQuery(const std::string& query) {
    search_query_ = query;
}

void ObjectESPManager::ScannerThread() {
    while (scanner_running_) {
        if (scanner_state_.scanning) {
            auto scan_start = std::chrono::high_resolution_clock::now();
            
            // Perform scan using DMA
            PerformScan();
            
            auto scan_end = std::chrono::high_resolution_clock::now();
            stats_.last_scan_time_ms = std::chrono::duration<float, std::milli>(scan_end - scan_start).count();
            
            scanner_state_.scanning = false;
            scanner_state_.scan_complete = true;
            scanner_state_.status_message = "Scan complete";
        }
        
        // Sleep between scans
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.scan_interval_ms));
    }
}

void ObjectESPManager::PerformScan() {
    // This would use DMA to scan the world for objects
    // For now, we'll simulate with the entity list from ESP manager
    
    std::lock_guard<std::mutex> lock(data_mutex_);
    scan_results_.clear();
    scanner_state_.total_entities_scanned = 0;
    scanner_state_.unique_models_found = 0;
    scanner_state_.total_objects_found = 0;
    
    // Get entities from FiveM ESP manager
    using namespace FiveM::ESP;
    if (!validPeds.empty() && !positions.empty()) {
        // For now, we'll use the ped list as a proxy for objects
        // In a real implementation, we'd scan the entity pool
        
        // Create a map to group by model
        std::unordered_map<std::string, ScanResult> model_map;
        
        for (size_t i = 0; i < validPeds.size() && i < positions.size(); ++i) {
            uintptr_t ped = validPeds[i];
            const Vec3& pos = positions[i];
            
            if (pos.IsZero()) continue;
            
            scanner_state_.total_entities_scanned++;
            
            // Try to get model name from entity
            std::string model = "Unknown";
            uint32_t hash = 0;
            
            // Try to read model info from the entity
            // This would require reading the entity's model info
            // For now, we'll use a placeholder
            
            // Calculate distance to local player
            float dist = 0.0f;
            if (offset::localplayer) {
                Vec3 localPos = mem.Read<Vec3>(offset::localplayer + offset::playerPosition);
                if (!localPos.IsZero()) {
                    dist = pos.distance_to(localPos);
                }
            }
            
            if (dist > config_.scan_radius) continue;
            
            scanner_state_.total_objects_found++;
            
            // Group by model
            auto& result = model_map[model];
            if (result.model.empty()) {
                result.model = model;
                result.hash = hash;
                result.nearest_distance = dist;
                scanner_state_.unique_models_found++;
            }
            result.count++;
            if (dist < result.nearest_distance || result.nearest_distance == 0) {
                result.nearest_distance = dist;
            }
            if (result.sample_positions.size() < 5) {
                result.sample_positions.push_back(pos);
            }
        }
        
        // Convert map to vector
        scan_results_.clear();
        for (auto& pair : model_map) {
            scan_results_.push_back(std::move(pair.second));
        }
        
        // Sort by count descending
        std::sort(scan_results_.begin(), scan_results_.end(),
            [](const ScanResult& a, const ScanResult& b) {
                return a.count > b.count;
            });
        
        // Limit results
        if (scan_results_.size() > config_.max_scan_results) {
            scan_results_.resize(config_.max_scan_results);
        }
        
        scanner_state_.unique_models_found = static_cast<int>(scan_results_.size());
        scanner_state_.scan_progress = 1.0f;
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
            if (result.model == entry.model || result.hash == entry.hash) {
                // Create tracked instances for each position
                for (const auto& pos : result.sample_positions) {
                    ObjectEntity entity;
                    entity.model = result.model;
                    entity.hash = result.hash;
                    entity.position = pos;
                    entity.distance = result.nearest_distance;
                    
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
    
    // Would check if object is in view frustum
    // For now, we skip this
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

void ObjectESPManager::UpdateInspector() {
    if (selected_model_.empty()) return;
    
    // Update inspector data with current info
    auto tracked_it = std::find_if(tracked_objects_.begin(), tracked_objects_.end(),
        [this](const TrackedInstance& t) { return t.entity.model == selected_model_; });
    
    if (tracked_it != tracked_objects_.end()) {
        inspector_data_.position = tracked_it->entity.position;
        inspector_data_.distance = tracked_it->entity.distance;
        inspector_data_.entity_handle = tracked_it->entity.entity_handle;
        inspector_data_.network_id = tracked_it->entity.network_id;
        inspector_data_.is_networked = tracked_it->entity.is_networked;
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

TrackedInstance* ObjectESPManager::FindTrackedObject(const std::string& model) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    
    auto it = std::find_if(tracked_objects_.begin(), tracked_objects_.end(),
        [&model](const TrackedInstance& t) { return t.entity.model == model; });
    
    return it != tracked_objects_.end() ? &(*it) : nullptr;
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
        
        if (key == "enabled") config_.enabled = (val == "1");
        else if (key == "max_distance") config_.max_distance = std::stof(val);
        else if (key == "scan_radius") config_.scan_radius = std::stof(val);
        else if (key == "scan_interval_ms") config_.scan_interval_ms = std::stoi(val);
        else if (key == "auto_scan") config_.auto_scan = (val == "1");
        else if (key == "show_name") config_.show_name = (val == "1");
        else if (key == "show_distance") config_.show_distance = (val == "1");
        else if (key == "show_category") config_.show_category = (val == "1");
        else if (key == "show_box") config_.show_box = (val == "1");
        else if (key == "show_marker") config_.show_marker = (val == "1");
        else if (key == "text_scale") config_.text_scale = std::stof(val);
        else if (key == "box_thickness") config_.box_thickness = std::stof(val);
        else if (key == "distance_culling") config_.distance_culling = (val == "1");
        else if (key == "frustum_culling") config_.frustum_culling = (val == "1");
        else if (key == "max_scan_results") config_.max_scan_results = std::stoi(val);
        else if (key == "scan_interval_ms") config_.scan_interval_ms = std::stoi(val);
        else if (key == "text_scale") config_.text_scale = std::stof(val);
        else if (key == "box_thickness") config_.box_thickness = std::stof(val);
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
        
        WhitelistEntry entry;
        entry.model = parts[0];
        entry.hash = std::stoul(parts[1], nullptr, 10);
        entry.display_name = parts[2];
        entry.category = static_cast<ObjectCategory>(std::stoi(parts[3]));
        entry.enabled = (parts[4] == "1");
        entry.max_distance = std::stof(parts[5]);
        entry.show_name = (parts[6] == "1");
        entry.show_distance = (parts[7] == "1");
        entry.show_category = (parts[8] == "1");
        entry.show_box = (parts[9] == "1");
        entry.show_marker = (parts[10] == "1");
        if (parts.size() > 11) {
            entry.color = std::stoul(parts[11], nullptr, 16);
        }
        if (parts.size() > 12) {
            entry.is_custom = (parts[12] == "1");
        }
        
        whitelist_.push_back(std::move(entry));
    }
}

void ObjectESPManager::SaveAll() {
    SaveConfig();
    SaveWhitelistToDisk();
}

void ObjectESPManager::LoadAll() {
    LoadConfig();
    LoadWhitelistFromDisk();
}

} // namespace object_esp