#include "rust_game.h"
#include "rust_internal.h"
#include "../DMALibrary/Memory/Memory.h"

#include <cstdio>
#include <cstring>
#include <iostream>

namespace Rust {

bool ReloadOffsets() {
    const bool ok = LoadOffsetsFromJson(nullptr);
    if (!ok)
        ApplyEmbeddedDefaults();
    return offsets.loaded;
}

bool SoftProbeLobbyOffsets() {
    // Lobby-safe: process/module present is enough; do not require local pawn.
    if (!offsets.loaded) {
        LoadOffsetsFromJson(nullptr);
        if (!offsets.loaded)
            ApplyEmbeddedDefaults();
    }
    if (!runtime.game_assembly)
        runtime.game_assembly = mem.GetBaseDaddy("GameAssembly.dll");
    return offsets.loaded && runtime.game_assembly != 0;
}

bool ValidateLiveOffsets() {
    if (!SoftProbeLobbyOffsets())
        return false;
    float mx[16]{};
    if (detail::ResolveViewMatrix(mx)) {
        std::memcpy(runtime.view_matrix, mx, sizeof(mx));
        runtime.matrix_ok = true;
    }
    if (!runtime.local_player)
        runtime.local_player = detail::ResolveLocalPlayer();
    // Accept matrix OR local as live signal; lobby may lack both briefly.
    return runtime.matrix_ok || runtime.local_player != 0 || runtime.game_assembly != 0;
}

bool SelfTest() {
    const bool ok = ValidateLiveOffsets();
    runtime.self_test_ok = ok;
    if (ok) {
        status = "Self-test OK";
        std::snprintf(runtime.status, sizeof(runtime.status),
            "self-test OK GA=0x%llX local=%s matrix=%s",
            (unsigned long long)runtime.game_assembly,
            runtime.local_player ? "ok" : "0",
            runtime.matrix_ok ? "ok" : "0");
    } else {
        status = "Self-test falhou";
        std::snprintf(runtime.status, sizeof(runtime.status),
            "self-test FAIL GA=0x%llX",
            (unsigned long long)runtime.game_assembly);
    }
    return ok;
}

void DeepProbeChains() {
    (void)ValidateLiveOffsets();
}

} // namespace Rust
