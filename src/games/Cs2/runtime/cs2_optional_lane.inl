// Optional-worker lifecycle and latest-state-wins job submission.
static void OptionalWorkerLoop() {
    while (!g_optional_stop.load(std::memory_order_acquire)) {
        Config jobConfig{};
        std::vector<OptionalTarget> jobTargets;
        bool jobInMatch = false, jobDroppedOnly = false;
        uintptr_t jobClientBase = 0;
        uint64_t jobGeneration = 0;
        {
            std::unique_lock lock(g_optional_mu);
            g_optional_cv.wait_for(lock, std::chrono::milliseconds(50), [] {
                return g_optional_pending.load(std::memory_order_acquire) ||
                    g_optional_stop.load(std::memory_order_acquire);
            });
            if (g_optional_stop.load(std::memory_order_acquire))
                break;
            if (!g_optional_pending.load(std::memory_order_acquire))
                continue;
            jobConfig = g_optional_config;
            jobTargets.swap(g_optional_targets); // steal payload; no extra alloc
            jobInMatch = g_optional_in_match;
            jobDroppedOnly = g_optional_dropped_only;
            jobClientBase = g_optional_client_base;
            jobGeneration = g_optional_job_generation.load(std::memory_order_acquire);
            g_optional_pending.store(false, std::memory_order_release);
        }
        g_optional_busy.store(true, std::memory_order_release);
        try {
            mem.SetDmaLane("optional");
            mem.SetDmaPriority(Memory::DmaPri::Bones);
            mem.SetDmaCallTag("CS2.Optional");
            RunOptionalEnrichment(jobConfig, std::move(jobTargets), jobInMatch,
                                  jobClientBase, jobDroppedOnly, jobGeneration);
        } catch (...) {
            // Never kill the worker on a single bad scan.
        }
        g_optional_busy.store(false, std::memory_order_release);
        mem.SetDmaLane("full");
                mem.SetDmaPriority(Memory::DmaPri::Position);
    }
}

static void QueueOptionalEnrichment(const Config& frame_config) {
    if (g_optional_stop.load(std::memory_order_acquire))
        return;
    // If previous optional still runs, skip this cycle — core already published
    // and presentation has positions/health. Avoid stacking optional DMA.
    if (g_optional_busy.load(std::memory_order_acquire) ||
        g_optional_pending.load(std::memory_order_acquire))
        return;
    {
        std::lock_guard lock(g_optional_mu);
        g_optional_config = frame_config;
        // Immutable job payload: acquisition thread owns runtime.players, so
        // the optional worker receives copies and never dereferences runtime.
        g_optional_in_match = runtime.in_match;
        g_optional_client_base = runtime.client_base;
        g_optional_dropped_only = false;
        g_optional_targets.clear();
        g_optional_targets.reserve((std::min)(runtime.players.size(), size_t{64}));
        for (const auto& p : runtime.players) {
            if (!IsUserPointer(p.pawn) || !p.alive) continue;
            if (g_optional_targets.size() >= 64) break;
            g_optional_targets.push_back({p.pawn, p.controller, p.scene,
                                          {p.pos[0], p.pos[1], p.pos[2]},
                                          p.position_sample_ms, p.name[0] != 0});
        }
        g_optional_job_generation.store(
            g_session_generation.load(std::memory_order_relaxed), std::memory_order_release);
        g_optional_pending.store(true, std::memory_order_release);
    }
    g_optional_cv.notify_one();
}

// Dropped-item discovery must run even when player scanning is disabled
// (on-demand branch). Queues a metadata-only optional job with no pawns.
static void QueueOptionalDroppedOnly(const Config& frame_config) {
    if (g_optional_stop.load(std::memory_order_acquire))
        return;
    if (g_optional_busy.load(std::memory_order_acquire) ||
        g_optional_pending.load(std::memory_order_acquire))
        return;
    {
        std::lock_guard lock(g_optional_mu);
        g_optional_config = frame_config;
        g_optional_in_match = runtime.in_match;
        g_optional_client_base = runtime.client_base;
        g_optional_dropped_only = true;
        g_optional_targets.clear();
        g_optional_job_generation.store(
            g_session_generation.load(std::memory_order_relaxed), std::memory_order_release);
        g_optional_pending.store(true, std::memory_order_release);
    }
    g_optional_cv.notify_one();
}


// Death-mode is deliberately a separate, low-frequency path.  It never reads
// player transforms, bones, weapons or aim data; it only follows observer
// handles so the spectator widget keeps working while the main acquisition is
// paused.  The normal scan resumes automatically as soon as local HP is back.

