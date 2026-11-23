#include "igame_module.h"
#include "Cs2/cs2_game_module.h"

namespace OmniGhost::Games {


namespace {
    struct GameModuleRegistrar {
        GameModuleRegistrar() {

            GameModuleRegistry::Instance().RegisterFactory(
                std::make_unique<GameModuleFactoryImpl<CS2GameModule>>()
            );
        }
    };


    static GameModuleRegistrar g_gameModuleRegistrar;
}

}