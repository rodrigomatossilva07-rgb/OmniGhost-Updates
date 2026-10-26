#include "fivem_collision_world.h"
#include "../../../platform/app_paths.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace FiveM::Collision {
namespace {
constexpr std::array<char, 8> kMagic{'O', 'G', 'C', 'O', 'L', '1', '\0', '\0'};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint64_t kHeaderBytes = 60;
constexpr std::uint32_t kMaxTriangles = 8'000'000;
constexpr std::uint32_t kMaxNodes = 16'000'000;

template <typename T> bool Read(std::ifstream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), static_cast<std::streamsize>(sizeof(value)));
    return static_cast<bool>(in);
}
bool ReadVec(std::ifstream& in, Vec3& value) { return Read(in, value.x) && Read(in, value.y) && Read(in, value.z); }
bool Finite(Vec3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
float Dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }

bool IntersectsBox(Vec3 origin, Vec3 delta, Vec3 mn, Vec3 mx, float best) {
    float low = 0.0f, high = best;
    const float* o = &origin.x; const float* d = &delta.x;
    const float* lo = &mn.x; const float* hi = &mx.x;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(d[axis]) < 1.0e-7f) {
            if (o[axis] < lo[axis] || o[axis] > hi[axis]) return false;
        } else {
            float a = (lo[axis] - o[axis]) / d[axis];
            float b = (hi[axis] - o[axis]) / d[axis];
            if (a > b) std::swap(a, b);
            low = (std::max)(low, a); high = (std::min)(high, b);
            if (low > high) return false;
        }
    }
    return true;
}

template <typename Triangle>
bool IntersectsTriangle(Vec3 origin, Vec3 delta, const Triangle& triangle, float& fraction, Vec3& normal) {
    const Vec3 e1 = Sub(triangle.b, triangle.a), e2 = Sub(triangle.c, triangle.a);
    const Vec3 p = Cross(delta, e2);
    const float determinant = Dot(e1, p);
    if (std::fabs(determinant) < 1.0e-7f) return false;
    const float inverse = 1.0f / determinant;
    const Vec3 q = Sub(origin, triangle.a);
    const float u = Dot(q, p) * inverse;
    if (u < 0.0f || u > 1.0f) return false;
    const Vec3 r = Cross(q, e1);
    const float v = Dot(delta, r) * inverse;
    if (v < 0.0f || u + v > 1.0f) return false;
    const float distance = Dot(e2, r) * inverse;
    if (distance < 0.0f || distance > fraction) return false;
    fraction = distance;
    normal = Cross(e1, e2);
    const float length = std::sqrt(Dot(normal, normal));
    if (length > 0.0f) { normal.x /= length; normal.y /= length; normal.z /= length; }
    return true;
}
}

bool Region::Load(const std::filesystem::path& path) {
    cellX_ = cellY_ = 0; regionSize_ = 0.0f;
    triangles_.clear(); nodes_.clear(); indices_.clear(); error_.clear();
    std::error_code ec;
    const std::uint64_t bytes = std::filesystem::file_size(path, ec);
    if (ec || bytes < kHeaderBytes) { error_ = "Ficheiro OGCOL ausente ou demasiado pequeno"; return false; }
    std::ifstream in(path, std::ios::binary);
    std::array<char, 8> magic{};
    std::uint32_t version{}, triangleCount{}, nodeCount{}, indexCount{};
    Vec3 rootMin{}, rootMax{};
    if (!in || !in.read(magic.data(), static_cast<std::streamsize>(magic.size())) || magic != kMagic ||
        !Read(in, version) || !Read(in, cellX_) || !Read(in, cellY_) || !Read(in, regionSize_) ||
        !Read(in, triangleCount) || !Read(in, nodeCount) || !Read(in, indexCount) ||
        !ReadVec(in, rootMin) || !ReadVec(in, rootMax)) {
        error_ = "Cabeçalho OGCOL inválido"; return false;
    }
    if (version != kVersion || !std::isfinite(regionSize_) || regionSize_ <= 0.0f || !triangleCount ||
        !nodeCount || triangleCount > kMaxTriangles || nodeCount > kMaxNodes || indexCount != triangleCount ||
        !Finite(rootMin) || !Finite(rootMax)) {
        error_ = "Limites ou versão OGCOL inválidos"; return false;
    }
    const std::uint64_t expected = kHeaderBytes + std::uint64_t(triangleCount) * 36u +
                                   std::uint64_t(nodeCount) * 40u + std::uint64_t(indexCount) * 4u;
    if (bytes != expected) { error_ = "Tamanho OGCOL inconsistente"; return false; }
    triangles_.resize(triangleCount); nodes_.resize(nodeCount); indices_.resize(indexCount);
    for (auto& t : triangles_) if (!ReadVec(in, t.a) || !ReadVec(in, t.b) || !ReadVec(in, t.c) ||
        !Finite(t.a) || !Finite(t.b) || !Finite(t.c)) { error_ = "Triângulos OGCOL inválidos"; break; }
    if (error_.empty()) for (auto& n : nodes_) if (!ReadVec(in, n.min) || !ReadVec(in, n.max) ||
        !Read(in, n.begin) || !Read(in, n.count) || !Read(in, n.left) || !Read(in, n.right) ||
        !Finite(n.min) || !Finite(n.max)) { error_ = "Nós OGCOL inválidos"; break; }
    if (error_.empty()) for (auto& index : indices_) if (!Read(in, index) || index >= triangleCount) {
        error_ = "Índices OGCOL inválidos"; break;
    }
    if (error_.empty()) for (const auto& n : nodes_) {
        const bool leaf = n.count != 0;
        if ((leaf && (std::uint64_t(n.begin) + n.count > indexCount)) ||
            (!leaf && (n.left >= nodeCount || n.right >= nodeCount))) {
            error_ = "Árvore BVH OGCOL inválida"; break;
        }
    }
    if (!error_.empty()) { triangles_.clear(); nodes_.clear(); indices_.clear(); return false; }
    return true;
}

RayHit Region::TraceRay(Vec3 from, Vec3 to) const {
    RayHit hit{};
    if (nodes_.empty()) return hit;
    const Vec3 delta = Sub(to, from);
    thread_local std::vector<std::uint32_t> stack;
    stack.clear(); stack.push_back(0);
    while (!stack.empty()) {
        const std::uint32_t id = stack.back(); stack.pop_back();
        const Node& node = nodes_[id];
        if (!IntersectsBox(from, delta, node.min, node.max, hit.fraction)) continue;
        if (node.count) {
            for (std::uint32_t i = 0; i < node.count; ++i) {
                float fraction = hit.fraction; Vec3 normal{};
                if (IntersectsTriangle(from, delta, triangles_[indices_[node.begin + i]], fraction, normal))
                    hit = {true, fraction,
                           {from.x + delta.x*fraction, from.y + delta.y*fraction, from.z + delta.z*fraction}, normal};
            }
        } else { stack.push_back(node.left); stack.push_back(node.right); }
    }
    return hit;
}

std::uint64_t Region::MemoryBytes() const noexcept {
    return sizeof(*this) + std::uint64_t(triangles_.capacity()) * sizeof(Triangle) +
           std::uint64_t(nodes_.capacity()) * sizeof(Node) +
           std::uint64_t(indices_.capacity()) * sizeof(std::uint32_t);
}

namespace {
std::int64_t CellKey(int x, int y) {
    const auto high = static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32;
    return static_cast<std::int64_t>(high | static_cast<std::uint32_t>(y));
}
std::pair<int, int> SplitCellKey(std::int64_t key) {
    return {static_cast<int>(key >> 32), static_cast<int>(static_cast<std::uint32_t>(key))};
}
std::vector<std::int64_t> CellsAlongRay(Vec3 from, Vec3 to, float size) {
    std::vector<std::int64_t> cells;
    if (!(size > 0.0f) || !Finite(from) || !Finite(to)) return cells;
    int x = static_cast<int>(std::floor(from.x / size));
    int y = static_cast<int>(std::floor(from.y / size));
    const int endX = static_cast<int>(std::floor(to.x / size));
    const int endY = static_cast<int>(std::floor(to.y / size));
    cells.push_back(CellKey(x, y));
    const float dx = to.x - from.x, dy = to.y - from.y;
    const int stepX = dx > 0.0f ? 1 : (dx < 0.0f ? -1 : 0);
    const int stepY = dy > 0.0f ? 1 : (dy < 0.0f ? -1 : 0);
    const float infinity = std::numeric_limits<float>::infinity();
    const float deltaX = stepX ? std::fabs(size / dx) : infinity;
    const float deltaY = stepY ? std::fabs(size / dy) : infinity;
    const float edgeX = stepX > 0 ? (float(x + 1) * size) : (float(x) * size);
    const float edgeY = stepY > 0 ? (float(y + 1) * size) : (float(y) * size);
    float maxX = stepX ? (edgeX - from.x) / dx : infinity;
    float maxY = stepY ? (edgeY - from.y) / dy : infinity;
    for (int guard = 0; (x != endX || y != endY) && guard < 128; ++guard) {
        if (maxX < maxY) { x += stepX; maxX += deltaX; }
        else { y += stepY; maxY += deltaY; }
        cells.push_back(CellKey(x, y));
    }
    return cells;
}
float ReadRegionSize(const std::filesystem::path& manifest) {
    std::ifstream in(manifest);
    if (!in) return 0.0f;
    std::ostringstream text; text << in.rdbuf();
    const std::string source = text.str();
    const std::string key = "\"region_size\"";
    const auto pos = source.find(key);
    if (pos == std::string::npos) return 0.0f;
    const auto colon = source.find(':', pos + key.size());
    if (colon == std::string::npos) return 0.0f;
    try {
        const float value = std::stof(source.substr(colon + 1));
        return std::isfinite(value) && value >= 128.0f && value <= 2048.0f ? value : 0.0f;
    } catch (...) { return 0.0f; }
}
}

struct World::Impl {
    struct Entry { std::shared_ptr<Region> region; std::uint64_t bytes{}; std::uint64_t lastUse{}; };
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::filesystem::path folder;
    std::unordered_map<std::int64_t, Entry> cache;
    std::deque<std::int64_t> queue;
    std::unordered_set<std::int64_t> queued;
    std::unordered_set<std::int64_t> unavailable;
    std::thread loader;
    bool initialized{};
    bool manifestValid{};
    bool stop{};
    float regionSize{};
    std::uint64_t cacheBytes{};
    std::uint64_t cacheLimitBytes{640ull * 1024ull * 1024ull};
    std::uint64_t useCounter{};
    std::uint64_t successfulLoads{};
    std::uint64_t failedLoads{};
    std::uint64_t missingQueries{};
    std::string status{"Colisão FiveM ainda não inicializada"};

    void QueueLocked(std::int64_t key) {
        if (cache.count(key) || unavailable.count(key) || !queued.insert(key).second) return;
        queue.push_back(key);
        wake.notify_one();
    }
    void EvictLocked(std::int64_t keep) {
        while (cacheBytes > cacheLimitBytes && cache.size() > 1) {
            auto victim = cache.end();
            for (auto it = cache.begin(); it != cache.end(); ++it)
                if (it->first != keep && (victim == cache.end() || it->second.lastUse < victim->second.lastUse))
                    victim = it;
            if (victim == cache.end()) break;
            cacheBytes -= victim->second.bytes;
            cache.erase(victim);
        }
    }
    void LoaderLoop() {
        for (;;) {
            std::int64_t key{};
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return stop || !queue.empty(); });
                if (stop) return;
                key = queue.front(); queue.pop_front(); queued.erase(key);
            }
            const auto [x, y] = SplitCellKey(key);
            const auto path = folder / ("region_" + std::to_string(x) + "_" + std::to_string(y) + ".ogcol");
            auto region = std::make_shared<Region>();
            const bool exists = std::filesystem::exists(path);
            const bool loaded = exists && region->Load(path);
            std::scoped_lock lock(mutex);
            if (stop) return;
            if (loaded) {
                const std::uint64_t bytes = region->MemoryBytes();
                cache[key] = {std::move(region), bytes, ++useCounter};
                cacheBytes += bytes; ++successfulLoads;
                status = "Colisão FiveM pronta";
                EvictLocked(key);
            } else {
                unavailable.insert(key);
                ++failedLoads;
                status = exists ? ("Região inválida: " + path.filename().string())
                                : ("Região não gerada: " + path.filename().string());
            }
        }
    }
};

World& World::Instance() { static World world; return world; }
World::World() : impl_(std::make_unique<Impl>()) {}
World::~World() { Shutdown(); }

bool World::Initialize(const std::filesystem::path& folder, std::uint64_t cacheLimitBytes) {
    Shutdown();
    std::error_code ec;
    std::filesystem::path absolute;
    float size = 0.0f;
    std::vector<std::filesystem::path> candidates;
    if (const char* overrideFolder = std::getenv("OMNIGHOST_FIVEM_COLLISION_DIR");
        overrideFolder && *overrideFolder) {
        candidates.emplace_back(overrideFolder);
    }
    candidates.push_back(folder);
    candidates.push_back(std::filesystem::path("..") / folder);
    candidates.push_back(std::filesystem::path("..") / ".." / folder);




    const auto executableDir = OmniGhost::Paths::InstallDirectory();
    candidates.push_back(executableDir / folder);
    candidates.push_back(executableDir / ".." / folder);
    candidates.push_back(executableDir / ".." / ".." / folder);
    candidates.push_back(OmniGhost::Paths::LocalData() / "collision" / "fivem");

    for (const auto& candidate : candidates) {
        ec.clear();
        const auto candidateAbsolute = std::filesystem::absolute(candidate, ec);
        if (ec) continue;
        const auto resolved = std::filesystem::weakly_canonical(candidateAbsolute, ec);
        if (ec) continue;
        const float candidateSize = ReadRegionSize(resolved / "manifest.json");
        if (candidateSize > 0.0f) { absolute = resolved; size = candidateSize; break; }
    }
    std::scoped_lock lock(impl_->mutex);
    impl_->folder = absolute;
    impl_->cacheLimitBytes = (std::max)(cacheLimitBytes, 256ull * 1024ull * 1024ull);
    impl_->regionSize = size;
    impl_->manifestValid = size > 0.0f;
    impl_->initialized = impl_->manifestValid;
    impl_->stop = false;
    impl_->status = impl_->initialized ? "Índice de colisão carregado; regiões sob procura"
                                       : "manifest.json de colisão ausente ou inválido";
    if (impl_->initialized) impl_->loader = std::thread([this] { impl_->LoaderLoop(); });
    return impl_->initialized;
}

void World::Shutdown() {
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->stop = true; impl_->wake.notify_all();
    }
    if (impl_->loader.joinable()) impl_->loader.join();
    std::scoped_lock lock(impl_->mutex);
    impl_->cache.clear(); impl_->queue.clear(); impl_->queued.clear(); impl_->unavailable.clear();
    impl_->cacheBytes = 0; impl_->initialized = false; impl_->manifestValid = false;
    impl_->regionSize = 0.0f; impl_->stop = false;
}

void World::RequestAround(Vec3 position, int radius) {
    std::scoped_lock lock(impl_->mutex);
    if (!impl_->initialized || !Finite(position)) return;
    const int cx = static_cast<int>(std::floor(position.x / impl_->regionSize));
    const int cy = static_cast<int>(std::floor(position.y / impl_->regionSize));
    radius = std::clamp(radius, 0, 2);
    for (int y = cy - radius; y <= cy + radius; ++y)
        for (int x = cx - radius; x <= cx + radius; ++x)
            impl_->QueueLocked(CellKey(x, y));
}

TraceResult World::TraceSegment(Vec3 from, Vec3 to) {
    std::vector<std::shared_ptr<Region>> regions;
    bool missing = false;
    {
        std::scoped_lock lock(impl_->mutex);
        if (!impl_->initialized) return {};
        for (const auto key : CellsAlongRay(from, to, impl_->regionSize)) {
            const auto found = impl_->cache.find(key);
            if (found == impl_->cache.end()) {
                impl_->QueueLocked(key); missing = true;
            } else {
                found->second.lastUse = ++impl_->useCounter;
                regions.push_back(found->second.region);
            }
        }
        if (missing) ++impl_->missingQueries;
    }
    RayHit closest{};
    for (const auto& region : regions) {
        const RayHit hit = region->TraceRay(from, to);
        if (hit.hit && (!closest.hit || hit.fraction < closest.fraction)) closest = hit;
    }
    if (closest.hit) return {TraceState::Blocked, closest};
    return {missing ? TraceState::MissingRegion : TraceState::Clear, {}};
}

Diagnostics World::GetDiagnostics() const {
    std::scoped_lock lock(impl_->mutex);
    return {impl_->initialized, impl_->manifestValid,
            static_cast<std::uint32_t>(impl_->cache.size()),
            static_cast<std::uint32_t>(impl_->queue.size()), impl_->cacheBytes,
            impl_->cacheLimitBytes, impl_->successfulLoads, impl_->failedLoads,
            impl_->missingQueries, impl_->regionSize, impl_->status};
}

}
