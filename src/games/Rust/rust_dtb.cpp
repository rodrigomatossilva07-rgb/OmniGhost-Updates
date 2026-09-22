#include "rust_dtb.h"
#include "../DMALibrary/Memory/Memory.h"
#include <iostream>

extern Memory mem;

namespace Rust {
namespace Dtb {

Result EnsureGameAssembly(const char* process_name) {
    Result r{};
    if (!process_name || !*process_name)
        process_name = "RustClient.exe";

    std::cout << "[Rust][DTB] ensuring GameAssembly for process=" << process_name << "\n";

    // Bind process (Init runs FixCr3 internally on validated bind).
    if (!mem.Init(process_name, false, false)) {
        r.detail = mem.last_attach_result == Memory::AttachResult::Waiting
            ? "Processo ok | memoria ainda a preparar (DTB)"
            : "Falha a associar processo / DTB";
        std::cout << "[Rust][DTB] Init bind failed: " << r.detail << "\n";
        return r;
    }

    uintptr_t ga = static_cast<uintptr_t>(mem.GetBaseDaddy("GameAssembly.dll"));

    r.attempts = 1;
    r.game_assembly = ga;
    if (ga && ga >= 0x10000) {
        r.ok = true;
        r.detail = "GameAssembly OK";
        std::cout << "[Rust][DTB] GameAssembly=0x" << std::hex << ga << std::dec << "\n";
    } else {
        r.detail = "GameAssembly nao mapeado apos DTB/CR3 — abre o Rust e confirma FPGA";
        std::cout << "[Rust][DTB] FAIL: GameAssembly not mapped\n";
    }
    return r;
}

} // namespace Dtb
} // namespace Rust
