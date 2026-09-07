#include "esp_optimizer.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <sstream>
#include <iomanip>

namespace Gameplay::ESPOptimizer {

// ============================================================================
// EntityCache Implementation
// ============================================================================

EntityCache::EntityCache(const Config& cfg) : config_(cfg) {
    last_cleanup_ = std::chrono::steady_clock::now();
    last_spatial_update_ = std::chrono::steady_clock::now();
    spatial_grid_.cell_size = 100.0f;
}

EntityCache::~EntityCache() {
    Clear();
}

void EntityCache::UpdateEntity(uint64_t id, const EntityData& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& entity = (*front_)[id];
    entity = data;
    entity.meta.last_update = std::chrono::steady_clock::now();
    entity.meta.dirty_flags = 0;
    
    // Update derived data
    UpdateDerivedData(entity);
    
    // Mark for spatial update
    spatial_grid_.Insert(id, entity.transform.position);
}

void EntityCache::RemoveEntity(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    front_->erase(id);
    back_buffer_.erase(id);
    spatial_grid_.Remove(id);
}

void EntityCache::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    front_->clear();
    back_buffer_.clear();
    spatial_grid_.Clear();
}

EntityData* EntityCache::GetEntity(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = front_->find(id);
    return it != front_->end() ? &it->second : nullptr;
}

const EntityData* EntityCache::GetEntity(uint64_t id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = front_->find(id);
    return it != front_->end() ? &it->second : nullptr;
}

std::vector<uint64_t> EntityCache::GetActiveEntities() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint64_t> result;
    result.reserve(front_->size());
    for (const auto& [id, entity] : *front_) {
        if (entity.state.is_alive && !entity.meta.culled) {
            result.push_back(id);
        }
    }
    return result;
}

std::vector<uint64_t> EntityCache::GetEntitiesInRange(const Vec3& center, float radius) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return spatial_grid_.QueryRadius(center, radius);
}

void EntityCache::BuildSpatialIndex() {
    spatial_grid_.Clear();
    for (const auto& [id, entity] : *front_) {
        if (entity.state.is_alive) {
            spatial_grid_.Insert(id, entity.transform.position);
        }
    }
}

std::vector<uint64_t> EntityCache::QueryFrustum(const Frustum& frustum) const {
    return spatial_grid_.QueryFrustum(frustum);
}

std::vector<uint64_t> EntityCache::QueryRadius(const Vec3& center, float radius) const {
    return spatial_grid_.QueryRadius(center, radius);
}

void EntityCache::SwapBuffers() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::swap(front_, back_);
    back_ = (front_ == &front_buffer_) ? &back_buffer_ : &front_buffer_;
}

EntityData* EntityCache::GetFrontBuffer(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = front_->find(id);
    return it != front_->end() ? &it->second : nullptr;
}

EntityData* EntityCache::GetBackBuffer(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = back_buffer_.find(id);
    return it != back_buffer_.end() ? &it->second : nullptr;
}

EntityCache::Stats EntityCache::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Stats stats;
    stats.total = front_->size();
    for (const auto& [id, entity] : *front_) {
        if (entity.state.is_alive) stats.active++;
        if (entity.meta.culled) stats.culled++;
    }
    stats.memory_mb = (front_->size() * sizeof(EntityData)) / (1024.0f * 1024.0f);
    return stats;
}

void EntityCache::Cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);
    CleanupStale();
    UpdateSpatialIndex();
}

void EntityCache::Update(float dt) {
    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - last_cleanup_).count();
    
    if (elapsed >= config_.cleanup_interval) {
        CleanupStale();
        last_cleanup_ = now;
    }
    
    elapsed = std::chrono::duration<float>(now - last_spatial_update_).count();
    if (elapsed >= 0.5f) {  // Update spatial index twice per second
        UpdateSpatialIndex();
        last_spatial_update_ = now;
    }
}

void EntityCache::CleanupStale() {
    auto now = std::chrono::steady_clock::now();
    float max_age = config_.max_entity_age;
    
    for (auto it = front_->begin(); it != front_->end();) {
        float age = std::chrono::duration<float>(now - it->second.meta.last_update).count();
        if (age > max_age) {
            spatial_grid_.Remove(it->first);
            front_->erase(it++);
        } else {
            ++it;
        }
    }
    
    // Also clean back buffer
    for (auto it = back_buffer_.begin(); it != back_buffer_.end();) {
        float age = std::chrono::duration<float>(now - it->second.meta.last_update).count();
        if (age > max_age * 2) {  // Keep back buffer longer
            back_buffer_.erase(it++);
        } else {
            ++it;
        }
    }
}

void EntityCache::UpdateSpatialIndex() {
    spatial_grid_.Clear();
    for (const auto& [id, entity] : *front_) {
        if (entity.state.is_alive && !entity.meta.culled) {
            spatial_grid_.Insert(id, entity.transform.position);
        }
    }
}

void EntityCache::UpdateDerivedData(EntityData& entity) {
    // Update visual data from state
    entity.visual.health_fraction = entity.state.max_health > 0 
        ? entity.state.health / entity.state.max_health : 0.0f;
    entity.visual.health_fraction = std::clamp(entity.visual.health_fraction, 0.0f, 1.0f);
    
    // Mark strings as dirty
    entity.strings_dirty = true;
    
    // Update spatial index position
    spatial_grid_.Update(entity.id, entity.transform.position, entity.transform.position);
}

// ============================================================================
// SpatialPartitioning Implementation
// ============================================================================

SpatialPartitioning::SpatialPartitioning(const Config& cfg) : config_(cfg) {
    cell_size_ = cfg.cell_size;
    inv_cell_size_ = 1.0f / cell_size_;
}

void SpatialPartitioning::Insert(uint64_t id, const Vec3& pos, float radius) {
    int x, y, z;
    GetCellCoords(pos, x, y, z);
    int64_t hash = HashCell(x, y, z);
    grid_[hash].entities.push_back(id);
    entity_cells_[id] = std::make_tuple(x, y, z);
}

void SpatialPartitioning::Update(uint64_t id, const Vec3& old_pos, const Vec3& new_pos, float radius) {
    auto it = entity_cells_.find(id);
    if (it != entity_cells_.end()) {
        auto [ox, oy, oz] = it->second;
        int64_t old_hash = HashCell(ox, oy, oz);
        auto& old_cell = grid_[old_hash];
        old_cell.entities.erase(
            std::remove(old_cell.entities.begin(), old_cell.entities.end(), id),
            old_cell.entities.end()
        );
        if (old_cell.entities.empty()) {
            grid_.erase(old_hash);
        }
    }
    Insert(id, new_pos, radius);
}

void SpatialPartitioning::Remove(uint64_t id) {
    auto it = entity_cells_.find(id);
    if (it != entity_cells_.end()) {
        auto [x, y, z] = it->second;
        int64_t hash = HashCell(x, y, z);
        auto& cell = grid_[hash];
        cell.entities.erase(
            std::remove(cell.entities.begin(), cell.entities.end(), id),
            cell.entities.end()
        );
        if (cell.entities.empty()) {
            grid_.erase(hash);
        }
        entity_cells_.erase(it);
    }
}

std::vector<uint64_t> SpatialPartitioning::QueryRadius(const Vec3& center, float radius) const {
    std::vector<uint64_t> results;
    int cx, cy, cz;
    GetCellCoords(center, cx, cy, cz);
    
    int cell_radius = static_cast<int>(std::ceil(radius / cell_size_));
    
    for (int x = -cell_radius; x <= cell_radius; ++x) {
        for (int y = -cell_radius; y <= cell_radius; ++y) {
            for (int z = -cell_radius; z <= cell_radius; ++z) {
                int64_t hash = HashCell(cx + x, cy + y, cz + z);
                auto it = grid_.find(hash);
                if (it != grid_.end()) {
                    for (uint64_t id : it->second.entities) {
                        // Could add distance check here for precision
                        results.push_back(id);
                    }
                }
            }
        }
    }
    return results;
}

std::vector<uint64_t> SpatialPartitioning::QueryBox(const Vec3& min, const Vec3& max) const {
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

std::vector<uint64_t> SpatialPartitioning::QueryFrustum(const Frustum& frustum) const {
    // Simplified - would check cell bounds against frustum planes
    std::vector<uint64_t> results;
    for (const auto& [hash, cell] : grid_) {
        results.insert(results.end(), cell.entities.begin(), cell.entities.end());
    }
    return results;
}

const std::vector<uint64_t>* SpatialPartitioning::GetCell(int x, int y, int z) const {
    int64_t hash = HashCell(x, y, z);
    auto it = grid_.find(hash);
    return it != grid_.end() ? &it->second.entities : nullptr;
}

void SpatialPartitioning::Clear() {
    grid_.clear();
    entity_cells_.clear();
}

void SpatialPartitioning::SetCellSize(float size) {
    cell_size_ = std::max(1.0f, size);
    inv_cell_size_ = 1.0f / cell_size_;
    Clear();
}

int64_t SpatialPartitioning::HashCell(int x, int y, int z) const {
    // Large primes for good distribution
    return (static_cast<int64_t>(x) * 73856093) ^ 
           (static_cast<int64_t>(y) * 19349663) ^ 
           (static_cast<int64_t>(z) * 83492791);
}

void SpatialPartitioning::GetCellCoords(const Vec3& pos, int& x, int& y, int& z) const {
    x = static_cast<int>(std::floor(pos.x * inv_cell_size_));
    y = static_cast<int>(std::floor(pos.y * inv_cell_size_));
    z = static_cast<int>(std::floor(pos.z * inv_cell_size_));
}

SpatialPartitioning::Stats SpatialPartitioning::GetStats() const {
    Stats stats;
    stats.occupied_cells = grid_.size();
    for (const auto& [hash, cell] : grid_) {
        stats.total_entities += cell.entities.size();
    }
    stats.avg_entities_per_cell = stats.occupied_cells > 0 
        ? static_cast<float>(stats.total_entities) / stats.occupied_cells : 0.0f;
    return stats;
}

// ============================================================================
// AdaptiveUpdater Implementation
// ============================================================================

AdaptiveUpdater::AdaptiveUpdater(const Config& cfg) : config_(cfg) {
    last_frame_ = std::chrono::steady_clock::now();
}

void AdaptiveUpdater::Register(uint64_t id, const UpdatePolicy& policy) {
    EntityUpdateInfo info;
    info.current_interval_ms = policy.base_interval_ms;
    info.dirty = true;
    entities_[id] = info;
    active_entities_.push_back(id);
}

void AdaptiveUpdater::Unregister(uint64_t id) {
    entities_.erase(id);
    active_entities_.erase(
        std::remove(active_entities_.begin(), active_entities_.end(), id),
        active_entities_.end()
    );
}

bool AdaptiveUpdater::ShouldUpdate(uint64_t id, float dt) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return false;
    
    auto& info = it->second;
    info.accumulated_time += dt * 1000.0f;  // Convert to ms
    
    if (info.dirty || info.accumulated_time >= info.current_interval_ms) {
        info.accumulated_time = 0.0f;
        info.dirty = false;
        return true;
    }
    return false;
}

void AdaptiveUpdater::MarkDirty(uint64_t id) {
    auto it = entities_.find(id);
    if (it != entities_.end()) {
        it->second.dirty = true;
    }
}

void AdaptiveUpdater::Update(float dt) {
    frame_time_accumulator_ += dt;
    
    auto now = std::chrono::steady_clock::now();
    float frame_time_ms = std::chrono::duration<float, std::milli>(now - last_frame_).count();
    last_frame_ = now;
    
    // Adapt intervals based on frame time
    if (frame_time_ms > 0) {
        float target_fps = 1000.0f / frame_time_ms;
        for (auto& [id, info] : entities_) {
            if (config_.adaptive) {
                // Adjust interval based on frame time and priority
                float target_interval = config_.base_interval_ms * (1.0f + (1.0f - info.priority) * 0.5f);
                info.current_interval_ms = std::clamp(target_interval, 
                    config_.min_interval_ms, config_.max_interval_ms);
            }
        }
    }
}

void AdaptiveUpdater::SetFrameBudget(float ms) {
    config_.budget_per_frame_ms = ms;
}

void AdaptiveUpdater::SortByPriority() {
    std::sort(active_entities_.begin(), active_entities_.end(),
        [this](uint64_t a, uint64_t b) {
            auto it_a = entities_.find(a);
            auto it_b = entities_.find(b);
            if (it_a == entities_.end() || it_b == entities_.end()) return false;
            return it_a->second.priority > it_b->second.priority;
        });
}

AdaptiveUpdater::Stats AdaptiveUpdater::GetStats() const {
    Stats stats;
    stats.registered = entities_.size();
    stats.avg_interval_ms = 0.0f;
    int count = 0;
    for (const auto& [id, info] : entities_) {
        stats.avg_interval_ms += info.current_interval_ms;
        count++;
    }
    if (count > 0) stats.avg_interval_ms /= count;
    return stats;
}

float AdaptiveUpdater::CalculateInterval(const EntityUpdateInfo& info) const {
    // Base interval adjusted by priority and state
    float interval = config_.base_interval_ms;
    
    // Higher priority = more frequent updates
    interval *= (1.0f - info.priority * 0.5f);
    
    // Closer entities update more frequently
    if (info.distance > 0) {
        interval *= std::max(0.5f, std::min(2.0f, info.distance / 100.0f));
    }
    
    // Faster moving entities update more frequently
    if (info.velocity_magnitude > 0) {
        interval *= std::max(0.5f, std::min(2.0f, 10.0f / info.velocity_magnitude));
    }
    
    return std::clamp(interval, config_.min_interval_ms, config_.max_interval_ms);
}

// ============================================================================
// CullingSystem Implementation
// ============================================================================

CullingSystem::CullingSystem(const CullingConfig& cfg) : config_(cfg) {}

std::pair<LODLevel, CullResult> CullingSystem::CullEntity(const EntityData& entity, const CullingContext& ctx) const {
    // 1. Invalid check
    if (!entity.state.is_alive) return {LODLevel::Culled, CullResult::Culled_Invalid};
    
    // 2. Distance culling
    float distance = entity.visual.distance;
    if (distance < config_.min_distance || distance > config_.max_distance) {
        return {LODLevel::Culled, CullResult::Culled_Distance};
    }
    
    // 3. Frustum culling
    if (!FrustumCull(entity.transform.position, 2.0f, ctx.frustum)) {
        return {LODLevel::Culled, CullResult::Culled_Frustum};
    }
    
    // 4. Screen space culling
    // Would need screen position - skipping for now
    
    // 5. Priority culling
    if (entity.meta.priority < config_.min_priority) {
        return {LODLevel::Culled, CullResult::Culled_Priority};
    }
    
    // 6. Calculate LOD
    LODLevel lod = CalculateLOD(distance);
    
    return {lod, CullResult::Visible};
}

void CullingSystem::CullBatch(const std::vector<uint64_t>& entities, 
                              const CullingContext& ctx,
                              std::vector<std::pair<uint64_t, std::pair<LODLevel, CullResult>>>& results) const {
    results.clear();
    results.reserve(entities.size());
    
    for (uint64_t id : entities) {
        // Would need entity data - placeholder
        results.emplace_back(id, std::make_pair(LODLevel::Full, CullResult::Visible));
    }
}

bool CullingSystem::FrustumCull(const Vec3& pos, float radius, const Frustum& frustum) const {
    for (int i = 0; i < 6; ++i) {
        float dist = frustum.planes[i].x * pos.x + 
                     frustum.planes[i].y * pos.y + 
                     frustum.planes[i].z * pos.z + 
                     frustum.planes[i].w;
        if (dist < -radius) return false;
    }
    return true;
}

bool CullingSystem::ScreenSpaceCull(const Vec2& screen_pos, float screen_size, const CullingContext& ctx) const {
    float margin = config_.screen_margin;
    if (screen_pos.x < -margin || screen_pos.x > ctx.screen_size.x + margin ||
        screen_pos.y < -margin || screen_pos.y > ctx.screen_size.y + margin) {
        return false;
    }
    return screen_size >= config_.min_screen_size;
}

bool CullingSystem::DistanceCull(float distance, float min_dist, float max_dist) const {
    return distance < min_dist || distance > max_dist;
}

bool CullingSystem::PriorityCull(uint8_t priority, uint8_t min_priority) const {
    return priority < min_priority;
}

LODLevel CullingSystem::CalculateLOD(float distance) const {
    for (int i = 0; i < 4; ++i) {
        if (distance <= config_.lod_distances[i]) {
            return static_cast<LODLevel>(i);
        }
    }
    return LODLevel::Low;
}

uint32_t CullingSystem::GetActiveFeatures(LODLevel lod) const {
    if (static_cast<int>(lod) < 4) {
        return config_.lod_features[static_cast<int>(lod)];
    }
    return config_.lod_features[3];  // Low LOD features
}

void CullingSystem::SetConfig(const CullingConfig& cfg) {
    config_ = cfg;
}

void CullingSystem::UpdateFrustum(const Matrix& view_proj, Frustum& frustum) const {
    // Extract frustum planes from view-projection matrix
    // Left plane
    frustum.planes[0] = Vec4(
        view_proj.m[3] + view_proj.m[0],
        view_proj.m[7] + view_proj.m[4],
        view_proj.m[11] + view_proj.m[8],
        view_proj.m[15] + view_proj.m[12]
    );
    // Right plane
    frustum.planes[1] = Vec4(
        view_proj.m[3] - view_proj.m[0],
        view_proj.m[7] - view_proj.m[4],
        view_proj.m[11] - view_proj.m[8],
        view_proj.m[15] - view_proj.m[12]
    );
    // Bottom plane
    frustum.planes[2] = Vec4(
        view_proj.m[3] + view_proj.m[1],
        view_proj.m[7] + view_proj.m[5],
        view_proj.m[11] + view_proj.m[9],
        view_proj.m[15] + view_proj.m[13]
    );
    // Top plane
    frustum.planes[3] = Vec4(
        view_proj.m[3] - view_proj.m[1],
        view_proj.m[7] - view_proj.m[5],
        view_proj.m[11] - view_proj.m[9],
        view_proj.m[15] - view_proj.m[13]
    );
    // Near plane
    frustum.planes[4] = Vec4(
        view_proj.m[3] + view_proj.m[2],
        view_proj.m[7] + view_proj.m[6],
        view_proj.m[11] + view_proj.m[10],
        view_proj.m[15] + view_proj.m[14]
    );
    // Far plane
    frustum.planes[5] = Vec4(
        view_proj.m[3] - view_proj.m[2],
        view_proj.m[7] - view_proj.m[6],
        view_proj.m[11] - view_proj.m[10],
        view_proj.m[15] - view_proj.m[14]
    );
}

bool CullingSystem::CheckSphereFrustum(const Vec3& center, float radius, const Frustum& frustum) const {
    for (int i = 0; i < 6; ++i) {
        float dist = frustum.planes[i].x * center.x + 
                     frustum.planes[i].y * center.y + 
                     frustum.planes[i].z * center.z + 
                     frustum.planes[i].w;
        if (dist < -radius) return false;
    }
    return true;
}

bool CullingSystem::CheckBoxFrustum(const Vec3& min, const Vec3& max, const Frustum& frustum) const {
    // Check all 8 corners of the box
    Vec3 corners[8] = {
        {min.x, min.y, min.z}, {max.x, min.y, min.z},
        {min.x, max.y, min.z}, {max.x, max.y, min.z},
        {min.x, min.y, max.z}, {max.x, min.y, max.z},
        {min.x, max.y, max.z}, {max.x, max.y, max.z}
    };
    
    for (int plane = 0; plane < 6; ++plane) {
        bool all_outside = true;
        for (int i = 0; i < 8; ++i) {
            float dist = frustum.planes[plane].x * corners[i].x +
                         frustum.planes[plane].y * corners[i].y +
                         frustum.planes[plane].z * corners[i].z +
                         frustum.planes[plane].w;
            if (dist >= 0) {
                all_outside = false;
                break;
            }
        }
        if (all_outside) return false;
    }
    return true;
}

// ============================================================================
// SkeletonSystem Implementation
// ============================================================================

SkeletonSystem::SkeletonSystem(const Config& cfg) : config_(cfg), last_cleanup_(std::chrono::steady_clock::now()) {}

void SkeletonSystem::UpdateBatch(const std::vector<uint64_t>& entity_ids,
                                 const std::function<Vec3(uint64_t, int)>& get_bone_pos,
                                 const Matrix& view_proj) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (uint64_t id : entity_ids) {
        UpdateSkeleton(id, get_bone_pos, view_proj);
    }
}

const SkeletonData* SkeletonSystem::GetSkeleton(uint64_t entity_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = skeletons_.find(entity_id);
    return it != skeletons_.end() ? &it->second : nullptr;
}

std::span<const int> SkeletonSystem::GetBonesForLOD(LODLevel lod) const {
    switch (lod) {
        case LODLevel::Full: return config_.lod_full_bones;
        case LODLevel::High: return config_.lod_high_bones;
        case LODLevel::Medium: return config_.lod_medium_bones;
        case LODLevel::Low: return config_.lod_low_bones;
        default: return config_.lod_low_bones;
    }
}

void SkeletonSystem::RenderSkeleton(const SkeletonData& skeleton, ImDrawList* draw_list,
                                    const Matrix& view_proj, ImU32 color, float thickness) const {
    if (!draw_list || !skeleton.valid) return;
    
    const auto& bones = GetBonesForLOD(static_cast<LODLevel>(skeleton.lod_level));
    const auto& connections = config_.connections;
    
    ImU32 joint_color = color;
    
    for (const auto& conn : connections) {
        int b1 = conn.first;
        int b2 = conn.second;
        
        // Check if both bones are in current LOD
        bool b1_in_lod = std::find(bones.begin(), bones.end(), b1) != bones.end();
        bool b2_in_lod = std::find(bones.begin(), bones.end(), b2) != bones.end();
        if (!b1_in_lod || !b2_in_lod) continue;
        
        if (skeleton.on_screen[b1] && skeleton.on_screen[b2]) {
            ImVec2 p1(skeleton.screen_positions[b1].x, skeleton.screen_positions[b1].y);
            ImVec2 p2(skeleton.screen_positions[b2].x, skeleton.screen_positions[b2].y);
            draw_list->AddLine(p1, p2, color, thickness);
        }
    }
    
    // Draw joints
    if (config_.joint_radius > 0) {
        for (int bone_id : bones) {
            if (skeleton.on_screen[bone_id]) {
                ImVec2 pos(skeleton.screen_positions[bone_id].x, skeleton.screen_positions[bone_id].y);
                draw_list->AddCircleFilled(pos, config_.joint_radius, color, 10);
                draw_list->AddCircle(pos, config_.joint_radius, IM_COL32(0, 0, 0, 180), 10, 1.0f);
            }
        }
    }
}

void SkeletonSystem::Cleanup() {
    CleanupStale();
}

void SkeletonSystem::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    skeletons_.clear();
}

void SkeletonSystem::UpdateSkeleton(uint64_t id, const std::function<Vec3(int)>& get_bone_pos, 
                                    const Matrix& view_proj) {
    auto& skeleton = skeletons_[id];
    skeleton.entity_id = id;
    skeleton.last_update = std::chrono::steady_clock::now();
    skeleton.valid = true;
    
    // Get bones for current LOD (would need distance)
    const auto& bones = config_.lod_full_bones;  // Simplified - use full for now
    
    for (int bone_id : bones) {
        skeleton.bone_positions[bone_id] = get_bone_pos(bone_id);
        skeleton.bone_valid[bone_id] = !skeleton.bone_positions[bone_id].IsZero();
    }
    
    UpdateScreenPositions(skeleton, view_proj);
}

void SkeletonSystem::UpdateScreenPositions(SkeletonData& skeleton, const Matrix& view_proj) {
    for (int i = 0; i < 64; ++i) {
        if (skeleton.bone_valid[i]) {
            Vec4 clip;
            const Vec3& pos = skeleton.bone_positions[i];
            clip.x = pos.x * view_proj.m[0] + pos.y * view_proj.m[4] + pos.z * view_proj.m[8] + view_proj.m[12];
            clip.y = pos.x * view_proj.m[1] + pos.y * view_proj.m[5] + pos.z * view_proj.m[9] + view_proj.m[13];
            clip.z = pos.x * view_proj.m[2] + pos.y * view_proj.m[6] + pos.z * view_proj.m[10] + view_proj.m[14];
            clip.w = pos.x * view_proj.m[3] + pos.y * view_proj.m[7] + pos.z * view_proj.m[11] + view_proj.m[15];
            
            if (clip.w > 0.01f) {
                float inv = 1.0f / clip.w;
                skeleton.screen_positions[i].x = (clip.x * clip.w * 0.5f + 0.5f); // Simplified
                skeleton.screen_positions[i].y = (1.0f - clip.y * clip.w * 0.5f); // Simplified
                skeleton.on_screen[i] = true;
            } else {
                skeleton.on_screen[i] = false;
            }
        } else {
            skeleton.on_screen[i] = false;
        }
    }
}

void SkeletonSystem::CleanupStale() {
    auto now = std::chrono::steady_clock::now();
    float max_age = config_.cache_validity_ms * 10.0f;  // 10x validity
    
    for (auto it = skeletons_.begin(); it != skeletons_.end();) {
        float age = std::chrono::duration<float, std::milli>(now - it->second.last_update).count();
        if (age > max_age) {
            it = skeletons_.erase(it);
        } else {
            ++it;
        }
    }
    
    last_cleanup_ = now;
}

void SkeletonSystem::CleanupStale() {
    CleanupStale();
}

SkeletonSystem::Stats SkeletonSystem::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Stats stats;
    stats.active_skeletons = skeletons_.size();
    stats.cache_size = skeletons_.size();
    stats.memory_mb = (skeletons_.size() * sizeof(SkeletonData)) / (1024.0f * 1024.0f);
    return stats;
}

// ============================================================================
// TextCache Implementation
// ============================================================================

TextCache::TextCache(const Config& cfg) : config_(cfg), last_cleanup_(std::chrono::steady_clock::now()) {}

const TextCache::CachedText* TextCache::GetText(uint64_t entity_id, uint32_t hash, 
                                                const std::function<std::string()>& generator) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& entity_cache = cache_[entity_id];
    auto it = entity_cache.find(hash);
    
    if (it != entity_cache.end()) {
        it->second.last_used = std::chrono::steady_clock::now();
        return &it->second;
    }
    
    // Generate new text
    std::string text = generator();
    CachedText cached;
    cached.text = text;
    cached.entity_id = entity_id;
    cached.hash = hash;
    cached.last_used = std::chrono::steady_clock::now();
    cached.size = ImGui::CalcTextSize(text.c_str());
    
    auto [it_new, inserted] = entity_cache.emplace(hash, std::move(cached));
    return &it_new->second;
}

const ImVec2* TextCache::GetTextSize(uint64_t entity_id, uint32_t hash) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto entity_it = cache_.find(entity_id);
    if (entity_it == cache_.end()) return nullptr;
    
    auto it = entity_it->second.find(hash);
    if (it == entity_it->second.end()) return nullptr;
    
    return &it->second.size;
}

void TextCache::PrecalculateSizes(const std::vector<std::pair<uint64_t, std::string>>& texts) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, text] : texts) {
        uint32_t hash = HashString(text);
        auto& entity_cache = cache_[id];
        auto it = entity_cache.find(hash);
        if (it == entity_cache.end()) {
            CachedText cached;
            cached.text = text;
            cached.entity_id = id;
            cached.hash = hash;
            cached.last_used = std::chrono::steady_clock::now();
            cached.size = ImGui::CalcTextSize(text.c_str());
            entity_cache.emplace(hash, std::move(cached));
        }
    }
}

void TextCache::DrawText(ImDrawList* draw_list, uint64_t entity_id, uint32_t hash,
                         const ImVec2& pos, ImU32 color) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto entity_it = cache_.find(entity_id);
    if (entity_it == cache_.end()) return;
    
    auto it = entity_it->second.find(hash);
    if (it == entity_it->second.end()) return;
    
    draw_list->AddText(pos, color, it->second.text.c_str());
}

void TextCache::DrawBatch(ImDrawList* draw_list,
                          const std::vector<std::tuple<uint64_t, uint32_t, ImVec2, ImU32>>& texts) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [entity_id, hash, pos, color] : texts) {
        auto entity_it = cache_.find(entity_id);
        if (entity_it == cache_.end()) continue;
        
        auto it = entity_it->second.find(std::get<1>(texts[0])); // hash
        if (it == entity_it->second.end()) continue;
        
        draw_list->AddText(pos, color, it->second.text.c_str());
    }
}

void TextCache::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
}

void TextCache::Cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);
    CleanupStale();
}

uint32_t TextCache::HashString(const std::string& str) const {
    uint32_t hash = 2166136261u;  // FNV-1a
    for (char c : str) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

void TextCache::CleanupStale() {
    auto now = std::chrono::steady_clock::now();
    float max_age = config_.ttl_seconds;
    
    for (auto& [entity_id, entity_cache] : cache_) {
        for (auto it = entity_cache.begin(); it != entity_cache.end();) {
            float age = std::chrono::duration<float>(now - it->second.last_used).count();
            if (age > max_age) {
                it = entity_cache.erase(it);
            } else {
                ++it;
            }
        }
        if (entity_cache.empty()) {
            cache_.erase(entity_id);
        }
    }
    
    last_cleanup_ = now;
}

TextCache::Stats TextCache::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Stats stats;
    stats.entries = 0;
    for (const auto& [entity_id, entity_cache] : cache_) {
        stats.entries += entity_cache.size();
    }
    stats.memory_mb = stats.entries * sizeof(CachedText) / (1024.0f * 1024.0f);
    return stats;
}

// ============================================================================
// TransformSystem Implementation
// ============================================================================

bool TransformSystem::WorldToScreen(const Vec3& world, const ViewData& view, Vec2& screen) const {
    // Transform world to clip space
    Vec4 clip;
    clip.x = world.x * view.view_proj.m[0] + world.y * view.view_proj.m[4] + world.z * view.view_proj.m[8] + view.view_proj.m[12];
    clip.y = world.x * view.view_proj.m[1] + world.y * view.view_proj.m[5] + world.z * view.view_proj.m[9] + view.view_proj.m[13];
    clip.z = world.x * view.view_proj.m[2] + world.y * view.view_proj.m[6] + world.z * view.view_proj.m[10] + view.view_proj.m[14];
    clip.w = world.x * view.view_proj.m[3] + world.y * view.view_proj.m[7] + world.z * view.view_proj.m[11] + view.view_proj.m[15];
    
    if (clip.w < 0.1f) return false;
    
    // Normalize device coordinates
    float ndc_x = clip.x / clip.w;
    float ndc_y = clip.y / clip.w;
    
    // Apply viewport transform
    screen.x = (ndc_x * 0.5f + 0.5f) * view.screen_size.x;
    screen.y = (1.0f - ndc_y * 0.5f) * view.screen_size.y;
    
    return true;
}

bool TransformSystem::WorldToScreenBatch(const std::vector<Vec3>& world_positions,
                                         const ViewData& view,
                                         std::vector<Vec2>& screen_positions,
                                         std::vector<bool>& on_screen) const {
    screen_positions.resize(world_positions.size());
    on_screen.resize(world_positions.size());
    
    // SIMD-friendly batch processing
    for (size_t i = 0; i < world_positions.size(); ++i) {
        on_screen[i] = WorldToScreen(world_positions[i], view, screen_positions[i]);
    }
    return true;
}

bool TransformSystem::ScreenToWorld(const Vec2& screen, float world_z, const ViewData& view, Vec3& world) const {
    // Inverse projection - simplified
    float ndc_x = (screen.x / view.screen_size.x) * 2.0f - 1.0f;
    float ndc_y = 1.0f - (screen.y / view.screen_size.y) * 2.0f;
    
    // Would need inverse view-projection matrix
    // Simplified implementation
    return false;
}

bool TransformSystem::IsInFrustum(const Vec3& pos, float radius, const ViewData& view) const {
    // Frustum culling against view frustum
    // Simplified - check distance and basic bounds
    Vec3 rel = pos - view.camera_pos;
    float dist = rel.Length();
    return dist < view.far_plane && dist > view.near_plane;
}

bool TransformSystem::IsInFrustum(const Vec3& min, const Vec3& max, const ViewData& view) const {
    // Check all 8 corners
    Vec3 corners[8] = {
        {min.x, min.y, min.z}, {max.x, min.y, min.z},
        {min.x, max.y, min.z}, {max.x, max.y, min.z},
        {min.x, min.y, max.z}, {max.x, min.y, max.z},
        {min.x, max.y, max.z}, {max.x, max.y, max.z}
    };
    
    for (int i = 0; i < 8; ++i) {
        if (IsInFrustum(corners[i], 0.0f, view)) return true;
    }
    return false;
}

float TransformSystem::GetDistanceToScreenCenter(const Vec2& screen_pos, const ViewData& view) const {
    float dx = screen_pos.x - view.screen_center.x;
    float dy = screen_pos.y - view.screen_center.y;
    return std::sqrt(dx * dx + dy * dy);
}

float TransformSystem::GetScreenSpaceSize(const Vec3& world_pos, float world_size, const ViewData& view) const {
    // Project world size to screen space
    Vec3 center = world_pos;
    Vec3 offset = world_pos;
    offset.x += world_size;
    
    Vec2 screen_center, screen_offset;
    WorldToScreen(center, view, screen_center);
    WorldToScreen(offset, view, screen_offset);
    
    return (screen_offset - screen_center).Length();
}

float TransformSystem::CalculateHorizontalFOV(float vertical_fov, float aspect) {
    return 2.0f * atanf(tanf(vertical_fov * 0.5f * 3.14159f / 180.0f) * aspect) * 180.0f / 3.14159f;
}

float TransformSystem::CalculateVerticalFOV(float horizontal_fov, float aspect) {
    return 2.0f * atanf(tanf(horizontal_fov * 0.5f * 3.14159f / 180.0f) / aspect) * 180.0f / 3.14159f;
}

float TransformSystem::CalculateIdealFOV(float monitor_width_mm, float view_distance_mm) {
    return 2.0f * atanf(monitor_width_mm / (2.0f * view_distance_mm)) * 180.0f / 3.14159f;
}

TransformSystem::ViewData TransformSystem::CreateViewData(const Matrix& view_matrix, const Matrix& proj_matrix,
                                                          const Vec2& screen_size) {
    ViewData view;
    view.view_matrix = view_matrix;
    view.proj_matrix = proj_matrix;
    
    // Multiply view * proj
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += view_matrix.m[i * 4 + k] * proj_matrix.m[k * 4 + j];
            }
            view.view_proj.m[i * 4 + j] = sum;
        }
    }
    
    view.screen_size = screen_size;
    view.screen_center = Vec2(screen_size.x * 0.5f, screen_size.y * 0.5f);
    view.aspect_ratio = screen_size.x / screen_size.y;
    
    return view;
}

// ============================================================================
// VisibilitySystem Implementation
// ============================================================================

VisibilitySystem::VisibilitySystem(const VisibilityConfig& cfg) : config_(cfg), last_cleanup_(std::chrono::steady_clock::now()) {}

VisibilityResult VisibilitySystem::CheckVisibility(uint64_t observer, uint64_t target,
                                                   const Vec3& observer_pos, const Vec3& target_pos,
                                                   int bone_index) {
    VisibilityResult result;
    result.distance = (target_pos - observer_pos).Length();
    
    if (config_.method == VisibilityConfig::Method::None) {
        result.state = VisibilityState::Visible;
        result.visibility = 1.0f;
        return result;
    }
    
    // Check cache
    // ... cache lookup ...
    
    // Perform visibility check based on method
    // This would call the virtual Raycast method
    
    result.state = VisibilityState::Visible;  // Placeholder
    result.visibility = 1.0f;
    result.check_time = std::chrono::microseconds(100);
    
    return result;
}

std::vector<std::pair<uint64_t, VisibilityResult>> VisibilitySystem::CheckMultiple(
    const std::vector<uint64_t>& targets, uint64_t observer, const Vec3& observer_pos) {
    std::vector<std::pair<uint64_t, VisibilityResult>> results;
    results.reserve(targets.size());
    
    Vec3 observer_pos{};
    // Would get observer position
    
    for (uint64_t target : targets) {
        Vec3 target_pos{};  // Would get target position
        results.emplace_back(target, CheckVisibility(0, target, observer_pos, target_pos, -1));
    }
    
    return results;
}

void VisibilitySystem::ClearCache() {
    std::lock_guard<std::mutex> lock(mutex_);
    visibility_cache_.clear();
}

void VisibilitySystem::PruneCache() {
    CleanupCache();
}

bool VisibilitySystem::IsCacheValid(const VisibilityResult& result) const {
    auto now = std::chrono::steady_clock::now();
    float age = std::chrono::duration<float>(now - result.check_time).count();
    return age < config_.cache_duration;
}

void VisibilitySystem::CleanupCache() {
    auto now = std::chrono::steady_clock::now();
    // Would remove stale cache entries
    last_cleanup_ = now;
}

// ============================================================================
// PrioritySystem Implementation
// ============================================================================

PrioritySystem::PrioritySystem(const PriorityConfig& cfg) : config_(cfg) {}

float PrioritySystem::CalculatePriority(const EntityData& entity, 
                                        const Vec3& camera_pos,
                                        const Vec2& screen_pos,
                                        float screen_size) const {
    float priority = 0.0f;
    
    // Distance weight (closer = higher priority)
    float distance = entity.visual.distance;
    if (distance > 0) {
        priority += config_.distance_weight * (1.0f / (1.0f + distance * 0.01f));
    }
    
    // Screen size weight
    priority += config_.screen_size_weight * screen_size * 0.01f;
    
    // Velocity weight
    float velocity = entity.transform.velocity.Length();
    priority += config_.velocity_weight * velocity * 0.1f;
    
    // Relevance (is friendly, is target, etc.)
    if (entity.state.is_friendly) priority += config_.relevance_weight * 0.5f;
    if (entity.visual.distance < 50.0f) priority += config_.relevance_weight;
    
    return priority;
}

PrioritySystem::Tier PrioritySystem::GetTier(float priority) const {
    if (priority >= config_.high_priority_threshold) return Tier::High;
    if (priority >= config_.low_priority_threshold) return Tier::Medium;
    return Tier::Low;
}

void PrioritySystem::SortByPriority(std::vector<uint64_t>& entities,
                                    const std::function<const EntityData*(uint64_t)>& get_entity,
                                    const Vec3& camera_pos) const {
    std::sort(entities.begin(), entities.end(),
        [this, &get_entity, &camera_pos](uint64_t a, uint64_t b) {
            const EntityData* ea = get_entity(a);
            const EntityData* eb = get_entity(b);
            if (!ea || !eb) return false;
            
            float pa = CalculatePriority(*ea, camera_pos, Vec2{}, 0.0f);
            float pb = CalculatePriority(*eb, camera_pos, Vec2{}, 0.0f);
            return pa > pb;
        });
}

size_t PrioritySystem::GetMaxEntities(Tier tier) const {
    switch (tier) {
        case Tier::High: return config_.max_high_priority;
        case Tier::Medium: return config_.max_medium_priority;
        case Tier::Low: return config_.max_low_priority;
        default: return 0;
    }
}

// ============================================================================
// AdaptiveQualitySystem Implementation
// ============================================================================

AdaptiveQualitySystem::AdaptiveQualitySystem(const AdaptiveQualityConfig& cfg) : config_(cfg) {}

void AdaptiveQualitySystem::Update(float frame_time_ms) {
    target_level_ = 0.0f;
    
    if (frame_time_ms > config_.critical_frame_time_ms) {
        target_level_ = 4.0f;  // Minimum quality
    } else if (frame_time_ms > config_.target_frame_time_ms * 1.5f) {
        target_level_ = 3.0f;
    } else if (frame_time_ms > config_.target_frame_time_ms * 1.2f) {
        target_level_ = 2.0f;
    } else if (frame_time_ms > config_.target_frame_time_ms) {
        target_level_ = 1.0f;
    } else {
        target_level_ = 0.0f;  // Full quality
    }
    
    // Smooth transition
    float diff = target_level_ - current_level_;
    if (std::abs(diff) > 0.01f) {
        current_level_ += diff * config_.transition_speed;
        current_level_ = std::clamp(current_level_, 0.0f, 4.0f);
    }
}

int AdaptiveQualitySystem::GetCurrentQualityLevel() const {
    return static_cast<int>(std::round(current_level_));
}

const AdaptiveQualityConfig::QualityLevel& AdaptiveQualitySystem::GetCurrentLevel() const {
    return config_.levels[GetCurrentQualityLevel()];
}

float AdaptiveQualitySystem::GetLODBias() const {
    return GetCurrentLevel().lod_bias;
}

float AdaptiveQualitySystem::GetUpdateRateScale() const {
    return GetCurrentLevel().update_rate_scale;
}

uint32_t AdaptiveQualitySystem::GetDisabledFeatures() const {
    return GetCurrentLevel().disabled_features;
}

float AdaptiveQualitySystem::GetCullDistanceScale() const {
    return GetCurrentLevel().cull_distance_scale;
}

float AdaptiveQualitySystem::GetMaxEntitiesScale() const {
    return GetCurrentLevel().max_entities_scale;
}

// ============================================================================
// ESPProfiler Implementation
// ============================================================================

void ESPProfiler::BeginFrame() {
    frame_start_ = std::chrono::steady_clock::now();
    metrics_ = Metrics{};
}

void ESPProfiler::EndFrame() {
    auto end = std::chrono::steady_clock::now();
    metrics_.total_ms = std::chrono::duration<float, std::milli>(end - frame_start_).count();
    last_frame_metrics_ = metrics_;
}

void ESPProfiler::Reset() {
    metrics_ = Metrics{};
    last_frame_metrics_ = Metrics{};
}

std::string ESPProfiler::GenerateReport() const {
    std::ostringstream oss;
    const auto& m = last_frame_metrics_;
    
    oss << "=== ESP Performance Report ===\n";
    oss << "Total Frame Time: " << std::fixed << std::setprecision(2) << m.total_ms << "ms\n";
    oss << "Entities Rendered: " << m.entities_rendered << "\n";
    oss << "Draw Calls: " << m.draw_calls << "\n";
    oss << "Memory: " << std::fixed << std::setprecision(2) << m.memory_mb << " MB\n\n";
    
    oss << "Breakdown:\n";
    oss << "  Acquire:     " << std::fixed << std::setprecision(2) << m.acquire_ms << "ms (" << m.entities_acquired << " entities)\n";
    oss << "  Process:     " << m.process_ms << "ms (" << m.entities_processed << " entities)\n";
    oss << "  World2Screen:" << m.w2s_ms << "ms (" << m.w2s_count << " calls)\n";
    oss << "  Visibility:  " << m.visibility_ms << "ms (" << m.visibility_checks << " checks)\n";
    oss << "  Skeleton:    " << m.skeleton_ms << "ms (" << m.skeletons_processed << " skeletons)\n";
    oss << "  Text:        " << m.text_ms << "ms (" << m.text_elements << " elements)\n";
    oss << "  Culling:     " << m.culling_ms << "ms (" << m.culled << " culled)\n";
    oss << "  Render:      " << m.render_ms << "ms (" << m.draw_calls << " draw calls)\n";
    
    return oss.str();
}

// ============================================================================
// ESPPipeline Implementation
// ============================================================================

ESPPipeline::ESPPipeline(const Config& cfg) : config_(cfg) {
    // Initialize all sub-systems
    entity_cache_ = std::make_unique<EntityCache>(config_.entity_cache);
    adaptive_updater_ = std::make_unique<AdaptiveUpdater>(config_.adaptive_updater);
    culling_system_ = std::make_unique<CullingSystem>(config_.culling);
    skeleton_system_ = std::make_unique<SkeletonSystem>(config_.skeleton);
    text_cache_ = std::make_unique<TextCache>(config_.text_cache);
    transform_system_ = std::make_unique<TransformSystem>();
    visibility_system_ = std::make_unique<VisibilitySystem>(config_.visibility);
    priority_system_ = std::make_unique<PrioritySystem>(config_.priority);
    adaptive_quality_ = std::make_unique<AdaptiveQualitySystem>(config_.adaptive_quality);
    spatial_index_ = std::make_unique<SpatialPartitioning>(SpatialPartitioning::Config{});
    profiler_ = std::make_unique<ESPProfiler>();
    thread_pool_ = std::make_unique<ThreadPool>(config_.max_threads);
    
    last_frame_time_ = std::chrono::steady_clock::now();
}

ESPPipeline::~ESPPipeline() = default;

void ESPPipeline::Update(const std::vector<uint64_t>& entity_ids,
                         const std::function<EntityData(uint64_t)>& data_provider,
                         const TransformSystem::ViewData& view_data) {
    auto frame_start = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(frame_start - last_frame_time_).count();
    last_frame_time_ = frame_start;
    
    profiler_->BeginFrame();
    
    // Update adaptive quality
    if (config_.enable_adaptive_quality) {
        // Would need frame time from previous frame
        // adaptive_quality_->Update(last_frame_metrics_.total_ms);
    }
    
    StageAcquire(entity_ids, data_provider);
    StageProcess(view_data);
    StageCull(view_data);
    
    profiler_->EndFrame();
    
    // Update adaptive systems
    adaptive_updater_->Update(std::chrono::duration<float>(std::chrono::steady_clock::now() - frame_start).count());
}

void ESPPipeline::StageAcquire(const std::vector<uint64_t>& entity_ids,
                               const std::function<EntityData(uint64_t)>& data_provider) {
    auto timer = profiler_->TimeAcquire();
    
    active_entities_.clear();
    active_entities_.reserve(entity_ids.size());
    
    for (uint64_t id : entity_ids) {
        EntityData data = data_provider(id);
        if (data.state.is_alive) {
            entity_cache_->UpdateEntity(id, data);
            active_entities_.push_back(id);
        }
    }
    
    profiler_->IncrementEntitiesAcquired(static_cast<int>(active_entities_.size()));
}

void ESPPipeline::StageProcess(const TransformSystem::ViewData& view_data) {
    auto timer = profiler_->TimeProcess();
    current_view_data_ = view_data;
    
    // Update adaptive updater
    float dt = ImGui::GetIO().DeltaTime;
    adaptive_updater_->Update(dt);
    
    // Process entities - check which need updating
    std::vector<uint64_t> to_process;
    to_process.reserve(active_entities_.size());
    
    for (uint64_t id : active_entities_) {
        if (adaptive_updater_->ShouldUpdate(id, ImGui::GetIO().DeltaTime)) {
            to_process.push_back(id);
        }
    }
    
    profiler_->IncrementEntitiesProcessed(static_cast<int>(to_process.size()));
    
    // Update skeletons in batch
    if (config_.skeleton.bone_validity_ms > 0) {
        std::vector<uint64_t> skeleton_entities;
        for (uint64_t id : to_process) {
            auto* entity = entity_cache_->GetEntity(id);
            if (entity && HasFeature(ESPFeature::Skeleton)) {
                skeleton_entities.push_back(id);
            }
        }
        
        if (!skeleton_entities.empty()) {
            skeleton_system_->UpdateBatch(skeleton_entities,
                [this](uint64_t id, int bone) -> Vec3 {
                    auto* entity = entity_cache_->GetEntity(id);
                    if (entity && bone >= 0 && bone < 64) {
                        return entity.transform.bone_positions[bone];
                    }
                    return Vec3{};
                },
                current_view_data_.view_proj);
        }
    }
    
    profiler_->IncrementEntitiesProcessed(static_cast<int>(to_process.size()));
}

void ESPPipeline::StageCull(const TransformSystem::ViewData& view_data) {
    auto timer = profiler_->TimeCulling();
    
    CullingContext ctx;
    ctx.camera_pos = view_data.camera_pos;
    ctx.camera_forward = view_data.camera_forward;
    ctx.fov_horizontal = view_data.fov_horizontal;
    ctx.fov_vertical = view_data.fov_vertical;
    ctx.frustum = {};  // Would build from view_proj
    ctx.screen_size = ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
    ctx.screen_center = ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
    ctx.view_proj = view_data.view_proj;
    ctx.current_time = ImGui::GetTime();
    ctx.frame_time = ImGui::GetIO().DeltaTime;
    
    // Update frustum
    culling_system_->UpdateFrustum(view_data.view_proj, ctx.frustum);
    
    culled_entities_.clear();
    cull_results_.clear();
    cull_results_.reserve(active_entities_.size());
    
    for (uint64_t id : active_entities_) {
        auto* entity = entity_cache_->GetFrontBuffer(id);
        if (!entity) continue;
        
        auto result = culling_system_->CullEntity(*entity, ctx);
        cull_results_.emplace_back(id, result);
        
        if (result.second != CullResult::Visible) {
            culled_entities_.push_back(id);
            entity->meta.culled = true;
        } else {
            entity->meta.culled = false;
        }
    }
    
    profiler_->IncrementCulled(static_cast<int>(culled_entities_.size()));
}

void ESPPipeline::Render(ImDrawList* draw_list,
                         const std::function<EntityData*(uint64_t)>& data_provider,
                         const TransformSystem::ViewData& view_data) {
    auto timer = profiler_->TimeRender();
    
    if (!draw_list) return;
    
    // Sort visible entities by priority
    std::vector<uint64_t> visible_entities;
    for (const auto& [id, result] : cull_results_) {
        if (result.second == CullResult::Visible) {
            visible_entities.push_back(id);
        }
    }
    
    // Sort by priority (highest first)
    priority_system_->SortByPriority(visible_entities,
        [this](uint64_t id) { return entity_cache_->GetFrontBuffer(id); },
        current_view_data_.camera_pos);
    
    // Limit entities based on quality
    auto& quality = adaptive_quality_->GetCurrentLevel();
    size_t max_entities = static_cast<size_t>(config_.culling.max_entities_per_frame * quality.max_entities_scale);
    if (visible_entities.size() > max_entities) {
        visible_entities.resize(max_entities);
    }
    
    // Render each entity
    for (uint64_t id : visible_entities) {
        auto* entity = data_provider(id);
        if (!entity) continue;
        
        auto cull_it = std::find_if(cull_results_.begin(), cull_results_.end(),
            [id](const auto& p) { return p.first == id; });
        if (cull_it == cull_results_.end()) continue;
        
        LODLevel lod = cull_it->second.first;
        
        // Render entity features based on LOD and enabled features
        // This would call game-specific render functions
        profiler_->IncrementEntitiesRendered();
        profiler_->IncrementDrawCalls();
    }
}

void ESPPipeline::StageCleanup() {
    entity_cache_->Cleanup();
    // Cleanup other systems
}

void ESPPipeline::AddEntity(uint64_t id, const EntityData& data) {
    entity_cache_->UpdateEntity(id, data);
    adaptive_updater_->Register(id);
    spatial_index_->Insert(id, data.transform.position);
}

void ESPPipeline::RemoveEntity(uint64_t id) {
    entity_cache_->RemoveEntity(id);
    adaptive_updater_->Unregister(id);
    spatial_index_->Remove(id);
}

void ESPPipeline::UpdateEntity(uint64_t id, const EntityData& data) {
    entity_cache_->UpdateEntity(id, data);
    adaptive_updater_->MarkDirty(id);
    spatial_index_->Update(id, entity_cache_->GetFrontBuffer(id)->transform.position, data.transform.position);
}

void ESPPipeline::SetFeatureEnabled(ESPFeature feature, bool enabled) {
    // Would update feature set
}

bool ESPPipeline::IsFeatureEnabled(ESPFeature feature) const {
    return false;  // Would check feature set
}

const ESPProfiler::Metrics& ESPPipeline::GetLastFrameMetrics() const {
    return profiler_->GetLastFrameMetrics();
}

std::string ESPPipeline::GetPerformanceReport() const {
    return profiler_->GenerateReport();
}

// ============================================================================
// ThreadPool Implementation
// ============================================================================

ESPPipeline::ThreadPool::ThreadPool(int num_threads) {
    workers_.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this, i] { WorkerLoop(i); });
    }
}

ESPPipeline::ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }
    condition_.notify_all();
    for (auto& worker : workers_) {
        if (worker.thread.joinable()) worker.thread.join();
    }
}

void ESPPipeline::ThreadPool::Enqueue(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        tasks_.push_back(std::move(task));
    }
    condition_.notify_one();
}

void ESPPipeline::ThreadPool::Wait() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    condition_.wait(lock, [this] { return tasks_.empty(); });
}

void ESPPipeline::ThreadPool::WorkerLoop(size_t index) {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            condition_.wait(lock, [this, index] {
                return !workers_[index].queue.empty() || workers_[index].stop;
            });
            
            if (workers_[index].stop && workers_[index].queue.empty()) break;
            
            if (!workers_[index].queue.empty()) {
                task = std::move(workers_[index].queue.front());
                workers_[index].queue.erase(workers_[index].queue.begin());
            }
        }
        
        if (task) task();
    }
}

// ============================================================================
// Global Accessor
// ============================================================================

ESPPipeline& GetESPPipeline() {
    static ESPPipeline instance;
    return instance;
}

} // namespace Gameplay::ESPOptimizer