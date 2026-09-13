#pragma once
#include <cstdint>
#include <string>

namespace Rust {
namespace Dtb {

struct Result {
    bool ok = false;
    uint64_t dtb = 0;
    uintptr_t game_assembly = 0;
    int attempts = 0;
    std::string detail;
};

// Ensures GameAssembly.dll is resolvable for the bound process.
// Prefers Memory::FixCr3() (already integrated in DMALibrary), then
// explicit module resolution. Safe to call multiple times.
Result EnsureGameAssembly(const char* process_name = "RustClient.exe");

} // namespace Dtb
} // namespace Rust
