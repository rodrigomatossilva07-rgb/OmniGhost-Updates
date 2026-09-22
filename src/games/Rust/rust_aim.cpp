#include "rust_aim.h"
#include "rust_entities.h"

namespace Rust {
namespace Aim {

void Run(const Runtime& rt, const Config& cfg) {
    if (!cfg.aim_enabled || !rt.attached || !rt.matrix_ok)
        return;
    // Stub: aim pipeline hook for Makcu/KMBox integration (same path as CS2).
    // Target selection by FOV against Entities::Players() goes here.
    (void)rt;
    (void)cfg;
}

} // namespace Aim
} // namespace Rust
