#pragma once

namespace Rust {
struct Runtime;
struct Config;
}

namespace Rust::ESP {
void Draw(const Runtime& snapshot, const Config& settings);
}
