#include "cs2_map_collision_cache.h"
#include "../../src/platform/app_paths.h"
#include <algorithm>
namespace CS2::Trajectory {
namespace { std::string Normalize(const char* text) { std::string value=text?text:""; const auto slash=value.find_last_of("/\\"); if(slash!=std::string::npos)value.erase(0,slash+1); const auto dot=value.find('.'); if(dot!=std::string::npos)value.erase(dot); std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));}); return value; } }
bool MapCollisionCache::LoadForMap(const char* mapName) {
    const std::string next=Normalize(mapName); if(next.empty()){error="Mapa atual indisponível";return false;}
    if(next==map && world.Ready()) return true;
    map=next; error.clear(); world={}; mesh={};
    const auto path=OmniGhost::Paths::InstallDirectory()/L"data"/L"cs2-collision"/(std::filesystem::path(map).wstring()+L".tri");
    if(!LoadTriFile(path,mesh)){error=mesh.error;return false;}
    if(!world.Build(mesh)){error="Não foi possível construir BVH";return false;} return true;
}
MapCollisionCache& CollisionCache(){static MapCollisionCache cache;return cache;}
}
