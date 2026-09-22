#pragma once
#include "rust_game.h"
#include <vector>

namespace Rust {
namespace Entities {

bool Refresh(); // update runtime players + camera
const std::vector<Player>& Players();
void Clear();

} // namespace Entities
} // namespace Rust
