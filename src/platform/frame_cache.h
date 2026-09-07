#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace OmniGhost::Platform {

// ============================================================
// Frame Cache - Per-frame caching for hot paths
// ============================================================

struct BoneMatrix {
    std::array<float, 16> matrix;  // 4x4 column-major
    uint64_t frameStamp = 0;
    bool valid = false;
};

struct WorldToScreenCache {
    float screenX = 0.0f;
    float screenY = 0.0f;
    float screenZ = 0.0f;
    bool onScreen = false;
    uint64_t frameStamp = 0;
    bool valid = false;
};

struct EntityCacheEntry {
    // Bone matrices (indexed by bone ID)
    std::unordered_map<uint32_t, BoneMatrix> bones;
    
    // World-to-screen
    WorldToScreenCache w2s;
    
    // Entity metadata
    uint64_t entityAddress = 0;
    uint32_t entityId = 0;
    float distance = 0.0f;
    bool isVisible = false;
    
    // Frame stamp for invalidation
    uint64_t frameStamp = 0;
    bool valid = false;
};

// ============================================================
// FrameCache - Per-frame cache with automatic invalidation
// ============================================================

class FrameCache {
public:
    FrameCache() = default;
    ~FrameCache() = default;
    
    // Frame management
    void BeginFrame(uint64_t frameNumber) noexcept;
    void EndFrame() noexcept;
    
    // Entity cache
    EntityCacheEntry* GetOrCreateEntity(uint64_t entityAddress, uint32_t entityId) noexcept;
    EntityCacheEntry* GetEntity(uint64_t entityAddress) noexcept;
    void InvalidateEntity(uint64_t entityAddress) noexcept;
    
    // Bone matrix cache
    BoneMatrix* GetBoneMatrix(EntityCacheEntry* entity, uint32_t boneId) noexcept;
    void SetBoneMatrix(EntityCacheEntry* entity, uint32_t boneId, const std::array<float, 16>& matrix) noexcept;
    
    // World-to-screen cache
    WorldToScreenCache* GetW2S(EntityCacheEntry* entity) noexcept;
    void SetW2S(EntityCacheEntry* entity, float x, float y, float z, bool onScreen) noexcept;
    
    // Distance culling
    void SetEntityDistance(EntityCacheEntry* entity, float distance) noexcept;
    [[nodiscard]] float GetEntityDistance(EntityCacheEntry* entity) const noexcept;
    
    // Visibility
    void SetEntityVisibility(EntityCacheEntry* entity, bool visible) noexcept;
    [[nodiscard]] bool GetEntityVisibility(EntityCacheEntry* entity) const noexcept;
    
    // Frame statistics
    struct Stats {
        size_t totalEntities = 0;
        size_t cachedBones = 0;
        size_t cachedW2S = 0;
        size_t cacheHits = 0;
        size_t cacheMisses = 0;
        double hitRate = 0.0;
    };
    
    [[nodiscard]] Stats GetStats() const noexcept;
    void ResetStats() noexcept;
    
    // Configuration
    void SetMaxEntities(size_t max) noexcept { maxEntities_ = max; }
    void SetMaxBonesPerEntity(size_t max) noexcept { maxBonesPerEntity_ = max; }
    
    // Global accessor
    static FrameCache& Instance() noexcept {
        static FrameCache instance;
        return instance;
    }
    
private:
    // Current frame stamp
    std::atomic<uint64_t> currentFrame_{0};
    
    // Entity cache (address -> entry)
    mutable std::mutex cacheMutex_;
    std::unordered_map<uint64_t, EntityCacheEntry> entityCache_;
    
    // Configuration
    size_t maxEntities_ = 1024;
    size_t maxBonesPerEntity_ = 128;
    
    // Statistics
    mutable std::mutex statsMutex_;
    size_t cacheHits_ = 0;
    size_t cacheMisses_ = 0;
    
    // Cleanup old entries
    void CleanupOldEntries() noexcept;
    
    // LRU tracking
    std::vector<uint64_t> lruList_;
};

// ============================================================
// Scoped Frame Cache Access
// ============================================================

class ScopedFrame {
public:
    explicit ScopedFrame(uint64_t frameNumber = 0) noexcept {
        FrameCache::Instance().BeginFrame(frameNumber);
    }
    
    ~ScopedFrame() noexcept {
        FrameCache::Instance().EndFrame();
    }
};

// Convenience functions
inline EntityCacheEntry* GetEntityCache(uint64_t address, uint32_t id = 0) noexcept {
    return FrameCache::Instance().GetOrCreateEntity(address, id);
}

inline BoneMatrix* GetCachedBone(EntityCacheEntry* entity, uint32_t boneId) noexcept {
    return FrameCache::Instance().GetBoneMatrix(entity, boneId);
}

inline WorldToScreenCache* GetCachedW2S(EntityCacheEntry* entity) noexcept {
    return FrameCache::Instance().GetW2S(entity);
}

inline void CacheBone(EntityCacheEntry* entity, uint32_t boneId, const std::array<float, 16>& matrix) noexcept {
    FrameCache::Instance().SetBoneMatrix(entity, boneId, matrix);
}

inline void CacheW2S(EntityCacheEntry* entity, float x, float y, float z, bool onScreen) noexcept {
    FrameCache::Instance().SetW2S(entity, x, y, z, onScreen);
}

} // namespace OmniGhost::Platform