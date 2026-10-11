// Central core-world scan and immutable snapshot assembly.
static void RunFrameWithConfig(const Config& frame_config) {
    runtime.read_fails = g_read_fails.load(std::memory_order_relaxed);
    // The network/process lifecycle is owned by its control worker, never by
    // the latency-critical acquisition lane.
    const int radarPort = frame_config.webradar_port > 0 && frame_config.webradar_port <= 65535
        ? frame_config.webradar_port : 8080;
    g_web_radar_desired_port.store(radarPort, std::memory_order_relaxed);
    g_web_radar_desired_cloudflare.store(frame_config.webradar_cloudflare, std::memory_order_relaxed);
    g_web_radar_desired_enabled.store(frame_config.webradar_enabled, std::memory_order_release);

    if (!ready || !offsets.loaded || !runtime.client_base)
        return;

    // Capture frame timestamp once to avoid repeated GetTickCount64() syscalls.
    const uint64_t frame_time_ms = GetTickCount64();

    // CS2 Diversos → Telemetria: mirror into the shared DMA telemetry logger.
    OmniGhost::Gameplay::DmaTelemetry::SetEnabled(frame_config.telemetry_enabled);
    if (frame_config.telemetry_enabled) {
        static uint64_t s_cfgBits = 0;
        uint64_t bits = 0;
        auto bit = [&](bool v, int i) { if (v) bits |= (1ull << i); };
        bit(frame_config.radar_2d || frame_config.webradar_enabled, 0);
        bit(frame_config.aim_enabled, 1);
        bit(frame_config.trigger_enabled, 2);
        bit(frame_config.bomb_timer, 3);
        bit(frame_config.spectator_list, 4);
        bit(frame_config.performance_mode, 5);
        if (bits != s_cfgBits) {
            s_cfgBits = bits;
            char blob[320];
            std::snprintf(blob, sizeof(blob),
                "radar=%d\naim=%d\ntrigger=%d\nbomb=%d\nspectators=%d\nperf_mode=%d",
                (frame_config.radar_2d || frame_config.webradar_enabled) ? 1 : 0,
                frame_config.aim_enabled ? 1 : 0,
                frame_config.trigger_enabled ? 1 : 0,
                frame_config.bomb_timer ? 1 : 0,
                frame_config.spectator_list ? 1 : 0,
                frame_config.performance_mode ? 1 : 0);
            OmniGhost::Gameplay::DmaTelemetry::LogConfigChanged("CS2", blob);
        }
    }

    const auto _frameWallBegin = std::chrono::steady_clock::now();
    ++runtime.frames;
    runtime.fps = g_presentation_fps.load(std::memory_order_relaxed);

    uint64_t currentGen = g_session_generation.load(std::memory_order_acquire);
    const uintptr_t client = runtime.client_base;
    // Keep last-good player snapshot when entity list briefly fails (prevents
    // ESP/aim going empty for 1–2 frames on ListEntry null flicker).
    struct RecoveryIdentity { uintptr_t pawn = 0; uintptr_t controller = 0; };
    static std::vector<RecoveryIdentity> last_good_players;
    static uint64_t last_good_ms = 0;
    // Maximum cache sizes to prevent unbounded memory growth
    constexpr size_t kMaxRecentPlayers = 128;
    constexpr size_t kMaxBoneCache = 128;
    constexpr size_t kMaxStickyBoneLayout = 128;
    constexpr size_t kMaxLastBoneMs = 128;
    
    struct RecentPlayer {
        Player player{};
        uint64_t last_seen_ms = 0;
        uint64_t entry_generation = 0;
        bool confirmed_dead = false; // P0-3: never reinsert after confirmed death
    };
    struct CachedBones {
        float joints[kBoneSlotCount][3]{};
        float origin[3]{};
        uint64_t last_valid_ms = 0;
        bool full_pose = false;
        uint64_t entry_generation = 0;
    };
    static std::unordered_map<uintptr_t, RecentPlayer> recent_players;
    static std::unordered_map<uintptr_t, CachedBones> bone_cache;
    static std::unordered_map<uintptr_t, uint8_t> s_stickyBoneLayout; // 0/1 once validated
    static std::unordered_map<uintptr_t, uint64_t> s_lastBoneMs;
    static uint64_t recoveryCacheGeneration = 0;
    static std::shared_mutex g_cache_mutex;
    if (recoveryCacheGeneration != g_session_generation.load(std::memory_order_acquire)) {
        last_good_players.clear(); last_good_ms = 0;
        recent_players.clear(); bone_cache.clear();
        s_stickyBoneLayout.clear(); s_lastBoneMs.clear();
        recoveryCacheGeneration = g_session_generation.load(std::memory_order_relaxed);
    }
    if (recent_players.bucket_count() < 128) recent_players.reserve(128);
    if (bone_cache.bucket_count() < 128) bone_cache.reserve(128);
    if (s_stickyBoneLayout.bucket_count() < 128) s_stickyBoneLayout.reserve(128);
    if (s_lastBoneMs.bucket_count() < 128) s_lastBoneMs.reserve(128);

    // Periodic cache size enforcement (every 1000 frames)
    static uint64_t s_cacheEnforceFrame = 0;
    if (++s_cacheEnforceFrame % 1000 == 0) {
        std::unique_lock lock(g_cache_mutex);
        // Enforce max sizes by removing oldest entries
        while (recent_players.size() > kMaxRecentPlayers) {
            auto oldest = recent_players.begin();
            for (auto it = std::next(oldest); it != recent_players.end(); ++it) {
                if (it->second.last_seen_ms < oldest->second.last_seen_ms)
                    oldest = it;
            }
            recent_players.erase(oldest);
        }
        while (bone_cache.size() > kMaxBoneCache) {
            auto oldest = bone_cache.begin();
            for (auto it = std::next(oldest); it != bone_cache.end(); ++it) {
                if (it->second.last_valid_ms < oldest->second.last_valid_ms)
                    oldest = it;
            }
            bone_cache.erase(oldest);
        }
        while (s_stickyBoneLayout.size() > kMaxStickyBoneLayout) {
            auto it = s_stickyBoneLayout.begin();
            s_stickyBoneLayout.erase(it);
        }
        while (s_lastBoneMs.size() > kMaxLastBoneMs) {
            auto it = s_lastBoneMs.begin();
            s_lastBoneMs.erase(it);
        }
    }

    // ── Merge optional enrichment results (P0 single-writer invariant) ───
    // The optional worker is the sole producer of g_optional_results and never
    // touches `runtime`. Draining here keeps THIS thread the only writer of
    // runtime.players and the only producer of g_runtime_snapshots.
    const auto drainOptionalResults = [&](uint64_t generation) {
        auto opt = g_optional_results.Acquire();
        if (!opt || opt->generation != generation)
            return;
        const uint64_t mergeNowMs = frame_time_ms;
        // Attribute the optional worker's phase timings to this scan.
        g_phase.bones_ms += opt->bones_ms;
        g_phase.weapon_ms += opt->weapon_ms;
        g_phase.optional_budget_left_ms = opt->budget_left_ms; // P2-8: track optional budget
        g_phase.bones_players += static_cast<int>(opt->bone_count);

        {
            std::unique_lock lock(g_cache_mutex);
            for (uint32_t i = 0; i < opt->bone_count && i < opt->bones.size(); ++i) {
                const OptionalBoneResult& r = opt->bones[i];
                if (!IsUserPointer(r.pawn) || !r.bones_ok || !r.sample_ms) continue;
                CachedBones& cb = bone_cache[r.pawn];
                if (cb.entry_generation != generation) { cb = CachedBones{}; cb.entry_generation = generation; }
                std::memcpy(cb.joints, r.bones.data(), sizeof(cb.joints));
                cb.last_valid_ms = r.sample_ms;
                cb.full_pose = r.full_bones_ok;
                std::memcpy(cb.origin, r.origin, sizeof(cb.origin));
            }
        }
        {
            std::unique_lock lock(g_cache_mutex);
            for (uint32_t i = 0; i < opt->name_count && i < opt->names.size(); ++i) {
                const OptionalNameResult& r = opt->names[i];
                if (!IsUserPointer(r.pawn) || !IsUserPointer(r.controller) || !r.name[0]) continue;
                RecentPlayer& rp = recent_players[r.pawn];
                if (rp.entry_generation != generation) {
                    rp = RecentPlayer{};
                    rp.player.pawn = r.pawn;
                    rp.entry_generation = generation;
                }
                rp.last_seen_ms = mergeNowMs;
                rp.player.controller = r.controller;
                std::snprintf(rp.player.name, sizeof(rp.player.name), "%s", r.name);
                rp.player.is_bot = IsBotPlayerName(r.name);
            }
        }
        {
            std::unique_lock lock(g_cache_mutex);
            for (uint32_t i = 0; i < opt->weapon_count && i < opt->weapons.size(); ++i) {
                const OptionalWeaponResult& r = opt->weapons[i];
                if (!IsUserPointer(r.pawn) || (r.weapon_def <= 0 && r.ammo_clip < 0)) continue;
                RecentPlayer& rp = recent_players[r.pawn];
                if (rp.entry_generation != generation) {
                    rp = RecentPlayer{};
                    rp.player.pawn = r.pawn;
                    rp.entry_generation = generation;
                }
                rp.last_seen_ms = mergeNowMs;
                if (r.weapon_def > 0) rp.player.weapon_def = r.weapon_def;
                if (r.weapon[0])
                    std::snprintf(rp.player.weapon, sizeof(rp.player.weapon), "%s", r.weapon);
                if (r.ammo_clip >= 0) rp.player.ammo_clip = r.ammo_clip;
            }
        }
        // Merge visibility results from optional worker
        {
            std::unique_lock lock(g_cache_mutex);
            for (uint32_t i = 0; i < opt->visibility_count && i < opt->visibility.size(); ++i) {
                const OptionalVisibilityResult& r = opt->visibility[i];
                if (!IsUserPointer(r.pawn) || !r.known) continue;
                RecentPlayer& rp = recent_players[r.pawn];
                if (rp.entry_generation != generation) {
                    rp = RecentPlayer{};
                    rp.player.pawn = r.pawn;
                    rp.entry_generation = generation;
                }
                rp.last_seen_ms = mergeNowMs;
                rp.player.visibility_known = true;
                rp.player.visible = r.visible;
            }
        }
        {
            std::unique_lock lock(g_cache_mutex);
            if (opt->dropped_weapons_valid)
                runtime.dropped_weapons = opt->dropped_weapons;
                runtime.dropped_weapons.reserve(24);
        }
    };
    drainOptionalResults(currentGen);
    // Recovery may freeze expensive topology/metadata work, but it must never
    // freeze death detection. Probe only the already-known pawn health fields,
    // publish them immediately, and remove confirmed deaths from every cache
    // that can republish a player. Sentinel values remain unknown/read-failed
    // and deliberately retain the short dropout grace period.
    const auto refresh_cached_liveness = [&](uint64_t now_ms) {
        if (last_good_players.empty() || !offsets.m_iHealth)
            return false;
        EnsureScatter();
        if (!g_scatter_full)
            return false;

        constexpr int kUnknownHealth = (std::numeric_limits<int>::min)();
        std::array<int, 64> health{};
        std::array<uint8_t, 64> controller_alive{};
        health.fill(kUnknownHealth);
        controller_alive.fill(0xFF);
        const size_t count = (std::min)(last_good_players.size(), health.size());
        size_t queued = 0;
        for (size_t i = 0; i < count; ++i) {
            if (!IsUserPointer(last_good_players[i].pawn))
                continue;
            mem.AddScatterReadRequest(g_scatter_full,
                last_good_players[i].pawn + offsets.m_iHealth,
                &health[i], sizeof(health[i]));
            if (offsets.m_bPawnIsAlive && IsUserPointer(last_good_players[i].controller))
                mem.AddScatterReadRequest(g_scatter_full,
                    last_good_players[i].controller + offsets.m_bPawnIsAlive,
                    &controller_alive[i], sizeof(controller_alive[i]));
            ++queued;
        }
        if (!queued)
            return false;

        mem.SetDmaCallTag("CS2.LivenessRecovery");
        mem.ExecuteReadScatter(g_scatter_full);
        LivenessSnapshot liveness{};
        liveness.timestamp_ms = frame_time_ms;
        liveness.generation = g_session_generation.load(std::memory_order_acquire);
        std::array<uintptr_t, 64> confirmed_dead{};
        size_t dead_count = 0;
        for (size_t i = 0; i < count; ++i) {
            // Values left at the sentinel are failed/unknown, not deaths.
            const bool healthKnown = health[i] != kUnknownHealth && health[i] >= 0 && health[i] <= 200;
            const bool controllerKnown = controller_alive[i] == 0 || controller_alive[i] == 1;
            if (!healthKnown && !controllerKnown)
                continue;
            auto& sample = liveness.players[liveness.count++];
            sample.pawn = last_good_players[i].pawn;
            sample.sample_ms = liveness.timestamp_ms;
            sample.generation = liveness.generation;
            sample.alive = !(healthKnown && health[i] <= 0) &&
                !(controllerKnown && controller_alive[i] == 0);
            if (!sample.alive)
                confirmed_dead[dead_count++] = sample.pawn;
        }
        if (liveness.count)
            PublishLivenessSnapshot(liveness);

        {
            std::unique_lock lock(g_cache_mutex);
            for (size_t i = 0; i < dead_count; ++i) {
                const uintptr_t pawn = confirmed_dead[i];
                last_good_players.erase(std::remove_if(last_good_players.begin(),
                    last_good_players.end(), [pawn](const RecoveryIdentity& p) { return p.pawn == pawn; }),
                    last_good_players.end());
                recent_players.erase(pawn);
                bone_cache.erase(pawn);
                s_stickyBoneLayout.erase(pawn);
                s_lastBoneMs.erase(pawn);
            }
        }
        auto& telemetry = OmniGhost::Gameplay::DmaTelemetry::CS2();
        telemetry.last_good_age_ms.store(last_good_ms && now_ms >= last_good_ms
            ? now_ms - last_good_ms : 0u, std::memory_order_relaxed);
        return true;
    };
    runtime.player_count = 0;
    runtime.enemy_count = 0;
    mem.SetDmaCallTag("CS2.Controllers");
    runtime.controller_count = 0;
    runtime.pawn_count = 0;
    runtime.players.clear();
    if (runtime.players.capacity() < 64)
        runtime.players.reserve(64);
    // Entity-list root/page validation is time-based inside
    // RefreshEntityListEntry; ordinary scans reuse the last confirmed pair.

    // Map detection is wall-clock based so acquisition cadence changes do not
    // accidentally make this expensive read more frequent. Lobby remains fast.
    static char previous_map[64]{};
    static uint64_t next_map_refresh_ms = 0;
    const uint64_t map_now_ms = frame_time_ms;
    if (map_now_ms >= next_map_refresh_ms) {
        mem.SetDmaCallTag("CS2.MapRefresh");
        RefreshMapName();
        // The map normally changes only on a transition.  Re-reading it every
        // few seconds was an expensive synchronous probe that could queue
        // behind player reads.  Keep lobby detection responsive, but let a
        // confirmed match reuse its valid name for a longer period.
        const int pressure = g_pressure_level.load(std::memory_order_relaxed);
        // Keep a cheap safety probe during a match. Session/topology changes
        // force an immediate refresh elsewhere, while this catches missed
        // transitions without retaining a collision world for minutes.
        next_map_refresh_ms = map_now_ms + (runtime.in_match
            ? (pressure >= 2 ? 60000u : 30000u)
            : 1000u);
    }

    // Match transition: map changed → force entity-list re-acquire so ESP
    // recovers automatically when leaving lobby / joining a new game.
    // Also force-clear Makcu button mask — release packets are often lost
    // during load and would leave LMB stuck ("mira sozinho").
    if (std::strncmp(previous_map, runtime.map_name, sizeof(previous_map)) != 0) {
        ResetSessionState("map-change");
        currentGen = g_session_generation.load(std::memory_order_acquire);
        std::memcpy(previous_map, runtime.map_name, sizeof(previous_map));
        g_pending_entity_list = 0;
        g_entity_list_confirmations = 0;
        runtime.entity_list_entry = 0;
        runtime.entity_list_addr = 0;
        g_entity_list_next_validation_ms = 0;
        g_entity_list_next_full_probe_ms = 0;
        g_entity_list_force_refresh = true;
        runtime.bomb = BombState{};
        {
            std::unique_lock lock(g_cache_mutex);
            recent_players.clear();
            bone_cache.clear();
            s_stickyBoneLayout.clear();
            s_lastBoneMs.clear();
        }
        runtime.players.clear();
        runtime.player_count = 0;
        runtime.pawn_count = 0;
        runtime.controller_count = 0;
        g_cached_entity_root = 0;
        makcu_wrapper::ForceClearButtons();
        if (runtime.map_name[0])
            std::cout << "[CS2] Mapa: " << runtime.map_name
                      << " (ESP auto-recover, config intact)" << std::endl;
    }

    // Visibility colours use the static collision mesh of the active map.
    // The owned loader performs all filesystem/decompression/BVH work away
    // from acquisition and replaces pending requests on a map transition.
    const bool needs_collision = frame_config.esp_enabled && frame_config.visibility_colors;
    g_collision_loader.Request(runtime.map_name, needs_collision, map_now_ms);

    const bool map_reports_match = IsPlayableMapName(runtime.map_name);
    runtime.in_match = map_reports_match;
    int entity_probe_controller_count = 0;

    // Detect lobby → match and match → match transitions even when the map
    // string stays empty briefly (common when INSERT was opened in the lobby).
    static bool was_in_match = false;
    static int zero_player_frames = 0;
    // Map refresh is the primary match signal. Some builds briefly expose an
    // empty map string after loading, so retain a deliberately strict and slow
    // fallback: a local pawn alone is also present in lobby, but a live entity
    // page with multiple controllers is not. This avoids activating the full
    // pipeline in lobby while still recovering automatically in a real match.
    static uint64_t next_match_fallback_ms = 0;
    if (!runtime.in_match && map_now_ms >= next_match_fallback_ms && !HeavyDmaCooldownActive()) {
        next_match_fallback_ms = map_now_ms + 2500u;
        uintptr_t probe_pawn = 0;
        uintptr_t entity_root = 0;
        uintptr_t entity_page = 0;
        if (QReadT(client + offsets.dwLocalPlayerPawn, probe_pawn, "CS2.MatchProbe") &&
            QReadT(client + offsets.dwEntityList, entity_root, "CS2.MatchProbe") &&
            IsUserPointer(probe_pawn) && IsUserPointer(entity_root) &&
            QReadT(entity_root + offsets.entity_page_table_offset, entity_page, "CS2.MatchProbe") &&
            IsUserPointer(entity_page)) {
            uintptr_t controllers[4]{};
            int controllerCount = 0;
            EnsureScatter();
            if (g_scatter_full) {
                mem.SetDmaCallTag("CS2.MatchProbe");
                for (int i = 1; i <= 4; ++i)
                    mem.AddScatterReadRequest(g_scatter_full,
                        entity_page + static_cast<uintptr_t>(i) * offsets.entity_identity_stride,
                        &controllers[i - 1], sizeof(uintptr_t));
                const auto probeBegin = std::chrono::steady_clock::now();
                mem.ExecuteReadScatter(g_scatter_full);
                NotePossibleDeviceStall(OmniGhost::Gameplay::TimeMs(probeBegin));
            } else {
                for (int i = 1; i <= 4; ++i)
                    QReadT(entity_page + static_cast<uintptr_t>(i) * offsets.entity_identity_stride,
                           controllers[i - 1], "CS2.MatchProbe");
            }
            for (const uintptr_t controller : controllers)
                controllerCount += IsUserPointer(controller) ? 1 : 0;
            entity_probe_controller_count = controllerCount;
            if (controllerCount >= 2) {
                runtime.in_match = true;
                runtime.local_pawn = probe_pawn;
            }
        }
    }

    // Entering a match (from lobby or after map change) → force entity list re-acquire.
    // Clear Makcu mask: load screens often drop the LMB release packet.
    if (runtime.in_match && !was_in_match) {
        g_pending_entity_list = 0;
        g_entity_list_confirmations = 0;
        runtime.entity_list_entry = 0;
        runtime.entity_list_addr = 0;
        g_cached_entity_root = 0;
        g_entity_list_next_validation_ms = 0;
        g_entity_list_next_full_probe_ms = 0;
        g_entity_list_force_refresh = true;
        zero_player_frames = 0;
        makcu_wrapper::ForceClearButtons();
        const char* match_source = map_reports_match ? "mapa" : "probe de entidades";
        std::cout << "[CS2] Entrada em partida (" << match_source
                  << ", controllers=" << entity_probe_controller_count
                  << ") — a revalidar entity list" << std::endl;
    }
    // Leaving a match → drop avatar cache (memory + %LocalAppData%/OmniGhost/cache/cs2/avatars).
    if (was_in_match && !runtime.in_match) {
        std::cout << "[CS2] Saída de partida — a limpar cache de avatares\n";
    }
    was_in_match = runtime.in_match;

    // LOBBY / MENU: keep light probes only; entity DMA resumes on match enter.
    if (!runtime.in_match) {
        zero_player_frames = 0;
        return;
    }

    // On-demand: no active features → zero entity DMA (pipeline sleeps).
    // Web radar / bomb still handled above or in this branch only if needed.
    if (!NeedsPlayerScan(frame_config)) {
        zero_player_frames = 0;
        runtime.players.clear();
        runtime.player_count = 0;
        runtime.enemy_count = 0;
        runtime.controller_count = 0;
        runtime.pawn_count = 0;
        if (frame_config.bomb_timer && (runtime.frames % 2) == 0)
            UpdateBombStateThrottled();
        else if (!frame_config.bomb_timer)
            runtime.bomb = BombState{};
        // World-only visuals deliberately bypass the player pipeline. They
        // retain their own rate/pressure gates and do not wake controller,
        // pawn, bone or weapon acquisition when no player ESP is enabled.
        // Discovery runs on the optional worker (single-threaded static
        // caches + own scatter handle); results merge on the next scan.
        QueueOptionalDroppedOnly(frame_config);

        // Merge any pending optional results so dropped items still refresh
        // while no player scan is active (this branch returns before the
        // normal merge point below).
        drainOptionalResults(currentGen);
        return;
    }

    // The camera lane already owns the high-rate view-matrix sampling. Reuse
    // its recent immutable snapshot instead of issuing a duplicate QRead in
    // every full player scan. Fall back only when that lane has no usable data.
    bool haveView = false;
    if (const auto camera = Camera::Acquire(); camera && camera->timestamp_ms) {
        const uint64_t now = GetTickCount64();
        // A fresh matrix is ideal, but after a transport stall a slightly old
        // coherent matrix is preferable to scheduling another synchronous
        // QRead.  The render keeps one consistent camera state until the fast
        // camera lane catches up again instead of amplifying the stall.
        if (now >= camera->timestamp_ms && now - camera->timestamp_ms <= 120u) {
            std::memcpy(runtime.view_matrix, camera->view_matrix, sizeof(runtime.view_matrix));
            haveView = true;
        }
    }
    if (!haveView && !ProbeViewMatrix(runtime.view_matrix)) {
        if ((runtime.frames % 180) == 1)
            std::cout << "[CS2] ViewMatrix fail (fails=" << runtime.read_fails << ")" << std::endl;
        g_publish_runtime_this_scan = false;
        return;
    }

    runtime.players.reserve(64);

    // Keep the last validated team so TEAM_INTERVAL_MS is effective.
    // Preserve the last valid yaw on a transient read failure.
    // Preserve the last valid local origin. A transient DMA failure must not
    // make every range calculation originate from world zero.
    // The local pawn/controller pointers rarely change while alive.  Refresh
    // them at a controlled cadence. Health joins the main local-state scatter
    // whenever the cached pawn is valid.
    static uint64_t s_lastLocalBootstrapMs = 0;
    const uint64_t localNowMs = frame_time_ms;
    const bool refreshLocalBootstrap = !IsUserPointer(runtime.local_pawn) ||
        !s_lastLocalBootstrapMs || localNowMs - s_lastLocalBootstrapMs >= 750u;
    uintptr_t localPawn = runtime.local_pawn;
    uintptr_t localController = runtime.local_controller;
    int sampled_health = runtime.local_health;
    EnsureScatter();
    if (g_scatter_full) {
        bool queuedLocalIdentity = false;
        mem.SetDmaCallTag("CS2.LocalIdentity");
        if (refreshLocalBootstrap) {
            mem.AddScatterReadRequest(g_scatter_full, client + offsets.dwLocalPlayerPawn,
                                      &localPawn, sizeof(localPawn));
            if (offsets.dwLocalPlayerController)
                mem.AddScatterReadRequest(g_scatter_full, client + offsets.dwLocalPlayerController,
                                          &localController, sizeof(localController));
            queuedLocalIdentity = true;
        }
        if (queuedLocalIdentity)
            mem.ExecuteReadScatter(g_scatter_full);
    } else {
        if (refreshLocalBootstrap) {
            QReadT(client + offsets.dwLocalPlayerPawn, localPawn, "CS2.LocalPawn");
            if (offsets.dwLocalPlayerController)
                QReadT(client + offsets.dwLocalPlayerController, localController, "CS2.LocalController");
        }
    }
    const uintptr_t previousLocalPawn = runtime.local_pawn;
    runtime.local_pawn = IsUserPointer(localPawn) ? localPawn : 0;
    if (runtime.local_pawn != previousLocalPawn) {
        runtime.local_shots_fired = 0;
        runtime.local_aim_sample_ms = 0;
        std::memset(runtime.local_aim_punch, 0, sizeof(runtime.local_aim_punch));
    }
    if (refreshLocalBootstrap && runtime.local_pawn)
        s_lastLocalBootstrapMs = localNowMs;
    if (refreshLocalBootstrap)
        runtime.local_controller = IsUserPointer(localController) ? localController : 0;
    else if (IsUserPointer(localController))
        runtime.local_controller = localController;
    if (!runtime.local_pawn) {
        runtime.local_health = 0;
    }

    if (runtime.local_pawn) {
        // Do not keep reading local movement,
        // view, item or trigger state after death; observer/bomb handling
        // below is the only remaining DMA work in that state.
        if (runtime.local_health > 0 || refreshLocalBootstrap) {
            static ULONGLONG s_lastTeamMs = 0;
            const ULONGLONG nowT = frame_time_ms;
            const bool refreshTeam = runtime.local_team < 2 || !s_lastTeamMs ||
                (nowT - s_lastTeamMs) > static_cast<ULONGLONG>(TEAM_INTERVAL_MS);

            uint8_t team8 = static_cast<uint8_t>(runtime.local_team);
            uintptr_t scene = 0;
            uintptr_t item_services = 0;
            static uintptr_t cached_local_scene = 0;
            static uintptr_t cached_local_item_services = 0;
            static uintptr_t cached_local_pawn = 0;
            static uint64_t cached_local_scene_until_ms = 0;
            static uint64_t cached_local_item_services_until_ms = 0;
            if (cached_local_pawn != runtime.local_pawn) {
                cached_local_pawn = runtime.local_pawn;
                cached_local_scene = 0;
                cached_local_item_services = 0;
                cached_local_scene_until_ms = 0;
                cached_local_item_services_until_ms = 0;
            }
            const bool refreshLocalSceneMetadata = localNowMs >= cached_local_scene_until_ms ||
                !IsUserPointer(cached_local_scene);
            const bool refreshLocalItemServices = localNowMs >= cached_local_item_services_until_ms ||
                !IsUserPointer(cached_local_item_services);
            scene = cached_local_scene;
            item_services = cached_local_item_services;
            float view_angles[2]{};
            float local_position_sample[3] = { runtime.local_pos[0], runtime.local_pos[1], runtime.local_pos[2] };
            bool scoped = runtime.local_scoped;
            int crosshair = 0;
            int shots = -1; // failed DMA must not masquerade as a fresh sample
            float aimPunch[2] = {
                (std::numeric_limits<float>::quiet_NaN)(),
                (std::numeric_limits<float>::quiet_NaN)()
            };
            const bool needRcsState = frame_config.aim_rcs_auto || frame_config.aim_rcs_standalone;
            runtime.local_has_defuser = false;

            EnsureScatter();
            if (g_scatter_full) {
                if (offsets.m_iHealth)
                    mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_iHealth,
                                              &sampled_health, sizeof(sampled_health));
                if (refreshTeam)
                    mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_iTeamNum,
                                              &team8, sizeof(team8));
                if (refreshLocalSceneMetadata)
                    mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_pGameSceneNode,
                                              &scene, sizeof(scene));
                mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_vecVelocity,
                                          runtime.local_vel, sizeof(runtime.local_vel));
                if (offsets.dwViewAngles)
                    mem.AddScatterReadRequest(g_scatter_full, client + offsets.dwViewAngles,
                                              view_angles, sizeof(view_angles));
                else
                    mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_angEyeAngles,
                                              view_angles, sizeof(view_angles));
                if (offsets.m_bIsScoped)
                    mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_bIsScoped,
                                              &scoped, sizeof(scoped));
                if (refreshLocalItemServices && offsets.m_pItemServices && offsets.m_bHasDefuser)
                    mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_pItemServices,
                                              &item_services, sizeof(item_services));
                if (frame_config.trigger_enabled && frame_config.trigger_use_ident && offsets.m_iIDEntIndex)
                    mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_iIDEntIndex,
                                              &crosshair, sizeof(crosshair));
                if (needRcsState && offsets.m_iShotsFired)
                    mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_iShotsFired,
                                              &shots, sizeof(shots));
                if (needRcsState && offsets.m_aimPunchAngle)
                    mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_aimPunchAngle,
                                              aimPunch, sizeof(aimPunch));
                // Scene/item pointers are stable. Read their dependent values
                // through the previous confirmed pointers in this same batch;
                // a pointer change naturally takes effect on the next scan.
                if (IsUserPointer(cached_local_scene))
                    mem.AddScatterReadRequest(g_scatter_full,
                        cached_local_scene + offsets.m_vecAbsOrigin,
                        local_position_sample, sizeof(local_position_sample));
                else
                    mem.AddScatterReadRequest(g_scatter_full,
                        runtime.local_pawn + offsets.m_vOldOrigin,
                        local_position_sample, sizeof(local_position_sample));
                if (IsUserPointer(cached_local_item_services) && offsets.m_bHasDefuser)
                    mem.AddScatterReadRequest(g_scatter_full,
                        cached_local_item_services + offsets.m_bHasDefuser,
                        &runtime.local_has_defuser, sizeof(runtime.local_has_defuser));
                mem.ExecuteReadScatter(g_scatter_full);
            } else {
                // Scatter handle was unavailable on the primary path — rebuild
                // once and batch every local field into a single round-trip.
                // Individual QReads remain only if EnsureScatter still fails.
                EnsureScatter();
                if (g_scatter_full) {
                    if (offsets.m_iHealth)
                        mem.AddScatterReadRequest(g_scatter_full,
                            runtime.local_pawn + offsets.m_iHealth,
                            &sampled_health, sizeof(sampled_health));
                    if (refreshTeam)
                        mem.AddScatterReadRequest(g_scatter_full,
                            runtime.local_pawn + offsets.m_iTeamNum,
                            &team8, sizeof(team8));
                    mem.AddScatterReadRequest(g_scatter_full,
                        runtime.local_pawn + offsets.m_pGameSceneNode,
                        &scene, sizeof(scene));
                    mem.AddScatterReadRequest(g_scatter_full,
                        runtime.local_pawn + offsets.m_vecVelocity,
                        runtime.local_vel, sizeof(runtime.local_vel));
                    if (offsets.dwViewAngles)
                        mem.AddScatterReadRequest(g_scatter_full,
                            client + offsets.dwViewAngles,
                            view_angles, sizeof(view_angles));
                    else
                        mem.AddScatterReadRequest(g_scatter_full,
                            runtime.local_pawn + offsets.m_angEyeAngles,
                            view_angles, sizeof(view_angles));
                    if (offsets.m_bIsScoped)
                        mem.AddScatterReadRequest(g_scatter_full,
                            runtime.local_pawn + offsets.m_bIsScoped,
                            &scoped, sizeof(scoped));
                    if (offsets.m_pItemServices && offsets.m_bHasDefuser)
                        mem.AddScatterReadRequest(g_scatter_full,
                            runtime.local_pawn + offsets.m_pItemServices,
                            &item_services, sizeof(item_services));
                    if (frame_config.trigger_enabled && frame_config.trigger_use_ident &&
                        offsets.m_iIDEntIndex)
                        mem.AddScatterReadRequest(g_scatter_full,
                            runtime.local_pawn + offsets.m_iIDEntIndex,
                            &crosshair, sizeof(crosshair));
                    if (needRcsState && offsets.m_iShotsFired)
                        mem.AddScatterReadRequest(g_scatter_full,
                            runtime.local_pawn + offsets.m_iShotsFired,
                            &shots, sizeof(shots));
                    if (needRcsState && offsets.m_aimPunchAngle)
                        mem.AddScatterReadRequest(g_scatter_full,
                            runtime.local_pawn + offsets.m_aimPunchAngle,
                            aimPunch, sizeof(aimPunch));
                    mem.SetDmaCallTag("CS2.LocalFallbackScatter");
                    mem.ExecuteReadScatter(g_scatter_full);
                } else {
                    // Last-resort: allocate a one-shot scatter handle so we still
                    // avoid a chain of sequential QReads on the hot path.
                    VMMDLL_SCATTER_HANDLE oneShot = mem.CreateScatterHandle();
                    auto queueLocal = [&](VMMDLL_SCATTER_HANDLE h) {
                        if (offsets.m_iHealth)
                            mem.AddScatterReadRequest(h, runtime.local_pawn + offsets.m_iHealth,
                                &sampled_health, sizeof(sampled_health));
                        if (refreshTeam)
                            mem.AddScatterReadRequest(h, runtime.local_pawn + offsets.m_iTeamNum,
                                &team8, sizeof(team8));
                        mem.AddScatterReadRequest(h, runtime.local_pawn + offsets.m_pGameSceneNode,
                            &scene, sizeof(scene));
                        mem.AddScatterReadRequest(h, runtime.local_pawn + offsets.m_vecVelocity,
                            runtime.local_vel, sizeof(runtime.local_vel));
                        if (offsets.dwViewAngles)
                            mem.AddScatterReadRequest(h, client + offsets.dwViewAngles,
                                view_angles, sizeof(view_angles));
                        else
                            mem.AddScatterReadRequest(h, runtime.local_pawn + offsets.m_angEyeAngles,
                                view_angles, sizeof(view_angles));
                        if (offsets.m_bIsScoped)
                            mem.AddScatterReadRequest(h, runtime.local_pawn + offsets.m_bIsScoped,
                                &scoped, sizeof(scoped));
                        if (offsets.m_pItemServices && offsets.m_bHasDefuser)
                            mem.AddScatterReadRequest(h, runtime.local_pawn + offsets.m_pItemServices,
                                &item_services, sizeof(item_services));
                        if (frame_config.trigger_enabled && frame_config.trigger_use_ident && offsets.m_iIDEntIndex)
                            mem.AddScatterReadRequest(h, runtime.local_pawn + offsets.m_iIDEntIndex,
                                &crosshair, sizeof(crosshair));
                        if (needRcsState && offsets.m_iShotsFired)
                            mem.AddScatterReadRequest(h, runtime.local_pawn + offsets.m_iShotsFired,
                                &shots, sizeof(shots));
                        if (needRcsState && offsets.m_aimPunchAngle)
                            mem.AddScatterReadRequest(h, runtime.local_pawn + offsets.m_aimPunchAngle,
                                aimPunch, sizeof(aimPunch));
                    };
                    if (oneShot) {
                        queueLocal(oneShot);
                        mem.SetDmaCallTag("CS2.LocalOneShotScatter");
                        mem.ExecuteReadScatter(oneShot);
                        mem.CloseScatterHandle(oneShot);
                    } else {
                        // Device refused any scatter handle — single combined
                        // failure is preferable to a multi-QRead stall chain.
                        // Keep last-known local state; next scan retries.
                    }
                }
            }

            // Preserve the last known value on a failed read; only accept a
            // valid game-domain health sample from the completed state batch.
            if (sampled_health >= 0 && sampled_health <= 200)
                runtime.local_health = sampled_health;

            if (refreshTeam && IsPlayableTeam(static_cast<int>(team8))) {
                runtime.local_team = static_cast<int>(team8);
                s_lastTeamMs = nowT;
            }
            if (std::isfinite(view_angles[1])) {
                runtime.local_view_yaw = view_angles[1];
                runtime.local_angles[0] = view_angles[0];
                runtime.local_angles[1] = view_angles[1];
            } else if (offsets.dwViewAngles) {
                // Schema/global drift fallback is rare and deliberately outside the hot batch.
                float fallback_angles[2]{};
                if (QRead(runtime.local_pawn + offsets.m_angEyeAngles, fallback_angles, sizeof(fallback_angles)) &&
                    std::isfinite(fallback_angles[1])) {
                    runtime.local_view_yaw = fallback_angles[1];
                    runtime.local_angles[0] = fallback_angles[0];
                    runtime.local_angles[1] = fallback_angles[1];
                }
            }
            runtime.local_scoped = scoped;
            runtime.local_crosshair_entity = (frame_config.trigger_enabled && frame_config.trigger_use_ident)
                ? crosshair : 0;

            cached_local_scene = IsUserPointer(scene) ? scene : cached_local_scene;
            cached_local_item_services = IsUserPointer(item_services)
                ? item_services : cached_local_item_services;
            if (refreshLocalSceneMetadata)
                cached_local_scene_until_ms = localNowMs + (IsUserPointer(scene) ? 750u : 50u);
            if (refreshLocalItemServices)
                cached_local_item_services_until_ms = localNowMs +
                    (IsUserPointer(item_services) ? 750u : 50u);
            if (!g_scatter_full) {
                runtime.local_has_defuser = false;
                if (IsUserPointer(scene))
                    QRead(scene + offsets.m_vecAbsOrigin, local_position_sample, sizeof(local_position_sample));
                else
                    QRead(runtime.local_pawn + offsets.m_vOldOrigin, local_position_sample, sizeof(local_position_sample));
                if (IsUserPointer(item_services) && offsets.m_bHasDefuser)
                    QReadT(item_services + offsets.m_bHasDefuser, runtime.local_has_defuser);
            }

            if (IsFinitePosition(local_position_sample))
                std::memcpy(runtime.local_pos, local_position_sample, sizeof(runtime.local_pos));

            if (needRcsState && offsets.m_iShotsFired && shots >= 0 && shots < 256) {
                static int previous_shots = -1;
                if (previous_shots >= 0 && shots > previous_shots)
                    runtime.local_last_shot_ms = frame_time_ms;
                previous_shots = shots;
                runtime.local_shots_fired = shots;
            }
            if (needRcsState && shots >= 0 && shots < 256 &&
                std::isfinite(aimPunch[0]) && std::isfinite(aimPunch[1])) {
                runtime.local_aim_punch[0] = aimPunch[0];
                runtime.local_aim_punch[1] = aimPunch[1];
                runtime.local_aim_sample_ms = frame_time_ms;
            }
        } else {
            // Death must not turn local-state acquisition off for the whole
            // bootstrap TTL. A tiny, throttled liveness lane detects respawn
            // promptly without paying for movement, item or trigger fields.
            static uint64_t nextDeadLocalLivenessMs = 0;
            const uint64_t deadNowMs = frame_time_ms;
            if (deadNowMs >= nextDeadLocalLivenessMs) {
                int respawnHealth = runtime.local_health;
                uint8_t controllerAlive = 0xFF;
                bool readAttempted = false;
                EnsureScatter();
                if (g_scatter_full) {
                    if (offsets.m_iHealth) {
                        mem.AddScatterReadRequest(g_scatter_full,
                            runtime.local_pawn + offsets.m_iHealth,
                            &respawnHealth, sizeof(respawnHealth));
                        readAttempted = true;
                    }
                    if (IsUserPointer(runtime.local_controller) && offsets.m_bPawnIsAlive) {
                        mem.AddScatterReadRequest(g_scatter_full,
                            runtime.local_controller + offsets.m_bPawnIsAlive,
                            &controllerAlive, sizeof(controllerAlive));
                        readAttempted = true;
                    }
                    if (readAttempted) {
                        mem.SetDmaCallTag("CS2.LocalRespawnLiveness");
                        mem.ExecuteReadScatter(g_scatter_full);
                    }
                } else if (offsets.m_iHealth) {
                    readAttempted = QReadT(runtime.local_pawn + offsets.m_iHealth, respawnHealth);
                }
                if (controllerAlive == 0)
                    runtime.local_health = 0;
                else if (readAttempted && respawnHealth >= 0 && respawnHealth <= 200)
                    runtime.local_health = respawnHealth;
                // An unknown controller flag never forces a false respawn.
                // The next pass enters the normal lane as soon as the health
                // sample becomes positive.
                nextDeadLocalLivenessMs = deadNowMs + 80u;
            }
            runtime.local_crosshair_entity = 0;
            runtime.local_has_defuser = false;
            runtime.local_scoped = false;
            runtime.local_shots_fired = 0;
            runtime.local_aim_sample_ms = 0;
            std::memset(runtime.local_aim_punch, 0, sizeof(runtime.local_aim_punch));
            std::memset(runtime.local_vel, 0, sizeof(runtime.local_vel));
        }
    } else {
        runtime.local_crosshair_entity = 0;
        runtime.local_has_defuser = false;
    }

    const int preTopologyPressure = g_pressure_level.load(std::memory_order_relaxed);
    bool recoveryLivenessFresh = false;
    if (preTopologyPressure >= 2)
        recoveryLivenessFresh = refresh_cached_liveness(frame_time_ms);
    if (!RefreshEntityListEntry()) {
        // Hold last-good ESP/aim targets briefly so the overlay does not blink
        // off when ListEntry is transiently null after map/round changes.
        const uint64_t now_ms = frame_time_ms;
        if (!recoveryLivenessFresh)
            (void)refresh_cached_liveness(now_ms);
        // Never republish stale world positions on a topology miss. The
        // renderer keeps its already-published snapshot while the next scan
        // reacquires health/origin/velocity from the live entity list.
        g_publish_runtime_this_scan = false;
        return;
    }

    // Renderer FPS must never decide who exists in the acquisition snapshot.
    // Optional lanes have their own DMA-pressure budgets.
    constexpr int kMaxSlots = 64;
    runtime.entity_cap_used = kMaxSlots;

    uintptr_t controllers[kMaxSlots]{};
    static uintptr_t cachedControllers[kMaxSlots]{};
    static int cachedControllerCount = 0;
    static uintptr_t cachedControllerListEntry = 0;
    static uint64_t nextControllerRefreshMs = 0;
    static int emptyEntryScans = 0;
    static uint64_t cachedControllerGeneration = 0;
    if (cachedControllerGeneration != currentGen) {
        std::fill(std::begin(cachedControllers), std::end(cachedControllers), 0);
        cachedControllerCount = 0;
        cachedControllerListEntry = 0;
        nextControllerRefreshMs = 0;
        emptyEntryScans = 0;
        cachedControllerGeneration = currentGen;
    }

    // Prefer the confirmed Source 2 stride (0x70). Fall back to 0x78 only if
    // the primary layout returns no controllers at all.
    const int recoveryPressure = g_pressure_level.load(std::memory_order_relaxed);
    // Pressure sheds optional metadata and bones below, never the core world
    // state. A stale position snapshot is more visible than a slower name or
    // weapon refresh, so recovery continues through health/origin/velocity.
    const uint64_t controllerNowMs = frame_time_ms;
    int collected = 0;
    if (cachedControllerCount > 0 &&
        cachedControllerListEntry == runtime.entity_list_entry &&
        controllerNowMs < nextControllerRefreshMs) {
        std::memcpy(controllers, cachedControllers, sizeof(controllers));
        collected = cachedControllerCount;
    } else {
        collected = CollectControllers(runtime.entity_list_entry, g_controller_stride, controllers, kMaxSlots);
        // Controller metadata changes only on joins/spawns. A 250 ms normal
        // cache prevents a broad list scatter from dominating the DMA lane.
        nextControllerRefreshMs = controllerNowMs + (recoveryPressure >= 1 ? 500u : 250u);
        if (collected > 0) {
            std::memcpy(cachedControllers, controllers, sizeof(controllers));
            cachedControllerCount = collected;
            cachedControllerListEntry = runtime.entity_list_entry;
            emptyEntryScans = 0;
        }
    }
    if (collected == 0) {
        const uintptr_t alternate = (g_controller_stride == offsets.entity_identity_stride)
            ? offsets.entity_identity_stride : offsets.entity_identity_stride;
        collected = CollectControllers(runtime.entity_list_entry, alternate, controllers, kMaxSlots);
        if (collected > 0) {
            g_controller_stride = alternate;
            g_pawn_stride = alternate;
            emptyEntryScans = 0;
        } else {
            // Entry pointer is stale (no controllers on either stride) —
            // request an immediate cached-root/page validation. Keep the old
            // snapshot briefly so a single transport miss does not blink ESP.
            g_entity_list_force_refresh = true;
            g_entity_list_next_validation_ms = 0;
            cachedControllerCount = 0;
            cachedControllerListEntry = 0;
            nextControllerRefreshMs = 0;
            if (++emptyEntryScans >= 6) {
                runtime.entity_list_entry = 0;
                g_pending_entity_list = 0;
                g_entity_list_confirmations = 0;
                g_cached_entity_root = 0;
                g_entity_list_next_full_probe_ms = 0;
                emptyEntryScans = 0;
            }
        }
    } else {
        emptyEntryScans = 0;
    }
    runtime.controller_stride = g_controller_stride;
    runtime.controller_count = collected;

    // The Source 2 controller array is sparse, but every later stage only
    // needs valid controller pointers. Compact it once so a four-player match
    // does not enqueue 64 pawn-handle/page requests on every acquisition scan.
    // This preserves controller/pawn pairing through slot_index while cutting
    // thousands of redundant scatter requests per second.
    int controller_span = 0;
    for (int i = 0; i < kMaxSlots; ++i) {
        if (!IsUserPointer(controllers[i])) continue;
        controllers[controller_span++] = controllers[i];
    }
    for (int i = controller_span; i < kMaxSlots; ++i)
        controllers[i] = 0;
    collected = controller_span;
    runtime.controller_count = collected;

    // While dead, do not enter the entity, position, bone, weapon, visibility
    // or aim acquisition stages.  Spectators and bomb state remain available
    // through their small dedicated paths; a positive local HP next frame
    // immediately falls through to the normal acquisition pipeline again.
    if (runtime.local_health <= 0) {
        runtime.players.clear();
        runtime.player_count = 0;
        runtime.pawn_count = 0;
        runtime.enemy_count = 0;
        UpdateSpectators(frame_config, controllers, collected);
        if (frame_config.bomb_timer)
            UpdateBombStateThrottled();
        else
            runtime.bomb = BombState{};
        return;
    }

    // Calibrate handle→pawn stride against the local player when available.
    if (runtime.local_controller && runtime.local_pawn)
        CalibratePawnStride(runtime.local_controller, runtime.local_pawn);

    // ── Phase 1: scatter all pawn handles in one DMA round-trip ──────────
    uint32_t handles[kMaxSlots]{};
    uintptr_t resolved_by_slot[kMaxSlots]{};
    struct CachedControllerPawn {
        uint32_t handle = 0;
        uintptr_t pawn = 0;
        uint64_t last_update_ms = 0;
        uint64_t last_seen_ms = 0;
        uint64_t entry_generation = 0;
    };
    static std::unordered_map<uintptr_t, CachedControllerPawn> controllerPawnCache;
    static uint64_t controllerPawnCacheGeneration = 0;
    if (controllerPawnCacheGeneration != g_session_generation.load(std::memory_order_acquire)) {
        controllerPawnCache.clear();
        controllerPawnCacheGeneration = g_session_generation.load(std::memory_order_relaxed);
    }
    if (controllerPawnCache.bucket_count() < 128) controllerPawnCache.reserve(128);
    uintptr_t controllersDue[kMaxSlots]{};
    uint32_t refreshedHandles[kMaxSlots]{};
    bool anyHandleDue = false;
    for (int i = 0; i < collected; ++i) {
        auto cached = controllerPawnCache.find(controllers[i]);
        if (cached != controllerPawnCache.end() && cached->second.entry_generation == currentGen) {
            cached->second.last_seen_ms = controllerNowMs;
            handles[i] = cached->second.handle;
            resolved_by_slot[i] = cached->second.pawn;
            if (cached->second.last_update_ms &&
                controllerNowMs - cached->second.last_update_ms < 200u &&
                IsUserPointer(cached->second.pawn))
                continue;
        }
        controllersDue[i] = controllers[i];
        anyHandleDue = true;
    }
    if (anyHandleDue)
        ScatterReadPawnHandles(controllersDue, refreshedHandles, collected);

    uint32_t handlesToResolve[kMaxSlots]{};
    bool anyHandleChanged = false;
    for (int i = 0; i < collected; ++i) {
        if (!controllersDue[i]) continue;
        auto& cached = controllerPawnCache.try_emplace(controllers[i]).first->second;
        cached.entry_generation = currentGen;
        cached.last_seen_ms = controllerNowMs;
        const uint32_t fresh = refreshedHandles[i];
        if (!fresh || fresh == 0xFFFFFFFFu) continue;
        if (fresh == cached.handle && IsUserPointer(cached.pawn)) {
            handles[i] = fresh;
            resolved_by_slot[i] = cached.pawn;
            cached.last_update_ms = controllerNowMs;
            continue;
        }
        handles[i] = fresh;
        handlesToResolve[i] = fresh;
        anyHandleChanged = true;
    }

    uintptr_t newlyResolved[kMaxSlots]{};
    int newlyResolvedCount = 0;
    if (anyHandleChanged) {
        const uintptr_t primaryStride = g_pawn_stride ? g_pawn_stride : offsets.entity_identity_stride;
        const uintptr_t alternateStride = (primaryStride == offsets.entity_identity_stride)
            ? offsets.entity_identity_stride : offsets.entity_identity_stride;
        newlyResolvedCount = ScatterResolvePawnHandles(
            handlesToResolve, newlyResolved, collected,
            primaryStride, alternateStride);
        if (newlyResolvedCount > 0 && g_pawn_stride != primaryStride) {
            g_pawn_stride = primaryStride;
        }
    }
    for (int i = 0; i < collected; ++i) {
        if (!handlesToResolve[i]) continue;
        auto& cached = controllerPawnCache.try_emplace(controllers[i]).first->second;
        cached.entry_generation = currentGen;
        if (IsUserPointer(newlyResolved[i])) {
            cached.handle = handlesToResolve[i];
            cached.pawn = newlyResolved[i];
            cached.last_update_ms = controllerNowMs;
            resolved_by_slot[i] = newlyResolved[i];
        } else {
            cached.pawn = 0;
            cached.last_update_ms = 0;
        }
    }
    if (anyHandleChanged && newlyResolvedCount == 0) {
        g_entity_list_force_refresh = true;
        g_entity_list_next_validation_ms = 0;
    }
    if (controllerPawnCache.size() > 128u) {
        for (auto it = controllerPawnCache.begin(); it != controllerPawnCache.end();) {
            if (controllerNowMs - it->second.last_seen_ms > 30000u)
                it = controllerPawnCache.erase(it);
            else
                ++it;
        }
    }
    int handle_nonzero = 0;
    int resolve_ok = 0;
    for (int i = 0; i < collected; ++i) {
        if (handles[i]) ++handle_nonzero;
        if (IsUserPointer(resolved_by_slot[i])) ++resolve_ok;
    }

    uintptr_t resolved_pawns[kMaxSlots]{};
    int slot_index[kMaxSlots]{};
    int candidate_count = 0;
    for (int i = 0; i < collected && candidate_count < kMaxSlots; ++i) {
        if (!controllers[i] || !handles[i]) continue;
        uintptr_t pawn = resolved_by_slot[i];
        if (!pawn) continue;
        resolved_pawns[candidate_count] = pawn;
        slot_index[candidate_count] = i;
        ++candidate_count;
    }
    runtime.pawn_count = candidate_count;
    runtime.pawn_stride = g_pawn_stride;

    // ── Phase 3: scatter health / team / armor / scene for candidates ────
    // Use unified FeatureFlags for RequiredFields
    CS2::UnifiedConfigAdapter::SyncToUnified(frame_config);
    const auto unifiedFlags = CS2::UnifiedConfigAdapter::GetFeatureFlags();
    const auto fields = unifiedFlags.RequiredFields();

    const bool need_armor = OmniGhost::Gameplay::UnifiedESP::Has(
        fields, OmniGhost::Gameplay::UnifiedESP::DataField::Armor);
    const bool need_yaw = frame_config.radar_2d ||
        OmniGhost::Gameplay::UnifiedESP::Has(
            fields, OmniGhost::Gameplay::UnifiedESP::DataField::Facing);
    const bool track_velocity = frame_config.esp_enabled ||
        OmniGhost::Gameplay::UnifiedESP::Has(
        fields, OmniGhost::Gameplay::UnifiedESP::DataField::Velocity);
    const bool flagsEnabled = false; // retired player flags
    const bool need_scoped = frame_config.trigger_scoped_only ||
        (flagsEnabled && frame_config.flag_scoped);
    const bool need_flash = flagsEnabled && frame_config.flag_blind;

    const uint64_t scan_now_ms = frame_time_ms;
    struct CachedSceneBone {
        uintptr_t scene = 0;
        uintptr_t bone_base = 0;
        uintptr_t bone_offset = 0;
        uint8_t team = 0;
        int armor = 0;
        uintptr_t weapon_services = 0;
        bool spotted = true;
        bool spotted_known = false;
        uint64_t scene_expires_ms = 0;
        uint64_t bone_expires_ms = 0;
        uint64_t team_expires_ms = 0;
        uint64_t armor_expires_ms = 0;
        uint64_t weapon_services_expires_ms = 0;
        uint64_t spotted_expires_ms = 0;
        uint64_t last_seen_ms = 0;
        uint64_t entry_generation = 0;
    };
    static std::unordered_map<uintptr_t, CachedSceneBone> sceneBoneCache;
    static uint64_t sceneBoneCacheGeneration = 0;
    if (sceneBoneCacheGeneration != g_session_generation.load(std::memory_order_acquire)) {
        sceneBoneCache.clear();
        sceneBoneCacheGeneration = g_session_generation.load(std::memory_order_relaxed);
    }
    if (sceneBoneCache.bucket_count() < 128) sceneBoneCache.reserve(128);
    uintptr_t cachedScenes[kMaxSlots]{};
    uint8_t cachedTeams[kMaxSlots]{};
    int cachedArmors[kMaxSlots]{};
    uintptr_t cachedWeaponServices[kMaxSlots]{};
    bool cachedSpotted[kMaxSlots]{};
    bool refreshScenes[kMaxSlots]{};
    bool refreshTeams[kMaxSlots]{};
    bool refreshArmors[kMaxSlots]{};
    bool refreshWeaponServices[kMaxSlots]{};
    bool refreshSpotted[kMaxSlots]{};
    float positions[kMaxSlots][3]{};
    for (int c = 0; c < candidate_count; ++c) {
        positions[c][0] = positions[c][1] = positions[c][2] =
            std::numeric_limits<float>::quiet_NaN();
        const auto cached = sceneBoneCache.find(resolved_pawns[c]);
        if (cached != sceneBoneCache.end() && cached->second.entry_generation == currentGen) {
            cachedScenes[c] = cached->second.scene;
            cachedTeams[c] = cached->second.team;
            cachedArmors[c] = cached->second.armor;
            cachedWeaponServices[c] = cached->second.weapon_services;
            cachedSpotted[c] = cached->second.spotted;
            cached->second.last_seen_ms = scan_now_ms;
            refreshScenes[c] = !IsUserPointer(cached->second.scene) ||
                scan_now_ms >= cached->second.scene_expires_ms;
            refreshTeams[c] = !IsPlayableTeam(static_cast<int>(cached->second.team)) ||
                scan_now_ms >= cached->second.team_expires_ms;
            refreshArmors[c] = need_armor &&
                scan_now_ms >= cached->second.armor_expires_ms;
            refreshWeaponServices[c] = scan_now_ms >= cached->second.weapon_services_expires_ms;
            refreshSpotted[c] = !cached->second.spotted_known ||
                scan_now_ms >= cached->second.spotted_expires_ms;
        } else {
            refreshScenes[c] = true;
            refreshTeams[c] = true;
            refreshArmors[c] = need_armor;
            refreshWeaponServices[c] = true;
            refreshSpotted[c] = true;
        }
    }
    PawnCoreFields core[kMaxSlots]{};
    uintptr_t candidateControllers[kMaxSlots]{};
    for (int c = 0; c < candidate_count; ++c)
        candidateControllers[c] = controllers[slot_index[c]];
    const bool need_weapon = OmniGhost::Gameplay::UnifiedESP::Has(
        fields, OmniGhost::Gameplay::UnifiedESP::DataField::Weapon);
    const bool need_spotted = (frame_config.aim_enabled && frame_config.aim_visibility_check) ||
        (frame_config.trigger_enabled && frame_config.aim_visibility_check);
    const bool need_weapon_services = need_weapon || need_spotted;
    // Health/team/scene stay on the full cadence. Armor is only four bytes per
    // pawn, so include it here instead of paying for a separate round-trip.
    mem.SetDmaCallTag("CS2.PawnCore");
    const auto _posBegin = std::chrono::steady_clock::now();
    const uint64_t position_read_begin_ms = GetTickCount64();
    ScatterReadPawnCore(resolved_pawns, candidateControllers, core, candidate_count, need_armor,
                        need_scoped, need_flash, need_yaw, need_weapon_services,
                        need_spotted, cachedTeams, refreshTeams,
                        cachedArmors, refreshArmors,
                        cachedScenes, refreshScenes, positions, track_velocity,
                        cachedWeaponServices, refreshWeaponServices,
                        cachedSpotted, refreshSpotted);
    const uint64_t position_read_end_ms = GetTickCount64();
    const uint64_t position_sample_ms = position_read_begin_ms +
        (position_read_end_ms - position_read_begin_ms) / 2u;
    runtime.world_sample_ms = position_sample_ms;
    g_phase.positions_ms = OmniGhost::Gameplay::TimeMs(_posBegin);
    NotePossibleDeviceStall(g_phase.positions_ms);
    for (int c = 0; c < candidate_count; ++c) {
        auto& cached = sceneBoneCache[resolved_pawns[c]];
        cached.entry_generation = currentGen;
        cached.last_seen_ms = scan_now_ms;
        if (IsPlayableTeam(static_cast<int>(core[c].team))) {
            cached.team = core[c].team;
            cached.team_expires_ms = scan_now_ms + 500u;
        } else {
            core[c].team = cached.team;
        }
        if (need_armor && refreshArmors[c] && core[c].armor >= 0 && core[c].armor <= 200) {
            cached.armor = core[c].armor;
            cached.armor_expires_ms = scan_now_ms + 75u;
        } else if (need_armor) {
            core[c].armor = cached.armor;
        }
        if (IsUserPointer(core[c].scene)) {
            if (IsUserPointer(cached.scene) && cached.scene != core[c].scene) {
                cached.bone_base = 0;
                cached.bone_offset = 0;
                cached.bone_expires_ms = 0;
            }
            cached.scene = core[c].scene;
            cached.scene_expires_ms = scan_now_ms + 500u;
        } else {
            core[c].scene = cached.scene;
        }
        // Cache weapon_services (stable pointer, long TTL)
        if (need_weapon_services && refreshWeaponServices[c] && IsUserPointer(core[c].weapon_services)) {
            cached.weapon_services = core[c].weapon_services;
            cached.weapon_services_expires_ms = scan_now_ms + 500u;
        }
        // Cache spotted (changes frequently, shorter TTL)
        if (need_spotted && refreshSpotted[c] && core[c].spotted_known) {
            cached.spotted = core[c].spotted;
            cached.spotted_known = true;
            cached.spotted_expires_ms = scan_now_ms + 75u;
        } else if (need_spotted) {
            core[c].spotted = cached.spotted;
            core[c].spotted_known = cached.spotted_known;
        }
    }

    // Deathmatch can transition dead → respawning before the optional bone
    // reads finish. Publish health immediately so presentation can hide a dead
    // pawn without waiting for the complete entity snapshot.
    LivenessSnapshot liveness{};
    liveness.timestamp_ms = frame_time_ms;
    liveness.generation = g_session_generation.load(std::memory_order_acquire);
    std::array<uintptr_t, 64> confirmedDead{};
    size_t confirmedDeadCount = 0;
    for (int c = 0; c < candidate_count &&
         liveness.count < static_cast<uint32_t>(liveness.players.size()); ++c) {
        const bool readOk = core[c].read_ok;
        const bool healthKnown = readOk && core[c].health >= 0 && core[c].health <= 200;
        const bool controllerKnown = readOk && (core[c].controller_alive == 0 ||
            core[c].controller_alive == 1);
        const bool dead = (healthKnown && core[c].health <= 0) ||
            (controllerKnown && core[c].controller_alive == 0);
        if (!readOk)
            continue; // both reads failed: retain grace, never call it death
        auto& sample = liveness.players[liveness.count++];
        sample.pawn = resolved_pawns[c];
        sample.sample_ms = position_sample_ms;
        sample.generation = liveness.generation;
        sample.alive = !dead;
        if (dead) {
            confirmedDead[confirmedDeadCount++] = sample.pawn;
            // A respawn may replace this controller's pawn before the normal
            // topology TTL. Re-read its handle on the very next core scan.
            controllerPawnCache.erase(candidateControllers[c]);
        }
    }
    PublishLivenessSnapshot(liveness);
    {
        std::unique_lock lock(g_cache_mutex);
        for (size_t i = 0; i < confirmedDeadCount; ++i) {
            const uintptr_t pawn = confirmedDead[i];
            if (auto it = recent_players.find(pawn); it != recent_players.end())
                it->second.confirmed_dead = true;
            bone_cache.erase(pawn);
            s_stickyBoneLayout.erase(pawn);
            sceneBoneCache.erase(pawn);
            s_lastBoneMs.erase(pawn);
            last_good_players.erase(std::remove_if(last_good_players.begin(),
                last_good_players.end(), [pawn](const RecoveryIdentity& p) { return p.pawn == pawn; }),
                last_good_players.end());
        }
    }

    int validCoreSamples = 0;
    for (int c = 0; c < candidate_count; ++c)
        validCoreSamples += IsPlayableTeam(static_cast<int>(core[c].team)) ? 1 : 0;
    if (candidate_count > 0 && validCoreSamples == 0) {
        // Cached pawn addresses stopped producing valid core data. Revalidate
        // root/page immediately and rebuild the topology on the next scan.
        g_entity_list_force_refresh = true;
        g_entity_list_next_validation_ms = 0;
        for (int c = 0; c < candidate_count; ++c)
            controllerPawnCache.erase(controllers[slot_index[c]]);
        g_publish_runtime_this_scan = false;
        return;
    }

    // Health and team are the minimum discriminator reads: without them we
    // cannot know which pawns to exclude. Compact the candidate set here so
    // dead players and hidden team-mates never reach position, bone, weapon or
    // name acquisition. Spectators are resolved later from controllers through
    // their own lightweight observer path.
    int eligible_count = 0;
    // The local pawn has a dedicated snapshot and must never enter the remote
    for (int c = 0; c < candidate_count; ++c) {
        const bool is_local = resolved_pawns[c] == runtime.local_pawn ||
            controllers[slot_index[c]] == runtime.local_controller;
        const bool healthAlive = core[c].health > 0 && core[c].health <= 200;
        const bool controllerKnown = core[c].controller_alive == 0 ||
            core[c].controller_alive == 1;
        const bool alive = healthAlive &&
            (!controllerKnown || core[c].controller_alive != 0) &&
            IsPlayableTeam(static_cast<int>(core[c].team));
        const bool hidden_teammate = frame_config.team_check &&
            static_cast<int>(core[c].team) == runtime.local_team;
        if (!alive || is_local || hidden_teammate) continue;
        // Keep every valid core sample; presentation owns the distance filter.
        if (!IsFinitePosition(positions[c]))
            continue;
        if (eligible_count != c) {
            resolved_pawns[eligible_count] = resolved_pawns[c];
            slot_index[eligible_count] = slot_index[c];
            candidateControllers[eligible_count] = candidateControllers[c];
            core[eligible_count] = core[c];
            std::memcpy(positions[eligible_count], positions[c], sizeof(positions[c]));
        }
        ++eligible_count;
    }
    candidate_count = eligible_count;
    runtime.pawn_count = candidate_count;

    // ── Early core publish (P0): positions / health / velocity / liveness ─
    // Presentation must not wait for bones, weapons, names or dropped items.
    // Build a core-only player list, publish immediately, then continue the
    // optional lanes which will overwrite runtime.players before the final
    // publish at the end of the scan.
    {
        ScopedPhase _phEarly(&g_phase.early_publish_ms);
        runtime.players.clear();
        runtime.enemy_count = 0;
        runtime.world_sample_ms = position_sample_ms ? position_sample_ms : frame_time_ms;
        for (int c = 0; c < candidate_count; ++c) {
            const uintptr_t pawn = resolved_pawns[c];
            const uintptr_t controller = candidateControllers[c];
            if (!IsUserPointer(pawn)) continue;
            if (!core[c].read_ok) continue; // BUG-008: Skip unread pawns
            const bool healthAlive = core[c].health > 0 && core[c].health <= 200;
            const bool controllerKnown = core[c].controller_alive == 0 ||
                core[c].controller_alive == 1;
            if (!healthAlive) continue;
            if (controllerKnown && core[c].controller_alive == 0) continue;
            if (!IsPlayableTeam(static_cast<int>(core[c].team))) continue;
            if (!IsFinitePosition(positions[c])) continue;

            Player corePlayer{};
            corePlayer.controller = controller;
            corePlayer.pawn = pawn;
            corePlayer.scene = IsUserPointer(core[c].scene) ? core[c].scene : 0;
            corePlayer.health = core[c].health;
            corePlayer.armor = core[c].armor;
            corePlayer.team = static_cast<int>(core[c].team);
            corePlayer.alive = true;
            corePlayer.is_bot = core[c].controller_steam_id == 0;
            corePlayer.is_local = pawn == runtime.local_pawn ||
                controller == runtime.local_controller;
            corePlayer.pos[0] = positions[c][0];
            corePlayer.pos[1] = positions[c][1];
            corePlayer.pos[2] = positions[c][2];
            corePlayer.position_sample_ms = position_sample_ms;
            // Source units are inches; convert to metres once for all consumers.
            const float dx_dist = corePlayer.pos[0] - runtime.local_pos[0];
            const float dy_dist = corePlayer.pos[1] - runtime.local_pos[1];
            const float dz_dist = corePlayer.pos[2] - runtime.local_pos[2];
            // BAIXO #24: Fast inverse square root using SSE rsqrt (approx 12-bit precision)
            // dist = sqrt(dx²+dy²+dz²) * 0.0254; compute as dist_sq * rsqrt(dist_sq)
            const float dist_sq = dx_dist * dx_dist + dy_dist * dy_dist + dz_dist * dz_dist;
            const __m128 dist_vec = _mm_set_ss(dist_sq + 1e-6f);
            const float inv_dist = _mm_cvtss_f32(_mm_rsqrt_ss(dist_vec));
            corePlayer.distance = dist_sq * inv_dist * 0.0254f;
            if (track_velocity) {
                for (int axis = 0; axis < 3; ++axis) {
                    const float v = core[c].velocity[axis];
                    corePlayer.velocity[axis] =
                        std::isfinite(v) && std::fabs(v) < 5000.f ? v : 0.f;
                }
            }
            {
                const float speed = std::sqrt(
                    corePlayer.velocity[0] * corePlayer.velocity[0] +
                    corePlayer.velocity[1] * corePlayer.velocity[1]);
                corePlayer.move_speed = std::isfinite(speed) ? speed : 0.f;
                corePlayer.is_moving = corePlayer.move_speed > 5.f;
            }
            // Carry last-good bones from cache so skeleton does not blink off
            // between optional refreshes — presentation enforces visual age cap.
            {
                std::shared_lock lock(g_cache_mutex);
                if (auto bit = bone_cache.find(pawn); bit != bone_cache.end() &&
                    bit->second.entry_generation == currentGen &&
                    bit->second.last_valid_ms) {
                std::memcpy(corePlayer.bones, bit->second.joints, sizeof(corePlayer.bones));
                const float cachedShift[3] = {
                    corePlayer.pos[0] - bit->second.origin[0],
                    corePlayer.pos[1] - bit->second.origin[1],
                    corePlayer.pos[2] - bit->second.origin[2]
                };
                for (std::size_t bone = 0; bone < kBoneSlotCount; ++bone) {
                    const auto& joint = corePlayer.bones[bone];
                    if (joint[0] == 0.f && joint[1] == 0.f && joint[2] == 0.f)
                        continue; // keep rejected joints invalid after translation
                    for (int axis = 0; axis < 3; ++axis)
                        corePlayer.bones[bone][axis] += cachedShift[axis];
                }
                corePlayer.bones_ok = true;
                corePlayer.full_bones_ok = bit->second.full_pose;
                corePlayer.bones_sample_ms = bit->second.last_valid_ms;
                corePlayer.bone_pose_sample_ms = bit->second.last_valid_ms;
                corePlayer.bone_origin_sample_ms = corePlayer.position_sample_ms;
                corePlayer.head[0] = corePlayer.bones[0][0];
                corePlayer.head[1] = corePlayer.bones[0][1];
                corePlayer.head[2] = corePlayer.bones[0][2];
            }
        }
        {
            std::shared_lock lock(g_cache_mutex);
            if (auto rit = recent_players.find(pawn); rit != recent_players.end() &&
                rit->second.entry_generation == currentGen) {
                const bool sameController = rit->second.player.controller == controller;
                if (sameController && rit->second.player.name[0]) {
                    std::snprintf(corePlayer.name, sizeof(corePlayer.name), "%s",
                        rit->second.player.name);
                    corePlayer.is_bot = corePlayer.is_bot || rit->second.player.is_bot;
                    rit->second.last_seen_ms = scan_now_ms;
                }
                if (rit->second.player.weapon[0])
                    std::snprintf(corePlayer.weapon, sizeof(corePlayer.weapon), "%s",
                        rit->second.player.weapon);
                corePlayer.weapon_def = rit->second.player.weapon_def;
                corePlayer.spotted = rit->second.player.spotted;
                corePlayer.visibility_known = rit->second.player.visibility_known;
                corePlayer.visible = rit->second.player.visible;
            }
        }
        // Keep bots in the acquisition snapshot: the optional identity
        // worker must see them before the renderer can filter them.
        if (!corePlayer.is_local && corePlayer.team != runtime.local_team)
            ++runtime.enemy_count;
        runtime.players.push_back(corePlayer);
        }
        runtime.player_count = static_cast<int>(runtime.players.size());
        if (g_publish_runtime_this_scan)
            PublishRuntimeSnapshot();
    }

    // P0: optional DMA (bones/weapons/names/flags/dropped) runs on the
    // optional worker. Core acquisition returns here so the next world scan
    // is never blocked by metadata. Presentation already has core state.
    {
        const uint64_t nowMs = frame_time_ms;
        if (!runtime.players.empty()) {
            last_good_players.clear();
            last_good_players.reserve(runtime.players.size());
            for (const auto& player : runtime.players)
                last_good_players.push_back({player.pawn, player.controller});
            last_good_ms = nowMs;
            OmniGhost::Gameplay::DmaTelemetry::CS2().last_good_age_ms.store(
                0, std::memory_order_relaxed);
        }
        // Bomb / local-weapon metadata are optional-pressure gated and cheap
        // enough to keep on the core path without blocking the next scan.
        if (!runtime.in_match || !frame_config.bomb_timer)
            runtime.bomb = BombState{};
        else if (g_pressure_level.load(std::memory_order_relaxed) < 2)
            UpdateBombStateThrottled();
        if (g_pressure_level.load(std::memory_order_relaxed) < 2)
            UpdateLocalWeaponDefinitionThrottled();
    }
    // EntityCacheHub policy on the core path: prune maps that feed the early
    // publish carry (bones/names) so pointer reuse cannot outlive last_seen.
    {
        constexpr uint64_t kGraceMs = 5000;
        const uint64_t nowMs = frame_time_ms;
        std::unique_lock lock(g_cache_mutex);
        auto prune = [&](auto& map, auto getLast) {
            for (auto it = map.begin(); it != map.end();) {
                const uint64_t last = getLast(it->second);
                if (last && nowMs > last && (nowMs - last) > kGraceMs)
                    it = map.erase(it);
                else
                    ++it;
            }
        };
        prune(recent_players, [](const RecentPlayer& e) { return e.last_seen_ms; });
        prune(bone_cache, [](const CachedBones& e) { return e.last_valid_ms; });
    }
    auto& cacheMetrics = CacheManager::Metrics();
    cacheMetrics.recent_players.store(recent_players.size(), std::memory_order_relaxed);
    cacheMetrics.bones.store(bone_cache.size(), std::memory_order_relaxed);
    cacheMetrics.controller_pawns.store(controllerPawnCache.size(), std::memory_order_relaxed);
    cacheMetrics.scene_bones.store(sceneBoneCache.size(), std::memory_order_relaxed);
    QueueOptionalEnrichment(frame_config);
    {
        // Residual phase accounting for core-only scans.
        const float wall = OmniGhost::Gameplay::TimeMs(_frameWallBegin);
        const float accounted = g_phase.webradar_ms + g_phase.entity_ms + g_phase.local_ms
            + g_phase.core_scatter_ms + g_phase.positions_ms + g_phase.early_publish_ms
            + g_phase.publish_ms;
        g_phase.other_ms = (std::max)(0.f, wall - accounted);
        g_phase.optional_budget_left_ms = 0.f;
    }
    return; // core path done — optional worker continues enrichment
}


