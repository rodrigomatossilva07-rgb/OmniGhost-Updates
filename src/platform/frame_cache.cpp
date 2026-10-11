#include "frame_cache.h"
#include <algorithm>
#include <chrono>
#include <functional>

namespace OmniGhost::Platform {

void FrameCache::BeginFrame(uint64_t frameNumber) noexcept {
    if (frameNumber == 0) {
        // Auto-generate frame number from timestamp
        frameNumber = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
    }
    currentFrame_.store(frameNumber, std::memory_order_release);
    
    // Remove expired entries at frame start (cheap, no periodic full scan)
    {
        std::unique_lock lock(cacheMutex_);
        RemoveExpiredEntriesLocked();
    }
}

void FrameCache::EndFrame() noexcept {
    // Frame ends, next BeginFrame will increment
}

EntityCacheEntry* FrameCache::GetOrCreateEntity(uint64_t entityAddress, uint32_t entityId) noexcept {
    // Fast path: try shared lock first for existing entries
    {
        std::shared_lock lock(cacheMutex_);
        auto it = entityCache_.find(entityAddress);
        if (it != entityCache_.end()) {
            EntityCacheEntry& entry = it->second;
            // Check TTL expiration
            if (std::chrono::steady_clock::now() < entry.expiresAt) {
                entry.frameStamp = currentFrame_.load(std::memory_order_acquire);
                entry.valid = true;
                
                // Update LRU (need unique lock for list modification)
                lock.unlock();
                std::unique_lock uLock(cacheMutex_);
                // Re-verify entry still exists after lock upgrade
                auto it2 = entityCache_.find(entityAddress);
                if (it2 != entityCache_.end()) {
                    UpdateLRULocked(it2->second);
                }
                {
                    std::lock_guard statsLock(statsMutex_);
                    cacheHits_++;
                }
                return &entry;
            }
            // Expired - fall through to unique lock path for removal/recreation
        }
    }
    
    // Unique lock path: create new or replace expired
    std::unique_lock lock(cacheMutex_);
    
    // Re-check after lock upgrade
    auto it = entityCache_.find(entityAddress);
    if (it != entityCache_.end()) {
        EntityCacheEntry& entry = it->second;
        if (std::chrono::steady_clock::now() < entry.expiresAt) {
            entry.frameStamp = currentFrame_.load(std::memory_order_acquire);
            entry.valid = true;
            UpdateLRULocked(entry);
            {
                std::lock_guard statsLock(statsMutex_);
                cacheHits_++;
            }
            return &entry;
        }
        // Expired - remove old entry
        lruList_.erase(entry.lruIt);
        entityCache_.erase(it);
    }
    
    // Evict LRU if at capacity
    EvictLRUIfNeededLocked();
    
    // Create new entry
    auto& entry = entityCache_[entityAddress];
    entry.entityAddress = entityAddress;
    entry.entityId = entityId;
    entry.frameStamp = currentFrame_.load(std::memory_order_acquire);
    entry.valid = true;
    entry.expiresAt = std::chrono::steady_clock::now() + entityTTL_;
    
    // Add to LRU list (back = most recent)
    lruList_.push_back(entityAddress);
    entry.lruIt = std::prev(lruList_.end());
    
    {
        std::lock_guard statsLock(statsMutex_);
        cacheMisses_++;
    }
    
    return &entry;
}

template<typename Fn>
bool FrameCache::AccessEntity(uint64_t entityAddress, Fn&& fn) noexcept {
    std::shared_lock lock(cacheMutex_);
    auto it = entityCache_.find(entityAddress);
    if (it != entityCache_.end()) {
        // Check TTL
        if (std::chrono::steady_clock::now() < it->second.expiresAt) {
            it->second.frameStamp = currentFrame_.load(std::memory_order_acquire);
            it->second.valid = true;
            fn(&it->second);
            {
                std::lock_guard statsLock(statsMutex_);
                cacheHits_++;
            }
            return true;
        }
    }
    {
        std::lock_guard statsLock(statsMutex_);
        cacheMisses_++;
    }
    return false;
}

// Explicit instantiation for common use cases
template bool FrameCache::AccessEntity(uint64_t, std::function<void(EntityCacheEntry*)>) noexcept;

void FrameCache::InvalidateEntity(uint64_t entityAddress) noexcept {
    std::unique_lock lock(cacheMutex_);
    auto it = entityCache_.find(entityAddress);
    if (it != entityCache_.end()) {
        lruList_.erase(it->second.lruIt);
        entityCache_.erase(it);
    }
}

BoneMatrix* FrameCache::GetBoneMatrix(EntityCacheEntry* entity, uint32_t boneId) noexcept {
    if (!entity || !entity->valid) return nullptr;
    
    auto it = entity->bones.find(boneId);
    if (it != entity->bones.end()) {
        if (it->second.frameStamp == currentFrame_.load(std::memory_order_acquire)) {
            return &it->second;
        }
        // Stale entry
        entity->bones.erase(it);
    }
    return nullptr;
}

void FrameCache::SetBoneMatrix(EntityCacheEntry* entity, uint32_t boneId, const std::array<float, 16>& matrix) noexcept {
    if (!entity || !entity->valid) return;
    
    if (entity->bones.size() >= maxBonesPerEntity_) {
        // Remove oldest bone (simple FIFO)
        if (!entity->bones.empty()) {
            entity->bones.erase(entity->bones.begin());
        }
    }
    
    BoneMatrix& bm = entity->bones[boneId];
    bm.matrix = matrix;
    bm.frameStamp = currentFrame_.load(std::memory_order_acquire);
    bm.valid = true;
}

WorldToScreenCache* FrameCache::GetW2S(EntityCacheEntry* entity) noexcept {
    if (!entity || !entity->valid) return nullptr;
    
    if (entity->w2s.frameStamp == currentFrame_.load(std::memory_order_acquire) && entity->w2s.valid) {
        return &entity->w2s;
    }
    return nullptr;
}

void FrameCache::SetW2S(EntityCacheEntry* entity, float x, float y, float z, bool onScreen) noexcept {
    if (!entity || !entity->valid) return;
    
    entity->w2s.screenX = x;
    entity->w2s.screenY = y;
    entity->w2s.screenZ = z;
    entity->w2s.onScreen = onScreen;
    entity->w2s.frameStamp = currentFrame_.load(std::memory_order_acquire);
    entity->w2s.valid = true;
}

void FrameCache::SetEntityDistance(EntityCacheEntry* entity, float distance) noexcept {
    if (entity) entity->distance = distance;
}

float FrameCache::GetEntityDistance(EntityCacheEntry* entity) const noexcept {
    return entity ? entity->distance : 0.0f;
}

void FrameCache::SetEntityVisibility(EntityCacheEntry* entity, bool visible) noexcept {
    if (entity) entity->isVisible = visible;
}

bool FrameCache::GetEntityVisibility(EntityCacheEntry* entity) const noexcept {
    return entity ? entity->isVisible : false;
}

FrameCache::Stats FrameCache::GetStats() const noexcept {
    std::shared_lock lock(cacheMutex_);
    std::lock_guard statsLock(statsMutex_);
    
    Stats stats;
    stats.totalEntities = entityCache_.size();
    
    for (const auto& [_, entry] : entityCache_) {
        stats.cachedBones += entry.bones.size();
        if (entry.w2s.valid) stats.cachedW2S++;
    }
    
    stats.cacheHits = cacheHits_;
    stats.cacheMisses = cacheMisses_;
    size_t total = cacheHits_ + cacheMisses_;
    stats.hitRate = total > 0 ? static_cast<double>(cacheHits_) / total : 0.0;
    
    return stats;
}

void FrameCache::ResetStats() noexcept {
    std::lock_guard lock(statsMutex_);
    cacheHits_ = 0;
    cacheMisses_ = 0;
}

void FrameCache::RemoveExpiredEntriesLocked() noexcept {
    auto now = std::chrono::steady_clock::now();
    auto it = entityCache_.begin();
    while (it != entityCache_.end()) {
        if (now >= it->second.expiresAt) {
            lruList_.erase(it->second.lruIt);
            it = entityCache_.erase(it);
        } else {
            ++it;
        }
    }
}

void FrameCache::EvictLRUIfNeededLocked() noexcept {
    if (entityCache_.size() >= maxEntities_ && !lruList_.empty()) {
        uint64_t lru = lruList_.front();
        lruList_.pop_front();
        entityCache_.erase(lru);
    }
}

void FrameCache::UpdateLRULocked(EntityCacheEntry& entry) noexcept {
    // Move to back (most recent)
    lruList_.erase(entry.lruIt);
    lruList_.push_back(entry.entityAddress);
    entry.lruIt = std::prev(lruList_.end());
}

} // namespace OmniGhost::Platform