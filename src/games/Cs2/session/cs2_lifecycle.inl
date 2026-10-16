
bool ReinitDma() {
    const bool restart_acquisition = g_acqState.IsAcquisitionRunning();
    if (restart_acquisition)
        StopAcquisition();



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
    g_controller_stride = offsets.entity_identity_stride;
    g_pawn_stride = offsets.entity_identity_stride;
    ready = false;
    g_read_fails.store(0, std::memory_order_relaxed);
    runtime = Runtime{};


    RuntimePipeline::ResetProducerThreadForShutdown();
    PublishRuntimeSnapshot();
    status = "Shutdown";
}

bool RefreshProcessLiveness() {





	const uint64_t now_ms = GetTickCount64();
	std::scoped_lock lock(g_process_liveness_mutex);
	if (now_ms < g_process_liveness_next_probe_ms)
		return g_process_liveness_last_result.load(std::memory_order_acquire);
	g_process_liveness_next_probe_ms = now_ms + 1000;


    DWORD pid = 0;
    if (mem.vHandle) {
        pid = mem.GetPidFromName("cs2.exe");
    }
	g_process_liveness_last_result.store(pid != 0, std::memory_order_release);
	return pid != 0;
}

bool IsGameProcessAlive() {


    if (g_acqState.IsAcquisitionRunning())
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
