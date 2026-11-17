#include "igame_module.h"
#include "Cs2/cs2_game_module.h"

namespace OmniGhost::Games {

// Force registration of game modules at static initialization time
namespace {
    struct GameModuleRegistrar {
        GameModuleRegistrar() {
            // Register CS2 module
            GameModuleRegistry::Instance().RegisterFactory(
                std::make_unique<GameModuleFactoryImpl<CS2GameModule>>()
            );
        }
    };

    // Static instance to trigger registration
    static GameModuleRegistrar g_gameModuleRegistrar;
}

} // namespace OmniGhost::Games