#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace OmniGhost::Platform {

// ============================================================
// Spatial Partitioning for Entity Culling
// ============================================================

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    
    Vec3 operator+(const Vec3& other) const { return {x + other.x, y + other.y, z + other.z}; }
    Vec3 operator-(const Vec3& other) const { return {x - other.x, y - other.y, z - other.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    
    float Length() const { return std::sqrt(x*x + y*y + z*z); }
    float LengthSq() const { return x*x + y*y + z*z; }
    float Distance(const Vec3& other) const { return (*this - other).Length(); }
    float DistanceSq(const Vec3& other) const { return (*this - other).LengthSq(); }
};

struct AABB {
    Vec3 min{FLT_MAX, FLT_MAX, FLT_MAX};
    Vec3 max{-FLT_MAX, -FLT_MAX, -FLT_MAX};
    
    AABB() = default;
    AABB(const Vec3& min_, const Vec3& max_) : min(min_), max(max_) {}
    
    void Expand(const Vec3& point) {
        min.x = std::min(min.x, point.x);
        min.y = std::min(min.y, point.y);
        min.z = std::min(min.z, point.z);
        max.x = std::max(max.x, point.x);
        max.y = std::max(max.y, point.y);
        max.z = std::max(max.z, point.z);
    }
    
    void Expand(const AABB& other) {
        Expand(other.min);
        Expand(other.max);
    }
    
    [[nodiscard]] bool Contains(const Vec3& point) const noexcept {
        return point.x >= min.x && point.x <= max.x &&
               point.y >= min.y && point.y <= max.y &&
               point.z >= min.z && point.z <= max.z;
    }
    
    [[nodiscard]] bool Intersects(const AABB& other) const noexcept {
        return min.x <= other.max.x && max.x >= other.min.x &&
               min.y <= other.max.y && max.y >= other.min.y &&
               min.z <= other.max.z && max.z >= other.min.z;
    }
    
    [[nodiscard]] Vec3 Center() const noexcept {
        return {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f};
    }
    
    [[nodiscard]] Vec3 Extents() const noexcept {
        return {(max.x - min.x) * 0.5f, (max.y - min.y) * 0.5f, (max.z - min.z) * 0.5f};
    }
    
    void Reset() {
        min = {FLT_MAX, FLT_MAX, FLT_MAX};
        max = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    }
    
    [[nodiscard]] bool Valid() const noexcept {
        return min.x <= max.x && min.y <= max.y && min.z <= max.z;
    }
};

struct Frustum {
    // 6 planes: left, right, top, bottom, near, far
    // Each plane: ax + by + cz + d = 0
    std::array<Vec3, 6> normals;
    std::array<float, 6> distances;
    
    // Build from view-projection matrix
    void BuildFromMatrix(const std::array<float, 16>& viewProj) noexcept {
        // Left plane
        normals[0] = {viewProj[3] + viewProj[0], viewProj[7] + viewProj[4], viewProj[11] + viewProj[8]};
        distances[0] = viewProj[15] + viewProj[12];
        
        // Right plane
        normals[1] = {viewProj[3] - viewProj[0], viewProj[7] - viewProj[4], viewProj[11] - viewProj[8]};
        distances[1] = viewProj[15] - viewProj[12];
        
        // Bottom plane
        normals[2] = {viewProj[3] + viewProj[1], viewProj[7] + viewProj[5], viewProj[11] + viewProj[9]};
        distances[2] = viewProj[15] + viewProj[13];
        
        // Top plane
        normals[3] = {viewProj[3] - viewProj[1], viewProj[7] - viewProj[5], viewProj[11] - viewProj[9]};
        distances[3] = viewProj[15] - viewProj[13];
        
        // Near plane
        normals[4] = {viewProj[3] + viewProj[2], viewProj[7] + viewProj[6], viewProj[11] + viewProj[10]};
        distances[4] = viewProj[15] + viewProj[14];
        
        // Far plane
        normals[4] = {viewProj[3] - viewProj[2], viewProj[7] - viewProj[6], viewProj[11] - viewProj[10]};
        distances[4] = viewProj[15] - viewProj[14];
        
        // Normalize
        for (int i = 0; i < 6; ++i) {
            float len = normals[i].Length();
            if (len > 0.0f) {
                normals[i] = normals[i] / len;
                distances[i] /= len;
            }
        }
    }
    
    [[nodiscard]] bool IntersectsSphere(const Vec3& center, float radius) const noexcept {
        for (int i = 0; i < 6; ++i) {
            float dist = normals[i].x * center.x + normals[i].y * center.y + normals[i].z * center.z + distances[i];
            if (dist < -radius) return false;
        }
        return true;
    }
    
    [[nodiscard]] bool IntersectsAABB(const AABB& aabb) const noexcept {
        if (!aabb.Valid()) return false;
        
        Vec3 center = aabb.Center();
        Vec3 extents = aabb.Extents();
        
        for (int i = 0; i < 6; ++i) {
            const Vec3& n = normals[i];
            float r = extents.x * std::abs(n.x) + extents.y * std::abs(n.y) + extents.z * std::abs(n.z);
            float dist = n.x * center.x + n.y * center.y + n.z * center.z + distances[i];
            if (dist < -r) return false;
        }
        return true;
    }
};

// ============================================================
// Spatial Hash Grid (Uniform Grid)
// ============================================================

template <typename T>
class SpatialHashGrid {
public:
    struct Cell {
        std::vector<T> entities;
    };
    
    SpatialHashGrid(float cellSize = 100.0f, int maxEntitiesPerCell = 64)
        : cellSize_(cellSize), invCellSize_(1.0f / cellSize), maxEntitiesPerCell_(maxEntitiesPerCell) {}
    
    void SetCellSize(float size) {
        cellSize_ = size;
        invCellSize_ = 1.0f / size;
    }
    
    void Clear() {
        cells_.clear();
    }
    
    // Insert entity with world position
    void Insert(uint64_t entityId, const Vec3& position, const T& data) {
        int cx = static_cast<int>(std::floor(position.x * invCellSize_));
        int cy = static_cast<int>(std::floor(position.y * invCellSize_));
        int cz = static_cast<int>(std::floor(position.z * invCellSize_));
        
        uint64_t key = Hash(cx, cy, cz);
        auto& cell = cells_[key];
        if (cell.entities.size() < static_cast<size_t>(maxEntitiesPerCell_)) {
            cell.entities.push_back(data);
        }
    }
    
    // Query entities within radius
    template <typename Func>
    void QueryRadius(const Vec3& center, float radius, Func&& func) const {
        int minX = static_cast<int>(std::floor((center.x - radius) * invCellSize_));
        int maxX = static_cast<int>(std::floor((center.x + radius) * invCellSize_));
        int minY = static_cast<int>(std::floor((center.y - radius) * invCellSize_));
        int maxY = static_cast<int>(std::floor((center.y + radius) * invCellSize_));
        int minZ = static_cast<int>(std::floor((center.z - radius) * invCellSize_));
        int maxZ = static_cast<int>(std::floor((center.z + radius) * invCellSize_));
        
        float radiusSq = radius * radius;
        
        for (int z = minZ; z <= maxZ; ++z) {
            for (int y = minY; y <= maxY; ++y) {
                for (int x = minX; x <= maxX; ++x) {
                    uint64_t key = Hash(x, y, z);
                    auto it = cells_.find(key);
                    if (it != cells_.end()) {
                        for (const auto& entity : it->second.entities) {
                            // Distance check would need position in entity data
                            func(entity);
                        }
                    }
                }
            }
        }
    }
    
    // Query entities within AABB
    template <typename Func>
    void QueryAABB(const AABB& aabb, Func&& func) const {
        if (!aabb.Valid()) return;
        
        int minX = static_cast<int>(std::floor(aabb.min.x * invCellSize_));
        int maxX = static_cast<int>(std::floor(aabb.max.x * invCellSize_));
        int minY = static_cast<int>(std::floor(aabb.min.y * invCellSize_));
        int maxY = static_cast<int>(std::floor(aabb.max.y * invCellSize_));
        int minZ = static_cast<int>(std::floor(aabb.min.z * invCellSize_));
        int maxZ = static_cast<int>(std::floor(aabb.max.z * invCellSize_));
        
        for (int z = minZ; z <= maxZ; ++z) {
            for (int y = minY; y <= maxY; ++y) {
                for (int x = minX; x <= maxX; ++x) {
                    uint64_t key = Hash(x, y, z);
                    auto it = cells_.find(key);
                    if (it != cells_.end()) {
                        for (const auto& entity : it->second.entities) {
                            func(entity);
                        }
                    }
                }
            }
        }
    }
    
    // Query entities within frustum
    template <typename Func>
    void QueryFrustum(const Frustum& frustum, Func&& func) const {
        // Get frustum bounds
        // For simplicity, query all cells and filter by frustum
        for (const auto& [key, cell] : cells_) {
            for (const auto& entity : cell.entities) {
                // Would need position in entity to check frustum
                func(entity);
            }
        }
    }
    
    [[nodiscard]] size_t CellCount() const noexcept { return cells_.size(); }
    [[nodiscard]] size_t TotalEntities() const noexcept {
        size_t count = 0;
        for (const auto& [_, cell] : cells_) {
            count += cell.entities.size();
        }
        return count;
    }

private:
    static uint64_t Hash(int x, int y, int z) noexcept {
        // 3D spatial hash (Murmur-like)
        uint64_t h = static_cast<uint64_t>(static_cast<uint32_t>(x));
        h = (h ^ (static_cast<uint64_t>(static_cast<uint32_t>(y)) << 32)) * 0x9e3779b97f4a7c15ULL;
        h = (h ^ static_cast<uint64_t>(static_cast<uint32_t>(z))) * 0x9e3779b97f4a7c15ULL;
        return h;
    }
    
    float cellSize_ = 100.0f;
    float invCellSize_ = 0.01f;
    int maxEntitiesPerCell_ = 64;
    std::unordered_map<uint64_t, Cell> cells_;
};

// ============================================================
// Octree for Hierarchical Culling
// ============================================================

template <typename T, int MaxDepth = 8, int MaxEntitiesPerNode = 16>
class Octree {
public:
    struct Node {
        AABB bounds;
        std::vector<T> entities;
        std::array<std::unique_ptr<Node>, 8> children;
        bool isLeaf = true;
        int depth = 0;
    };
    
    Octree(const AABB& worldBounds, int maxDepth = MaxDepth, int maxEntities = MaxEntitiesPerNode)
        : root_(std::make_unique<Node>()), maxDepth_(maxDepth), maxEntitiesPerNode_(maxEntities) {
        root_->bounds = worldBounds;
        root_->depth = 0;
    }
    
    void Clear() {
        root_ = std::make_unique<Node>();
        root_->bounds = root_->bounds; // Keep same bounds
        root_->depth = 0;
    }
    
    void SetWorldBounds(const AABB& bounds) {
        root_->bounds = bounds;
    }
    
    void Insert(const T& entity, const Vec3& position) {
        InsertRecursive(root_.get(), entity, position);
    }
    
    template <typename Func>
    void QueryRadius(const Vec3& center, float radius, Func&& func) const {
        AABB queryBounds{center - Vec3(radius, radius, radius), center + Vec3(radius, radius, radius)};
        QueryAABBRecursive(root_.get(), queryBounds, std::forward<Func>(func));
    }
    
    template <typename Func>
    void QueryAABB(const AABB& aabb, Func&& func) const {
        QueryAABBRecursive(root_.get(), aabb, std::forward<Func>(func));
    }
    
    template <typename Func>
    void QueryFrustum(const Frustum& frustum, Func&& func) const {
        QueryFrustumRecursive(root_.get(), frustum, std::forward<Func>(func));
    }
    
    [[nodiscard]] size_t TotalEntities() const noexcept {
        return CountEntities(root_.get());
    }
    
    [[nodiscard]] int NodeCount() const noexcept {
        return CountNodes(root_.get());
    }
    
    [[nodiscard]] int MaxDepth() const noexcept { return maxDepth_; }

private:
    std::unique_ptr<Node> root_;
    int maxDepth_;
    int maxEntitiesPerNode_;
    
    void InsertRecursive(Node* node, const T& entity, const Vec3& position) {
        if (!node->bounds.Contains(position)) return;
        
        if (node->isLeaf) {
            node->entities.push_back(entity);
            
            // Split if too many entities and not at max depth
            if (node->entities.size() > static_cast<size_t>(maxEntitiesPerNode_) && node->depth < maxDepth_) {
                SplitNode(node);
            }
        } else {
            // Find child
            int childIndex = GetChildIndex(node, position);
            if (!node->children[childIndex]) {
                CreateChild(node, childIndex);
            }
            InsertRecursive(node->children[childIndex].get(), entity, position);
        }
    }
    
    void SplitNode(Node* node) {
        // Create 8 children
        Vec3 center = node->bounds.Center();
        Vec3 extents = node->bounds.Extents();
        Vec3 halfExtents = extents * 0.5f;
        
        for (int i = 0; i < 8; ++i) {
            Vec3 childMin, childMax;
            
            childMin.x = (i & 1) ? center.x : node->bounds.min.x;
            childMin.y = (i & 2) ? center.y : node->bounds.min.y;
            childMin.z = (i & 4) ? center.z : node->bounds.min.z;
            
            childMax.x = (i & 1) ? node->bounds.max.x : center.x;
            childMax.y = (i & 2) ? node->bounds.max.y : center.y;
            childMax.z = (i & 4) ? node->bounds.max.z : center.z;
            
            node->children[i] = std::make_unique<Node>();
            node->children[i]->bounds = AABB(childMin, childMax);
            node->children[i]->depth = node->depth + 1;
        }
        
        // Redistribute entities to children
        std::vector<T> oldEntities = std::move(node->entities);
        node->entities.clear();
        node->isLeaf = false;
        
        for (const auto& entity : oldEntities) {
            // Would need position stored in entity
            // For now, just keep in parent
            node->entities.push_back(entity);
        }
    }
    
    void CreateChild(Node* parent, int index) {
        if (parent->children[index]) return;
        
        Vec3 center = parent->bounds.Center();
        Vec3 halfExtents = parent->bounds.Extents() * 0.5f;
        
        Vec3 childMin, childMax;
        childMin.x = (index & 1) ? center.x : parent->bounds.min.x;
        childMin.y = (index & 2) ? center.y : parent->bounds.min.y;
        childMin.z = (index & 4) ? center.z : parent->bounds.min.z;
        
        childMax.x = (index & 1) ? parent->bounds.max.x : center.x;
        childMax.y = (index & 2) ? parent->bounds.max.y : center.y;
        childMax.z = (index & 4) ? parent->bounds.max.z : center.z;
        
        parent->children[index] = std::make_unique<Node>();
        parent->children[index]->bounds = AABB(childMin, childMax);
        parent->children[index]->depth = parent->depth + 1;
    }
    
    int GetChildIndex(Node* node, const Vec3& position) const {
        Vec3 center = node->bounds.Center();
        int index = 0;
        if (position.x > center.x) index |= 1;
        if (position.y > center.y) index |= 2;
        if (position.z > center.z) index |= 4;
        return index;
    }
    
    template <typename Func>
    void QueryAABBRecursive(Node* node, const AABB& aabb, Func&& func) const {
        if (!node || !node->bounds.Intersects(aabb)) return;
        
        if (node->isLeaf) {
            for (const auto& entity : node->entities) {
                func(entity);
            }
        } else {
            for (int i = 0; i < 8; ++i) {
                if (node->children[i] && node->children[i]->bounds.Intersects(aabb)) {
                    QueryAABBRecursive(node->children[i].get(), aabb, func);
                }
            }
        }
    }
    
    template <typename Func>
    void QueryFrustumRecursive(Node* node, const Frustum& frustum, Func&& func) const {
        if (!node || !frustum.IntersectsAABB(node->bounds)) return;
        
        if (node->isLeaf) {
            for (const auto& entity : node->entities) {
                func(entity);
            }
        } else {
            for (int i = 0; i < 8; ++i) {
                if (node->children[i]) {
                    QueryFrustumRecursive(node->children[i].get(), frustum, func);
                }
            }
        }
    }
    
    size_t CountEntities(Node* node) const {
        if (!node) return 0;
        size_t count = node->entities.size();
        if (!node->isLeaf) {
            for (int i = 0; i < 8; ++i) {
                if (node->children[i]) {
                    count += CountEntities(node->children[i].get());
                }
            }
        }
        return count;
    }
    
    int CountNodes(Node* node) const {
        if (!node) return 0;
        int count = 1;
        if (!node->isLeaf) {
            for (int i = 0; i < 8; ++i) {
                if (node->children[i]) {
                    count += CountNodes(node->children[i].get());
                }
            }
        }
        return count;
    }
};

} // namespace OmniGhost::Platform