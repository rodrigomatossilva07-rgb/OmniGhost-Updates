#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace FiveM::Collision {

struct Vec3 { float x{}, y{}, z{}; };
struct RayHit { bool hit{}; float fraction{1.0f}; Vec3 position{}; Vec3 normal{}; };

class Region {
public:
    bool Load(const std::filesystem::path& path);
    RayHit TraceRay(Vec3 from, Vec3 to) const;
    [[nodiscard]] bool Ready() const noexcept { return !nodes_.empty(); }
    [[nodiscard]] int CellX() const noexcept { return cellX_; }
    [[nodiscard]] int CellY() const noexcept { return cellY_; }
    [[nodiscard]] float RegionSize() const noexcept { return regionSize_; }
    [[nodiscard]] std::uint64_t MemoryBytes() const noexcept;
    [[nodiscard]] const std::string& Error() const noexcept { return error_; }

private:
    struct Triangle { Vec3 a{}, b{}, c{}; };
    struct Node { Vec3 min{}, max{}; std::uint32_t begin{}, count{}, left{}, right{}; };
    int cellX_{};
    int cellY_{};
    float regionSize_{};
    std::vector<Triangle> triangles_;
    std::vector<Node> nodes_;
    std::vector<std::uint32_t> indices_;
    std::string error_;
};

enum class TraceState : std::uint8_t { Clear, Blocked, MissingRegion, Unavailable };
struct TraceResult { TraceState state{TraceState::Unavailable}; RayHit hit{}; };
struct Diagnostics {
    bool initialized{};
    bool manifestValid{};
    std::uint32_t cachedRegions{};
    std::uint32_t pendingRegions{};
    std::uint64_t cacheBytes{};
    std::uint64_t cacheLimitBytes{};
    std::uint64_t successfulLoads{};
    std::uint64_t failedLoads{};
    std::uint64_t missingQueries{};
    float regionSize{};
    std::string status;
};

class World {
public:
    static World& Instance();
    bool Initialize(const std::filesystem::path& folder,
                    std::uint64_t cacheLimitBytes = 640ull * 1024ull * 1024ull);
    void Shutdown();
    void RequestAround(Vec3 position, int radius = 0);
    TraceResult TraceSegment(Vec3 from, Vec3 to);
    [[nodiscard]] Diagnostics GetDiagnostics() const;

    struct Impl;
private:
    World();
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    std::unique_ptr<Impl> impl_;
};

}
