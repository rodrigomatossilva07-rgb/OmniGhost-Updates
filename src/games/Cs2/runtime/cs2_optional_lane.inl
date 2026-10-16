
static void OptionalWorkerLoop() {
    Config jobConfig{};
    std::vector<OptionalTarget> jobTargets;
    bool jobInMatch = false, jobDroppedOnly = false;
    uintptr_t jobClientBase = 0;
    uint64_t jobGeneration = 0;

    while (true) {
        if (!g_acqState.ClaimOptional(jobConfig, jobTargets, jobInMatch, jobDroppedOnly, jobClientBase, jobGeneration)) {
            break;
        }
        try {
            mem.SetDmaLane("optional");
            mem.SetDmaPriority(Memory::DmaPri::Bones);
            mem.SetDmaCallTag("CS2.Optional");
            RunOptionalEnrichment(jobConfig, std::move(jobTargets), jobInMatch,
                                  jobClientBase, jobDroppedOnly, jobGeneration);
        } catch (...) {

        }
        g_acqState.ReleaseOptional();
        mem.SetDmaLane("full");
        mem.SetDmaPriority(Memory::DmaPri::Position);
    }
}

static void QueueOptionalEnrichment(const Config& frame_config) {
    if (!g_acqState.TryQueueOptional())
        return;
    {
        std::lock_guard lock(g_acqState.optional_mu);
        g_acqState.optional_config = frame_config;


        g_acqState.g_optional_in_match = runtime.in_match;
        g_acqState.g_optional_client_base = runtime.client_base;
        g_acqState.g_optional_dropped_only = false;
        g_acqState.g_optional_targets.clear();
        g_acqState.g_optional_targets.reserve((std::min)(runtime.players.size(), kOptionalMaxPlayers));
        for (const auto& p : runtime.players) {
            if (!IsUserPointer(p.pawn) || !p.alive) continue;
            if (g_acqState.g_optional_targets.size() >= kOptionalMaxPlayers) break;
            g_acqState.g_optional_targets.push_back({p.pawn, p.controller, p.scene,
                                                      {p.pos[0], p.pos[1], p.pos[2]},
                                                      p.position_sample_ms, p.name[0] != 0});
        }
        g_acqState.optional_job_generation.store(
            g_session_generation.load(std::memory_order_relaxed), std::memory_order_release);
    }
    g_acqState.optional_cv.notify_one();
}



static void QueueOptionalDroppedOnly(const Config& frame_config) {
    if (!g_acqState.TryQueueOptional())
        return;
    {
        std::lock_guard lock(g_acqState.optional_mu);
        g_acqState.optional_config = frame_config;
        g_acqState.g_optional_in_match = runtime.in_match;
        g_acqState.g_optional_client_base = runtime.client_base;
        g_acqState.g_optional_dropped_only = true;
        g_acqState.g_optional_targets.clear();
        g_acqState.optional_job_generation.store(
            g_session_generation.load(std::memory_order_relaxed), std::memory_order_release);
    }
    g_acqState.optional_cv.notify_one();
}
