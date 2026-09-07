#pragma once

#include <memory_resource>
#include <memory>
#include <mutex>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <string>

namespace OmniGhost::Platform {

// ============================================================
// PMR Allocators for Hot Paths
// ============================================================

// ============================================================
// Monotonic Buffer Resource - Fast bump-pointer allocator
// ============================================================

class MonotonicBufferResource final : public std::pmr::memory_resource {
public:
    explicit MonotonicBufferResource(size_t bufferSize = 1024 * 1024)  // 1MB default
        : buffer_(std::make_unique<std::byte[]>(bufferSize))
        , capacity_(bufferSize)
        , offset_(0) {}
    
    MonotonicBufferResource(std::byte* externalBuffer, size_t size)
        : buffer_()  // empty unique_ptr — external buffer is not owned
        , externalBuffer_(externalBuffer)
        , capacity_(size)
        , offset_(0)
        , ownsBuffer_(false) {}
    
    ~MonotonicBufferResource() override = default;
    
    void Reset() noexcept {
        std::lock_guard lock(mutex_);
        offset_ = 0;
    }
    
    [[nodiscard]] size_t Used() const noexcept {
        return offset_;
    }
    
    [[nodiscard]] size_t Capacity() const noexcept {
        return capacity_;
    }
    
    [[nodiscard]] size_t Remaining() const noexcept {
        return capacity_ > offset_ ? capacity_ - offset_ : 0;
    }
    
    [[nodiscard]] double Utilization() const noexcept {
        return capacity_ > 0 ? static_cast<double>(offset_) / capacity_ : 0.0;
    }

protected:
    void* do_allocate(size_t bytes, size_t alignment) override {
        std::lock_guard lock(mutex_);
        
        // Align offset
        size_t alignedOffset = (offset_ + alignment - 1) & ~(alignment - 1);
        
        if (alignedOffset + bytes > capacity_) {
            // Buffer exhausted - could grow or throw
            throw std::bad_alloc();
        }
        
        void* ptr = externalBuffer_ ? externalBuffer_ + alignedOffset : buffer_.get() + alignedOffset;
        offset_ = alignedOffset + bytes;
        return ptr;
    }
    
    void do_deallocate(void* p, size_t bytes, size_t alignment) override {
        // Monotonic - no individual deallocation
        // Memory is reclaimed on Reset()
        (void)p; (void)bytes; (void)alignment;
    }
    
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

private:
    std::unique_ptr<std::byte[]> buffer_;
    std::byte* externalBuffer_ = nullptr;
    const size_t capacity_;
    size_t offset_ = 0;
    bool ownsBuffer_ = true;
    mutable std::mutex mutex_;
};

// ============================================================
// Pool Resource - Fixed-size block allocator
// ============================================================

template <size_t BlockSize, size_t BlocksPerChunk = 256>
class PoolResource final : public std::pmr::memory_resource {
    static_assert(BlockSize >= sizeof(void*), "BlockSize must be at least pointer size");
    static_assert((BlockSize & (BlockSize - 1)) == 0, "BlockSize must be power of 2");
    
public:
    PoolResource(size_t initialChunks = 1)
        : blockSize_(BlockSize)
        , blocksPerChunk_(BlocksPerChunk) {
        AddChunk(initialChunks);
    }
    
    ~PoolResource() override {
        for (auto chunk : chunks_) {
            ::operator delete(chunk);
        }
    }
    
    [[nodiscard]] size_t TotalBlocks() const noexcept {
        return chunks_.size() * BlocksPerChunk;
    }
    
    [[nodiscard]] size_t FreeBlocks() const noexcept {
        return freeList_.size();
    }
    
    [[nodiscard]] double Utilization() const noexcept {
        size_t total = TotalBlocks();
        return total > 0 ? 1.0 - static_cast<double>(freeList_.size()) / total : 0.0;
    }

protected:
    void* do_allocate(size_t bytes, size_t alignment) override {
        (void)alignment;
        if (bytes > BlockSize || alignment > BlockSize) {
            throw std::bad_alloc();
        }
        
        std::lock_guard lock(mutex_);
        if (freeList_.empty()) {
            AddChunk(1);
        }
        
        void* block = freeList_.back();
        freeList_.pop_back();
        return block;
    }
    
    void do_deallocate(void* p, size_t bytes, size_t alignment) override {
        (void)alignment;
        if (!p) return;
        if (bytes > BlockSize) return;
        
        std::lock_guard lock(mutex_);
        freeList_.push_back(p);
    }
    
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

private:
    void AddChunk(size_t count = 1) {
        for (size_t c = 0; c < count; ++c) {
            void* chunk = ::operator new(BlockSize * BlocksPerChunk, std::align_val_t(BlockSize));
            chunks_.push_back(static_cast<std::byte*>(chunk));
            
            // Link blocks into free list
            for (size_t i = 0; i < BlocksPerChunk; ++i) {
                void* block = chunks_.back() + i * BlockSize;
                freeList_.push_back(block);
            }
        }
    }
    
    const size_t blockSize_;
    const size_t blocksPerChunk_;
    std::vector<std::byte*> chunks_;
    std::vector<void*> freeList_;
    mutable std::mutex mutex_;
};

// ============================================================
// Thread-Local PMR Allocator
// ============================================================

class ThreadLocalAllocator {
public:
    using MonotonicResource = MonotonicBufferResource;
    using PoolResource = PoolResource<256, 256>;
    
    ThreadLocalAllocator(size_t monotonicSize = 64 * 1024,  // 64KB per thread
                         size_t poolSize = 128 * 1024)      // 128KB pool per thread
        : monotonic_(monotonicSize)
        , pool_(poolSize / 256) {}  // 256-byte blocks
    
    [[nodiscard]] std::pmr::memory_resource* Monotonic() noexcept {
        return &monotonic_;
    }
    
    [[nodiscard]] std::pmr::memory_resource* Pool() noexcept {
        return &pool_;
    }
    
    void Reset() noexcept {
        monotonic_.Reset();
    }
    
    [[nodiscard]] std::pmr::polymorphic_allocator<std::byte> GetMonotonicAllocator() noexcept {
        return std::pmr::polymorphic_allocator<std::byte>(&monotonic_);
    }
    
    [[nodiscard]] std::pmr::polymorphic_allocator<std::byte> GetPoolAllocator() noexcept {
        return std::pmr::polymorphic_allocator<std::byte>(&pool_);
    }
    
private:
    MonotonicResource monotonic_;
    PoolResource pool_;
};

// Thread-local instance
inline ThreadLocalAllocator& GetThreadLocalAllocator() noexcept {
    thread_local ThreadLocalAllocator tlsAllocator;
    return tlsAllocator;
}

// Convenience functions
inline std::pmr::memory_resource* GetThreadMonotonicResource() noexcept {
    return GetThreadLocalAllocator().Monotonic();
}

inline std::pmr::memory_resource* GetThreadPoolResource() noexcept {
    return GetThreadLocalAllocator().Pool();
}

inline std::pmr::polymorphic_allocator<std::byte> GetThreadMonotonicAllocator() noexcept {
    return GetThreadLocalAllocator().GetMonotonicAllocator();
}

inline std::pmr::polymorphic_allocator<std::byte> GetThreadPoolAllocator() noexcept {
    return GetThreadLocalAllocator().GetPoolAllocator();
}

inline void ResetThreadAllocators() noexcept {
    GetThreadLocalAllocator().Reset();
}

// ============================================================
// PMR-Aware Containers
// ============================================================

template <typename T>
using PmrVector = std::pmr::vector<T>;

template <typename T>
using PmrDeque = std::pmr::deque<T>;

template <typename K, typename V>
using PmrUnorderedMap = std::unordered_map<
    K, V, 
    std::hash<K>, 
    std::equal_to<K>, 
    std::pmr::polymorphic_allocator<std::pair<const K, V>>
>;

template <typename T>
using PmrUnorderedSet = std::unordered_set<
    T, 
    std::hash<T>, 
    std::equal_to<T>, 
    std::pmr::polymorphic_allocator<T>
>;

template <typename T>
using PmrString = std::basic_string<
    char, 
    std::char_traits<char>, 
    std::pmr::polymorphic_allocator<char>
>;

// Convenience aliases with thread-local monotonic allocator
template <typename T>
using ThreadVector = std::vector<T, std::pmr::polymorphic_allocator<T>>;

template <typename T>
ThreadVector<T> MakeThreadVector() noexcept {
    return ThreadVector<T>(GetThreadMonotonicAllocator());
}

template <typename T>
ThreadVector<T> MakeThreadVector(size_t count) noexcept {
    return ThreadVector<T>(count, GetThreadMonotonicAllocator());
}

template <typename T>
ThreadVector<T> MakeThreadVector(size_t count, const T& value) noexcept {
    return ThreadVector<T>(count, value, GetThreadMonotonicAllocator());
}

template <typename K, typename V>
using ThreadUnorderedMap = PmrUnorderedMap<K, V>;

template <typename K, typename V>
ThreadUnorderedMap<K, V> MakeThreadUnorderedMap() noexcept {
    return ThreadUnorderedMap<K, V>(GetThreadMonotonicAllocator());
}

template <typename T>
using ThreadUnorderedSet = PmrUnorderedSet<T>;

template <typename T>
ThreadUnorderedSet<T> MakeThreadUnorderedSet() noexcept {
    return ThreadUnorderedSet<T>(GetThreadMonotonicAllocator());
}

using ThreadString = PmrString<char>;

inline ThreadString MakeThreadString() noexcept {
    return ThreadString(GetThreadMonotonicAllocator());
}

inline ThreadString MakeThreadString(std::string_view sv) noexcept {
    return ThreadString(sv, GetThreadMonotonicAllocator());
}

// ============================================================
// Global PMR Resources (for non-thread-local allocations)
// ============================================================

class GlobalPmrResources {
public:
    static GlobalPmrResources& Instance() noexcept {
        static GlobalPmrResources instance;
        return instance;
    }
    
    void Initialize(size_t monotonicSize = 16 * 1024 * 1024,  // 16MB
                   size_t poolSize = 4 * 1024 * 1024) {       // 4MB
        globalMonotonic_ = std::make_unique<MonotonicBufferResource>(monotonicSize);
        globalPool_ = std::make_unique<PoolResource<512, 512>>(poolSize / 512);
    }
    
    void Shutdown() noexcept {
        globalMonotonic_.reset();
        globalPool_.reset();
    }
    
    void Reset() noexcept {
        if (globalMonotonic_) globalMonotonic_->Reset();
    }
    
    [[nodiscard]] std::pmr::memory_resource* Monotonic() noexcept {
        return globalMonotonic_.get();
    }
    
    [[nodiscard]] std::pmr::memory_resource* Pool() noexcept {
        return globalPool_.get();
    }
    
    [[nodiscard]] std::pmr::polymorphic_allocator<std::byte> GetMonotonicAllocator() noexcept {
        return std::pmr::polymorphic_allocator<std::byte>(globalMonotonic_.get());
    }
    
    [[nodiscard]] std::pmr::polymorphic_allocator<std::byte> GetPoolAllocator() noexcept {
        return std::pmr::polymorphic_allocator<std::byte>(globalPool_.get());
    }
    
private:
    std::unique_ptr<MonotonicBufferResource> globalMonotonic_;
    std::unique_ptr<PoolResource<512, 512>> globalPool_;
};

// ============================================================
// PMR-Aware Object Pool
// ============================================================

template <typename T, size_t PoolSize = 256>
class PmrObjectPool {
public:
    explicit PmrObjectPool(std::pmr::memory_resource* resource = GetThreadPoolResource())
        : resource_(resource) {
        pool_.reserve(PoolSize);
        for (size_t i = 0; i < PoolSize; ++i) {
            pool_.push_back(AllocateNew());
        }
    }
    
    template <typename... Args>
    T* Acquire(Args&&... args) {
        std::lock_guard lock(mutex_);
        if (pool_.empty()) {
            return AllocateNew(std::forward<Args>(args)...);
        }
        T* obj = pool_.back();
        pool_.pop_back();
        std::construct_at(obj, std::forward<Args>(args)...);
        return obj;
    }
    
    void Release(T* obj) {
        if (!obj) return;
        std::lock_guard lock(mutex_);
        if (pool_.size() < PoolSize) {
            std::destroy_at(obj);
            pool_.push_back(obj);
        } else {
            Deallocate(obj);
        }
    }
    
    [[nodiscard]] size_t Available() const noexcept {
        std::lock_guard lock(mutex_);
        return pool_.size();
    }
    
    [[nodiscard]] size_t Capacity() const noexcept { return PoolSize; }

private:
    T* AllocateNew() {
        void* mem = resource_->allocate(sizeof(T), alignof(T));
        return ::new (mem) T();
    }
    
    template <typename... Args>
    T* AllocateNew(Args&&... args) {
        void* mem = resource_->allocate(sizeof(T), alignof(T));
        return ::new (mem) T(std::forward<Args>(args)...);
    }
    
    void Deallocate(T* obj) {
        std::destroy_at(obj);
        resource_->deallocate(obj, sizeof(T), alignof(T));
    }
    
    std::pmr::memory_resource* resource_;
    std::vector<T*> pool_;
    mutable std::mutex mutex_;
};

// Scoped pointer for PMR object pool
template <typename T>
class ScopedPmrPtr {
public:
    using Deleter = void(*)(T*);
    
    ScopedPmrPtr(T* ptr, Deleter deleter) : ptr_(ptr), deleter_(deleter) {}
    ~ScopedPmrPtr() { if (ptr_) deleter_(ptr_); }
    
    ScopedPmrPtr(const ScopedPmrPtr&) = delete;
    ScopedPmrPtr& operator=(const ScopedPmrPtr&) = delete;
    
    ScopedPmrPtr(ScopedPmrPtr&& other) noexcept : ptr_(other.ptr_), deleter_(other.deleter_) {
        other.ptr_ = nullptr;
    }
    
    ScopedPmrPtr& operator=(ScopedPmrPtr&& other) noexcept {
        if (this != &other) {
            if (ptr_) deleter_(ptr_);
            ptr_ = other.ptr_;
            deleter_ = other.deleter_;
            other.ptr_ = nullptr;
        }
        return *this;
    }
    
    T* get() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }
    
private:
    T* ptr_ = nullptr;
    void (*deleter_)(T*) = nullptr;
};

} // namespace OmniGhost::Platform