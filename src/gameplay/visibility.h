#pragma once
#include "../../ImGui/imgui.h"
#include <vector>
#include <array>
#include <functional>
#include <chrono>
#include <unordered_map>
#include <cmath>

namespace Gameplay::Visibility {

    // Helper: Vec3 for visibility
    struct Vec3 {
        float x = 0, y = 0, z = 0;
        Vec3() = default;
        Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
        
        Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
        Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
        Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
        Vec3 operator/(float s) const { return Vec3(x / s, y / s, z / s); }
        
        float Length() const { return sqrtf(x*x + y*y + z*z); }
        float Length2D() const { return sqrtf(x*x + y*y); }
        float DistTo(const Vec3& o) const { return (*this - o).Length(); }
        Vec3 Normalized() const { float l = Length(); return l > 0 ? *this / l : Vec3{}; }
        float Dot(const Vec3& o) const { return x*o.x + y*o.y + z*o.z; }
        Vec3 Cross(const Vec3& o) const {
            return Vec3(y*o.z - z*o.y, z*o.x - x*o.z, x*o.y - y*o.x);
        }
        bool IsZero() const { return x == 0 && y == 0 && z == 0; }
    };

    // 4x4 Matrix for view projection
    struct Matrix {
        float m[16] = {};
        float& operator()(int row, int col) { return m[row * 4 + col]; }
        const float& operator()(int row, int col) const { return m[row * 4 + col]; }
    };

    // Visibility check methods
    enum class CheckMethod : int {
        None = 0,
        Raycast = 1,           // Simple raycast
        MultiPoint = 2,        // Multiple raycasts (head, chest, etc.)
        Bresenham3D = 3,       // Voxel-based line of sight
        HardwareOcclusion = 4, // GPU occlusion query
        Hybrid = 5             // Combination of methods
    };

    // Visibility result
    struct VisibilityResult {
        bool visible = false;
        float visibility = 0.0f;       // 0.0 - 1.0 (percentage visible)
        int hit_bone = -1;             // Which bone was hit (if any)
        float distance = 0.0f;         // Distance to target
        int surface_type = 0;          // Material/surface type hit
        bool through_smoke = false;    // Line passes through smoke
        bool through_glass = false;    // Line passes through glass
        bool through_grate = false;    // Line passes through grate/fence
        std::chrono::microseconds check_time; // Time taken for check
    };

    // Visibility check configuration
    struct VisibilityConfig {
        CheckMethod method = CheckMethod::MultiPoint;
        
        // Multi-point settings
        struct MultiPointConfig {
            bool check_head = true;
            bool check_neck = true;
            bool check_chest = true;
            bool check_pelvis = true;
            bool check_limbs = false;
            float min_visible_points = 0.3f; // Minimum fraction of points visible
            int max_points = 8;
        } multi_point;
        
        // Raycast settings
        struct RaycastConfig {
            bool ignore_smoke = false;
            bool ignore_glass = false;
            bool ignore_grates = false;
            bool ignore_foliage = true;
            float max_distance = 500.0f;
            float radius = 0.0f; // 0 = line, >0 = capsule
        } raycast;
        
        // Caching
        bool enable_caching = true;
        float cache_duration = 0.1f;        // Cache validity (seconds)
        int max_cache_entries = 1024;
        bool cache_per_bone = true;
        
        // Performance
        int max_checks_per_frame = 32;
        bool async_checks = true;           // Use background thread
        bool prioritize_closest = true;
        bool prioritize_aim_target = true;
        
        // Penetration
        bool check_penetration = true;
        float max_penetration_depth = 30.0f;
        int max_penetration_count = 2;
        
        // Debug
        bool debug_draw = false;
        ImU32 debug_visible_color = IM_COL32(0, 255, 0, 200);
        ImU32 debug_occluded_color = IM_COL32(255, 0, 0, 200);
        ImU32 debug_partial_color = IM_COL32(255, 255, 0, 200);
    };

    // Visibility cache entry
    struct CacheEntry {
        uint64_t observer_id = 0;
        uint64_t target_id = 0;
        int bone_index = -1;
        VisibilityResult result;
        std::chrono::steady_clock::time_point timestamp;
        bool valid = false;
    };

    // Visibility checker interface
    class IVisibilityChecker {
    public:
        virtual ~IVisibilityChecker() = default;
        virtual VisibilityResult CheckVisibility(uint64_t observer, uint64_t target, 
                                                const Vec3& observer_pos, const Vec3& target_pos,
                                                int bone_index = -1) = 0;
        virtual void SetConfig(const VisibilityConfig& config) = 0;
        virtual const VisibilityConfig& GetConfig() const = 0;
        virtual void ClearCache() = 0;
        virtual void Update(float dt) = 0;
    };

    // Game-specific visibility implementation
    class VisibilityManager {
    public:
        struct EntityData {
            uint64_t id = 0;
            Vec3 position{};
            Vec3 velocity{};
            bool is_local = false;
            bool is_alive = true;
            int team = 0;
            std::array<Vec3, 50> bone_positions{};
            std::array<bool, 50> bone_valid{};
            float bounds_radius = 1.0f;
            float bounds_height = 2.0f;
        };
        
        static VisibilityManager& Instance();
        
        void SetConfig(const VisibilityConfig& config) { config_ = config; }
        const VisibilityConfig& GetConfig() const { return config_; }
        
        // Entity management
        void UpdateEntity(const EntityData& entity);
        void RemoveEntity(uint64_t id);
        void ClearEntities();
        
        // Local player
        void SetLocalPlayer(uint64_t id) { local_player_id_ = id; }
        uint64_t GetLocalPlayer() const { return local_player_id_; }
        
        // Visibility checks
        VisibilityResult CheckVisibility(uint64_t target_id, int bone_index = -1);
        VisibilityResult CheckVisibility(uint64_t observer_id, uint64_t target_id, int bone_index = -1);
        
        // Batch check
        std::vector<std::pair<uint64_t, VisibilityResult>> CheckMultiple(
            const std::vector<uint64_t>& targets, int bone_index = -1);
        
        // Cache management
        void ClearCache();
        void PruneCache();
        size_t GetCacheSize() const;
        
        // Stats
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
        
        // Debug
        void DrawDebug(ImDrawList* draw, const Vec3& camera_pos, const Matrix& view_proj);
        
        // Game-specific raycast (to be implemented by game)
        virtual bool Raycast(const Vec3& start, const Vec3& end, Vec3* hit_pos = nullptr, 
                            int* surface_type = nullptr, bool* through_smoke = nullptr) = 0;
        
    protected:
        VisibilityConfig config_;
        std::unordered_map<uint64_t, EntityData> entities_;
        std::unordered_map<uint64_t, std::vector<CacheEntry>> cache_; // per observer
        uint64_t local_player_id_ = 0;
        Stats stats_;
        
        VisibilityResult CheckVisibilityInternal(uint64_t observer_id, uint64_t target_id, int bone_index);
        VisibilityResult PerformMultiPointCheck(const EntityData& observer, const EntityData& target);
        VisibilityResult PerformRaycastCheck(const Vec3& start, const Vec3& end);
        bool IsCacheValid(const CacheEntry& entry) const;
        void AddToCache(uint64_t observer, uint64_t target, int bone, const VisibilityResult& result);
        VisibilityResult GetFromCache(uint64_t observer, uint64_t target, int bone);
    };

    // Predefined bone indices for common visibility points
    enum class VisibilityBone : int {
        Head = 0,
        Neck = 1,
        UpperChest = 2,
        Chest = 3,
        LowerChest = 4,
        Pelvis = 5,
        LeftShoulder = 6,
        RightShoulder = 7,
        LeftElbow = 8,
        RightElbow = 9,
        LeftHand = 10,
        RightHand = 11,
        LeftKnee = 12,
        RightKnee = 13,
        LeftFoot = 14,
        RightFoot = 15
    };

    // Get standard visibility bone positions for a game
    std::vector<int> GetStandardVisibilityBones(const char* game_name);

    // Visibility presets
    VisibilityConfig GetLegitVisibilityConfig();
    VisibilityConfig GetRageVisibilityConfig();
    VisibilityConfig GetSniperVisibilityConfig();
    VisibilityConfig GetCompetitiveVisibilityConfig();

    // Serialization
    std::string SerializeVisibilityConfig(const VisibilityConfig& config);
    bool DeserializeVisibilityConfig(const std::string& data, VisibilityConfig& config);

} // namespace Gameplay::Visibility