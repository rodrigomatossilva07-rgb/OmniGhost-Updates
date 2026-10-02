#include "cs2_map_collision_cache.h"
#include "../../../platform/embedded_resources.h"
#include <algorithm>
#include <cctype>
#include <mutex>
#include <utility>
namespace CS2::Trajectory {
namespace { std::string Normalize(const char* text) { std::string value=text?text:""; const auto slash=value.find_last_of("/\\"); if(slash!=std::string::npos)value.erase(0,slash+1); const auto dot=value.find('.'); if(dot!=std::string::npos)value.erase(dot); std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));}); return value; } }
bool MapCollisionCache::LoadForMap(const char* mapName) {
    const std::string next=Normalize(mapName); if(next.empty()){std::unique_lock write_lock(mutex);error="Mapa atual indisponível";return false;}
    std::uint64_t requestedGeneration = 0;
    {
        std::shared_lock read_lock(mutex);
        if(next==map && world && world->Ready()) return true;
        requestedGeneration = generation;
    }
    const std::string logicalName = "cs2/collision/" + next + ".tri";
    auto bytes = OmniGhost::LoadEmbeddedResource(logicalName);
    CollisionMesh loaded;
    if (!bytes || !LoadTriBytes(bytes ? bytes->data() : nullptr, bytes ? bytes->size() : 0, std::filesystem::path(logicalName), loaded)) {
        std::unique_lock write_lock(mutex);
        if (requestedGeneration == generation)
            error = bytes ? loaded.error : "Colisão do mapa não está embutida no executável";
        return false;
    }
    {
        std::shared_lock read_lock(mutex);
        if (requestedGeneration != generation) return false;
    }
    auto built = std::make_shared<CollisionBvh>();
    if(!built->Build(std::move(loaded))){
        std::unique_lock write_lock(mutex);
        if (requestedGeneration == generation) error="Não foi possível construir BVH";
        return false;
    }
    std::unique_lock write_lock(mutex);
    if (requestedGeneration != generation) return false;
    // CollisionBvh owns its compact copy of the triangles. Do not retain a
    // second complete mesh after construction; large maps otherwise cost
    // hundreds of MiB while the trajectory feature is enabled.
    map=next; error.clear(); world=std::move(built); return true;
}
bool MapCollisionCache::IsLoadedFor(const char* mapName) const {
    const std::string next = Normalize(mapName);
    std::shared_lock lock(mutex);
    return !next.empty() && next == map && world && world->Ready();
}
void MapCollisionCache::Clear() {
    std::unique_lock lock(mutex);
    ++generation;
    map.clear(); error.clear(); world.reset();
}
std::shared_ptr<const CollisionBvh> MapCollisionCache::WorldSnapshot() const { std::shared_lock lock(mutex); return world; }
std::string MapCollisionCache::Map() const { std::shared_lock lock(mutex); return map; }
std::string MapCollisionCache::Error() const { std::shared_lock lock(mutex); return error; }
MapCollisionCache& CollisionCache(){static MapCollisionCache cache;return cache;}
}
