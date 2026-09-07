#include "frame_cache.h"
#include <algorithm>
#include <chrono>

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
    
    // Periodic cleanup (every 60 frames)
    if (frameNumber % 60 == 0) {
        CleanupOldEntries();
    }
}

void FrameCache::EndFrame() noexcept {
    // Frame ends, next BeginFrame will increment
}

EntityCacheEntry* FrameCache::GetOrCreateEntity(uint64_t entityAddress, uint32_t entityId) noexcept {
    std::lock_guard lock(cacheMutex_);
    
    auto it = entityCache_.find(entityAddress);
    if (it != entityCache_.end()) {
        // Update existing
        it->second.frameStamp = currentFrame_.load(std::memory_order_acquire);
        it->second.valid = true;
        
        // Update LRU
        auto lruIt = std::find(lruList_.begin(), lruList_.end(), entityAddress);
        if (lruIt != lruList_.end()) {
            lruList_.erase(lruIt);
        }
        lruList_.push_back(entityAddress);
        
        {
            std::lock_guard statsLock(statsMutex_);
            cacheHits_++;
        }
        return &it->second;
    }
    
    // Create new entry
    if (entityCache_.size() >= maxEntities_) {
        // Remove LRU entry
        if (!lruList_.empty()) {
            uint64_t lru = lruList_.front();
            lruList_.erase(lruList_.begin());
            entityCache_.erase(lru);
        }
    }
    
    auto& entry = entityCache_[entityAddress];
    entry.entityAddress = entityAddress;
    entry.entityId = entityId;
    entry.frameStamp = currentFrame_.load(std::memory_order_acquire);
    entry.valid = true;
    lruList_.push_back(entityAddress);
    
    {
        std::lock_guard statsLock(statsMutex_);
        cacheMisses_++;
    }
    
    return &entry;
}

EntityCacheEntry* FrameCache::GetEntity(uint64_t entityAddress) noexcept {
    std::lock_guard lock(cacheMutex_);
    auto it = entityCache_.find(entityAddress);
    if (it != entityCache_.end()) {
        {
            std::lock_guard statsLock(statsMutex_);
            cacheHits_++;
        }
        return &it->second;
    }
    {
        std::lock_guard statsLock(statsMutex_);
        cacheMisses_++;
    }
    return nullptr;
}

void FrameCache::InvalidateEntity(uint64_t entityAddress) noexcept {
    std::lock_guard lock(cacheMutex_);
    entityCache_.erase(entityAddress);
    auto it = std::find(lruList_.begin(), lruList_.end(), entityAddress);
    if (it != lruList_.end()) {
        lruList_.erase(it);
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
    std::lock_guard lock(cacheMutex_);
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

void FrameCache::CleanupOldEntries() noexcept {
    std::lock_guard lock(cacheMutex_);
    uint64_t current = currentFrame_.load(std::memory_order_acquire);
    
    // Remove entries older than 2 frames
    auto it = entityCache_.begin();
    while (it != entityCache_.end()) {
        if (current - it->second.frameStamp > 2) {
            auto lruIt = std::find(lruList_.begin(), lruList_.end(), it->first);
            if (lruIt != lruList_.end()) lruList_.erase(lruIt);
            it = entityCache_.erase(it);
        } else {
            ++it;
        }
    }
    
    // Trim LRU list if needed
    if (lruList_.size() > maxEntities_) {
        lruList_.erase(lruList_.begin(), lruList_.begin() + (lruList_.size() - maxEntities_));
    }
}

} // namespace OmniGhost::Platform