#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace CS2::Trajectory {
struct Vec3 { float x{}, y{}, z{}; };
struct Triangle { Vec3 a{}, b{}, c{}; };
struct CollisionMesh { std::vector<Triangle> triangles; std::filesystem::path source; bool loaded{}; std::string error; };
bool LoadTriFile(const std::filesystem::path& path, CollisionMesh& output);
}
