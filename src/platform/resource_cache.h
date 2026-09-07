#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace OmniGhost::Platform {

// ============================================================
// Lazy Resource Loading System
// ============================================================

enum class ResourceType : uint8_t {
    Texture,
    Font,
    Shader,
    Mesh,
    Audio,
    Json,
    Binary,
    Count
};

enum class LoadPriority : uint8_t {
    Low = 0,
    Normal = 1,
    High = 2,
    Critical = 3
};

struct ResourceHandle {
    uint64_t id = 0;
    bool valid = false;
    
    [[nodiscard]] explicit operator bool() const noexcept { return valid; }
    [[nodiscard]] bool operator==(const ResourceHandle& other) const noexcept { return id == other.id; }
};

struct ResourceInfo {
    std::string name;
    std::string path;
    ResourceType type = ResourceType::Binary;
    size_t size = 0;
    bool loaded = false;
    LoadPriority priority = LoadPriority::Normal;
    std::string gameId;  // Empty = universal
    uint64_t lastAccess = 0;
    size_t accessCount = 0;
};

struct LoadRequest {
    ResourceHandle handle;
    LoadPriority priority;
    std::function<void(std::shared_ptr<void>)> callback;
    std::chrono::steady_clock::time_point requestedAt;
};

// Forward declare
class ResourceCache;

// Resource base class
class IResource {
public:
    virtual ~IResource() = default;
    virtual ResourceType GetType() const noexcept = 0;
    virtual size_t GetSize() const noexcept = 0;
    virtual bool IsLoaded() const noexcept = 0;
    virtual void Unload() noexcept = 0;
};

template <typename T>
class TypedResource : public IResource {
public:
    explicit TypedResource(T&& data) : data_(std::move(data)) {}
    explicit TypedResource(const T& data) : data_(data) {}
    
    [[nodiscard]] ResourceType GetType() const noexcept override { return ResourceType::Binary; }
    [[nodiscard]] size_t GetSize() const noexcept override { return sizeof(T); }
    [[nodiscard]] bool IsLoaded() const noexcept override { return true; }
    void Unload() noexcept override { data_ = T{}; }
    
    [[nodiscard]] T& Get() noexcept { return data_; }
    [[nodiscard]] const T& Get() const noexcept { return data_; }
    
private:
    T data_;
};

// ============================================================
// Resource Cache with Lazy Loading
// ============================================================

class ResourceCache {
public:
    ResourceCache() = default;
    ~ResourceCache() { Shutdown(); }
    
    void Initialize(size_t maxMemoryBytes = 100 * 1024 * 1024);  // 100MB default
    void Shutdown();
    
    // Register a resource (doesn't load yet)
    ResourceHandle Register(const std::string& name, const std::string& path, 
                           ResourceType type, LoadPriority priority = LoadPriority::Normal,
                           std::string_view gameId = {});
    
    // Load resource synchronously
    template <typename T>
    std::shared_ptr<T> Load(ResourceHandle handle) {
        std::lock_guard lock(mutex_);
        auto it = resources_.find(handle.id);
        if (it == resources_.end()) return nullptr;
        
        auto& entry = it->second;
        if (entry.info.loaded && entry.resource) {
            entry.info.lastAccess = GetCurrentTime();
            entry.info.accessCount++;
            return std::static_pointer_cast<T>(entry.resource);
        }
        
        // Load from file
        auto resource = LoadFromFile<T>(entry.info.path);
        if (resource) {
            entry.resource = std::move(resource);
            entry.info.loaded = true;
            currentMemoryUsage_ += entry.info.size;
            entry.info.lastAccess = GetCurrentTime();
            entry.info.accessCount++;
            return std::static_pointer_cast<T>(entry.resource);
        }
        
        return nullptr;
    }
    
    // Load resource asynchronously
    void LoadAsync(ResourceHandle handle, LoadPriority priority = LoadPriority::Normal,
                   std::function<void(std::shared_ptr<void>)> callback = {});
    
    // Get resource if already loaded (non-blocking)
    template <typename T>
    std::shared_ptr<T> TryGet(ResourceHandle handle) noexcept {
        std::lock_guard lock(mutex_);
        auto it = resources_.find(handle.id);
        if (it != resources_.end() && it->second.info.loaded && it->second.resource) {
            it->second.info.lastAccess = GetCurrentTime();
            it->second.info.accessCount++;
            return std::static_pointer_cast<T>(it->second.resource);
        }
        return nullptr;
    }
    
    // Unload resource to free memory
    void Unload(ResourceHandle handle) noexcept;
    
    // Unload all resources for a specific game
    void UnloadGameResources(std::string_view gameId) noexcept;
    
    // Preload resources for a game (called when entering game)
    void PreloadGameResources(std::string_view gameId, LoadPriority minPriority = LoadPriority::Normal) {
        std::vector<ResourceHandle> toLoad;
        {
            std::lock_guard lock(mutex_);
            for (auto& [id, entry] : resources_) {
                if (entry.info.gameId == gameId && !entry.info.loaded && 
                    static_cast<uint8_t>(entry.info.priority) >= static_cast<uint8_t>(minPriority)) {
                    toLoad.push_back({id, true});
                }
            }
        }
        
        for (auto& handle : toLoad) {
            LoadAsync(handle, LoadPriority::High);
        }
    }
    
    // Memory management
    void SetMemoryLimit(size_t bytes) noexcept { maxMemoryBytes_ = bytes; }
    [[nodiscard]] size_t GetMemoryUsage() const noexcept { return currentMemoryUsage_.load(); }
    [[nodiscard]] size_t GetMemoryLimit() const noexcept { return maxMemoryBytes_; }
    [[nodiscard]] float GetMemoryUsageRatio() const noexcept { 
        return maxMemoryBytes_ > 0 ? float(currentMemoryUsage_.load()) / maxMemoryBytes_ : 0.0f; 
    }
    
    // Evict least recently used resources
    void EvictLRU(size_t targetBytes) noexcept;
    
    // Resource info
    [[nodiscard]] const ResourceInfo* GetInfo(ResourceHandle handle) const noexcept {
        std::lock_guard lock(mutex_);
        auto it = resources_.find(handle.id);
        return it != resources_.end() ? &it->second.info : nullptr;
    }
    
    [[nodiscard]] std::vector<ResourceInfo> GetAllResources() const noexcept {
        std::lock_guard lock(mutex_);
        std::vector<ResourceInfo> result;
        result.reserve(resources_.size());
        for (const auto& [_, entry] : resources_) {
            result.push_back(entry.info);
        }
        return result;
    }
    
    [[nodiscard]] std::vector<ResourceInfo> GetGameResources(std::string_view gameId) const noexcept {
        std::lock_guard lock(mutex_);
        std::vector<ResourceInfo> result;
        for (const auto& [_, entry] : resources_) {
            if (entry.info.gameId == gameId) result.push_back(entry.info);
        }
        return result;
    }
    
    // Background loader thread
    void StartLoaderThread();
    void StopLoaderThread();
    
    // Statistics
    struct Stats {
        size_t totalResources = 0;
        size_t loadedResources = 0;
        size_t memoryUsage = 0;
        size_t loadRequestsPending = 0;
        double hitRate = 0.0;
    };
    
    [[nodiscard]] Stats GetStats() const noexcept;
    
    // Global accessor
    static ResourceCache& Instance() noexcept {
        static ResourceCache instance;
        return instance;
    }

private:
    struct ResourceEntry {
        ResourceInfo info;
        std::shared_ptr<void> resource;  // Type-erased
    };
    
    // File loading
    template <typename T>
    std::shared_ptr<T> LoadFromFile(const std::string& path) {
        // Implement based on resource type
        // For now, return nullptr - implement based on actual needs
        return nullptr;
    }
    
    // Background loader thread
    void LoaderThread();
    
    // Time
    [[nodiscard]] static uint64_t GetCurrentTime() noexcept {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    
    // Eviction
    void EvictOneLRU() noexcept;
    
    // Mutexes
    mutable std::mutex mutex_;
    std::mutex requestMutex_;
    std::condition_variable requestCond_;
    
    // Data
    std::unordered_map<uint64_t, ResourceEntry> resources_;
    std::vector<LoadRequest> loadRequests_;
    std::atomic<uint64_t> nextResourceId_{1};
    
    // Memory management
    std::atomic<size_t> currentMemoryUsage_{0};
    size_t maxMemoryBytes_ = 100 * 1024 * 1024;
    
    // Loader thread
    std::thread loaderThread_;
    std::atomic<bool> loaderRunning_{false};
    
    // Stats
    std::atomic<uint64_t> totalRequests_{0};
    std::atomic<uint64_t> cacheHits_{0};
    std::atomic<uint64_t> cacheMisses_{0};
};

// Convenience functions
inline ResourceHandle RegisterResource(const std::string& name, const std::string& path, 
                                       ResourceType type, LoadPriority priority = LoadPriority::Normal,
                                       std::string_view gameId = {}) {
    return ResourceCache::Instance().Register(name, path, type, priority, gameId);
}

template <typename T>
std::shared_ptr<T> LoadResource(ResourceHandle handle) {
    return ResourceCache::Instance().Load<T>(handle);
}

template <typename T>
std::shared_ptr<T> TryGetResource(ResourceHandle handle) noexcept {
    return ResourceCache::Instance().TryGet<T>(handle);
}

inline void LoadResourceAsync(ResourceHandle handle, LoadPriority priority = LoadPriority::Normal,
                             std::function<void(std::shared_ptr<void>)> callback = {}) {
    ResourceCache::Instance().LoadAsync(handle, priority, std::move(callback));
}

inline void UnloadResource(ResourceHandle handle) noexcept {
    ResourceCache::Instance().Unload(handle);
}

inline void PreloadGameResources(std::string_view gameId, LoadPriority minPriority = LoadPriority::Normal) {
    ResourceCache::Instance().PreloadGameResources(gameId, minPriority);
}

inline void UnloadGameResources(std::string_view gameId) noexcept {
    ResourceCache::Instance().UnloadGameResources(gameId);
}

} // namespace OmniGhost::Platform