// Acquisition, camera and WebRadar-control thread orchestration.
void RunFrame() {
    RunFrameWithConfig(config);
}

void SubmitAcquisitionConfig(const Config& next) noexcept {
    RuntimePipeline::SubmitConfig(next);
}

void EnsureAcquisitionStarted() {
    if (!g_acqState.TryStart())
        return;

    {
        std::scoped_lock lock(g_process_liveness_mutex);
        g_process_liveness_next_probe_ms = 0;
        g_process_liveness_last_result.store(true, std::memory_order_release);
    }
    g_web_radar_control_stop.store(false, std::memory_order_release);
    g_web_radar_desired_enabled.store(false, std::memory_order_release);
    g_web_radar_control_thread = std::thread([] {
        uint64_t nextCloudflareAttemptMs = 0;
        uint64_t nextServerAttemptMs = 0;
        while (!g_web_radar_control_stop.load(std::memory_order_acquire)) {
            (void)RefreshProcessLiveness();
            if (g_web_radar_desired_enabled.load(std::memory_order_acquire)) {
                const int port = g_web_radar_desired_port.load(std::memory_order_relaxed);
                const uint64_t now = GetTickCount64();
                if ((!WebRadar::IsRunning() || WebRadar::Port() != port) &&
                    now >= nextServerAttemptMs) {
                    WebRadar::Start(port);
                    nextServerAttemptMs = now + 1000u;
                }
                if (WebRadar::IsRunning() &&
                    g_web_radar_desired_cloudflare.load(std::memory_order_relaxed)) {
                    if (!WebRadar::CloudflareRunning() && now >= nextCloudflareAttemptMs) {
                        WebRadar::StartCloudflare();
                        nextCloudflareAttemptMs = now + 5000u;
                    }
                } else if (WebRadar::CloudflareRunning()) {
                    WebRadar::StopCloudflare();
                }
            } else {
                if (WebRadar::IsRunning()) WebRadar::Stop();
                else if (WebRadar::CloudflareRunning()) WebRadar::StopCloudflare();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        WebRadar::Stop();
    });
    Camera::Reset();
    // Legitimate producer handoff: this init publish happens on the calling
    // thread BEFORE the acquisition worker exists. Rebind so the worker
    // remains the only producer from here on.
    RuntimePipeline::ResetProducerThreadForShutdown();
    PublishRuntimeSnapshot();
    // Every worker below gates its loop on IsRunning().  Publish the running
    // state before constructing any thread so a newly scheduled worker cannot
    // observe Starting and exit permanently before the caller reaches it.
    g_acqState.MarkRunning();
    // Camera and origins are latency-critical for visual attachment while
    // turning or moving.  Bones remain in the validated full scan; this small
    // lane refreshes only their common origin and the view matrix.
    g_optional_thread = std::thread(OptionalWorkerLoop);

    g_camera_thread = std::thread([] {
        mem.SetDmaLane("camera");
        mem.SetDmaPriority(Memory::DmaPri::Camera);
        OmniGhost::Gameplay::FixedRateScheduler scheduler;
        float matrix[16]{};
        uint64_t next_camera_ms = 0;
        uint64_t consecutive_busy = 0;
        // Cache runtime state atomically instead of acquiring full snapshot every cycle
        uintptr_t cached_client_base = 0;
        bool cached_in_match = false;
        while (g_acqState.IsRunning()) {
            // Lightweight atomic reads instead of full snapshot acquire
            if (cached_client_base != runtime.client_base) {
                cached_client_base = runtime.client_base;
            }
            if (cached_in_match != runtime.in_match) {
                cached_in_match = runtime.in_match;
            }
            const uintptr_t client_base = cached_client_base;
            const bool in_match = cached_in_match;
            const bool canRead = ready && offsets.loaded && client_base && offsets.dwViewMatrix;
            const uint64_t camera_now_ms = GetTickCount64();
            const int camera_pressure = g_pressure_level.load(std::memory_order_relaxed);
            // IDENTICAL to FiveM: camera stays at 16ms through pressure 0-2, only 25ms at pressure 3 (recovery)
            // No dynamic adjustment based on camPosDelta — fixed cadence prevents temporal skew
            int camera_period_ms = camera_pressure >= 3 ? 25 : g_intervals.camera_ms;
            Camera::SetDeadline(next_camera_ms);
            int wait_ms = in_match ? g_intervals.camera_ms : 20;
            bool busyRetry = false;
            if (canRead && in_match && camera_now_ms >= next_camera_ms) {
                Camera::SetPending(true);
                auto& camera_tel = OmniGhost::Gameplay::DmaTelemetry::CS2();
                if (HeavyDmaCooldownActive())
                    camera_tel.camera_cooldown_bypasses.fetch_add(1, std::memory_order_relaxed);
                bool attempted = false;
                const auto camera_read_begin = std::chrono::steady_clock::now();
                const uint64_t camera_read_begin_ms = GetTickCount64();
                const bool read_ok = QTryRead(client_base + offsets.dwViewMatrix, matrix, sizeof(matrix),
                                              attempted, "CS2.Camera");
                const uint64_t camera_read_end_ms = GetTickCount64();
                if (read_ok)
                    Camera::Publish(matrix, camera_read_begin_ms +
                        (camera_read_end_ms - camera_read_begin_ms) / 2u);
                if (attempted) {
                    if (read_ok && consecutive_busy > 0)
                        camera_tel.camera_busy_retry_success.fetch_add(1, std::memory_order_relaxed);
                    consecutive_busy = 0;
                    OmniGhost::Gameplay::DmaTelemetry::ObserveCameraRead(
                        camera_tel,
                        OmniGhost::Gameplay::TimeMs(camera_read_begin));
                    // Keep a stable cadence relative to the previous deadline,
                    // not relative to the variable-duration DMA read.
                    next_camera_ms += static_cast<uint64_t>(camera_period_ms);
                    const uint64_t after_read_ms = GetTickCount64();
                    if (next_camera_ms <= after_read_ms)
                        next_camera_ms = after_read_ms + static_cast<uint64_t>(camera_period_ms);
                    Camera::SetDeadline(next_camera_ms);
                    Camera::SetPending(false);
                    wait_ms = camera_period_ms;
                }
                else {
                    ++consecutive_busy;
                    camera_tel.camera_busy_skips.fetch_add(
                        1, std::memory_order_relaxed);
                    auto observed = camera_tel.camera_consecutive_busy_max.load(std::memory_order_relaxed);
                    while (observed < consecutive_busy &&
                           !camera_tel.camera_consecutive_busy_max.compare_exchange_weak(
                               observed, consecutive_busy, std::memory_order_relaxed)) {}
                    // Keep the pending flag raised so optional full-scan lanes yield.
                    busyRetry = true;
                }
            }
            else if (!in_match || !canRead) {
                Camera::SetPending(false);
                consecutive_busy = 0;
            }
            // The full scan already publishes coherent player positions. Keep this
            // worker dedicated to the matrix instead of acquiring snapshots for the
            // retired motion lane on every wake-up.
            // Keep the camera snappy — the matrix must stay fluid for ESP.
            if (busyRetry) {
                // sleep_until(1 ms) can round up to a full Windows timer tick.
                // A bounded yield gives the camera a real ~1 ms retry without
                // allowing one busy skip to cost another 16 ms.
                const auto retryAt = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(1);
                do {
                    std::this_thread::yield();
                } while (g_acqState.IsRunning() &&
                         std::chrono::steady_clock::now() < retryAt);
            } else {
                scheduler.Wait(std::chrono::milliseconds(wait_ms));
            }
        }
        Camera::SetPending(false);
    });
    g_acquisition_thread = std::thread([] {
        OmniGhost::Gameplay::FixedRateScheduler scheduler;
        std::array<float, 32> recentActiveScans{}; // BUG-035 Fix: Increased from 16 to 32 for stable pressure recovery
        size_t recentScanCount = 0;
        size_t recentScanCursor = 0;
        uint64_t pressureSessionGeneration = 0;
        int healthyPressureScans = 0;
        while (g_acqState.IsRunning()) {
            const auto acquire_begin = std::chrono::steady_clock::now();
            try {
                // A 300 ms+ device stall is a board/driver queue event. Do
                // not immediately start another heavy scan: retain the last
                // published immutable snapshot and give the DMA transport a
                // short recovery window.
                const uint64_t now = GetTickCount64();
                const uint64_t coolUntil = g_heavy_dma_cooldown_until_ms.load(std::memory_order_acquire);
                if (now < coolUntil) {
                    scheduler.Wait(std::chrono::milliseconds((std::min)(20ull, coolUntil - now)));
                    continue;
                }
                g_phase = Cs2PhaseTiming{};
                g_phase.scan_id = g_scan_id.fetch_add(1, std::memory_order_relaxed) + 1;
                mem.ResetThreadDmaWaitUs();
                mem.SetDmaScanId(g_phase.scan_id);
                mem.SetDmaLane("full");
                mem.SetDmaCallTag("CS2.Acquire");
                auto frame_config = RuntimePipeline::AcquireConfig();
                g_publish_runtime_this_scan = true;
                RunFrameWithConfig(*frame_config);
                runtime.acquisition_ms = OmniGhost::Gameplay::TimeMs(acquire_begin);
                if (g_publish_runtime_this_scan) {
                    ScopedPhase _phPub(&g_phase.publish_ms);
                    PublishRuntimeSnapshot();
                }
                runtime.acquisition_ms = OmniGhost::Gameplay::TimeMs(acquire_begin);
                {
                    auto& tel = OmniGhost::Gameplay::DmaTelemetry::CS2();
                    const bool activeSample = runtime.in_match && runtime.player_count > 0;
                    if (!runtime.in_match)
                        tel.idle_reason.store(2, std::memory_order_relaxed); // not_in_match
                    else if (runtime.player_count <= 0)
                        tel.idle_reason.store(5, std::memory_order_relaxed); // no_local/no players
                    else
                        tel.idle_reason.store(0, std::memory_order_relaxed);
                    g_phase.lock_wait_ms = static_cast<float>(mem.ConsumeThreadDmaWaitUs()) / 1000.f;
                    NotePossibleDeviceStall(runtime.acquisition_ms);
                    OmniGhost::Gameplay::DmaTelemetry::ObserveAcquire(
                        tel, runtime.acquisition_ms, activeSample, g_phase.scan_id);
                    tel.entities.store(runtime.player_count, std::memory_order_relaxed);
                    tel.snapshot_drops.store(RuntimePipeline::SnapshotDrops(), std::memory_order_relaxed);
                    tel.dma_open.store(mem.vHandle != nullptr, std::memory_order_relaxed);
                    tel.process_id.store(static_cast<std::uint32_t>(mem.GetDiagnosticsSnapshot().processId), std::memory_order_relaxed);
                    tel.process_connected.store(runtime.client_base != 0, std::memory_order_relaxed);
                    tel.snapshot_hz.store(RuntimePipeline::AcquisitionHz(), std::memory_order_relaxed);
                    tel.bones_players.store(g_phase.bones_players, std::memory_order_relaxed);
					tel.pressure_level.store(
						g_pressure_level.load(std::memory_order_relaxed), std::memory_order_relaxed);
					{
						const uint64_t now_ms = GetTickCount64();
						const uint64_t cool_until = g_heavy_dma_cooldown_until_ms.load(std::memory_order_acquire);
						tel.cooldown_remaining_ms.store(
							cool_until > now_ms ? cool_until - now_ms : 0, std::memory_order_relaxed);
					}
                    {
                        const auto cam = Camera::Acquire();
                        if (cam) {
                            const float age = static_cast<float>(GetTickCount64() - cam->timestamp_ms);
							OmniGhost::Gameplay::DmaTelemetry::ObserveCameraAge(tel, age);
                        }
                        const auto rt = RuntimePipeline::Acquire();
                        if (rt) {
                            const uint64_t nowMs = GetTickCount64();
                            const float snapshotAge = static_cast<float>(nowMs - rt->snapshot_timestamp_ms);
                            const uint64_t worldSample = rt->world_sample_ms ? rt->world_sample_ms : rt->snapshot_timestamp_ms;
                            const float worldAge = static_cast<float>(nowMs - worldSample);
                            const float publishDelay = rt->snapshot_timestamp_ms >= worldSample
                                ? static_cast<float>(rt->snapshot_timestamp_ms - worldSample) : 0.f;
                            tel.snapshot_age_ms.store(snapshotAge, std::memory_order_relaxed);
                            tel.world_age_ms.store(worldAge, std::memory_order_relaxed);
                            tel.publish_delay_ms.store(publishDelay, std::memory_order_relaxed);
                        }
                    }
                    tel.snapshot_interval_ms.store(
                        RuntimePipeline::AcquisitionHz() > 0.1f
                            ? (1000.f / RuntimePipeline::AcquisitionHz())
                            : 0.f,
                        std::memory_order_relaxed);
                    if (OmniGhost::Gameplay::DmaTelemetry::IsEnabled())
                        OmniGhost::Gameplay::DmaTelemetry::Tick("CS2");
                    if (OmniGhost::Gameplay::DmaTelemetry::IsEnabled() &&
                        activeSample && runtime.acquisition_ms >= 50.f) {
                        OmniGhost::Gameplay::DmaTelemetry::SpikeBreakdown bd{};
                        bd.scan_id = g_phase.scan_id;
                        bd.total_ms = runtime.acquisition_ms;
                        bd.lock_wait_ms = g_phase.lock_wait_ms;
                        bd.other_ms = g_phase.other_ms;
                        {
                            const auto ds = mem.GetScanDmaStats();
                            bd.qread_calls = ds.qread_calls;
                            bd.qread_total_ms = static_cast<float>(ds.qread_total_ms);
                            bd.qread_max_ms = static_cast<float>(ds.qread_max_ms);
                            bd.scatter_calls = ds.scatter_calls;
                            bd.scatter_total_ms = static_cast<float>(ds.scatter_total_ms);
                            bd.scatter_max_ms = static_cast<float>(ds.scatter_max_ms);
                            bd.top_tag = ds.top_tag;
                            bd.top_tag_ms = static_cast<float>(ds.top_tag_ms);
                            bd.top2_tag = ds.top2_tag;
                            bd.top2_tag_ms = static_cast<float>(ds.top2_tag_ms);
                            bd.top3_tag = ds.top3_tag;
                            bd.top3_tag_ms = static_cast<float>(ds.top3_tag_ms);
                        }
                        bd.webradar_ms = g_phase.webradar_ms;
                        bd.entity_ms = g_phase.entity_ms;
                        bd.local_ms = g_phase.local_ms;
                        bd.core_scatter_ms = g_phase.core_scatter_ms;
                        bd.positions_ms = g_phase.positions_ms;
                        bd.bones_ms = g_phase.bones_ms;
                        bd.weapon_ms = g_phase.weapon_ms;
                        bd.ammo_ms = g_phase.ammo_ms;
                        bd.flags_ms = g_phase.flags_ms;
                        bd.spectator_ms = g_phase.spectator_ms;
                        bd.bomb_ms = g_phase.bomb_ms;
                        bd.publish_ms = g_phase.publish_ms;
                        bd.cleanup_ms = g_phase.cleanup_ms;
                        bd.players = runtime.player_count;
                        bd.bones_players = g_phase.bones_players;
                        bd.entity_full_probe = g_phase.entity_full_probe;
                        bd.spectator_refresh = g_phase.spectator_refresh;
                        bd.bomb_refresh = g_phase.bomb_refresh;
                        bd.cache_cleanup = g_phase.cache_cleanup;
                        OmniGhost::Gameplay::DmaTelemetry::LogSpikeBreakdown("CS2", bd);
                    }
                }
                // Processing is deliberately kept producer-side and currently
                // consists of validation/cache assembly included in RunFrame.
                runtime.processing_ms = runtime.acquisition_ms;
                OmniGhost::Gameplay::PipelineTelemetry::Smooth(
                    g_pipeline_metrics.acquire_ms, runtime.acquisition_ms);
                g_pipeline_metrics.entities.store(runtime.player_count, std::memory_order_relaxed);
                // lock_wait_ms is captured once above. Do not overwrite the
                // residual CPU attribution with a second, unrelated counter read.
            } catch (const std::exception& ex) {
                std::cerr << "[CS2] acquisition exception: " << ex.what() << std::endl;
            } catch (...) {
                std::cerr << "[CS2] acquisition unknown exception" << std::endl;
            }

            // Core position/HP acquisition targets 18 ms. Bones have their own
            // 24/32/50 ms per-pawn cadence, while presentation remains uncapped and
            // presents the immutable snapshots with bounded extrapolation.
            // Adaptive scheduler (hysteresis): based on measured work, not board name.
            // IDENTICAL to FiveM esp_manager.cpp:758-763
            int delayMs = runtime.in_match ? g_intervals.full_scan_ms : 16;
            try {
                auto cfgLease = RuntimePipeline::AcquireConfig();
                if (cfgLease) {
                    if (!NeedsPlayerScan(*cfgLease))
                        delayMs = 50; // on-demand sleep: zero wasted transfers
                    else if (runtime.in_match) {
                        // Performance mode base: 24ms, else 18ms — FiveM uses 24/18
                        delayMs = cfgLease->performance_mode ? 24 : 18;
                    }
                }

            } catch (...) {
                std::cerr << "[CS2] acquisition config snapshot failed with unknown exception" << std::endl;
            }
            {
                const float work = runtime.acquisition_ms;
                const uint64_t generation = g_session_generation.load(std::memory_order_acquire);
                if (generation != pressureSessionGeneration) {
                    pressureSessionGeneration = generation;
                    recentScanCount = recentScanCursor = 0;
                    healthyPressureScans = 0;
                }
                if (runtime.in_match && runtime.player_count > 0 && std::isfinite(work)) {
                    recentActiveScans[recentScanCursor] = work;
                    recentScanCursor = (recentScanCursor + 1) % recentActiveScans.size();
                    recentScanCount = (std::min)(recentScanCount + 1, recentActiveScans.size());
                }
                auto sortedScans = recentActiveScans;
                std::sort(sortedScans.begin(), sortedScans.begin() + recentScanCount);
                const float recentP95 = recentScanCount
                    ? sortedScans[(recentScanCount - 1) * 95 / 100] : 0.f;
                OmniGhost::Gameplay::DmaTelemetry::CS2().pressure_window_p95_ms.store(
                    recentP95, std::memory_order_relaxed);
                int level = g_pressure_level.load(std::memory_order_relaxed);
                // IDENTICAL to FiveM esp_manager.cpp:722-743 — adaptive pressure with hysteresis
                int demanded = 0;
                if (work > PRESSURE_ENTER_L3_WORK || recentP95 > PRESSURE_ENTER_L3_P95) demanded = 3;
                else if (work > PRESSURE_ENTER_L2_WORK || recentP95 > PRESSURE_ENTER_L2_P95) demanded = 2;
                else if (work > PRESSURE_ENTER_L1_WORK || recentP95 > PRESSURE_ENTER_L1_P95) demanded = 1;
                const float exitWork = level >= 3 ? PRESSURE_EXIT_L3_WORK : level == 2 ? PRESSURE_EXIT_L2_WORK : PRESSURE_EXIT_L1_WORK;
                const float exitP95 = level >= 3 ? PRESSURE_EXIT_L3_P95 : level == 2 ? PRESSURE_EXIT_L2_P95 : PRESSURE_EXIT_L1_P95;
                if (demanded > level) {
                    level = demanded;
                    healthyPressureScans = 0;
                } else if (work < exitWork && recentP95 < exitP95) {
                    if (++healthyPressureScans >= PRESSURE_HEALTHY_SCANS_TO_RECOVER) {
                        level = (std::max)(0, level - 1);
                        healthyPressureScans = 0;
                    }
                } else if (demanded < level) {
                    if (healthyPressureScans > 0) --healthyPressureScans;
                } else {
                    healthyPressureScans = 0;
                }
                g_pressure_level.store(level, std::memory_order_relaxed);
                // Update dynamic intervals based on current conditions
                g_intervals.Update(runtime.player_count, runtime.acquisition_ms, level, kOptionalBudgetMs);
                // Delay stretch — IDENTICAL to FiveM: 40/28/22 ms at pressure 3/2/1
                if (level >= 3) delayMs = (std::max)(delayMs, 40);
                else if (level == 2) delayMs = (std::max)(delayMs, 28);
                else if (level == 1) delayMs = (std::max)(delayMs, 22);
            }
            const auto waitBegin = std::chrono::steady_clock::now();
            scheduler.Wait(std::chrono::milliseconds(delayMs));
            OmniGhost::Gameplay::DmaTelemetry::ObserveSchedulerWait(
                OmniGhost::Gameplay::DmaTelemetry::CS2(),
                OmniGhost::Gameplay::TimeMs(waitBegin));
        }
    });
}

void StopAcquisition() {
    if (OmniGhost::Gameplay::DmaTelemetry::IsEnabled())
        OmniGhost::Gameplay::DmaTelemetry::LogSessionSummary("CS2");
    g_acqState.RequestStop();
    if (g_optional_thread.joinable())
        g_optional_thread.join();
    if (g_camera_thread.joinable())
        g_camera_thread.join();
    if (g_acquisition_thread.joinable())
        g_acquisition_thread.join();
    g_scatter_optional.Reset();
    // FIX MEM-002: Also reset g_scatter_full to avoid leak
    g_scatter_full.Reset();
    g_web_radar_control_stop.store(true, std::memory_order_release);
    if (g_web_radar_control_thread.joinable())
        g_web_radar_control_thread.join();
    Camera::Reset();
    g_acqState.MarkStopped();
}

RuntimeSnapshotLease AcquireRuntimeSnapshot() {
    return RuntimePipeline::Acquire();
}

CameraSnapshotLease AcquireCameraSnapshot() {
    return Camera::Acquire();
}

LivenessSnapshotLease AcquireLivenessSnapshot() {
    return RuntimePipeline::AcquireLiveness();
}

CameraSnapshot SelectCameraSnapshot(uint64_t world_sample_ms) {
    return Camera::Select(world_sample_ms);
}

bool AcquisitionRunning() noexcept {
    return g_acqState.IsAcquisitionRunning();
}

void SetPresentationFps(float fps) noexcept {
    if (std::isfinite(fps) && fps >= 0.f)
        g_presentation_fps.store(fps, std::memory_order_relaxed);
}

int DmaPressureLevel() noexcept {
    return g_pressure_level.load(std::memory_order_relaxed);
}

float LastSlowDmaMs() noexcept {
    return g_last_slow_dma_ms.load(std::memory_order_relaxed);
}

float LastWeaponPhaseMs() noexcept {
    return g_last_weapon_ms.load(std::memory_order_relaxed);
}

float LastAmmoPhaseMs() noexcept {
    return g_last_ammo_ms.load(std::memory_order_relaxed);
}

float LastFlagsPhaseMs() noexcept {
    return g_last_flags_ms.load(std::memory_order_relaxed);
}



