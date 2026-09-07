#pragma once

#include "performance_manager.h"
#include "frame_cache.h"
#include <vector>
#include <array>
#include <memory>

namespace OmniGhost::Platform {

// ============================================================
// Specialized Object Pools for Hot Paths
// ============================================================

// Common vector types used in aim/ESP
using Vec2 = std::array<float, 2>;
using Vec3 = std::array<float, 3>;
using Vec4 = std::array<float, 4>;
using Matrix4x4 = std::array<float, 16>;

// Entity data for ESP
struct EntityEspData {
    uint64_t address = 0;
    uint32_t id = 0;
    Vec3 position{};
    Vec3 headPosition{};
    Vec2 screenPos{};
    float distance = 0.0f;
    float health = 0.0f;
    float maxHealth = 100.0f;
    bool isVisible = false;
    bool isTeam = false;
    bool isDead = false;
    bool isKnocked = false;
    uint32_t weaponId = 0;
    char name[64]{};
    uint32_t color = 0xFFFFFFFF;
    uint64_t frameStamp = 0;
};

// Aim target data
struct AimTarget {
    uint64_t entityAddress = 0;
    Vec3 targetPos{};
    Vec3 aimPos{};
    float distance = 0.0f;
    float fov = 0.0f;
    float smoothFactor = 1.0f;
    uint32_t boneId = 0;
    bool isValid = false;
    uint64_t frameStamp = 0;
};

// ESP render command
struct EspRenderCmd {
    enum class Type : uint8_t {
        Box2D,
        Box3D,
        Skeleton,
        HealthBar,
        ArmorBar,
        Name,
        Distance,
        Weapon,
        Snapline,
        HeadCircle,
        Trail,
        Radar
    } type;
    
    Vec2 pos1{}, pos2{};
    Vec4 color{};
    float thickness = 1.0f;
    float size = 1.0f;
    char text[64]{};
    uint64_t frameStamp = 0;
};

// ============================================================
// Pool Manager - Centralized pool access
// ============================================================

class PoolManager {
public:
    static PoolManager& Instance() noexcept {
        static PoolManager instance;
        return instance;
    }
    
    // Entity ESP data pool
    ObjectPool<EntityEspData, 512>& GetEntityEspPool() noexcept { return entityEspPool_; }
    
    // Aim target pool
    ObjectPool<AimTarget, 256>& GetAimTargetPool() noexcept { return aimTargetPool_; }
    
    // ESP render command pool
    ObjectPool<EspRenderCmd, 1024>& GetEspRenderPool() noexcept { return espRenderPool_; }
    
    // Vector pools
    ObjectPool<Vec2, 1024>& GetVec2Pool() noexcept { return vec2Pool_; }
    ObjectPool<Vec3, 1024>& GetVec3Pool() noexcept { return vec3Pool_; }
    ObjectPool<Vec4, 1024>& GetVec4Pool() noexcept { return vec4Pool_; }
    ObjectPool<Matrix4x4, 256>& GetMatrixPool() noexcept { return matrixPool_; }
    
    // String pool for entity names
    ObjectPool<std::string, 256>& GetStringPool() noexcept { return stringPool_; }
    
    // Vector of entities pool (for batch processing)
    using EntityVec = std::vector<EntityEspData>;
    ObjectPool<EntityVec, 64>& GetEntityVecPool() noexcept { return entityVecPool_; }
    
    // Render command vector pool
    using RenderCmdVec = std::vector<EspRenderCmd>;
    ObjectPool<RenderCmdVec, 64>& GetRenderCmdVecPool() noexcept { return renderCmdVecPool_; }
    
    // Reset all pools (call between frames if needed)
    void ResetAll() noexcept {
        entityEspPool_.Reset();
        aimTargetPool_.Reset();
        espRenderPool_.Reset();
        vec2Pool_.Reset();
        vec3Pool_.Reset();
        vec4Pool_.Reset();
        matrixPool_.Reset();
        stringPool_.Reset();
        entityVecPool_.Reset();
        renderCmdVecPool_.Reset();
    }
    
    // Get pool stats
    struct PoolStats {
        size_t entityEspUsed = 0, entityEspFree = 0;
        size_t aimTargetUsed = 0, aimTargetFree = 0;
        size_t espRenderUsed = 0, espRenderFree = 0;
        size_t vec2Used = 0, vec2Free = 0;
        size_t vec3Used = 0, vec3Free = 0;
        size_t vec4Used = 0, vec4Free = 0;
        size_t matrixUsed = 0, matrixFree = 0;
    };
    
    PoolStats GetStats() const noexcept {
        PoolStats stats;
        stats.entityEspUsed = entityEspPool_.Capacity() - entityEspPool_.Available();
        stats.entityEspFree = entityEspPool_.Available();
        stats.aimTargetUsed = aimTargetPool_.Capacity() - aimTargetPool_.Available();
        stats.aimTargetFree = aimTargetPool_.Available();
        stats.espRenderUsed = espRenderPool_.Capacity() - espRenderPool_.Available();
        stats.espRenderFree = espRenderPool_.Available();
        stats.vec2Used = vec2Pool_.Capacity() - vec2Pool_.Available();
        stats.vec2Free = vec2Pool_.Available();
        stats.vec3Used = vec3Pool_.Capacity() - vec3Pool_.Available();
        stats.vec3Free = vec3Pool_.Available();
        stats.vec4Used = vec4Pool_.Capacity() - vec4Pool_.Available();
        stats.vec4Free = vec4Pool_.Available();
        stats.matrixUsed = matrixPool_.Capacity() - matrixPool_.Available();
        stats.matrixFree = matrixPool_.Available();
        return stats;
    }

private:
    PoolManager() = default;
    
    ObjectPool<EntityEspData, 512> entityEspPool_;
    ObjectPool<AimTarget, 256> aimTargetPool_;
    ObjectPool<EspRenderCmd, 1024> espRenderPool_;
    ObjectPool<Vec2, 1024> vec2Pool_;
    ObjectPool<Vec3, 1024> vec3Pool_;
    ObjectPool<Vec4, 1024> vec4Pool_;
    ObjectPool<Matrix4x4, 256> matrixPool_;
    ObjectPool<std::string, 256> stringPool_;
    ObjectPool<std::vector<EntityEspData>, 64> entityVecPool_;
    ObjectPool<std::vector<EspRenderCmd>, 64> renderCmdVecPool_;
};

// Convenience functions
inline EntityEspData* AllocEntityEsp() noexcept {
    return PoolManager::Instance().GetEntityEspPool().Acquire();
}

inline void FreeEntityEsp(EntityEspData* ptr) noexcept {
    PoolManager::Instance().GetEntityEspPool().Release(ptr);
}

inline AimTarget* AllocAimTarget() noexcept {
    return PoolManager::Instance().GetAimTargetPool().Acquire();
}

inline void FreeAimTarget(AimTarget* ptr) noexcept {
    PoolManager::Instance().GetAimTargetPool().Release(ptr);
}

inline EspRenderCmd* AllocEspRenderCmd() noexcept {
    return PoolManager::Instance().GetEspRenderPool().Acquire();
}

inline void FreeEspRenderCmd(EspRenderCmd* ptr) noexcept {
    PoolManager::Instance().GetEspRenderPool().Release(ptr);
}

inline Vec2* AllocVec2() noexcept {
    return PoolManager::Instance().GetVec2Pool().Acquire();
}

inline void FreeVec2(Vec2* ptr) noexcept {
    PoolManager::Instance().GetVec2Pool().Release(ptr);
}

inline Vec3* AllocVec3() noexcept {
    return PoolManager::Instance().GetVec3Pool().Acquire();
}

inline void FreeVec3(Vec3* ptr) noexcept {
    PoolManager::Instance().GetVec3Pool().Release(ptr);
}

inline Vec4* AllocVec4() noexcept {
    return PoolManager::Instance().GetVec4Pool().Acquire();
}

inline void FreeVec4(Vec4* ptr) noexcept {
    PoolManager::Instance().GetVec4Pool().Release(ptr);
}

inline Matrix4x4* AllocMatrix() noexcept {
    return PoolManager::Instance().GetMatrixPool().Acquire();
}

inline void FreeMatrix(Matrix4x4* ptr) noexcept {
    PoolManager::Instance().GetMatrixPool().Release(ptr);
}

// Scoped allocation helpers (auto-release on scope exit)
template <typename T>
class ScopedPoolPtr {
public:
    using Deleter = void(*)(T*);
    
    ScopedPoolPtr(T* ptr, Deleter deleter) : ptr_(ptr), deleter_(deleter) {}
    ~ScopedPoolPtr() { if (ptr_) deleter_(ptr_); }
    
    ScopedPoolPtr(const ScopedPoolPtr&) = delete;
    ScopedPoolPtr& operator=(const ScopedPoolPtr&) = delete;
    ScopedPoolPtr(ScopedPoolPtr&& other) noexcept : ptr_(other.ptr_), deleter_(other.deleter_) {
        other.ptr_ = nullptr;
    }
    ScopedPoolPtr& operator=(ScopedPoolPtr&& other) noexcept {
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
    Deleter deleter_ = nullptr;
};

inline ScopedPoolPtr<EntityEspData> MakeScopedEntityEsp() noexcept {
    return ScopedPoolPtr<EntityEspData>(AllocEntityEsp(), FreeEntityEsp);
}

inline ScopedPoolPtr<AimTarget> MakeScopedAimTarget() noexcept {
    return ScopedPoolPtr<AimTarget>(AllocAimTarget(), FreeAimTarget);
}

inline ScopedPoolPtr<EspRenderCmd> MakeScopedEspRenderCmd() noexcept {
    return ScopedPoolPtr<EspRenderCmd>(AllocEspRenderCmd(), FreeEspRenderCmd);
}

inline ScopedPoolPtr<Vec2> MakeScopedVec2() noexcept {
    return ScopedPoolPtr<Vec2>(AllocVec2(), FreeVec2);
}

inline ScopedPoolPtr<Vec3> MakeScopedVec3() noexcept {
    return ScopedPoolPtr<Vec3>(AllocVec3(), FreeVec3);
}

inline ScopedPoolPtr<Vec3> MakeScopedVec3(const std::array<float, 3>& init) noexcept {
    auto ptr = AllocVec3();
    *ptr = init;
    return ScopedPoolPtr<Vec3>(ptr, FreeVec3);
}

inline ScopedPoolPtr<Vec2> MakeScopedVec2(const std::array<float, 2>& init) noexcept {
    auto ptr = AllocVec2();
    *ptr = init;
    return ScopedPoolPtr<Vec2>(ptr, FreeVec2);
}

} // namespace OmniGhost::Platform