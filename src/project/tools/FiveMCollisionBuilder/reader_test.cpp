#include "fivem_collision_world.h"

#include <filesystem>
#include <iostream>
#include <chrono>
#include <thread>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "uso: FiveMCollisionReaderTest <região.ogcol>\n";
        return 2;
    }
    FiveM::Collision::Region region;
    if (!region.Load(std::filesystem::path(argv[1]))) {
        std::cerr << "falha: " << region.Error() << '\n';
        return 1;
    }
    if (!region.Ready() || region.RegionSize() < 128.0f) {
        std::cerr << "região carregada com metadados inválidos\n";
        return 1;
    }
    (void)region.TraceRay({0.0f, 0.0f, -2000.0f}, {0.0f, 0.0f, 4000.0f});
    auto& world = FiveM::Collision::World::Instance();
    if (!world.Initialize(std::filesystem::path(argv[1]).parent_path())) {
        std::cerr << "falha ao inicializar o índice regional\n";
        return 1;
    }
    const float x = (static_cast<float>(region.CellX()) + 0.5f) * region.RegionSize();
    const float y = (static_cast<float>(region.CellY()) + 0.5f) * region.RegionSize();
    world.RequestAround({x, y, 0.0f});
    for (int attempt = 0; attempt < 200 && world.GetDiagnostics().cachedRegions == 0; ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto diagnostics = world.GetDiagnostics();
    if (!diagnostics.initialized || diagnostics.cachedRegions == 0 || diagnostics.successfulLoads == 0) {
        std::cerr << "cache regional assíncrona não carregou a célula pedida: " << diagnostics.status << '\n';
        return 1;
    }
    (void)world.TraceSegment({x, y, -2000.0f}, {x, y, 4000.0f});
    world.Shutdown();
    std::cout << "OK: célula " << region.CellX() << ',' << region.CellY()
              << " tamanho " << region.RegionSize() << '\n';
    return 0;
}
