// Cached batched spectator pipeline.
static void UpdateSpectators(const Config& frame_config,
                             const uintptr_t* controllers, int count) {
    runtime.spectators.clear();
    runtime.spectator_count = 0;
    runtime.spectator_target = runtime.local_pawn;
    runtime.spectator_target_name[0] = '\0';
    if (!frame_config.spectator_list || !runtime.local_pawn ||
        !offsets.m_pObserverServices || !offsets.m_hObserverTarget)
        return;

    static std::vector<Player> cachedSpectators;
    static uintptr_t cachedTarget = 0;
    static uintptr_t cachedLocalPawn = 0;
    static bool cachedLocalDead = false;
    static uint64_t nextRefreshMs = 0;
    static uint64_t cachedGeneration = 0;
    const uint64_t generation = g_session_generation.load(std::memory_order_acquire);
    if (cachedGeneration != generation) {
        cachedSpectators.clear();
        cachedTarget = 0;
        cachedLocalPawn = 0;
        cachedLocalDead = false;
        nextRefreshMs = 0;
        cachedGeneration = generation;
    }
    const uint64_t now = GetTickCount64();
    const bool localDead = runtime.local_health <= 0;
    if (cachedLocalPawn == runtime.local_pawn && cachedLocalDead == localDead &&
        (now < nextRefreshMs || g_pressure_level.load(std::memory_order_relaxed) >= 2)) {
        runtime.spectator_target = cachedTarget;
        runtime.spectators = cachedSpectators;
        runtime.spectator_count = static_cast<int>(runtime.spectators.size());
        return;
    }
    if (g_pressure_level.load(std::memory_order_relaxed) >= 2)
        return; // no cached target for this pawn; shed optional DMA entirely
    // The target and list are one optional 500 ms lane; do not read the
    // observer pointer chain on every 18 ms core scan while dead.
    CS2::GiveCameraPriorityBeforeOptionalDma();
    ScopedPhase _phSpectators(&g_phase.spectator_ms);
    g_phase.spectator_refresh = 1;
    if (localDead) {
        uintptr_t localServices = 0;
        uint32_t watchedHandle = 0;
        if (QReadT(runtime.local_pawn + offsets.m_pObserverServices, localServices) &&
            IsUserPointer(localServices) &&
            QReadT(localServices + offsets.m_hObserverTarget, watchedHandle)) {
            const uintptr_t watched = ResolveEntityByHandle(
                watchedHandle, g_pawn_stride ? g_pawn_stride : kEntityIdentityStride);
            if (IsUserPointer(watched)) runtime.spectator_target = watched;
        }
    }

    // Fetch every observer handle in one DMA trip. The old path made one
    // blocking QRead per controller before even knowing who was spectating.
    constexpr int kSpectatorSlots = 64;
    uint32_t observerHandles[kSpectatorSlots]{};
    EnsureScatter();
    if (!g_scatter_full || !offsets.m_hObserverPawn) {
        nextRefreshMs = now + 500u;
        return; // optional UI lane must not fall back to 64 synchronous reads
    }
    const int spectatorControllerCount = (std::min)(count, kSpectatorSlots);
    bool queuedObservers = false;
    for (int i = 0; i < spectatorControllerCount; ++i) {
        if (!IsUserPointer(controllers[i]) || controllers[i] == runtime.local_controller)
            continue;
        mem.AddScatterReadRequest(g_scatter_full,
            controllers[i] + offsets.m_hObserverPawn,
            &observerHandles[i], sizeof(observerHandles[i]));
        queuedObservers = true;
    }
    if (queuedObservers) {
        CS2::GiveCameraPriorityBeforeOptionalDma();
        mem.SetDmaCallTag("CS2.Spectators.Handles");
        mem.ExecuteReadScatter(g_scatter_full);
    }
    uintptr_t observerPawns[kSpectatorSlots]{};
    if (queuedObservers) {
        const uintptr_t primaryStride = g_pawn_stride ? g_pawn_stride : kEntityIdentityStride;
        const uintptr_t alternateStride = (primaryStride == kEntityIdentityStride)
            ? kLegacyEntityIdentityStride : kEntityIdentityStride;
        ScatterResolvePawnHandles(observerHandles, observerPawns,
            spectatorControllerCount, primaryStride, alternateStride, true);
    }
    uintptr_t observerServices[kSpectatorSlots]{};
    bool queuedServices = false;
    for (int i = 0; i < spectatorControllerCount; ++i) {
        if (!IsUserPointer(observerPawns[i])) continue;
        mem.AddScatterReadRequest(g_scatter_full,
            observerPawns[i] + offsets.m_pObserverServices,
            &observerServices[i], sizeof(observerServices[i]));
        queuedServices = true;
    }
    if (queuedServices) {
        CS2::GiveCameraPriorityBeforeOptionalDma();
        mem.SetDmaCallTag("CS2.Spectators.Services");
        mem.ExecuteReadScatter(g_scatter_full);
    }
    uint32_t targetHandles[kSpectatorSlots]{};
    bool queuedTargets = false;
    for (int i = 0; i < spectatorControllerCount; ++i) {
        if (!IsUserPointer(observerServices[i])) continue;
        mem.AddScatterReadRequest(g_scatter_full,
            observerServices[i] + offsets.m_hObserverTarget,
            &targetHandles[i], sizeof(targetHandles[i]));
        queuedTargets = true;
    }
    if (queuedTargets) {
        CS2::GiveCameraPriorityBeforeOptionalDma();
        mem.SetDmaCallTag("CS2.Spectators.Targets");
        mem.ExecuteReadScatter(g_scatter_full);
    }
    uintptr_t targetPawns[kSpectatorSlots]{};
    if (queuedTargets) {
        const uintptr_t primaryStride = g_pawn_stride ? g_pawn_stride : kEntityIdentityStride;
        const uintptr_t alternateStride = (primaryStride == kEntityIdentityStride)
            ? kLegacyEntityIdentityStride : kEntityIdentityStride;
        ScatterResolvePawnHandles(targetHandles, targetPawns,
            spectatorControllerCount, primaryStride, alternateStride, true);
    }

    char spectatorNames[kSpectatorSlots][64]{};
    uint64_t spectatorSteamIds[kSpectatorSlots]{};
    bool queuedIdentity = false;
    for (int i = 0; i < spectatorControllerCount; ++i) {
        if (!IsUserPointer(controllers[i]) ||
            !IsUserPointer(observerPawns[i]) ||
            targetPawns[i] != runtime.spectator_target)
            continue;
        mem.AddScatterReadRequest(g_scatter_full,
            controllers[i] + offsets.m_iszPlayerName,
            spectatorNames[i], sizeof(spectatorNames[i]) - 1);
        if (offsets.m_steamID)
            mem.AddScatterReadRequest(g_scatter_full,
                controllers[i] + offsets.m_steamID,
                &spectatorSteamIds[i], sizeof(spectatorSteamIds[i]));
        queuedIdentity = true;
    }
    if (queuedIdentity) {
        CS2::GiveCameraPriorityBeforeOptionalDma();
        mem.SetDmaCallTag("CS2.Spectators.Identity");
        mem.ExecuteReadScatter(g_scatter_full);
    }
    for (int i = 0; i < spectatorControllerCount; ++i) {
        const uintptr_t controller = controllers[i];
        if (!IsUserPointer(controller) || controller == runtime.local_controller)
            continue;
        const uint32_t observerHandle = observerHandles[i];
        if (!observerHandle || observerHandle == 0xFFFFFFFFu)
            continue;
        const uintptr_t observerPawn = observerPawns[i];
        if (!IsUserPointer(observerPawn) ||
            targetPawns[i] != runtime.spectator_target)
            continue;

        Player spectator{};
        spectator.controller = controller;
        spectator.pawn = observerPawn;
        spectator.is_spectator = true;
        spectator.alive = false;
        spectator.steam_id = spectatorSteamIds[i];
        std::memcpy(spectator.name, spectatorNames[i], sizeof(spectator.name));
        spectator.name[sizeof(spectator.name) - 1] = '\0';
        if (!spectator.name[0])
            std::snprintf(spectator.name, sizeof(spectator.name), "Jogador_%d", i + 1);
        runtime.spectators.push_back(spectator);
    }
    cachedSpectators = runtime.spectators;
    cachedTarget = runtime.spectator_target;
    cachedLocalPawn = runtime.local_pawn;
    cachedLocalDead = localDead;
    nextRefreshMs = now + 500u;
    runtime.spectator_count = static_cast<int>(runtime.spectators.size());
}


