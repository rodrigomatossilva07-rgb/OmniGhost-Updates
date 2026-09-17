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
    : scanner_state_(), stats_(), inspector_open_(false), current_filter_(ObjectCategory::All), selected_model_() {
    // Initialize category visibility
    for (int i = 0; i < static_cast<int>(ObjectCategory::Count); ++i) {
        config_.category_visible[i] = true;
    }
    
    renderer_ = std::make_unique<ObjectRenderer>();
    renderer_->Initialize();
}

ObjectESPManager::~ObjectESPManager() {
    Shutdown();
    g_manager = nullptr;
}

bool ObjectESPManager::Initialize() {
    if (initialized_)
        return true;

    if (!renderer_) {
        renderer_ = std::make_unique<ObjectRenderer>();
        renderer_->Initialize();
    }
    LoadAll();
    initialized_ = true;
    std::cout << "[ObjectESP] Initialized (frame-snapshot scanner)" << std::endl;
    return true;
}

void ObjectESPManager::Shutdown() {
    if (!initialized_)
        return;

    scanner_state_.scanning = false;
    scanner_state_.scan_complete = true;
    renderer_.reset();
    SaveAll();
    std::lock_guard<std::mutex> lock(data_mutex_);
    tracked_objects_.clear();
    scan_results_.clear();
    initialized_ = false;
    std::cout << "[ObjectESP] Shutdown complete" << std::endl;
}

void ObjectESPManager::Update() {
    try {
    auto start_time = std::chrono::high_resolution_clock::now();

    // A scan is requested by the UI but executed here, on the same serialized
    // frame that has just refreshed FiveM::ESP::{validPeds,positions}.  The old
    // background thread was never started by the game lifecycle and could leave
    // the UI permanently at 0%; it also raced the frame containers.
    if (initialized_ && scanner_state_.scanning) {
        const auto scan_start = std::chrono::high_resolution_clock::now();
        PerformScan();
        const auto scan_end = std::chrono::high_resolution_clock::now();
        stats_.last_scan_time_ms = std::chrono::duration<float, std::milli>(scan_end - scan_start).count();
        scanner_state_.status_message = "Object discovery is unavailable for this FiveM build";
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
    } catch (const std::exception& ex) {
        scanner_state_.scanning = false;
        scanner_state_.scan_complete = true;
        scanner_state_.error_message = "Object ESP update failed safely";
        scanner_state_.status_message = scanner_state_.error_message;
        std::cerr << "[ObjectESP] Update failed: " << ex.what() << std::endl;
    } catch (...) {
        scanner_state_.scanning = false;
        scanner_state_.scan_complete = true;
        scanner_state_.error_message = "Object ESP update failed safely";
        scanner_state_.status_message = scanner_state_.error_message;
        std::cerr << "[ObjectESP] Update failed with an unknown exception" << std::endl;
    }
}

void ObjectESPManager::StartScan(float radius) {
    if (scanner_state_.scanning) return;
    if (!initialized_) {
        scanner_state_.Reset();
        scanner_state_.scan_complete = true;
        scanner_state_.status_message = "Start a FiveM session before scanning";
        std::cout << "[ObjectESP] Scan rejected: FiveM session is not initialized" << std::endl;
        return;
    }
    if (!HasValidatedDiscoverySource()) {
        scanner_state_.Reset();
        scanner_state_.scan_complete = true;
        scanner_state_.status_message = "Object discovery is not validated for this FiveM build";
        scanner_state_.error_message = scanner_state_.status_message;
        std::cout << "[ObjectESP] Scan rejected: no validated object discovery source" << std::endl;
        return;
    }
    
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
    }
    
    std::cout << "[ObjectESP] Inspector opened for: " << model << std::endl;
}

void ObjectESPManager::CloseInspector() {
    inspector_open_ = false;
    selected_model_.clear();
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
    // object_pool set by game_setup for supported builds (e.g. b3258).
    return base != 0 && object_pool != 0;
}

namespace {

bool ReadU64Safe(uintptr_t addr, uintptr_t& out) {
    out = 0;
    if (!addr) return false;
    return mem.Read(addr, &out, sizeof(out)) && out > 0x10000ULL && out < 0x00007FFFFFFFFFFFULL;
}

bool ReadVec3Safe(uintptr_t addr, Vec3& out) {
    out = {};
    if (!addr) return false;
    return mem.Read(addr, &out, sizeof(out));
}

bool LooksFinite(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z)
        && std::fabs(v.x) < 50000.f && std::fabs(v.y) < 50000.f && std::fabs(v.z) < 50000.f;
}

std::string HashToModelLabel(uint32_t hash) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%08X", hash);
    return buf;
}

// Prefer known prop name patterns; otherwise keep hex hash as model id.
std::string GuessModelName(uint32_t hash) {
    return HashToModelLabel(hash);
}

} // namespace

void ObjectESPManager::PerformScan() {
    try {
    using namespace FiveM::offset;
    std::lock_guard<std::mutex> lock(data_mutex_);
    scan_results_.clear();
    scanner_state_.total_entities_scanned = 0;
    scanner_state_.unique_models_found = 0;
    scanner_state_.total_objects_found = 0;
    scanner_state_.scan_progress = 0.05f;

    if (!HasValidatedDiscoverySource()) {
        scanner_state_.scanning = false;
        scanner_state_.scan_complete = true;
        scanner_state_.status_message = "Object pool unavailable";
        scanner_state_.scan_progress = 1.0f;
        return;
    }

    Vec3 localPos{};
    if (localplayer)
        ReadVec3Safe(localplayer + playerPosition, localPos);

    // Resolve pool: object_pool may be a pointer-to-pool or the pool base itself.
    uintptr_t pool = 0;
    if (!ReadU64Safe(object_pool, pool))
        pool = object_pool;

    uintptr_t items = 0;
    uintptr_t flags = 0;
    uint32_t size = 0;
    uint32_t itemSize = 0;

    // rage::fwBasePool layout (common external FiveM)
    ReadU64Safe(pool + 0x0, items);
    ReadU64Safe(pool + 0x8, flags);
    mem.Read(pool + 0x10, &size, sizeof(size));
    mem.Read(pool + 0x14, &itemSize, sizeof(itemSize));

    // Fallback: some builds store pool pointer one indirection deeper
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
        scanner_state_.scanning = false;
        scanner_state_.scan_complete = true;
        scanner_state_.status_message = "Object pool layout not readable";
        scanner_state_.error_message = scanner_state_.status_message;
        scanner_state_.scan_progress = 1.0f;
        std::cout << "[ObjectESP] Pool unreadable pool=0x" << std::hex << pool
                  << " items=0x" << items << " size=" << std::dec << size << std::endl;
        return;
    }
    if (itemSize == 0 || itemSize > 0x4000)
        itemSize = 0x10; // treat as pointer table

    const float maxR = config_.scan_radius > 1.f ? config_.scan_radius : 500.f;
    const float maxR2 = maxR * maxR;

    struct Acc {
        ScanResult result;
    };
    std::unordered_map<uint32_t, Acc> byHash;
    byHash.reserve(256);

    const uint32_t maxIter = (std::min)(size, 4000u); // budgeted — avoid 20k sync slots
    // Time-budgeted scan: process up to maxIter but yield after ~3ms of work.
    const auto scanDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3);
    for (uint32_t i = 0; i < maxIter; ++i) {
        if ((i & 0x3F) == 0 && i > 0 && std::chrono::steady_clock::now() >= scanDeadline)
            break;
        if ((i & 0xFF) == 0)
            scanner_state_.scan_progress = 0.05f + 0.9f * (float)i / (float)maxIter;

        if (flags) {
            uint8_t bit = 0;
            if (mem.Read(flags + i, &bit, 1) && (bit & 0x80))
                continue; // free slot
        }

        uintptr_t ent = 0;
        // Pointer table vs inline entities
        if (itemSize <= 0x20) {
            if (!ReadU64Safe(items + (uintptr_t)i * itemSize, ent))
                continue;
        } else {
            ent = items + (uintptr_t)i * itemSize;
        }
        if (!ent || ent < 0x10000ULL)
            continue;

        ++scanner_state_.total_entities_scanned;

        // Position: CEntity/CPhysical +0x90 (FiveM ped/object convention in this project)
        Vec3 pos{};
        if (!ReadVec3Safe(ent + playerPosition, pos) || !LooksFinite(pos))
            continue;

        float dist2 = 0.f;
        if (!localPos.IsZero()) {
            const float dx = pos.x - localPos.x;
            const float dy = pos.y - localPos.y;
            const float dz = pos.z - localPos.z;
            dist2 = dx * dx + dy * dy + dz * dz;
            if (dist2 > maxR2)
                continue;
        }

        // Model hash: try CEntity model info pointer chain
        uint32_t hash = 0;
        uintptr_t modelInfo = 0;
        if (ReadU64Safe(ent + 0x20, modelInfo) && modelInfo) {
            mem.Read(modelInfo + 0x18, &hash, sizeof(hash));
        }
        if (!hash)
            mem.Read(ent + 0x18, &hash, sizeof(hash));
        if (!hash)
            continue;

        const float dist = localPos.IsZero() ? 0.f : std::sqrt(dist2);
        auto& acc = byHash[hash];
        if (acc.result.hash == 0) {
            acc.result.hash = hash;
            acc.result.model = GuessModelName(hash);
            acc.result.category = ObjectCategory::Other;
            // Heuristic: very high hashes often custom streamed assets
            acc.result.is_custom = (hash > 0x10000000u);
        }
        acc.result.count++;
        if (acc.result.sample_positions.size() < 8)
            acc.result.sample_positions.push_back(pos);
        if (acc.result.count == 1 || dist < acc.result.nearest_distance)
            acc.result.nearest_distance = dist;
    }

    scan_results_.clear();
    scan_results_.reserve(byHash.size());
    for (auto& kv : byHash)
        scan_results_.push_back(std::move(kv.second.result));

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
    std::cout << "[ObjectESP] " << msg << std::endl;
    } catch (const std::exception& ex) {
        scanner_state_.scanning = false;
        scanner_state_.scan_complete = true;
        scanner_state_.status_message = "Scan failed";
        scanner_state_.error_message = ex.what();
        std::cerr << "[ObjectESP] PerformScan exception: " << ex.what() << std::endl;
    } catch (...) {
        scanner_state_.scanning = false;
        scanner_state_.scan_complete = true;
        scanner_state_.status_message = "Scan failed";
        scanner_state_.error_message = "unknown";
        std::cerr << "[ObjectESP] PerformScan unknown exception" << std::endl;
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
