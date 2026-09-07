#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include <chrono>
#include <mutex>
#include <functional>
#include <memory>
#include <cmath>

namespace Gameplay::EntityCache {

    // Helper: Vec3
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
        float Dot(const Vec3& o) const { return x*o.x + y*o.y + z*o.z; }
        Vec3 Cross(const Vec3& o) const { return Vec3(y*o.z - z*o.y, z*o.x - x*o.z, x*o.y - y*o.x); }
        Vec3 Normalized() const { float l = Length(); return l > 0 ? *this / l : Vec3{}; }
        bool IsZero() const { return x == 0 && y == 0 && z == 0; }
    };

    struct Frustum {
        Vec3 planes[6][4]; // 6 planes, 4 coefficients each (ax + by + cz + d = 0)
    };

    // Entity types
    enum class EntityType : int {
        Player = 0,
        Vehicle = 1,
        Animal = 2,
        Item = 3,
        Projectile = 4,
        Building = 5,
        NPC = 6,
        Resource = 7,
        DroppedItem = 8,
        Custom = 99
    };

    // Entity data structure
    struct EntityData {
        uint64_t id = 0;
        EntityType type = EntityType::Player;
        std::string name;
        std::string model;
        Vec3 position{};
        Vec3 velocity{};
        Vec3 angles{};
        float health = 100.0f;
        float max_health = 100.0f;
        float armor = 0.0f;
        int team = 0;
        bool is_local = false;
        bool is_friendly = false;
        bool is_visible = false;
        bool is_alive = true;
        bool is_dormant = false;
        std::chrono::steady_clock::time_point last_update;
        std::chrono::steady_clock::time_point first_seen;
        uint32_t flags = 0;
        std::unordered_map<std::string, std::string> custom_data;
        
        // Derived
        float distance_to_local = 0.0f;
        float screen_distance = 0.0f;
    };

    // Cache configuration
    struct CacheConfig {
        size_t max_entities = 1024;
        float max_age = 2.0f;                    // Seconds before stale
        float cleanup_interval = 1.0f;           // Seconds between cleanups
        bool enable_interpolation = true;
        float interpolation_time = 0.1f;         // Seconds
        bool enable_prediction = false;
        float prediction_time = 0.05f;
        bool thread_safe = true;
        size_t max_history_per_entity = 10;      // Position history for interpolation
        float position_threshold = 0.1f;         // Min position change to record
        bool compress_history = true;
    };

    // Entity cache
    class EntityCache {
    public:
        EntityCache();
        explicit EntityCache(const CacheConfig& config);
        ~EntityCache();
        
        void SetConfig(const CacheConfig& config) { config_ = config; }
        const CacheConfig& GetConfig() const { return config_; }
        
        // Entity management
        void UpdateEntity(const EntityData& entity);
        void RemoveEntity(uint64_t id);
        void Clear();
        
        // Queries
        EntityData* GetEntity(uint64_t id);
        const EntityData* GetEntity(uint64_t id) const;
        EntityData* GetOrCreateEntity(uint64_t id, EntityType type);
        
        std::vector<EntityData*> GetEntities(EntityType type = EntityType::Player);
        std::vector<EntityData*> GetEntitiesInRange(const Vec3& center, float radius, EntityType type = EntityType::Player);
        std::vector<EntityData*> GetVisibleEntities(EntityType type = EntityType::Player);
        std::vector<EntityData*> GetEntitiesByTeam(int team, EntityType type = EntityType::Player);
        
        // Local player
        void SetLocalPlayer(uint64_t id) { local_player_id_ = id; }
        uint64_t GetLocalPlayer() const { return local_player_id_; }
        EntityData* GetLocalPlayerEntity() { return GetEntity(local_player_id_); }
        
        // Interpolation
        void EnableInterpolation(bool enable) { config_.enable_interpolation = enable; }
        Vec3 GetInterpolatedPosition(uint64_t id, float alpha);
        Vec3 GetPredictedPosition(uint64_t id, float time_ahead);
        
        // History
        void RecordPosition(uint64_t id, const Vec3& pos, const Vec3& vel);
        std::vector<Vec3> GetPositionHistory(uint64_t id, float time_range) const;
        
        // Stats
        struct Stats {
            size_t total_entities = 0;
            size_t active_entities = 0;
            size_t stale_entities = 0;
            float memory_usage_mb = 0.0f;
            float avg_update_time_ms = 0.0f;
        };
        Stats GetStats() const;
        
        // Maintenance
        void Cleanup();
        void Update(float dt);
        
        // Serialization
        std::string Serialize() const;
        bool Deserialize(const std::string& data);
        
    private:
        CacheConfig config_;
        std::unordered_map<uint64_t, EntityData> entities_;
        mutable std::mutex mutex_;
        uint64_t local_player_id_ = 0;
        
        // Position history for interpolation
        struct HistoryEntry {
            Vec3 position;
            Vec3 velocity;
            std::chrono::steady_clock::time_point timestamp;
        };
        std::unordered_map<uint64_t, std::vector<HistoryEntry>> position_history_;
        
        void CleanupStaleEntities();
        void PruneHistory(uint64_t id);
        void UpdateDerivedData(EntityData& entity);
    };
    
    // Multi-threaded entity cache
    class ThreadedEntityCache {
    public:
        ThreadedEntityCache(size_t num_threads = 2);
        ~ThreadedEntityCache();
        
        // Async operations
        void AsyncUpdateEntity(const EntityData& entity);
        void AsyncRemoveEntity(uint64_t id);
        
        // Sync access
        EntityData* GetEntity(uint64_t id);
        const EntityData* GetEntity(uint64_t id) const;
        
        // Flush pending operations
        void Flush();
        
        // Worker management
        void SetNumThreads(size_t n);
        size_t GetNumThreads() const;
        
    private:
        struct WorkerData {
            std::thread thread;
            std::mutex queue_mutex;
            std::vector<std::function<void()>> queue;
            std::condition_variable cv;
            bool stop = false;
        };
        
        std::vector<WorkerData> workers_;
        std::atomic<bool> running_ = true;
        
        void WorkerLoop(size_t index);
    };
    
    // Entity pool for memory efficiency
    class EntityPool {
    public:
        EntityPool(size_t initial_size = 1024);
        ~EntityPool();
        
        EntityData* Acquire();
        void Release(EntityData* entity);
        
        size_t Available() const;
        size_t InUse() const;
        size_t Total() const;
        
        void Clear();
        
    private:
        std::vector<std::unique_ptr<EntityData>> pool_;
        std::vector<EntityData*> free_list_;
        std::mutex mutex_;
    };
    
    // Spatial partitioning for fast queries
    class SpatialHash {
    public:
        SpatialHash(float cell_size = 100.0f);
        
        void Insert(uint64_t entity_id, const Vec3& position);
        void Remove(uint64_t entity_id);
        void Update(uint64_t entity_id, const Vec3& old_pos, const Vec3& new_pos);
        
        std::vector<uint64_t> QueryRadius(const Vec3& center, float radius) const;
        std::vector<uint64_t> QueryBox(const Vec3& min, const Vec3& max) const;
        std::vector<uint64_t> QueryFrustum(const Frustum& frustum) const;
        
        void Clear();
        void SetCellSize(float size);
        
        struct Stats {
            size_t total_entities = 0;
            size_t occupied_cells = 0;
            float avg_entities_per_cell = 0.0f;
        };
        Stats GetStats() const;
        
    private:
        struct Cell {
            std::vector<uint64_t> entities;
        };
        
        float cell_size_ = 100.0f;
        float inv_cell_size_ = 0.01f;
        std::unordered_map<int64_t, Cell> grid_;
        
        int64_t HashCell(int x, int y, int z) const;
        void GetCellCoords(const Vec3& pos, int& x, int& y, int& z) const;
    };
    
    // Serialization
    std::string SerializeEntityData(const EntityData& entity);
    bool DeserializeEntityData(const std::string& data, EntityData& entity);

} // namespace Gameplay::EntityCache