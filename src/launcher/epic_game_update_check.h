#pragma once
#include "launcher_data.h"
#include <string>
namespace Launcher::EpicGameUpdateCheck {
struct Status { bool checking{}; bool updatePending{}; bool installed{}; std::string detail; };
void StartForLibrary();
Status Get(GameId game);
}
