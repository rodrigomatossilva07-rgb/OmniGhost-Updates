#include "cs2_collision_mesh.h"
#include <cmath>
#include <fstream>
namespace CS2::Trajectory {
bool LoadTriFile(const std::filesystem::path& path, CollisionMesh& output) {
    output = {}; output.source = path; std::error_code ec; const auto bytes = std::filesystem::file_size(path, ec);
    constexpr std::uintmax_t record = sizeof(float) * 9;
    if (ec || !bytes || bytes % record) { output.error = "Ficheiro .tri inválido"; return false; }
    const auto count = bytes / record; if (count > 20'000'000) { output.error = "Mapa demasiado grande"; return false; }
    std::ifstream in(path, std::ios::binary); if (!in) { output.error = "Não foi possível abrir o .tri"; return false; }
    output.triangles.resize(static_cast<size_t>(count)); in.read(reinterpret_cast<char*>(output.triangles.data()), static_cast<std::streamsize>(bytes));
    if (!in) { output = {}; output.source = path; output.error = "Leitura incompleta"; return false; }
    for (const auto& t : output.triangles) { const auto* v = reinterpret_cast<const float*>(&t); for (int i=0;i<9;++i) if (!std::isfinite(v[i])) { output = {}; output.source = path; output.error = "Coordenadas inválidas"; return false; } }
    output.loaded = true; return true;
}
}
