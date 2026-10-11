// Reinitialization, detach/shutdown and process-liveness lifecycle.
bool ReinitDma() {
    const bool restart_acquisition = AcquisitionRunning();
    if (restart_acquisition)
        StopAcquisition();
    // No acquisition worker can still observe the old process at this point.
    // Advance the shared generation before attaching again so pointer-keyed
    // caches cannot leak state across a DMA/process rebind.
    ResetSessionState("dma-reinit");
    status = "Reinit DMA...";
    std::cout << "[CS2] Reinit DMA\n";
    mem.InvalidateProcess();
    bool ok = mem.Init("cs2.exe", true, false) || mem.Init("CS2.exe", true, false);
    if (!ok) {
        mem.ResetDevice();
        if (!mem.Init(std::string(), true, false)) {
            status = "Reinit DMA: FPGA falhou";
            return false;
        }
        ok = mem.Init("cs2.exe", true, false) || mem.Init("CS2.exe", true, false);
    }
    if (!ok) {
        status = "Reinit DMA: attach falhou";
        return false;
    }
    if (!ResolveModuleBases()) {
        status = "Reinit DMA: client.dll nao encontrado";
        std::cout << "[CS2] " << status << std::endl;
        return false;
    }
    status = "Reinit DMA OK";
    std::cout << "[CS2] " << status << std::endl;
    if (restart_acquisition)
        EnsureAcquisitionStarted();
    return true;
}

void Shutdown() {
    StopAcquisition();
    g_visibility_worker.Stop();
    g_collision_loader.Stop();
    WebRadar::Stop();
    DestroyScatter();
    ResetSessionState("shutdown");
    g_pending_entity_list = 0;
    g_entity_list_confirmations = 0;
    g_entity_list_next_validation_ms = 0;
    g_entity_list_next_full_probe_ms = 0;
    g_entity_list_force_refresh = true;
    g_controller_stride = kEntityIdentityStride;
    g_pawn_stride = kEntityIdentityStride;
    ready = false;
    g_read_fails.store(0, std::memory_order_relaxed);
    runtime = Runtime{};
    // Handoff: acquisition threads are joined above; publish the cleared
    // runtime from this thread without tripping the single-producer guard.
    RuntimePipeline::ResetProducerThreadForShutdown();
    PublishRuntimeSnapshot();
    status = "Shutdown";
}

bool RefreshProcessLiveness() {
	// Process liveness is control-plane state, not frame state. Repeated
	// VMMDLL_PidGetFromName calls at the old every-eight-scans cadence can cause
	// needless native-resource churn in some MemProcFS builds. Cache the probe
	// for one second; shutdown detection remains responsive without polling VMM
	// dozens of times per second.
	const uint64_t now_ms = GetTickCount64();
	std::scoped_lock lock(g_process_liveness_mutex);
	if (now_ms < g_process_liveness_next_probe_ms)
		return g_process_liveness_last_result.load(std::memory_order_acquire);
	g_process_liveness_next_probe_ms = now_ms + 1000;

    // A local process is not proof of liveness on the DMA target PC.
    DWORD pid = 0;
    if (mem.vHandle) {
        pid = mem.GetPidFromName("cs2.exe");
    }
	g_process_liveness_last_result.store(pid != 0, std::memory_order_release);
	return pid != 0;
}

bool IsGameProcessAlive() {
    // Presentation/UI never performs a potentially blocking MemProcFS process
    // query while a CS2 acquisition session is running.
    if (g_acquisition_running.load(std::memory_order_acquire))
        return g_process_liveness_last_result.load(std::memory_order_acquire);
    return RefreshProcessLiveness();
}

const char* StatusLine() {
    static char buf[96];
    if (!ready)
        std::snprintf(buf, sizeof(buf), "%s", status.c_str());
    else {
        const auto snapshot = AcquireRuntimeSnapshot();
        std::snprintf(buf, sizeof(buf), "CS2 b%u  %d jogadores",
            snapshot ? snapshot->build_number : 0,
            snapshot ? snapshot->player_count : 0);
    }
    return buf;
}

int PlayerCount() {
    const auto snapshot = AcquireRuntimeSnapshot();
    return snapshot ? snapshot->player_count : 0;
}

// Soft probe that works in lobby/menu: entity list + view matrix must resolve.
// Does NOT require local pawn (often null in main menu).

