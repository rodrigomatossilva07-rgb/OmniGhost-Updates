#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace CS2::Trajectory {
struct Vec3 { float x{}, y{}, z{}; };
struct Triangle { Vec3 a{}, b{}, c{}; };
struct CollisionMesh { std::vector<Triangle> triangles; std::filesystem::path source; bool loaded{}; std::string error; };
bool LoadTriFile(const std::filesystem::path& path, CollisionMesh& output);
struct RayHit { bool hit{}; float fraction{1.f}; Vec3 position{}; Vec3 normal{}; };
class CollisionBvh {
public:
    bool Build(const CollisionMesh& mesh);
    RayHit TraceRay(Vec3 from, Vec3 to) const;
    [[nodiscard]] bool Ready() const noexcept { return !nodes.empty(); }
private:
    struct Node { Vec3 min{}, max{}; unsigned begin{}, count{}, left{}, right{}; };
    std::vector<Triangle> triangles;
    std::vector<unsigned> indices;
    std::vector<Node> nodes;
    unsigned BuildNode(unsigned begin, unsigned end);
};
}
