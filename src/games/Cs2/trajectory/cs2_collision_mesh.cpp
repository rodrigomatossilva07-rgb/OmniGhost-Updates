#include "cs2_collision_mesh.h"
#include <cmath>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <limits>
#include <numeric>
namespace CS2::Trajectory {
bool LoadTriBytes(const std::uint8_t* bytes, std::size_t size, const std::filesystem::path& source, CollisionMesh& output) {
    output = {}; output.source = source;
    constexpr std::uintmax_t record = sizeof(float) * 9;
    if (!bytes || !size || size % record) { output.error = "Ficheiro .tri inválido"; return false; }
    const auto count = size / record; if (count > 20'000'000) { output.error = "Mapa demasiado grande"; return false; }
    output.triangles.resize(static_cast<size_t>(count));
    std::memcpy(output.triangles.data(), bytes, size);
    for (const auto& t : output.triangles) { const auto* v = reinterpret_cast<const float*>(&t); for (int i=0;i<9;++i) if (!std::isfinite(v[i])) { output = {}; output.source = source; output.error = "Coordenadas inválidas"; return false; } }
    output.loaded = true; return true;
}
bool LoadTriFile(const std::filesystem::path& path, CollisionMesh& output) {
    output = {}; output.source = path; std::error_code ec; const auto size = std::filesystem::file_size(path, ec);
    if (ec || !size || size > static_cast<std::uintmax_t>((std::numeric_limits<std::size_t>::max)())) { output.error = "Ficheiro .tri inválido"; return false; }
    std::ifstream in(path, std::ios::binary); if (!in) { output.error = "Não foi possível abrir o .tri"; return false; }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!in) { output = {}; output.source = path; output.error = "Leitura incompleta"; return false; }
    return LoadTriBytes(bytes.data(), bytes.size(), path, output);
}

namespace { using CS2::Trajectory::Vec3; using CS2::Trajectory::Triangle;
Vec3 Sub(Vec3 a, Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};} Vec3 Cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
float Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
bool Box(Vec3 o, Vec3 d, Vec3 mn, Vec3 mx, float best){ float lo=0,hi=best; const float* a=&o.x;const float* b=&d.x;const float* l=&mn.x;const float* h=&mx.x; for(int i=0;i<3;++i){if(std::fabs(b[i])<1e-7f){if(a[i]<l[i]||a[i]>h[i])return false;}else{float x=(l[i]-a[i])/b[i],y=(h[i]-a[i])/b[i];if(x>y)std::swap(x,y);lo=(std::max)(lo,x);hi=(std::min)(hi,y);if(lo>hi)return false;}}return true;}
bool Tri(Vec3 o,Vec3 d,const Triangle&t,float& u,Vec3&n){auto e1=Sub(t.b,t.a),e2=Sub(t.c,t.a),p=Cross(d,e2);float det=Dot(e1,p);if(std::fabs(det)<1e-7f)return false;float inv=1/det;auto q=Sub(o,t.a);float a=Dot(q,p)*inv;if(a<0||a>1)return false;auto r=Cross(q,e1);float b=Dot(d,r)*inv;if(b<0||a+b>1)return false;float v=Dot(e2,r)*inv;if(v<0||v>u)return false;u=v;n=Cross(e1,e2);float l=std::sqrt(Dot(n,n));if(l>0){n.x/=l;n.y/=l;n.z/=l;}return true;}}
bool CollisionBvh::Build(const CollisionMesh& mesh){triangles=mesh.triangles;indices.resize(triangles.size());std::iota(indices.begin(),indices.end(),0);nodes.clear();if(triangles.empty())return false;BuildNode(0,(unsigned)indices.size());return true;}
unsigned CollisionBvh::BuildNode(unsigned begin,unsigned end){Node n{};n.min={INFINITY,INFINITY,INFINITY};n.max={-INFINITY,-INFINITY,-INFINITY};for(unsigned i=begin;i<end;++i){const auto&t=triangles[indices[i]];for(auto p:{t.a,t.b,t.c}){n.min.x=(std::min)(n.min.x,p.x);n.min.y=(std::min)(n.min.y,p.y);n.min.z=(std::min)(n.min.z,p.z);n.max.x=(std::max)(n.max.x,p.x);n.max.y=(std::max)(n.max.y,p.y);n.max.z=(std::max)(n.max.z,p.z);}}unsigned id=(unsigned)nodes.size();nodes.push_back(n);if(end-begin<=12){nodes[id].begin=begin;nodes[id].count=end-begin;return id;}int axis=0;auto span=Sub(n.max,n.min);if(span.y>span.x)axis=1;if((&span.x)[2]>(&span.x)[axis])axis=2;unsigned mid=(begin+end)/2;std::nth_element(indices.begin()+begin,indices.begin()+mid,indices.begin()+end,[&](unsigned a,unsigned b){return ((&triangles[a].a.x)[axis]+(&triangles[a].b.x)[axis]+(&triangles[a].c.x)[axis])<((&triangles[b].a.x)[axis]+(&triangles[b].b.x)[axis]+(&triangles[b].c.x)[axis]);});nodes[id].left=BuildNode(begin,mid);nodes[id].right=BuildNode(mid,end);return id;}
RayHit CollisionBvh::TraceRay(Vec3 from,Vec3 to)const{RayHit out{};if(nodes.empty())return out;Vec3 d=Sub(to,from);std::vector<unsigned> stack{0};while(!stack.empty()){auto id=stack.back();stack.pop_back();const auto&n=nodes[id];if(!Box(from,d,n.min,n.max,out.fraction))continue;if(n.count)for(unsigned i=0;i<n.count;++i){float f=out.fraction;Vec3 normal{};if(Tri(from,d,triangles[indices[n.begin+i]],f,normal)){out={true,f,{from.x+d.x*f,from.y+d.y*f,from.z+d.z*f},normal};}}else{stack.push_back(n.left);stack.push_back(n.right);}}return out;}
}
