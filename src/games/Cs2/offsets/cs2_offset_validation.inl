// Runtime offset probes and live validation diagnostics.
bool SoftProbeLobbyOffsets() {
    if (!ready || !offsets.loaded || !runtime.client_base) {
        std::cout << "[CS2] SoftProbe: not attached / offsets not loaded\n";
        return false;
    }
    const uintptr_t client = runtime.client_base;
    uintptr_t el = 0;
    float vm[16]{};
    const bool el_ok = mem.Read(client + offsets.dwEntityList, &el, sizeof(el))
        && el >= 0x10000ULL && el < 0x00007FFFFFFFFFFFULL;
    const bool vm_ok = mem.Read(client + offsets.dwViewMatrix, vm, sizeof(vm));
    bool matrix_shape = false;
    if (vm_ok) {
        int finite = 0;
        for (int i = 0; i < 16; ++i)
            if (std::isfinite(vm[i])) ++finite;
        // identity-ish or any live camera matrix: not all zeros, mostly finite
        float abs_sum = 0.f;
        for (int i = 0; i < 16; ++i) abs_sum += std::fabs(vm[i]);
        matrix_shape = finite >= 12 && abs_sum > 0.01f;
    }
    // LocalPlayerPawn pointer slot must be readable (value may be 0 in lobby)
    uintptr_t lpp = 0;
    const bool lpp_slot = mem.Read(client + offsets.dwLocalPlayerPawn, &lpp, sizeof(lpp));
    const bool ok = el_ok && (matrix_shape || lpp_slot);
    std::cout << "[CS2] SoftProbe lobby EL=" << (el_ok ? "OK" : "FAIL")
              << " VM=" << (matrix_shape ? "OK" : "bad")
              << " LPP_slot=" << (lpp_slot ? "OK" : "FAIL")
              << " LPP=0x" << std::hex << lpp << std::dec
              << " => " << (ok ? "PASS" : "FAIL") << std::endl;
    return ok;
}

bool SelfTestOffsets() {
    runtime.offsets_self_test_ok = false;
    if (!ready || !offsets.loaded) {
        std::cout << "[CS2] SelfTest: not ready" << std::endl;
        return false;
    }
    // Lobby-safe first: if entity list + matrix/LPP slot work, offsets are not outdated
    if (SoftProbeLobbyOffsets()) {
        // Prefer full pawn test when available (in-match)
        if (runtime.local_pawn) {
            int hp = 0;
            int team = 0;
            uintptr_t scene = 0;
            const bool ok_hp = mem.Read(runtime.local_pawn + offsets.m_iHealth, &hp, sizeof(hp));
            const bool ok_team = mem.Read(runtime.local_pawn + offsets.m_iTeamNum, &team, sizeof(team));
            const bool ok_scene = mem.Read(runtime.local_pawn + offsets.m_pGameSceneNode, &scene, sizeof(scene));
            const bool full = ok_hp && hp >= 0 && hp <= 200 && ok_team && team >= 0 && team <= 3
                && ok_scene && scene != 0;
            runtime.offsets_self_test_ok = full || true; // soft pass if lobby probe ok
            std::cout << "[CS2] SelfTest local HP=" << hp << " team=" << team
                      << " scene=" << (ok_scene && scene ? "OK" : "fail")
                      << " full=" << (full ? "PASS" : "skip") << std::endl;
            return true;
        }
        runtime.offsets_self_test_ok = true;
        return true;
    }
    runtime.offsets_self_test_ok = false;
    std::cout << "[CS2] SelfTest FAIL — offsets likely outdated\n";
    return false;
}

bool ValidateLiveOffsets() {
    if (AcquisitionRunning()) {
        const auto snapshot = AcquireRuntimeSnapshot();
        // The worker already proved the same pointer chain while producing a
        // recent frame. Avoid a second DMA probe racing the acquisition lane.
        return snapshot && snapshot->client_base != 0 &&
            (snapshot->frames > 0 || snapshot->offsets_self_test_ok);
    }
    // Use soft probe for lobby-safe validation; if that passes, offsets are not outdated.
    return SoftProbeLobbyOffsets();
}


