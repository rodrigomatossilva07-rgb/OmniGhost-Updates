
static int ReinitDmaBlocking() {
    const bool restart_acquisition = g_acqState.IsAcquisitionRunning();
    if (restart_acquisition)
        StopAcquisition();



    ResetSessionState("dma-reinit");
    std::cout << "[CS2] Reinit DMA\n";
    mem.InvalidateProcess();
    bool ok = mem.Init("cs2.exe", true, false) || mem.Init("CS2.exe", true, false);
    if (!ok) {
        mem.ResetDevice();
        if (!mem.Init(std::string(), true, false)) {
            return 3;
        }
        ok = mem.Init("cs2.exe", true, false) || mem.Init("CS2.exe", true, false);
    }
    if (!ok) {
        return 4;
    }
    if (!ResolveModuleBases()) {
        std::cout << "[CS2] Reinit DMA: client.dll nao encontrado" << std::endl;
        return 5;
    }
    std::cout << "[CS2] Reinit DMA OK" << std::endl;
    if (restart_acquisition)
        EnsureAcquisitionStarted();
    return 2;
}

static std::jthread g_reinit_worker;
static std::atomic<int> g_reinit_state{0};

bool ReinitDmaPending() {
    return g_reinit_state.load(std::memory_order_acquire) == 1;
}

int PollReinitDma() {
    return g_reinit_state.load(std::memory_order_acquire);
}

void ReinitDmaAsync() {
    if (g_reinit_state.load(std::memory_order_acquire) == 1)
        return;
    if (g_reinit_worker.joinable())
        g_reinit_worker.join();
    g_reinit_state.store(1, std::memory_order_release);
    g_reinit_worker = std::jthread([](std::stop_token) {
        g_reinit_state.store(ReinitDmaBlocking(), std::memory_order_release);
    });
}

bool ReinitDma() {
    const int result = ReinitDmaBlocking();
    SetStatus(result == 2 ? "Reinit DMA OK"
        : result == 3 ? "Reinit DMA: FPGA falhou"
        : result == 4 ? "Reinit DMA: attach falhou"
        : "Reinit DMA: client.dll nao encontrado");
    return result == 2;
}

void Shutdown() {
    std::clog << "[CS2][Shutdown] acquisition begin\n";
    StopAcquisition();
    std::clog << "[CS2][Shutdown] acquisition complete; visibility begin\n";
    g_visibility_worker.Stop();
    std::clog << "[CS2][Shutdown] visibility complete; collision begin\n";
    g_collision_loader.Stop();
    std::clog << "[CS2][Shutdown] collision complete; radar begin\n";
    WebRadar::Stop();
    std::clog << "[CS2][Shutdown] radar complete; scatter begin\n";
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
    // CSE-002: liberta os SRV dos ícones de armas no fim da sessão CS2.
    CS2_WeaponIcons::Shutdown();
    std::clog << "[CS2][Shutdown] complete\n";
    SetStatus("Shutdown");
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
        std::snprintf(buf, sizeof(buf), "%s", GetStatus().c_str());
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
