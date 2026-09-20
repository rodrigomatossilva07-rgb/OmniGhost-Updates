#pragma once
#include "cs2_collision_mesh.h"
#include <string>
namespace CS2::Trajectory {
class MapCollisionCache {
public:
    // mapName accepts "de_mirage", "maps/de_mirage.vpk" or resource paths.
    bool LoadForMap(const char* mapName);
    const CollisionBvh& World() const noexcept { return world; }
    const std::string& Map() const noexcept { return map; }
    const std::string& Error() const noexcept { return error; }
private:
    std::string map, error;
    CollisionMesh mesh;
    CollisionBvh world;
};
MapCollisionCache& CollisionCache();
}
