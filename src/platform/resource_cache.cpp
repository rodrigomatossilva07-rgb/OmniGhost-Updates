#include "resource_cache.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>

namespace OmniGhost::Platform {

void ResourceCache::Initialize(size_t maxMemoryBytes) {
    maxMemoryBytes_ = maxMemoryBytes;
    StartLoaderThread();
}

void ResourceCache::Shutdown() {
    StopLoaderThread();
    
    std::lock_guard lock(mutex_);
    for (auto& [_, entry] : resources_) {
        if (entry.resource) {
            entry.resource.reset();
        }
    }
    resources_.clear();
    currentMemoryUsage_.store(0);
}

ResourceHandle ResourceCache::Register(const std::string& name, const std::string& path, 
                                       ResourceType type, LoadPriority priority,
                                       std::string_view gameId) {
    std::lock_guard lock(mutex_);
    
    uint64_t id = nextResourceId_.fetch_add(1);
    
    ResourceEntry entry;
    entry.info.name = name;
    entry.info.path = path;
    entry.info.type = type;
    entry.info.priority = priority;
    entry.info.gameId = gameId;
    entry.info.size = 0;  // Will be updated on load
    entry.info.loaded = false;
    entry.info.lastAccess = GetCurrentTime();
    entry.info.accessCount = 0;
    
    resources_[id] = std::move(entry);
    return {id, true};
}

void ResourceCache::Unload(ResourceHandle handle) noexcept {
    std::lock_guard lock(mutex_);
    auto it = resources_.find(handle.id);
    if (it != resources_.end() && it->second.info.loaded) {
        if (it->second.resource) {
            currentMemoryUsage_ -= it->second.info.size;
            it->second.resource.reset();
        }
        it->second.info.loaded = false;
    }
}

void ResourceCache::UnloadGameResources(std::string_view gameId) noexcept {
    std::lock_guard lock(mutex_);
    for (auto& [id, entry] : resources_) {
        if (entry.info.gameId == gameId && entry.info.loaded) {
            if (entry.resource) {
                currentMemoryUsage_ -= entry.info.size;
                entry.resource.reset();
            }
            entry.info.loaded = false;
        }
    }
}

void ResourceCache::EvictLRU(size_t targetBytes) noexcept {
    while (currentMemoryUsage_.load() > targetBytes) {
        EvictOneLRU();
        if (currentMemoryUsage_.load() == 0) break;
    }
}

void ResourceCache::EvictOneLRU() noexcept {
    std::lock_guard lock(mutex_);
    
    uint64_t oldestTime = UINT64_MAX;
    uint64_t oldestId = 0;
    bool found = false;
    
    for (auto& [id, entry] : resources_) {
        if (entry.info.loaded && entry.resource) {
            if (entry.info.lastAccess < oldestTime) {
                oldestTime = entry.info.lastAccess;
                oldestId = id;
                found = true;
            }
        }
    }
    
    if (found) {
        auto it = resources_.find(oldestId);
        if (it != resources_.end() && it->second.info.loaded) {
            currentMemoryUsage_ -= it->second.info.size;
            it->second.resource.reset();
            it->second.info.loaded = false;
        }
    }
}

void ResourceCache::StartLoaderThread() {
    if (loaderRunning_.exchange(true)) return;
    
    loaderThread_ = std::thread(&ResourceCache::LoaderThread, this);
}

void ResourceCache::StopLoaderThread() {
    if (!loaderRunning_.exchange(false)) return;
    
    {
        std::lock_guard lock(requestMutex_);
        loadRequests_.clear();
    }
    requestCond_.notify_all();
    
    if (loaderThread_.joinable()) {
        loaderThread_.join();
    }
}

void ResourceCache::LoaderThread() {
    while (loaderRunning_.load()) {
        LoadRequest request;
        {
            std::unique_lock lock(requestMutex_);
            requestCond_.wait_for(lock, std::chrono::milliseconds(100), [this] {
                return !loadRequests_.empty() || !loaderRunning_.load();
            });
            
            if (!loaderRunning_.load()) break;
            if (loadRequests_.empty()) continue;
            
            // Sort by priority
            std::sort(loadRequests_.begin(), loadRequests_.end(), 
                [](const LoadRequest& a, const LoadRequest& b) {
                    return a.priority > b.priority;
                });
            
            request = std::move(loadRequests_.back());
            loadRequests_.pop_back();
        }
        
        // Process request
        if (request.callback) {
            auto resource = std::shared_ptr<void>();  // Would load actual resource
            request.callback(resource);
        }
    }
}

void ResourceCache::LoadAsync(ResourceHandle handle, LoadPriority priority,
                              std::function<void(std::shared_ptr<void>)> callback) {
    std::lock_guard lock(requestMutex_);
    loadRequests_.push_back({handle, priority, std::move(callback), 
                            std::chrono::steady_clock::now()});
    requestCond_.notify_one();
}


ResourceCache::Stats ResourceCache::GetStats() const noexcept {
    Stats stats;
    std::lock_guard lock(mutex_);
    
    stats.totalResources = resources_.size();
    stats.memoryUsage = currentMemoryUsage_.load();
    stats.loadRequestsPending = loadRequests_.size();
    
    size_t loaded = 0;
    for (const auto& [_, entry] : resources_) {
        if (entry.info.loaded) loaded++;
    }
    stats.loadedResources = loaded;
    
    uint64_t hits = cacheHits_.load();
    uint64_t misses = cacheMisses_.load();
    uint64_t total = hits + misses;
    stats.hitRate = total > 0 ? double(hits) / total : 0.0;
    
    return stats;
}

} // namespace OmniGhost::Platform