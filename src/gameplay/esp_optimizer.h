#pragma once
#include <array>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <atomic>
#include <mutex>
#include <functional>
#include <memory>
#include <cstdint>
#include <string>
#include <cmath>
#include <algorithm>
#include <immintrin.h>
#include <span>
#include <queue>
#include <condition_variable>

#include "../../ImGui/imgui.h"
#include "../../src/platform/object_pools.h"

namespace Gameplay::ESPOptimizer {

using Vec2 = OmniGhost::Platform::Vec2;
using Vec3 = OmniGhost::Platform::Vec3;
using Vec4 = OmniGhost::Platform::Vec4;
using Matrix = OmniGhost::Platform::Matrix4x4;

struct Frustum {
    Vec4 planes[6];
};

// ============================================================================
// Feature System - Independent feature states with zero cost when disabled
// ============================================================================

enum class ESPFeature : uint32_t {
    None = 0,
    Box2D = 1u << 0,
    CornerBox = 1u << 1,
    Skeleton = 1u << 2,
    HeadCircle = 1u << 3,
    Name = 1u << 4,
    Distance = 1u << 5,
    HealthBar = 1u << 6,
    ArmorBar = 1u << 7,
    Snaplines = 1u << 8,
    WeaponName = 1u << 9,
    HeadDot = 1u << 10,
    Trails = 1u << 11,
    HeadHalo = 1u << 12,
    LookDirection = 1u << 13,
    Vehicle = 1u << 14,
    Animal = 1u << 15,
    Resource = 1u << 16,
    Count = 17
};

constexpr ESPFeature operator|(ESPFeature a, ESPFeature b) {
    return static_cast<ESPFeature>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr ESPFeature& operator|=(ESPFeature& a, ESPFeature b) {
    a = a | b; return a;
}

constexpr bool HasFeature(ESPFeature features, ESPFeature flag) {
    return (static_cast<uint32_t>(features) & static_cast<uint32_t>(flag)) != 0;
}

// Feature configuration with independent enable/disable
struct ESPFeatureConfig {
    bool enabled = false;
    float update_interval_ms = 0.0f;  // 0 = every frame
    float max_distance = 0.0f;        // 0 = no limit
    int priority = 0;                 // Higher = more important
    
    ESPFeatureConfig() = default;
    ESPFeatureConfig(bool en, float interval, float dist, int pri)
        : enabled(en), update_interval_ms(interval), max_distance(dist), priority(pri) {}
};

// Feature set with independent enable/disable
struct ESPFeatureSet {
    std::array<ESPFeatureConfig, 18> features{};
    
    // Core features
    ESPFeatureConfig box_2d{false, 0, 200.0f, 10};
    ESPFeatureConfig corner_box{false, 0, 200.0f, 9};
    ESPFeatureConfig skeleton{false, 0, 150.0f, 8};
    ESPFeatureConfig head_circle{false, 0, 200.0f, 7};
    ESPFeatureConfig name{false, 50, 300.0f, 6};
    ESPFeatureConfig distance{false, 50, 300.0f, 5};
    ESPFeatureConfig health_bar{false, 0, 200.0f, 9};
    ESPFeatureConfig armor_bar{false, 50, 200.0f, 6};
    ESPFeatureConfig snaplines{false, 50, 200.0f, 5};
    ESPFeatureConfig weapon_name{false, 100, 200.0f, 4};
    ESPFeatureConfig head_dot{false, 0, 200.0f, 6};
    ESPFeatureConfig armor_bar2{false, 50, 200.0f, 6};
    ESPFeatureConfig snaplines2{false, 50, 200.0f, 5};
    ESPFeatureConfig head_dot2{false, 0, 200.0f, 6};
    ESPFeatureConfig trails{false, 33, 200.0f, 3};
    ESPFeatureConfig head_halo{false, 33, 150.0f, 3};
    ESPFeatureConfig look_direction{false, 33, 150.0f, 3};
    ESPFeatureConfig vehicle_esp{false, 100, 500.0f, 2};

    bool Has(ESPFeature feature) const {
        return features[static_cast<int>(feature)].enabled;
    }
    
    void Set(ESPFeature feature, bool enabled) {
        features[static_cast<int>(feature)].enabled = enabled;
    }
    
    uint32_t GetActiveMask() const {
        uint32_t mask = 0;
        for (int i = 0; i < 18; ++i) {
            if (features[i].enabled) mask |= (1u << i);
        }
        return mask;
    }
};

// ============================================================================
// Entity Cache System - Cache-friendly, double buffered
// ============================================================================

struct EntityData {
    // Immutable ID
    uint64_t id = 0;
    
    // Transform (updated frequently)
    struct Transform {
        Vec3 position{};
        Vec3 velocity{};
        Vec3 angles{};
        Matrix bone_matrix{};
        std::array<Vec3, 64> bone_positions{};  // SoA for SIMD
    } transform;
    
    // State (updated less frequently)
    struct State {
        float health = 100.0f;
        float max_health = 100.0f;
        float armor = 0.0f;
        int team = 0;
        bool is_local = false;
        bool is_friendly = false;
        bool is_visible = true;
        bool is_alive = true;
        bool is_dormant = false;
        uint32_t flags = 0;
    } state;
    
    // Visual (derived from state)
    struct Visual {
        ImU32 color_visible = IM_COL32(255, 255, 255, 255);
        ImU32 color_occluded = IM_COL32(255, 100, 100, 255);
        ImU32 color_dead = IM_COL32(100, 100, 100, 255);
        std::string name;
        std::string weapon;
        float distance = 0.0f;
        float screen_distance = 0.0f;
        float health_fraction = 1.0f;
    } visual;
    
    // Caching metadata
    struct CacheMeta {
        std::chrono::steady_clock::time_point last_update{};
        std::chrono::steady_clock::time_point last_world_to_screen{};
        std::chrono::steady_clock::time_point last_visibility_check{};
        uint32_t dirty_flags = 0;
        uint8_t lod_level = 0;
        uint8_t priority = 0;
        bool culled = false;
        bool visible_last_frame = true;
    } meta;
    
    // String caches (avoid allocations)
    mutable std::string cached_name;
    mutable std::string cached_weapon;
    mutable std::string cached_distance_str;
    mutable std::string cached_health_str;
    mutable ImVec2 cached_text_size{};
    mutable bool strings_dirty = true;
};

// Double-buffered entity cache
class EntityCache {
public:
    struct Config {
        size_t max_entities = 2048;
        float max_entity_age = 2.0f;           // seconds
        float cleanup_interval = 1.0f;         // seconds
        size_t max_history_per_entity = 10;
        float position_threshold = 0.1f;
        bool enable_interpolation = true;
        float interpolation_time = 0.1f;
        bool enable_prediction = false;
        float prediction_time = 0.05f;
    };
    
    explicit EntityCache(const Config& cfg = Config());
    ~EntityCache();
    
    // Update entity data
    void UpdateEntity(uint64_t id, const EntityData& data);
    void RemoveEntity(uint64_t id);
    void Clear();
    
    // Query
    EntityData* GetEntity(uint64_t id);
    const EntityData* GetEntity(uint64_t id) const;
    std::vector<uint64_t> GetActiveEntities() const;
    std::vector<uint64_t> GetEntitiesInRange(const Vec3& center, float radius) const;
    
    // Spatial queries
    void BuildSpatialIndex();
    std::vector<uint64_t> QueryFrustum(const Frustum& frustum) const;
    std::vector<uint64_t> QueryRadius(const Vec3& center, float radius) const;
    
    // Double buffering
    void SwapBuffers();
    EntityData* GetFrontBuffer(uint64_t id);
    EntityData* GetBackBuffer(uint64_t id);
    
    // Stats
    struct Stats {
        size_t total = 0;
        size_t active = 0;
        size_t culled = 0;
        float memory_mb = 0.0f;
        float avg_update_ms = 0.0f;
    };
    Stats GetStats() const;
    
    void Cleanup();
    void Update(float dt);
    
private:
    Config config_;
    std::unordered_map<uint64_t, EntityData> front_buffer_;
    std::unordered_map<uint64_t, EntityData> back_buffer_;
    std::unordered_map<uint64_t, EntityData>* front_ = &front_buffer_;
    std::unordered_map<uint64_t, EntityData>* back_ = &back_buffer_;
    mutable std::mutex mutex_;
    
    // Spatial partitioning
    struct SpatialGrid {
        float cell_size = 100.0f;
        std::unordered_map<int64_t, std::vector<uint64_t>> grid_;
        int64_t HashCell(int x, int y, int z) const;
    } spatial_grid_;
    
    std::chrono::steady_clock::time_point last_cleanup_;
    std::chrono::steady_clock::time_point last_spatial_update_;
    
    void CleanupStale();
    void UpdateSpatialIndex();
    void UpdateDerivedData(EntityData& entity);
};

// ============================================================================
// Adaptive Update System - Throttling, dirty flags, frame budgets
// ============================================================================

class AdaptiveUpdater {
public:
    struct Config {
        float base_interval_ms = 16.67f;
        float min_interval_ms = 4.0f;
        float max_interval_ms = 200.0f;
        float budget_per_frame_ms = 2.0f;
        bool adaptive = true;
        bool prioritize_by_distance = true;
        bool prioritize_by_screen_size = true;
        bool prioritize_by_velocity = true;
    };
    
    struct UpdatePolicy {
        float base_interval_ms = 16.67f;     // 60 FPS base
        float min_interval_ms = 4.0f;        // Max 250 Hz
        float max_interval_ms = 200.0f;      // Min 5 Hz
        float budget_per_frame_ms = 2.0f;    // Time budget per frame
        bool adaptive = true;
        bool prioritize_by_distance = true;
        bool prioritize_by_screen_size = true;
        bool prioritize_by_velocity = true;
    };
    
    struct EntityUpdateInfo {
        std::chrono::steady_clock::time_point last_update{};
        float current_interval_ms = 16.67f;
        float accumulated_time = 0.0f;
        bool dirty = true;
        uint8_t priority = 0;
        float distance = 0.0f;
        float screen_size = 0.0f;
        float velocity_magnitude = 0.0f;
    };
    
    explicit AdaptiveUpdater(const Config& cfg);
    
    // Register entity for adaptive updates
    void Register(uint64_t id, const UpdatePolicy& policy = {});
    void Unregister(uint64_t id);
    
    // Check if entity should update this frame
    bool ShouldUpdate(uint64_t id, float dt);
    
    // Mark entity as dirty (needs update)
    void MarkDirty(uint64_t id);
    
    // Update all entities' timers
    void Update(float dt);
    
    // Set global frame budget
    void SetFrameBudget(float ms);
    
    // Get stats
    struct Stats {
        size_t registered = 0;
        size_t updated_this_frame = 0;
        size_t skipped = 0;
        float avg_interval_ms = 0.0f;
        float frame_time_ms = 0.0f;
    };
    Stats GetStats() const;
    
private:
    Config config_;
    std::unordered_map<uint64_t, EntityUpdateInfo> entities_;
    std::vector<uint64_t> active_entities_;
    float current_budget_ms_ = 2.0f;
    float frame_time_accumulator_ = 0.0f;
    std::chrono::steady_clock::time_point last_frame_;
    
    float CalculateInterval(const EntityUpdateInfo& info) const;
    void SortByPriority();
};

// ============================================================================
// Culling Pipeline - Multi-layer with LOD
// ============================================================================

enum class CullResult {
    Visible,
    Culled_Frustum,
    Culled_Distance,
    Culled_ScreenSpace,
    Culled_Priority,
    Culled_Occluded,
    Culled_Invalid
};

enum class LODLevel : uint8_t {
    Full = 0,      // All features, full quality
    High = 1,      // Most features, high quality
    Medium = 2,    // Essential features, reduced quality
    Low = 3,       // Minimal features
    Culled = 255   // Completely culled
};

struct CullingContext {
    Frustum frustum;
    Vec3 camera_pos;
    Vec3 camera_forward;
    float near_plane = 0.1f;
    float far_plane = 10000.0f;
    float fov_horizontal = 90.0f;
    float fov_vertical = 70.0f;
    Vec2 screen_size;
    Vec2 screen_center;
    Matrix view_proj;
    float current_time = 0.0f;
    float frame_time = 0.0f;
};

struct CullingConfig {
    // Distance culling
    float min_distance = 0.0f;
    float max_distance = 1000.0f;
    
    // Screen space culling
    float screen_margin = 50.0f;
    float min_screen_size = 4.0f;  // pixels
    
    // Priority culling
    uint8_t min_priority = 0;
    size_t max_entities_per_frame = 2048;
    
    // LOD distances
    float lod_distances[4] = { 30.0f, 80.0f, 150.0f, 300.0f };
    
    // LOD feature sets (what features at each LOD)
    uint32_t lod_features[4] = {
        0xFFFFFFFF,  // Full: all features
        0x00001FF,   // High: core features
        0x000003F,   // Medium: basic features
        0x0000007    // Low: minimal features
    };
    
    // Adaptive quality
    bool adaptive_quality = true;
    float target_frame_time_ms = 16.67f;
    float quality_reduction_factor = 0.5f;
};

class CullingSystem {
public:
    explicit CullingSystem(const CullingConfig& cfg = CullingConfig());
    
    // Main culling function - returns LOD level and cull result
    std::pair<LODLevel, CullResult> CullEntity(const EntityData& entity, const CullingContext& ctx) const;
    
    // Batch culling for multiple entities
    void CullBatch(const std::vector<uint64_t>& entities, 
                   const CullingContext& ctx,
                   std::vector<std::pair<uint64_t, std::pair<LODLevel, CullResult>>>& results) const;
    
    // Frustum culling
    bool FrustumCull(const Vec3& pos, float radius, const Frustum& frustum) const;
    
    // Screen space culling
    bool ScreenSpaceCull(const Vec2& screen_pos, float screen_size, const CullingContext& ctx) const;
    
    // Distance culling
    bool DistanceCull(float distance, float min_dist, float max_dist) const;
    
    // Priority culling
    bool PriorityCull(uint8_t priority, uint8_t min_priority) const;
    
    // Calculate LOD level based on distance
    LODLevel CalculateLOD(float distance) const;
    
    // Get active features for LOD
    uint32_t GetActiveFeatures(LODLevel lod) const;
    
    // Update config
    void SetConfig(const CullingConfig& cfg);
    const CullingConfig& GetConfig() const { return config_; }
    
private:
     CullingConfig config_;
     
     void UpdateFrustum(const Matrix& view_proj, Frustum& frustum) const;
     bool CheckSphereFrustum(const Vec3& center, float radius, const Frustum& frustum) const;
     bool CheckBoxFrustum(const Vec3& min, const Vec3& max, const Frustum& frustum) const;
 };

// ============================================================================
// Spatial Partitioning - Grid/Octree for fast queries
// ============================================================================

class SpatialPartitioning {
public:
    struct Config {
        float cell_size = 100.0f;
        size_t max_entities_per_cell = 64;
        bool use_octree = false;
        float octree_min_size = 50.0f;
    };
    
    explicit SpatialPartitioning(const Config& cfg = Config());
    
    // Insert/update/remove
    void Insert(uint64_t id, const Vec3& pos, float radius = 1.0f);
    void Update(uint64_t id, const Vec3& old_pos, const Vec3& new_pos, float radius = 1.0f);
    void Remove(uint64_t id);
    
    // Queries
    std::vector<uint64_t> QueryRadius(const Vec3& center, float radius) const;
    std::vector<uint64_t> QueryBox(const Vec3& min, const Vec3& max) const;
    std::vector<uint64_t> QueryFrustum(const Frustum& frustum) const;
    
    // Get entities in cell
    const std::vector<uint64_t>* GetCell(int x, int y, int z) const;
    
    void Clear();
    void SetCellSize(float size);
    
    struct Stats {
        size_t total_entities = 0;
        size_t occupied_cells = 0;
        float avg_per_cell = 0.0f;
    };
    Stats GetStats() const;
    
private:
    struct Cell {
        std::vector<uint64_t> entities;
    };
    
    Config config_;
    float cell_size_;
    float inv_cell_size_;
    std::unordered_map<int64_t, std::vector<uint64_t>> grid_;
    std::unordered_map<uint64_t, std::tuple<int, int, int>> entity_cells_;
    
    int64_t HashCell(int x, int y, int z) const;
    void GetCellCoords(const Vec3& pos, int& x, int& y, int& z) const;
};

// ============================================================================
// Skeleton System with LOD and Bone Caching
// ============================================================================

struct SkeletonData {
    std::array<Vec3, 64> bone_positions{};  // SoA for SIMD
    std::array<bool, 64> bone_valid{};
    std::array<Vec2, 64> screen_positions{};
    std::array<bool, 64> on_screen{};
    Vec3 origin{};
    float distance = 0.0f;
    uint64_t entity_id = 0;
    std::chrono::steady_clock::time_point last_update{};
    uint16_t bone_mask = 0;
    uint8_t lod_level = 0;
    bool valid = false;
};

class SkeletonSystem {
public:
    struct Config {
        // Bone indices for different LODs
        std::array<int, 9> lod_full_bones = {0, 1, 2, 3, 4, 5, 6, 7, 8};      // All
        std::array<int, 5> lod_high_bones = {0, 1, 7, 8, 4};                   // Head, neck, shoulders, pelvis
        std::array<int, 3> lod_medium_bones = {0, 7, 8};                       // Head, neck, pelvis
        std::array<int, 1> lod_low_bones = {0};                                // Head only
        
        // Skeleton connections (bone index pairs)
        std::array<std::pair<int, int>, 12> connections = {{
            {0, 1}, {1, 7}, {7, 8},           // Spine
            {7, 5}, {5, 6},                   // Left arm
            {7, 6},                           // Right arm
            {8, 3}, {3, 4},                   // Left leg
            {8, 4},                           // Right leg
            {7, 8}                            // Torso
        }};
        
        // Caching
        float cache_validity_ms = 50.0f;
        float cache_cleanup_interval_s = 2.0f;
        size_t max_cached_skeletons = 1024;
        
        // Rendering
        float joint_radius = 3.0f;
        float line_thickness = 1.5f;
    };
    
    explicit SkeletonSystem(const Config& cfg = Config());
    
    // Batch update skeletons
    void UpdateBatch(const std::vector<uint64_t>& entity_ids,
                     const std::function<Vec3(uint64_t, int)>& get_bone_pos,
                     const Matrix& view_proj);
    
    // Get skeleton for rendering
    const SkeletonData* GetSkeleton(uint64_t entity_id) const;
    
// Get bones for LOD
     const std::vector<int>& GetBonesForLOD(LODLevel lod) const;
    
    // Render skeleton
    void RenderSkeleton(const SkeletonData& skeleton, ImDrawList* draw_list,
                        const Matrix& view_proj, ImU32 color, float thickness = 1.5f) const;
    
    // Cleanup
    void Cleanup();
    void Clear();
    
    struct Stats {
        size_t active_skeletons = 0;
        size_t cache_size = 0;
        float memory_mb = 0.0f;
    };
    Stats GetStats() const;
    
private:
    Config config_;
    std::unordered_map<uint64_t, SkeletonData> skeletons_;
    mutable std::mutex mutex_;
    std::chrono::steady_clock::time_point last_cleanup_;
    
    void CleanupStale();
    void UpdateSkeleton(uint64_t id, const std::function<Vec3(int)>& get_bone_pos, 
                        const Matrix& view_proj);
    void UpdateScreenPositions(SkeletonData& skeleton, const Matrix& view_proj);
};

// ============================================================================
// Text System - Caching and Batching
// ============================================================================

class TextCache {
public:
    struct CachedText {
        std::string text;
        ImVec2 size;
        std::chrono::steady_clock::time_point last_used;
        uint64_t entity_id = 0;
        uint32_t hash = 0;
    };
    
    struct Config {
        size_t max_entries = 4096;
        float ttl_seconds = 5.0f;
        bool enable_atlas = true;
    };
    
    explicit TextCache(const Config& cfg = Config());
    
    // Get or create cached text
    const CachedText* GetText(uint64_t entity_id, uint32_t hash, 
                              const std::function<std::string()>& generator);
    
    // Get text size without generating
    const ImVec2* GetTextSize(uint64_t entity_id, uint32_t hash) const;
    
    // Pre-calculate text sizes for batch
    void PrecalculateSizes(const std::vector<std::pair<uint64_t, std::string>>& texts);
    
    // Draw cached text
    void DrawText(ImDrawList* draw_list, uint64_t entity_id, uint32_t hash,
                  const ImVec2& pos, ImU32 color) const;
    
    // Batch draw multiple texts
    void DrawBatch(ImDrawList* draw_list,
                   const std::vector<std::tuple<uint64_t, uint32_t, ImVec2, ImU32>>& texts) const;
    
    void Clear();
    void Cleanup();
    
    struct Stats {
        size_t entries = 0;
        size_t hits = 0;
        size_t misses = 0;
        float memory_mb = 0.0f;
    };
    Stats GetStats() const;
    
private:
    Config config_;
    std::unordered_map<uint64_t, std::unordered_map<uint32_t, CachedText>> cache_;
    mutable std::mutex mutex_;
    std::chrono::steady_clock::time_point last_cleanup_;
    
    uint32_t HashString(const std::string& str) const;
    void CleanupStale();
};

// ============================================================================
// Transform System - World-to-Screen Optimization
// ============================================================================

class TransformSystem {
public:
    struct ViewData {
        Matrix view_matrix;
        Matrix proj_matrix;
        Matrix view_proj;
        Vec3 camera_pos;
        Vec3 camera_forward;
        Vec3 camera_up;
        Vec3 camera_right;
        float fov_horizontal = 90.0f;
        float fov_vertical = 70.0f;
        float aspect_ratio = 1.777f;
        float near_plane = 0.1f;
        float far_plane = 10000.0f;
        Vec2 screen_center;
        Vec2 screen_size;
    };
    
    // World to screen with caching
    bool WorldToScreen(const Vec3& world, const ViewData& view, Vec2& screen) const;
    bool WorldToScreenBatch(const std::vector<Vec3>& world_positions,
                            const ViewData& view,
                            std::vector<Vec2>& screen_positions,
                            std::vector<bool>& on_screen) const;
    
    // Screen to world
    bool ScreenToWorld(const Vec2& screen, float world_z,
                       const ViewData& view, Vec3& world) const;
    
    // Frustum culling
    bool IsInFrustum(const Vec3& pos, float radius, const ViewData& view) const;
    bool IsInFrustum(const Vec3& min, const Vec3& max, const ViewData& view) const;
    
    // Distance calculations
    float GetDistanceToScreenCenter(const Vec2& screen_pos, const ViewData& view) const;
    float GetScreenSpaceSize(const Vec3& world_pos, float world_size, const ViewData& view) const;
    
    // FOV calculations
    static float CalculateHorizontalFOV(float vertical_fov, float aspect);
    static float CalculateVerticalFOV(float horizontal_fov, float aspect);
    static float CalculateIdealFOV(float monitor_width_mm, float view_distance_mm);
    
    // View data setup
    static ViewData CreateViewData(const Matrix& view_matrix, const Matrix& proj_matrix,
                                   const Vec2& screen_size);
    
private:
    mutable std::unordered_map<uint64_t, Vec2> screen_cache_;
    mutable std::mutex cache_mutex_;
};

// ============================================================================
// Visibility System - VISIBLE / OCCLUDED
// ============================================================================

enum class VisibilityState {
    Unknown = 0,
    Visible = 1,
    Occluded = 2,
    PartiallyVisible = 3
};

struct VisibilityResult {
    VisibilityState state = VisibilityState::Unknown;
    float visibility = 1.0f;          // 0.0 - 1.0
    float distance = 0.0f;
    int hit_bone = -1;
    int surface_type = 0;
    bool through_smoke = false;
    bool through_glass = false;
    bool through_grate = false;
    std::chrono::microseconds check_time{};
};

struct VisibilityConfig {
    enum class Method {
        None = 0,
        Raycast = 1,
        MultiPoint = 2,
        Bresenham3D = 3,
        HardwareOcclusion = 4,
        Hybrid = 5
    };
    
    Method method = Method::MultiPoint;
    
    struct MultiPointConfig {
        bool check_head = true;
        bool check_neck = true;
        bool check_chest = true;
        bool check_pelvis = true;
        bool check_limbs = false;
        float min_visible_points = 0.3f;
        int max_points = 8;
    } multi_point;
    
    struct RaycastConfig {
        bool ignore_smoke = true;
        bool ignore_glass = true;
        bool ignore_grates = true;
        bool ignore_foliage = true;
        float max_distance = 500.0f;
        float radius = 0.0f;
    } raycast;
    
    bool enable_caching = true;
    float cache_duration = 0.1f;
    int max_cache_entries = 1024;
    bool cache_per_bone = true;
    
    int max_checks_per_frame = 32;
    bool async_checks = true;
    bool prioritize_closest = true;
    bool prioritize_aim_target = true;
    
    bool check_penetration = true;
    float max_penetration_depth = 30.0f;
    int max_penetration_count = 2;
};

class VisibilitySystem {
public:
    explicit VisibilitySystem(const VisibilityConfig& cfg = VisibilityConfig());
    
    VisibilityResult CheckVisibility(uint64_t observer, uint64_t target,
                                     const Vec3& observer_pos, const Vec3& target_pos,
                                     int bone_index = -1);
    
    // Batch check
    std::vector<std::pair<uint64_t, VisibilityResult>> CheckMultiple(
        const std::vector<uint64_t>& targets,
        uint64_t observer, const Vec3& observer_pos);
    
    // Cache management
    void ClearCache();
    void PruneCache();
    
    // Raycast interface (to be implemented per game)
    virtual bool Raycast(const Vec3& start, const Vec3& end,
                         Vec3* hit_pos = nullptr, int* surface_type = nullptr) = 0;
    
    struct Stats {
        int checks_per_frame = 0;
        int cache_hits = 0;
        int cache_misses = 0;
        float avg_check_time_ms = 0.0f;
        int visible_count = 0;
        int occluded_count = 0;
        int partial_count = 0;
    };
    Stats GetStats() const { return stats_; }
    void ResetStats();
    
protected:
    VisibilityConfig config_;
    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, std::vector<VisibilityResult>> visibility_cache_;
    std::chrono::steady_clock::time_point last_cleanup_;
    Stats stats_;
    
    void CleanupCache();
    bool IsCacheValid(const VisibilityResult& result) const;
};

// ============================================================================
// Priority System & Adaptive Quality
// ============================================================================

struct PriorityConfig {
    // Weights for priority calculation
    float distance_weight = 1.0f;
    float screen_size_weight = 2.0f;
    float velocity_weight = 0.5f;
    float relevance_weight = 1.5f;
    
    // Priority thresholds
    float high_priority_threshold = 0.7f;
    float low_priority_threshold = 0.3f;
    
    // Max entities per priority level
    size_t max_high_priority = 50;
    size_t max_medium_priority = 200;
    size_t max_low_priority = 500;
};

class PrioritySystem {
public:
    explicit PrioritySystem(const PriorityConfig& cfg = PriorityConfig());
    
    // Calculate priority for entity
    float CalculatePriority(const EntityData& entity, 
                            const Vec3& camera_pos,
                            const Vec2& screen_pos,
                            float screen_size) const;
    
    // Get priority tier
    enum class Tier { High, Medium, Low, Culled };
    Tier GetTier(float priority) const;
    
    // Sort entities by priority (highest first)
    void SortByPriority(std::vector<uint64_t>& entities,
                        const std::function<const EntityData*(uint64_t)>& get_entity,
                        const Vec3& camera_pos) const;
    
    // Get max entities per tier
    size_t GetMaxEntities(Tier tier) const;
    
private:
    PriorityConfig config_;
};

// Adaptive Quality System
struct AdaptiveQualityConfig {
    float target_frame_time_ms = 16.67f;
    float critical_frame_time_ms = 25.0f;
    
    // Quality levels
    struct QualityLevel {
        float lod_bias = 0.0f;           // Positive = higher LOD (lower quality)
        float update_rate_scale = 1.0f;  // Multiply update intervals
        uint32_t disabled_features = 0;  // Feature mask to disable
        float cull_distance_scale = 1.0f;
        float max_entities_scale = 1.0f;
    };
    
    QualityLevel levels[5] = {
        { 0.0f, 1.0f, 0, 1.0f, 1.0f },      // Full quality
        { 1.0f, 1.5f, 0x00060000, 1.2f, 0.9f },   // Slight reduction
        { 2.0f, 2.0f, 0x000E0000, 1.5f, 0.7f },   // Medium reduction
        { 3.0f, 3.0f, 0x001E0000, 2.0f, 0.5f },   // Heavy reduction
        { 4.0f, 5.0f, 0x003E0000, 3.0f, 0.3f }    // Minimum quality
    };
    
    float transition_speed = 0.1f;  // How fast to transition
};

class AdaptiveQualitySystem {
public:
    explicit AdaptiveQualitySystem(const AdaptiveQualityConfig& cfg = AdaptiveQualityConfig());
    
    void Update(float frame_time_ms);
    int GetCurrentQualityLevel() const { return current_level_; }
    const AdaptiveQualityConfig::QualityLevel& GetCurrentLevel() const;
    
    // Apply quality level to systems
    template<typename T>
    void ApplyToSystem(T& system) const;
    
    // Get recommended LOD bias
    float GetLODBias() const;
    float GetUpdateRateScale() const;
    uint32_t GetDisabledFeatures() const;
    float GetCullDistanceScale() const;
    float GetMaxEntitiesScale() const;
    
private:
    AdaptiveQualityConfig config_;
    int current_level_ = 0;
    float target_level_ = 0.0f;
    std::chrono::steady_clock::time_point last_transition_;
    
    void TransitionQuality(float target_level);
};

// ============================================================================
// Profiling System
// ============================================================================

struct ESPProfiler {
    struct Metrics {
        // Acquisition
        float acquire_ms = 0.0f;
        int entities_acquired = 0;
        
        // Processing
        float process_ms = 0.0f;
        int entities_processed = 0;
        
        // World-to-screen
        float w2s_ms = 0.0f;
        int w2s_count = 0;
        
        // Visibility
        float visibility_ms = 0.0f;
        int visibility_checks = 0;
        
        // Skeleton
        float skeleton_ms = 0.0f;
        int skeletons_processed = 0;
        
        // Text
        float text_ms = 0.0f;
        int text_elements = 0;
        
        // Culling
        float culling_ms = 0.0f;
        int culled = 0;
        
        // LOD
        float lod_ms = 0.0f;
        
        // Rendering
        float render_ms = 0.0f;
        int draw_calls = 0;
        
        // Total
        float total_ms = 0.0f;
        int entities_rendered = 0;
        
        // Memory
        float memory_mb = 0.0f;
    };
    
    class ScopedTimer {
    public:
        ScopedTimer(ESPProfiler& profiler, float& target_metric);
        ~ScopedTimer();
    private:
        ESPProfiler& profiler_;
        float& target_;
        std::chrono::steady_clock::time_point start_;
    };
    
    void BeginFrame();
    void EndFrame();
    void Reset();
    
    ScopedTimer TimeAcquire() { return ScopedTimer(*this, metrics_.acquire_ms); }
    ScopedTimer TimeProcess() { return ScopedTimer(*this, metrics_.process_ms); }
    ScopedTimer TimeW2S() { return ScopedTimer(*this, metrics_.w2s_ms); }
    ScopedTimer TimeVisibility() { return ScopedTimer(*this, metrics_.visibility_ms); }
    ScopedTimer TimeSkeleton() { return ScopedTimer(*this, metrics_.skeleton_ms); }
    ScopedTimer TimeText() { return ScopedTimer(*this, metrics_.text_ms); }
    ScopedTimer TimeCulling() { return ScopedTimer(*this, metrics_.culling_ms); }
    ScopedTimer TimeRender() { return ScopedTimer(*this, metrics_.render_ms); }
    
    const Metrics& GetMetrics() const { return metrics_; }
    const Metrics& GetLastFrameMetrics() const { return last_frame_metrics_; }
    
    void IncrementEntitiesAcquired(int count = 1) { metrics_.entities_acquired += count; }
    void IncrementEntitiesProcessed(int count = 1) { metrics_.entities_processed += count; }
    void IncrementW2SCount(int count = 1) { metrics_.w2s_count += count; }
    void IncrementVisibilityChecks(int count = 1) { metrics_.visibility_checks += count; }
    void IncrementSkeletonsProcessed(int count = 1) { metrics_.skeletons_processed += count; }
    void IncrementTextElements(int count = 1) { metrics_.text_elements += count; }
    void IncrementCulled(int count = 1) { metrics_.culled += count; }
    void IncrementDrawCalls(int count = 1) { metrics_.draw_calls += count; }
    void IncrementEntitiesRendered(int count = 1) { metrics_.entities_rendered += count; }
    
    std::string GenerateReport() const;
    
private:
    Metrics metrics_;
    Metrics last_frame_metrics_;
    std::chrono::steady_clock::time_point frame_start_;
};

// ScopedTimer implementation
inline ESPProfiler::ScopedTimer::ScopedTimer(ESPProfiler& profiler, float& target_metric)
    : profiler_(profiler), target_(target_metric), start_(std::chrono::steady_clock::now()) {}

inline ESPProfiler::ScopedTimer::~ScopedTimer() {
    auto end = std::chrono::steady_clock::now();
    float ms = std::chrono::duration<float, std::milli>(end - start_).count();
    target_ += ms;
}

// ============================================================================
// Main ESP Pipeline - Integrates all systems
// ============================================================================

class ESPPipeline {
public:
    struct Config {
        // Sub-system configs
        EntityCache::Config entity_cache;
        AdaptiveUpdater::Config adaptive_updater;
        CullingConfig culling;
        SkeletonSystem::Config skeleton;
        TextCache::Config text_cache;
        VisibilityConfig visibility;
        PriorityConfig priority;
        AdaptiveQualityConfig adaptive_quality;
        ESPProfiler::Metrics profiler;
        
        // Feature toggles
        bool enable_spatial_partitioning = true;
        bool enable_adaptive_updates = true;
        bool enable_adaptive_quality = true;
        bool enable_profiling = true;
        bool enable_async_visibility = true;
        int max_threads = 2;
    };
    
    explicit ESPPipeline(const Config& cfg = Config());
    ~ESPPipeline();
    
    // Main pipeline update
    void Update(const std::vector<uint64_t>& entity_ids,
                const std::function<EntityData(uint64_t)>& data_provider,
                const TransformSystem::ViewData& view_data);
    
    // Render
    void Render(ImDrawList* draw_list,
                const std::function<EntityData*(uint64_t)>& data_provider,
                const TransformSystem::ViewData& view_data);
    
    // Entity management
    void AddEntity(uint64_t id, const EntityData& data);
    void RemoveEntity(uint64_t id);
    void UpdateEntity(uint64_t id, const EntityData& data);
    
    // Feature control
    void SetFeatureEnabled(ESPFeature feature, bool enabled);
    bool IsFeatureEnabled(ESPFeature feature) const;
    
    // Get stats
    const ESPProfiler::Metrics& GetLastFrameMetrics() const;
    std::string GetPerformanceReport() const;
    
    // Profiling
    ESPProfiler& GetProfiler() { return *profiler_; }
    
private:
    Config config_;
    
    // Sub-systems
    std::unique_ptr<EntityCache> entity_cache_;
    std::unique_ptr<AdaptiveUpdater> adaptive_updater_;
    std::unique_ptr<CullingSystem> culling_system_;
    std::unique_ptr<SkeletonSystem> skeleton_system_;
    std::unique_ptr<TextCache> text_cache_;
    std::unique_ptr<TransformSystem> transform_system_;
    std::unique_ptr<VisibilitySystem> visibility_system_;
    std::unique_ptr<PrioritySystem> priority_system_;
    std::unique_ptr<AdaptiveQualitySystem> adaptive_quality_;
    std::unique_ptr<SpatialPartitioning> spatial_index_;
    std::unique_ptr<ESPProfiler> profiler_;
    
    // Thread pool for async operations
    class ThreadPool {
    public:
        explicit ThreadPool(int num_threads);
        ~ThreadPool();
        void Enqueue(std::function<void()> task);
        void Wait();
    private:
        std::vector<std::thread> workers_;
        std::queue<std::function<void()>> tasks_;
        std::mutex queue_mutex_;
        std::condition_variable condition_;
        std::atomic<bool> stop_{false};
    };
    std::unique_ptr<ThreadPool> thread_pool_;
    
    // Frame data
    std::vector<uint64_t> active_entities_;
    std::vector<uint64_t> culled_entities_;
    std::vector<std::pair<uint64_t, std::pair<LODLevel, CullResult>>> cull_results_;
    TransformSystem::ViewData current_view_data_;
    std::chrono::steady_clock::time_point last_frame_time_;
    
    // Pipeline stages
    void StageAcquire(const std::vector<uint64_t>& entity_ids,
                      const std::function<EntityData(uint64_t)>& provider);
    void StageProcess(const TransformSystem::ViewData& view_data);
    void StageCull(const TransformSystem::ViewData& view_data);
    void StageRender(ImDrawList* draw_list);
    void StageCleanup();
    
    // Quality adaptation
    void AdaptQuality(float frame_time_ms);
    
    // Profiling
    ESPProfiler::Metrics last_metrics_;
};

// ============================================================================
// Global accessor
// ============================================================================

ESPPipeline& GetESPPipeline();

} // namespace Gameplay::ESPOptimizer