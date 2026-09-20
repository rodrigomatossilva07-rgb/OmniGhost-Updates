#pragma once
#include "cs2_collision_mesh.h"
#include <memory>
#include <shared_mutex>
#include <string>
namespace CS2::Trajectory {
class MapCollisionCache {
public:
    // mapName accepts "de_mirage", "maps/de_mirage.vpk" or resource paths.
    bool LoadForMap(const char* mapName);
    std::shared_ptr<const CollisionBvh> WorldSnapshot() const;
    const std::string& Map() const noexcept { return map; }
    const std::string& Error() const noexcept { return error; }
private:
    std::string map, error;
    CollisionMesh mesh;
    std::shared_ptr<CollisionBvh> world;
    mutable std::shared_mutex mutex;
};
MapCollisionCache& CollisionCache();
}
