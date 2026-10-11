#include "entity_cache.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <random>

namespace Gameplay::EntityCache {

    EntityCache::EntityCache() {
        config_ = CacheConfig{};
    }

    EntityCache::EntityCache(const CacheConfig& config) : config_(config) {}

    EntityCache::~EntityCache() {
        Clear();
    }

    void EntityCache::UpdateEntity(const EntityData& entity) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto& stored = entities_[entity.id];
        bool is_new = stored.id == 0;
        
        stored = entity;
        stored.last_update = std::chrono::steady_clock::now();
        
        if (is_new) {
            stored.first_seen = stored.last_update;
        }
        
        UpdateDerivedData(stored);
        
        // Record position history
        if (config_.enable_interpolation && !entity.position.IsZero()) {
            RecordPosition(entity.id, entity.position, entity.velocity);
        }
    }

    void EntityCache::RemoveEntity(uint64_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        entities_.erase(id);
        position_history_.erase(id);
    }

    void EntityCache::Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        entities_.clear();
        position_history_.clear();
        local_player_id_ = 0;
    }

    EntityData* EntityCache::GetEntity(uint64_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entities_.find(id);
        return it != entities_.end() ? &it->second : nullptr;
    }

    const EntityData* EntityCache::GetEntity(uint64_t id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entities_.find(id);
        return it != entities_.end() ? &it->second : nullptr;
    }

    EntityData* EntityCache::GetOrCreateEntity(uint64_t id, EntityType type) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& entity = entities_[id];
        if (entity.id == 0) {
            entity.id = id;
            entity.type = type;
            entity.first_seen = std::chrono::steady_clock::now();
        }
        entity.last_update = std::chrono::steady_clock::now();
        return &entity;
    }

    std::vector<EntityData*> EntityCache::GetEntities(EntityType type) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<EntityData*> result;
        for (auto& [id, entity] : entities_) {
            if (type == EntityType::Player || entity.type == type) {
                result.push_back(&entity);
            }
        }
        return result;
    }

    std::vector<EntityData*> EntityCache::GetEntitiesInRange(const Vec3& center, float radius, EntityType type) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<EntityData*> result;
        float radius_sq = radius * radius;
        
        for (auto& [id, entity] : entities_) {
            if (type != EntityType::Player && entity.type != type) continue;
            if (!entity.is_alive) continue;
            
            float dist_sq = (entity.position - center).Length2D();
            dist_sq *= dist_sq;
            if (dist_sq <= radius * radius) {
                result.push_back(&entity);
            }
        }
        return result;
    }

    std::vector<EntityData*> EntityCache::GetVisibleEntities(EntityType type) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<EntityData*> result;
        for (auto& [id, entity] : entities_) {
            if (type != EntityType::Player && entity.type != type) continue;
            if (entity.is_visible && entity.is_alive) {
                result.push_back(&entity);
            }
        }
        return result;
    }

    std::vector<EntityData*> EntityCache::GetEntitiesByTeam(int team, EntityType type) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<EntityData*> result;
        for (auto& [id, entity] : entities_) {
            if (type != EntityType::Player && entity.type != type) continue;
            if (entity.team == team && entity.is_alive) {
                result.push_back(&entity);
            }
        }
        return result;
    }

    Vec3 EntityCache::GetInterpolatedPosition(uint64_t id, float alpha) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = position_history_.find(id);
        if (it == position_history_.end() || it->second.size() < 2) {
            auto* entity = GetEntity(id);
            return entity ? entity->position : Vec3{};
        }
        
        const auto& history = it->second;
        if (history.size() < 2) return history.back().position;
        
        // Find two closest timestamps
        // Simplified: just interpolate between last two
        const auto& p1 = history[history.size() - 2];
        const auto& p2 = history[history.size() - 1];
        
        return p1.position + (p2.position - p1.position) * alpha;
    }

    Vec3 EntityCache::GetPredictedPosition(uint64_t id, float time_ahead) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entities_.find(id);
        if (it == entities_.end()) return Vec3{};
        
        const auto& entity = it->second;
        return entity.position + entity.velocity * time_ahead;
    }

    void EntityCache::RecordPosition(uint64_t id, const Vec3& pos, const Vec3& vel) {
        if (!config_.enable_interpolation) return;
        
        auto& history = position_history_[id];
        
        // Check if position changed enough
        if (!history.empty()) {
            const auto& last = history.back();
            float dist = (pos - last.position).Length();
            if (dist < config_.position_threshold) return;
        }
        
        // BUG-017 Fix: Check capacity BEFORE push to avoid transient overshoot
        if (history.size() >= config_.max_history_per_entity) {
            history.erase(history.begin()); // Remove oldest
        }
        
        HistoryEntry entry;
        entry.position = pos;
        entry.velocity = vel;
        entry.timestamp = std::chrono::steady_clock::now();
        
        history.push_back(entry);
        PruneHistory(id); // Still prune by time
    }

    std::vector<Vec3> EntityCache::GetPositionHistory(uint64_t id, float time_range) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = position_history_.find(id);
        if (it == position_history_.end()) return {};
        
        auto now = std::chrono::steady_clock::now();
        float max_age = time_range;
        
        std::vector<Vec3> result;
        for (const auto& entry : it->second) {
            float age = std::chrono::duration<float>(now - entry.timestamp).count();
            if (age <= max_age) {
                result.push_back(entry.position);
            }
        }
        return result;
    }

    EntityCache::Stats EntityCache::GetStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        Stats stats;
        stats.total_entities = entities_.size();
        
        auto now = std::chrono::steady_clock::now();
        for (const auto& [id, entity] : entities_) {
            if (entity.is_alive) stats.active_entities++;
            float age = std::chrono::duration<float>(now - entity.last_update).count();
            if (age > config_.max_age) stats.stale_entities++;
        }
        
        // BUG-010 Fix: More accurate memory estimation including container overhead
        size_t entities_map_overhead = entities_.bucket_count() * sizeof(void*); // bucket array
        size_t entities_node_overhead = entities_.size() * (sizeof(void*) * 3 + 1); // approx node (ptr + ptr + hash + flags)
        
        size_t history_map_overhead = position_history_.bucket_count() * sizeof(void*);
        size_t history_node_overhead = position_history_.size() * (sizeof(void*) * 3 + 1);
        
        // Estimate string allocations (name + model)
        size_t string_overhead = 0;
        for (const auto& [id, entity] : entities_) {
            string_overhead += entity.name.capacity() + entity.model.capacity();
            for (const auto& [k, v] : entity.custom_data) {
                string_overhead += k.capacity() + v.capacity();
            }
        }
        
        // Estimate history vector allocations
        size_t history_vec_overhead = 0;
        for (const auto& [id, hist] : position_history_) {
            history_vec_overhead += hist.capacity() * sizeof(HistoryEntry);
        }
        
        size_t total_bytes = entities_.size() * sizeof(EntityData) +
                             entities_map_overhead + entities_node_overhead +
                             history_map_overhead + history_node_overhead +
                             string_overhead + history_vec_overhead;
        
        stats.memory_usage_mb = static_cast<float>(total_bytes) / (1024.0f * 1024.0f);
        
        return stats;
    }

    void EntityCache::Cleanup() {
        std::lock_guard<std::mutex> lock(mutex_);
        CleanupStaleEntities();
    }

    void EntityCache::Update(float dt) {
        std::lock_guard<std::mutex> lock(mutex_);
        CleanupStaleEntities();
    }

    void EntityCache::CleanupStaleEntities() {
        auto now = std::chrono::steady_clock::now();
        
        for (auto it = entities_.begin(); it != entities_.end();) {
            float age = std::chrono::duration<float>(now - it->second.last_update).count();
            if (age > config_.max_age) {
                position_history_.erase(it->first);
                it = entities_.erase(it);
            } else {
                ++it;
            }
        }
        
        // Prune history
        for (auto& [id, history] : position_history_) {
            PruneHistory(id);
        }
    }

    void EntityCache::PruneHistory(uint64_t id) {
        auto it = position_history_.find(id);
        if (it == position_history_.end()) return;
        
        auto& history = it->second;
        if (history.size() > config_.max_history_per_entity) {
            history.erase(history.begin(), history.end() - config_.max_history_per_entity);
        }
        
        // Remove old entries
        auto now = std::chrono::steady_clock::now();
        float max_age = config_.max_age * 2.0f; // Keep history longer than entity
        
        history.erase(
            std::remove_if(history.begin(), history.end(),
                [&](const HistoryEntry& entry) {
                    float age = std::chrono::duration<float>(now - entry.timestamp).count();
                    return age > max_age;
                }),
            history.end());
        
        if (history.empty()) {
            position_history_.erase(it);
        }
    }

    void EntityCache::UpdateDerivedData(EntityData& entity) {
        // Distance to local player
        if (local_player_id_ != 0 && entity.id != local_player_id_) {
            auto* local = GetEntity(local_player_id_);
            if (local) {
                entity.distance_to_local = entity.position.DistTo(local->position);
            }
        }
    }

    std::string EntityCache::Serialize() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream out;
        out << "ENTITY_CACHE_V1\n";
        out << entities_.size() << "\n";
        
        for (const auto& [id, entity] : entities_) {
            out << id << "|"
                << static_cast<int>(entity.type) << "|"
                << entity.name << "|"
                << entity.model << "|"
                << entity.position.x << "," << entity.position.y << "," << entity.position.z << "|"
                << entity.velocity.x << "," << entity.velocity.y << "," << entity.velocity.z << "|"
                << entity.angles.x << "," << entity.angles.y << "," << entity.angles.z << "|"
                << entity.health << "|"
                << entity.max_health << "|"
                << entity.armor << "|"
                << entity.team << "|"
                << entity.is_local << "|"
                << entity.is_friendly << "|"
                << entity.is_visible << "|"
                << entity.is_alive << "|"
                << entity.is_dormant << "|"
                << entity.flags << "|"
                // BUG-022 Fix: Serialize as steady_clock milliseconds (monotonic, NTP-safe)
                << std::chrono::duration_cast<std::chrono::milliseconds>(entity.last_update.time_since_epoch()).count() << "|"
                << std::chrono::duration_cast<std::chrono::milliseconds>(entity.first_seen.time_since_epoch()).count() << "\n";
        }
        return out.str();
    }

    bool EntityCache::Deserialize(const std::string& data) {
        std::istringstream in(data);
        std::string line;
        
        if (!std::getline(in, line)) return false;
        if (line != "ENTITY_CACHE_V1") return false;
        
        std::getline(in, line);
        int count = std::stoi(line);
        
        std::lock_guard<std::mutex> lock(mutex_);
        entities_.clear();
        entities_.reserve(count);
        
        for (int i = 0; i < count; ++i) {
            std::string line;
            std::getline(in, line);
            
            std::istringstream ss(line);
            std::string token;
            EntityData entity;
            
            std::getline(ss, token, '|'); entity.id = std::stoull(token);
            std::getline(ss, token, '|'); entity.type = static_cast<EntityType>(std::stoi(token));
            std::getline(ss, entity.name, '|');
            std::getline(ss, entity.model, '|');
            
            std::getline(ss, token, '|'); 
            size_t c1 = token.find(','), c2 = token.find(',', c1 + 1);
            entity.position.x = std::stof(token.substr(0, c1));
            entity.position.y = std::stof(token.substr(c1 + 1, c2 - c1 - 1));
            entity.position.z = std::stof(token.substr(c2 + 1));
            
            std::getline(ss, token, '|');
            c1 = token.find(','); c2 = token.find(',', c1 + 1);
            entity.velocity.x = std::stof(token.substr(0, c1));
            entity.velocity.y = std::stof(token.substr(c1 + 1, c2 - c1 - 1));
            entity.velocity.z = std::stof(token.substr(c2 + 1));
            
            std::getline(ss, token, '|');
            c1 = token.find(','); c2 = token.find(',', c1 + 1);
            entity.angles.x = std::stof(token.substr(0, c1));
            entity.angles.y = std::stof(token.substr(c1 + 1, c2 - c1 - 1));
            entity.angles.z = std::stof(token.substr(c2 + 1));
            
            std::getline(ss, token, '|'); entity.health = std::stof(token);
            std::getline(ss, token, '|'); entity.max_health = std::stof(token);
            std::getline(ss, token, '|'); entity.armor = std::stof(token);
            std::getline(ss, token, '|'); entity.team = std::stoi(token);
            std::getline(ss, token, '|'); entity.is_local = token == "1";
            std::getline(ss, token, '|'); entity.is_friendly = token == "1";
            std::getline(ss, token, '|'); entity.is_visible = token == "1";
            std::getline(ss, token, '|'); entity.is_alive = token == "1";
            std::getline(ss, token, '|'); entity.is_dormant = token == "1";
            std::getline(ss, token, '|'); entity.flags = std::stoul(token);
            std::getline(ss, token, '|'); 
            // BUG-022 Fix: Deserialize timestamps stored as steady_clock milliseconds since epoch
            uint64_t last_update_ms = std::stoull(token);
            entity.last_update = std::chrono::steady_clock::time_point(std::chrono::milliseconds(last_update_ms));
            std::getline(ss, token, '|'); 
            uint64_t first_seen_ms = std::stoull(token);
            entity.first_seen = std::chrono::steady_clock::time_point(std::chrono::milliseconds(first_seen_ms));
            
            entities_[entity.id] = entity;
        }
        return true;
    }

    // EntityPool implementation
    EntityPool::EntityPool(size_t initial_size) {
        pool_.reserve(initial_size);
        for (size_t i = 0; i < initial_size; ++i) {
            pool_.push_back(std::make_unique<EntityData>());
            free_list_.push_back(pool_.back().get());
        }
    }

    EntityPool::~EntityPool() = default;

    EntityData* EntityPool::Acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (free_list_.empty()) {
            pool_.push_back(std::make_unique<EntityData>());
            free_list_.push_back(pool_.back().get());
        }
        EntityData* entity = free_list_.back();
        free_list_.pop_back();
        *entity = EntityData{}; // Reset
        return entity;
    }

    void EntityPool::Release(EntityData* entity) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (entity) {
            *entity = EntityData{};
            free_list_.push_back(entity);
        }
    }

    size_t EntityPool::Available() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return free_list_.size();
    }

    size_t EntityPool::InUse() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pool_.size() - free_list_.size();
    }

    size_t EntityPool::Total() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pool_.size();
    }

    void EntityPool::Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        free_list_.clear();
        for (auto& entity : pool_) {
            free_list_.push_back(entity.get());
        }
    }

    // SpatialHash implementation
    SpatialHash::SpatialHash(float cell_size) {
        SetCellSize(cell_size);
    }

    void SpatialHash::Insert(uint64_t entity_id, const Vec3& position) {
        int x, y, z;
        GetCellCoords(position, x, y, z);
        int64_t hash = HashCell(x, y, z);
        grid_[hash].entities.push_back(entity_id);
        entity_to_cell_[entity_id] = hash;
    }

    void SpatialHash::Remove(uint64_t entity_id) {
        auto it = entity_to_cell_.find(entity_id);
        if (it == entity_to_cell_.end()) return;
        
        int64_t hash = it->second;
        auto cell_it = grid_.find(hash);
        if (cell_it != grid_.end()) {
            auto& entities = cell_it->second.entities;
            entities.erase(std::remove(entities.begin(), entities.end(), entity_id), entities.end());
            
            // Clean up empty cells
            if (entities.empty()) {
                grid_.erase(cell_it);
            }
        }
        entity_to_cell_.erase(it);
    }

    void SpatialHash::Update(uint64_t entity_id, const Vec3& old_pos, const Vec3& new_pos) {
        Remove(entity_id);
        Insert(entity_id, new_pos);
    }

    std::vector<uint64_t> SpatialHash::QueryRadius(const Vec3& center, float radius) const {
        std::vector<uint64_t> results;
        int cx, cy, cz;
        GetCellCoords(center, cx, cy, cz);
        
        int cell_radius = (int)ceil(radius / cell_size_);
        
        for (int x = -cell_radius; x <= cell_radius; ++x) {
            for (int y = -cell_radius; y <= cell_radius; ++y) {
                for (int z = -cell_radius; z <= cell_radius; ++z) {
                    int64_t hash = HashCell(cx + x, cy + y, cz + z);
                    auto it = grid_.find(hash);
                    if (it != grid_.end()) {
                        for (uint64_t id : it->second.entities) {
                            results.push_back(id);
                        }
                    }
                }
            }
        }
        return results;
    }

    std::vector<uint64_t> SpatialHash::QueryBox(const Vec3& min, const Vec3& max) const {
        std::vector<uint64_t> results;
        
        int min_x, min_y, min_z;
        int max_x, max_y, max_z;
        GetCellCoords(min, min_x, min_y, min_z);
        GetCellCoords(max, max_x, max_y, max_z);
        
        for (int x = min_x; x <= max_x; ++x) {
            for (int y = min_y; y <= max_y; ++y) {
                for (int z = min_z; z <= max_z; ++z) {
                    int64_t hash = HashCell(x, y, z);
                    auto it = grid_.find(hash);
                    if (it != grid_.end()) {
                        results.insert(results.end(), it->second.entities.begin(), it->second.entities.end());
                    }
                }
            }
        }
        return results;
    }

    std::vector<uint64_t> SpatialHash::QueryFrustum(const Frustum& frustum) const {
        // Simplified - would check frustum intersection with cells
        return {};
    }

    void SpatialHash::Clear() {
        grid_.clear();
        entity_to_cell_.clear();
    }
    }

    void SpatialHash::SetCellSize(float size) {
        cell_size_ = std::max(1.0f, size);
        inv_cell_size_ = 1.0f / cell_size_;
    }

    int64_t SpatialHash::HashCell(int x, int y, int z) const {
        // Simple hash combining
        return ((int64_t)x * 73856093) ^ ((int64_t)y * 19349663) ^ ((int64_t)z * 83492791);
    }

    void SpatialHash::GetCellCoords(const Vec3& pos, int& x, int& y, int& z) const {
        x = (int)floor(pos.x * inv_cell_size_);
        y = (int)floor(pos.y * inv_cell_size_);
        z = (int)floor(pos.z * inv_cell_size_);
    }

    SpatialHash::Stats SpatialHash::GetStats() const {
        Stats stats;
        stats.total_entities = 0;
        stats.occupied_cells = grid_.size();
        
        for (const auto& [hash, cell] : grid_) {
            stats.total_entities += cell.entities.size();
        }
        stats.avg_entities_per_cell = stats.occupied_cells > 0 ? 
            (float)stats.total_entities / stats.occupied_cells : 0.0f;
        return stats;
    }

    // Serialization
    std::string SerializeEntityData(const EntityData& entity) {
        std::ostringstream out;
        out << entity.id << "|"
            << static_cast<int>(entity.type) << "|"
            << entity.name << "|"
            << entity.model << "|"
            << entity.position.x << "," << entity.position.y << "," << entity.position.z << "|"
            << entity.velocity.x << "," << entity.velocity.y << "," << entity.velocity.z << "|"
            << entity.angles.x << "," << entity.angles.y << "," << entity.angles.z << "|"
            << entity.health << "|"
            << entity.max_health << "|"
            << entity.armor << "|"
            << entity.team << "|"
            << entity.is_local << "|"
            << entity.is_friendly << "|"
            << entity.is_visible << "|"
            << entity.is_alive << "|"
            << entity.is_dormant << "|"
            << entity.flags << "|"
            // BUG-022 Fix: Serialize as steady_clock milliseconds (monotonic, NTP-safe)
            << std::chrono::duration_cast<std::chrono::milliseconds>(entity.last_update.time_since_epoch()).count() << "|"
            << std::chrono::duration_cast<std::chrono::milliseconds>(entity.first_seen.time_since_epoch()).count();
        return out.str();
    }

    bool DeserializeEntityData(const std::string& data, EntityData& entity) {
        std::istringstream ss(data);
        std::string token;
        
        std::getline(ss, token, '|'); entity.id = std::stoull(token);
        std::getline(ss, token, '|'); entity.type = static_cast<EntityType>(std::stoi(token));
        std::getline(ss, entity.name, '|');
        std::getline(ss, entity.model, '|');
        
        std::getline(ss, token, '|'); 
        size_t c1 = token.find(','), c2 = token.find(',', c1 + 1);
        entity.position.x = std::stof(token.substr(0, c1));
        entity.position.y = std::stof(token.substr(c1 + 1, c2 - c1 - 1));
        entity.position.z = std::stof(token.substr(c2 + 1));
        
        std::getline(ss, token, '|');
        c1 = token.find(','); c2 = token.find(',', c1 + 1);
        entity.velocity.x = std::stof(token.substr(0, c1));
        entity.velocity.y = std::stof(token.substr(c1 + 1, c2 - c1 - 1));
        entity.velocity.z = std::stof(token.substr(c2 + 1));
        
        std::getline(ss, token, '|');
        c1 = token.find(','); c2 = token.find(',', c1 + 1);
        entity.angles.x = std::stof(token.substr(0, c1));
        entity.angles.y = std::stof(token.substr(c1 + 1, c2 - c1 - 1));
        entity.angles.z = std::stof(token.substr(c2 + 1));
        
        std::getline(ss, token, '|'); entity.health = std::stof(token);
        std::getline(ss, token, '|'); entity.max_health = std::stof(token);
        std::getline(ss, token, '|'); entity.armor = std::stof(token);
        std::getline(ss, token, '|'); entity.team = std::stoi(token);
        std::getline(ss, token, '|'); entity.is_local = token == "1";
        std::getline(ss, token, '|'); entity.is_friendly = token == "1";
        std::getline(ss, token, '|'); entity.is_visible = token == "1";
        std::getline(ss, token, '|'); entity.is_alive = token == "1";
        std::getline(ss, token, '|'); entity.is_dormant = token == "1";
        std::getline(ss, token, '|'); entity.flags = std::stoul(token);
        std::getline(ss, token, '|'); 
        // BUG-022 Fix: Deserialize timestamps stored as steady_clock milliseconds since epoch
        uint64_t last_update_ms = std::stoull(token);
        entity.last_update = std::chrono::steady_clock::time_point(std::chrono::milliseconds(last_update_ms));
        std::getline(ss, token, '|'); 
        uint64_t first_seen_ms = std::stoull(token);
        entity.first_seen = std::chrono::steady_clock::time_point(std::chrono::milliseconds(first_seen_ms));
        
        return true;
    }

} // namespace Gameplay::EntityCache