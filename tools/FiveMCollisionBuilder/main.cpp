#include <ragekit/ragekit.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr std::array<char, 8> kMagic{'O', 'G', 'C', 'O', 'L', '1', '\0', '\0'};
constexpr std::uint32_t kFormatVersion = 1;
constexpr std::uint32_t kLeafSize = 12;

struct Vec3 {
    float x{}, y{}, z{};
};

struct Triangle {
    Vec3 a{}, b{}, c{};
};

struct Node {
    Vec3 min{}, max{};
    std::uint32_t begin{}, count{}, left{}, right{};
};

struct CellKey {
    int x{}, y{};
    bool operator<(const CellKey& other) const noexcept {
        return std::tie(x, y) < std::tie(other.x, other.y);
    }
};

struct BuildOptions {
    std::string gameDir;
    fs::path output;
    float regionSize = 512.0f;
    int primitiveSegments = 8;
    bool includeInteriors = true;
    bool includeHighDetail = true;
    bool includeSeeThrough = false;
    bool singlePlayerMap = false;
    std::optional<std::array<float, 4>> area;
};

struct Counters {
    std::uint64_t boundsRead{};
    std::uint64_t boundsSkipped{};
    std::uint64_t mloInstances{};
    std::uint64_t sourceTriangles{};
    std::uint64_t transparentTriangles{};
    std::uint64_t storedTriangles{};
    std::uint64_t errors{};
};

struct RegionResult {
    CellKey key;
    fs::path file;
    std::uint64_t bytes{};
    std::uint32_t triangles{};
    std::uint32_t nodes{};
    Vec3 min{}, max{};
};

[[noreturn]] void Usage(const char* exe, const std::string& error = {}) {
    if (!error.empty()) std::cerr << "erro: " << error << "\n\n";
    std::cerr
        << "Uso:\n  " << exe << " --output <pasta> [opções]\n\n"
        << "Opções:\n"
        << "  --game <pasta>          Instalação GTA V (deteção automática por omissão)\n"
        << "  --region-size <metros>  Tamanho das regiões, 128..2048 (padrão 512)\n"
        << "  --bounds x0 y0 x1 y1    Exportar apenas uma área de teste\n"
        << "  --segments <n>          Segmentos de primitivas, 6..24 (padrão 8)\n"
        << "  --no-interiors          Não instanciar colisões MLO\n"
        << "  --no-high-detail        Ignorar colisão hi@\n"
        << "  --include-see-through   Manter materiais marcados SEE_THROUGH\n"
        << "  --sp                    Usar mapa single-player em vez do mapa MP/FiveM\n";
    std::exit(error.empty() ? 0 : 2);
}

float ParseFloat(const std::string& text, const char* name) {
    std::size_t used = 0;
    const float value = std::stof(text, &used);
    if (used != text.size() || !std::isfinite(value)) throw std::runtime_error(std::string("valor inválido para ") + name);
    return value;
}

int ParseInt(const std::string& text, const char* name) {
    std::size_t used = 0;
    const int value = std::stoi(text, &used);
    if (used != text.size()) throw std::runtime_error(std::string("valor inválido para ") + name);
    return value;
}

BuildOptions ParseArgs(int argc, char** argv) {
    BuildOptions o;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (++i >= argc) Usage(argv[0], arg + " requer um valor");
            return argv[i];
        };
        if (arg == "--help" || arg == "-h") Usage(argv[0]);
        else if (arg == "--game") o.gameDir = next();
        else if (arg == "--output") o.output = fs::path(next());
        else if (arg == "--region-size") o.regionSize = ParseFloat(next(), "--region-size");
        else if (arg == "--segments") o.primitiveSegments = ParseInt(next(), "--segments");
        else if (arg == "--bounds") {
            std::array<float, 4> box{};
            for (float& value : box) value = ParseFloat(next(), "--bounds");
            if (box[0] > box[2]) std::swap(box[0], box[2]);
            if (box[1] > box[3]) std::swap(box[1], box[3]);
            o.area = box;
        } else if (arg == "--no-interiors") o.includeInteriors = false;
        else if (arg == "--no-high-detail") o.includeHighDetail = false;
        else if (arg == "--include-see-through") o.includeSeeThrough = true;
        else if (arg == "--sp") o.singlePlayerMap = true;
        else Usage(argv[0], "opção desconhecida: " + arg);
    }
    if (o.output.empty()) Usage(argv[0], "--output é obrigatório");
    if (o.regionSize < 128.0f || o.regionSize > 2048.0f) Usage(argv[0], "--region-size deve estar entre 128 e 2048");
    if (o.primitiveSegments < 6 || o.primitiveSegments > 24) Usage(argv[0], "--segments deve estar entre 6 e 24");
    if (o.gameDir.empty()) o.gameDir = rk::GameIndex::default_game_dir(rk::Game::Legacy);
    if (o.gameDir.empty()) Usage(argv[0], "instalação GTA V Legacy não encontrada; usa --game");
    return o;
}

Vec3 Convert(const rk::Vec3& v) { return {v.x, v.y, v.z}; }

Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

bool Finite(const Triangle& t) {
    for (const Vec3 p : {t.a, t.b, t.c})
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
    const Vec3 n = Cross(Sub(t.b, t.a), Sub(t.c, t.a));
    return Dot(n, n) > 1.0e-10f;
}

bool Intersects2D(const Triangle& t, const std::array<float, 4>& a) {
    const float mnx = (std::min)({t.a.x, t.b.x, t.c.x});
    const float mny = (std::min)({t.a.y, t.b.y, t.c.y});
    const float mxx = (std::max)({t.a.x, t.b.x, t.c.x});
    const float mxy = (std::max)({t.a.y, t.b.y, t.c.y});
    return mxx >= a[0] && mnx <= a[2] && mxy >= a[1] && mny <= a[3];
}

bool BoxIntersects2D(const rk::Vec3& mn, const rk::Vec3& mx, const std::array<float, 4>& a) {
    return mx.x >= a[0] && mn.x <= a[2] && mx.y >= a[1] && mn.y <= a[3];
}

class RegionCollector {
public:
    RegionCollector(float size, std::optional<std::array<float, 4>> area) : size_(size), area_(area) {}

    void Add(const Triangle& t) {
        if (!Finite(t) || (area_ && !Intersects2D(t, *area_))) return;
        const float mnx = (std::min)({t.a.x, t.b.x, t.c.x});
        const float mny = (std::min)({t.a.y, t.b.y, t.c.y});
        const float mxx = (std::max)({t.a.x, t.b.x, t.c.x});
        const float mxy = (std::max)({t.a.y, t.b.y, t.c.y});
        const int x0 = static_cast<int>(std::floor(mnx / size_));
        const int y0 = static_cast<int>(std::floor(mny / size_));
        const int x1 = static_cast<int>(std::floor(mxx / size_));
        const int y1 = static_cast<int>(std::floor(mxy / size_));
        if (x1 - x0 > 64 || y1 - y0 > 64) return; // malformed/astronomical source polygon
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                regions_[{x, y}].push_back(t);
    }

    std::map<CellKey, std::vector<Triangle>>& Regions() { return regions_; }

private:
    float size_;
    std::optional<std::array<float, 4>> area_;
    std::map<CellKey, std::vector<Triangle>> regions_;
};

class BvhBuilder {
public:
    explicit BvhBuilder(const std::vector<Triangle>& triangles) : triangles_(triangles) {
        indices_.resize(triangles.size());
        std::iota(indices_.begin(), indices_.end(), 0u);
        if (!indices_.empty()) BuildNode(0, static_cast<std::uint32_t>(indices_.size()));
    }

    const std::vector<Node>& Nodes() const { return nodes_; }
    const std::vector<std::uint32_t>& Indices() const { return indices_; }

private:
    std::uint32_t BuildNode(std::uint32_t begin, std::uint32_t end) {
        Node n;
        n.min = {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
        n.max = {-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
        for (std::uint32_t i = begin; i < end; ++i) {
            const Triangle& t = triangles_[indices_[i]];
            for (const Vec3 p : {t.a, t.b, t.c}) {
                n.min.x = (std::min)(n.min.x, p.x); n.min.y = (std::min)(n.min.y, p.y); n.min.z = (std::min)(n.min.z, p.z);
                n.max.x = (std::max)(n.max.x, p.x); n.max.y = (std::max)(n.max.y, p.y); n.max.z = (std::max)(n.max.z, p.z);
            }
        }
        const std::uint32_t id = static_cast<std::uint32_t>(nodes_.size());
        nodes_.push_back(n);
        if (end - begin <= kLeafSize) {
            nodes_[id].begin = begin;
            nodes_[id].count = end - begin;
            return id;
        }
        const Vec3 span = Sub(n.max, n.min);
        int axis = span.y > span.x ? 1 : 0;
        if ((&span.x)[2] > (&span.x)[axis]) axis = 2;
        const std::uint32_t middle = begin + (end - begin) / 2;
        std::nth_element(indices_.begin() + begin, indices_.begin() + middle, indices_.begin() + end,
            [&](std::uint32_t lhs, std::uint32_t rhs) {
                const Triangle& a = triangles_[lhs];
                const Triangle& b = triangles_[rhs];
                return ((&a.a.x)[axis] + (&a.b.x)[axis] + (&a.c.x)[axis]) <
                       ((&b.a.x)[axis] + (&b.b.x)[axis] + (&b.c.x)[axis]);
            });
        const std::uint32_t left = BuildNode(begin, middle);
        const std::uint32_t right = BuildNode(middle, end);
        nodes_[id].left = left;
        nodes_[id].right = right;
        return id;
    }

    const std::vector<Triangle>& triangles_;
    std::vector<std::uint32_t> indices_;
    std::vector<Node> nodes_;
};

template <typename T>
void WriteValue(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), static_cast<std::streamsize>(sizeof(value)));
    if (!out) throw std::runtime_error("falha ao gravar ficheiro de colisão");
}

void WriteVec3(std::ofstream& out, const Vec3& value) {
    WriteValue(out, value.x); WriteValue(out, value.y); WriteValue(out, value.z);
}

void WriteTriangle(std::ofstream& out, const Triangle& value) {
    WriteVec3(out, value.a); WriteVec3(out, value.b); WriteVec3(out, value.c);
}

void WriteNode(std::ofstream& out, const Node& value) {
    WriteVec3(out, value.min); WriteVec3(out, value.max);
    WriteValue(out, value.begin); WriteValue(out, value.count);
    WriteValue(out, value.left); WriteValue(out, value.right);
}

RegionResult WriteRegion(const fs::path& folder, CellKey key, float regionSize, const std::vector<Triangle>& triangles) {
    BvhBuilder bvh(triangles);
    if (bvh.Nodes().empty()) throw std::runtime_error("BVH vazio");
    const Node& root = bvh.Nodes().front();
    std::ostringstream name;
    name << "region_" << key.x << '_' << key.y << ".ogcol";
    const fs::path finalPath = folder / name.str();
    const fs::path temporary = finalPath.string() + ".tmp";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("não foi possível criar " + temporary.string());
    out.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
    WriteValue(out, kFormatVersion);
    WriteValue(out, key.x); WriteValue(out, key.y); WriteValue(out, regionSize);
    const std::uint32_t triangleCount = static_cast<std::uint32_t>(triangles.size());
    const std::uint32_t nodeCount = static_cast<std::uint32_t>(bvh.Nodes().size());
    const std::uint32_t indexCount = static_cast<std::uint32_t>(bvh.Indices().size());
    WriteValue(out, triangleCount); WriteValue(out, nodeCount); WriteValue(out, indexCount);
    WriteVec3(out, root.min); WriteVec3(out, root.max);
    for (const Triangle& triangle : triangles) WriteTriangle(out, triangle);
    for (const Node& node : bvh.Nodes()) WriteNode(out, node);
    for (const std::uint32_t index : bvh.Indices()) WriteValue(out, index);
    out.close();
    if (!out) throw std::runtime_error("gravação incompleta: " + temporary.string());
    std::error_code ec;
    fs::remove(finalPath, ec);
    fs::rename(temporary, finalPath, ec);
    if (ec) throw std::runtime_error("não foi possível finalizar " + finalPath.string() + ": " + ec.message());
    return {key, finalPath.filename(), fs::file_size(finalPath), triangleCount, nodeCount, root.min, root.max};
}

std::string JsonEscape(const std::string& value) {
    std::ostringstream out;
    for (const unsigned char c : value) {
        switch (c) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
            else out << static_cast<char>(c);
        }
    }
    return out.str();
}

void WriteManifest(const BuildOptions& options, const rk::GameIndex& gi, const Counters& counters,
                   const std::vector<RegionResult>& regions) {
    const fs::path path = options.output / "manifest.json";
    std::ofstream out(path, std::ios::trunc);
    if (!out) throw std::runtime_error("não foi possível criar " + path.string());
    out << "{\n"
        << "  \"format\": \"OmniGhost FiveM Collision\",\n"
        << "  \"version\": " << kFormatVersion << ",\n"
        << "  \"game\": \"GTA V Legacy\",\n"
        << "  \"game_version\": \"" << JsonEscape(gi.info().exe_version) << "\",\n"
        << "  \"index_key\": \"" << JsonEscape(gi.info().key) << "\",\n"
        << "  \"region_size\": " << options.regionSize << ",\n"
        << "  \"primitive_segments\": " << options.primitiveSegments << ",\n"
        << "  \"map_mode\": \"" << (options.singlePlayerMap ? "single-player" : "multiplayer-fivem") << "\",\n"
        << "  \"includes_interiors\": " << (options.includeInteriors ? "true" : "false") << ",\n"
        << "  \"includes_high_detail\": " << (options.includeHighDetail ? "true" : "false") << ",\n"
        << "  \"bounds_read\": " << counters.boundsRead << ",\n"
        << "  \"mlo_instances\": " << counters.mloInstances << ",\n"
        << "  \"source_triangles\": " << counters.sourceTriangles << ",\n"
        << "  \"stored_triangles\": " << counters.storedTriangles << ",\n"
        << "  \"regions\": [\n";
    for (std::size_t i = 0; i < regions.size(); ++i) {
        const RegionResult& r = regions[i];
        out << "    {\"x\":" << r.key.x << ",\"y\":" << r.key.y
            << ",\"file\":\"" << JsonEscape(r.file.string()) << "\",\"triangles\":" << r.triangles
            << ",\"nodes\":" << r.nodes << ",\"bytes\":" << r.bytes
            << ",\"min\":[" << r.min.x << ',' << r.min.y << ',' << r.min.z << "]"
            << ",\"max\":[" << r.max.x << ',' << r.max.y << ',' << r.max.z << "]}";
        if (i + 1 != regions.size()) out << ',';
        out << '\n';
    }
    out << "  ]\n}\n";
}

Triangle TransformMlo(const rk::BoundTriangle& source, const rk::WorldMloInstance& instance) {
    Triangle out;
    Vec3* destination[3]{&out.a, &out.b, &out.c};
    for (int i = 0; i < 3; ++i) {
        const rk::Vec3 rotated = rk::rotate(instance.orientation, source.v[i]);
        *destination[i] = {rotated.x + instance.position.x, rotated.y + instance.position.y, rotated.z + instance.position.z};
    }
    return out;
}

Triangle ConvertTriangle(const rk::BoundTriangle& source) {
    return {Convert(source.v[0]), Convert(source.v[1]), Convert(source.v[2])};
}

bool Opaque(const rk::BoundTriangle& triangle, const BuildOptions& options) {
    return options.includeSeeThrough || (triangle.material.flags() & rk::kMatSeeThrough) == 0;
}

void AddTriangles(const std::vector<rk::BoundTriangle>& source, RegionCollector& collector,
                  const BuildOptions& options, Counters& counters,
                  const rk::WorldMloInstance* instance = nullptr) {
    for (const rk::BoundTriangle& triangle : source) {
        ++counters.sourceTriangles;
        if (!Opaque(triangle, options)) { ++counters.transparentTriangles; continue; }
        collector.Add(instance ? TransformMlo(triangle, *instance) : ConvertTriangle(triangle));
    }
}

int Run(const BuildOptions& options) {
    std::cout << "FiveM Collision Builder\n"
              << "  GTA: " << options.gameDir << "\n"
              << "  saída: " << options.output.string() << "\n";
    fs::create_directories(options.output);

    rk::GameIndexOptions gio;
    gio.progress = [](const std::string& text) { std::cerr << "[índice] " << text << '\n'; };
    const rk::GameIndex gi = rk::GameIndex::open(options.gameDir, gio);
    rk::WorldIndexOptions wio;
    wio.sp_map = options.singlePlayerMap;
    wio.progress = [](const std::string& text) { std::cerr << "[mundo] " << text << '\n'; };
    const rk::WorldIndex world = rk::WorldIndex::open(gi, wio);

    RegionCollector collector(options.regionSize, options.area);
    Counters counters;
    std::vector<std::uint32_t> selected;
    if (options.area) {
        const auto& a = *options.area;
        selected = world.bounds_in({a[0], a[1], -2000.0f}, {a[2], a[3], 4000.0f}, options.includeInteriors);
    } else {
        selected.resize(world.bounds().size());
        std::iota(selected.begin(), selected.end(), 0u);
    }

    std::unordered_map<std::uint32_t, std::vector<rk::BoundTriangle>> interiorTriangles;
    std::set<std::uint32_t> wantedInteriors;
    if (options.includeInteriors) {
        for (const rk::WorldMloInstance& instance : world.mlo_instances()) {
            if (!options.area || BoxIntersects2D(instance.box_min, instance.box_max, *options.area))
                wantedInteriors.insert(instance.archetype);
        }
    }

    std::size_t progress = 0;
    for (const std::uint32_t index : selected) {
        if (index >= world.bounds().size()) continue;
        const rk::WorldBounds& entry = world.bounds()[index];
        const std::uint32_t layer = entry.layer & 0xFFu;
        if (layer == 2 || (!options.includeHighDetail && layer == 1)) { ++counters.boundsSkipped; continue; }
        if (entry.interior != 0 && (!options.includeInteriors || !wantedInteriors.count(entry.interior))) {
            ++counters.boundsSkipped;
            continue;
        }
        try {
            const rk::BoundPtr bound = rk::read_ybn(gi.extract(entry.file));
            if (!bound) throw std::runtime_error("YBN sem bound raiz");
            std::vector<rk::BoundTriangle> triangles = rk::bound_triangles(*bound, options.primitiveSegments);
            ++counters.boundsRead;
            if (entry.interior == 0) AddTriangles(triangles, collector, options, counters);
            else interiorTriangles[entry.interior].insert(interiorTriangles[entry.interior].end(),
                                                          std::make_move_iterator(triangles.begin()),
                                                          std::make_move_iterator(triangles.end()));
        } catch (const std::exception& e) {
            ++counters.errors;
            std::cerr << "[aviso] " << gi.path(entry.file) << ": " << e.what() << '\n';
        }
        if ((++progress % 100) == 0 || progress == selected.size())
            std::cout << "\r  YBN " << progress << '/' << selected.size() << std::flush;
    }
    std::cout << '\n';

    if (options.includeInteriors) {
        for (const rk::WorldMloInstance& instance : world.mlo_instances()) {
            if (options.area && !BoxIntersects2D(instance.box_min, instance.box_max, *options.area)) continue;
            const auto found = interiorTriangles.find(instance.archetype);
            if (found == interiorTriangles.end()) continue;
            AddTriangles(found->second, collector, options, counters, &instance);
            ++counters.mloInstances;
        }
    }

    if (collector.Regions().empty()) throw std::runtime_error("nenhuma geometria foi encontrada para a área pedida");
    std::vector<RegionResult> results;
    results.reserve(collector.Regions().size());
    for (auto& [key, triangles] : collector.Regions()) {
        if (triangles.size() > (std::numeric_limits<std::uint32_t>::max)())
            throw std::runtime_error("uma região excedeu o limite de triângulos");
        counters.storedTriangles += triangles.size();
        results.push_back(WriteRegion(options.output, key, options.regionSize, triangles));
        std::cout << "  " << results.back().file.string() << ": " << triangles.size() << " triângulos, "
                  << results.back().nodes << " nós\n";
    }
    WriteManifest(options, gi, counters, results);
    std::cout << "Concluído: " << results.size() << " regiões, " << counters.storedTriangles
              << " triângulos armazenados, " << counters.errors << " erros de origem.\n";
    return counters.errors == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return Run(ParseArgs(argc, argv));
    } catch (const std::exception& e) {
        std::cerr << "erro fatal: " << e.what() << '\n';
        return 1;
    }
}

