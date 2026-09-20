#include "cs2_map_collision_cache.h"
#include "../../src/platform/embedded_resources.h"
#include <algorithm>
namespace CS2::Trajectory {
namespace { std::string Normalize(const char* text) { std::string value=text?text:""; const auto slash=value.find_last_of("/\\"); if(slash!=std::string::npos)value.erase(0,slash+1); const auto dot=value.find('.'); if(dot!=std::string::npos)value.erase(dot); std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));}); return value; } }
bool MapCollisionCache::LoadForMap(const char* mapName) {
    const std::string next=Normalize(mapName); if(next.empty()){error="Mapa atual indisponível";return false;}
    if(next==map && world.Ready()) return true;
    map=next; error.clear(); world={}; mesh={};
    const std::string logicalName = "cs2/collision/" + map + ".tri";
    auto bytes = OmniGhost::LoadEmbeddedResource(logicalName);
    if (!bytes) { error="Colisão do mapa não está embutida no executável"; return false; }
    if(!LoadTriBytes(bytes->data(),bytes->size(),std::filesystem::path(logicalName),mesh)){error=mesh.error;return false;}
    if(!world.Build(mesh)){error="Não foi possível construir BVH";return false;} return true;
}
MapCollisionCache& CollisionCache(){static MapCollisionCache cache;return cache;}
}
