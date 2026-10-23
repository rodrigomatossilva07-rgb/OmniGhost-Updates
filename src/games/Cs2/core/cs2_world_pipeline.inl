


static void RunFrameWithConfig(const Config& frame_config) {
    static std::mutex s_frame_mutex;
    std::unique_lock<std::mutex> frame_call_lock(s_frame_mutex);
    runtime.read_fails = g_read_fails.load(std::memory_order_relaxed);


    const int radarPort = frame_config.webradar_port > 0 && frame_config.webradar_port <= 65535
        ? frame_config.webradar_port : 8080;
    g_web_radar_desired_port.store(radarPort, std::memory_order_relaxed);
    g_web_radar_desired_cloudflare.store(frame_config.webradar_cloudflare, std::memory_order_relaxed);
    g_web_radar_desired_enabled.store(frame_config.webradar_enabled, std::memory_order_release);

    if (!ready || !offsets.loaded || !runtime.client_base)
        return;


    const uint64_t frame_time_ms = CS2::SampleClockMs();



    if (frame_config.telemetry_enabled) {
        static uint64_t s_cfgBits = 0;
        static bool s_cfgInitialized = false;
        uint64_t bits = 0;
        auto bit = [&](bool v, int i) { if (v) bits |= (1ull << i); };
        bit(frame_config.radar_2d || frame_config.webradar_enabled, 0);
        bit(frame_config.aim_enabled, 1);
        bit(frame_config.trigger_enabled, 2);
        bit(frame_config.bomb_timer, 3);
        bit(frame_config.spectator_list, 4);
        bit(frame_config.performance_mode, 5);
        if (!s_cfgInitialized || bits != s_cfgBits) {
            s_cfgInitialized = true;
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

    struct PhaseFinalize {
        decltype(_frameWallBegin) begin;
        ~PhaseFinalize() {
            const float wall = OmniGhost::Gameplay::TimeMs(begin);
            const float accounted = g_phase.webradar_ms + g_phase.entity_ms + g_phase.local_ms
                + g_phase.core_scatter_ms + g_phase.positions_ms + g_phase.early_publish_ms
                + g_phase.publish_ms;
            g_phase.other_ms = (std::max)(0.f, wall - accounted);
            g_phase.optional_budget_left_ms = 0.f;
        }
    } phase_finalize{_frameWallBegin};
    ++runtime.frames;
    runtime.fps = g_presentation_fps.load(std::memory_order_relaxed);

    uint64_t currentGen = g_session_generation.load(std::memory_order_acquire);
    const uintptr_t client = runtime.client_base;


    struct RecoveryIdentity { uintptr_t pawn = 0; uintptr_t controller = 0; };
    static std::vector<RecoveryIdentity> last_good_players;
    static uint64_t last_good_ms = 0;

    constexpr size_t kMaxRecentPlayers = 128;
    constexpr size_t kMaxBoneCache = 128;

    struct RecentPlayer {
        Player player{};
        uint64_t last_seen_ms = 0;
        uint64_t entry_generation = 0;
        bool confirmed_dead = false;
        uint64_t weapon_received_ms = 0;
        uint64_t visibility_received_ms = 0;
    };
    struct CachedBones {
        float joints[kBoneSlotCount][3]{};
        float origin[3]{};
        uintptr_t controller = 0;
        uint64_t last_valid_ms = 0;
        bool full_pose = false;
        uint64_t entry_generation = 0;
    };
    static std::unordered_map<uintptr_t, RecentPlayer> recent_players;
    static std::unordered_map<uintptr_t, CachedBones> bone_cache;
    static uint64_t recoveryCacheGeneration = 0;
    static std::shared_mutex g_cache_mutex;
    if (recoveryCacheGeneration != g_session_generation.load(std::memory_order_acquire)) {
        std::unique_lock cache_lock(g_cache_mutex);
        last_good_players.clear(); last_good_ms = 0;
        recent_players.clear(); bone_cache.clear();
        recoveryCacheGeneration = g_session_generation.load(std::memory_order_acquire);
    }
    if (recent_players.bucket_count() < 128) recent_players.reserve(128);
    if (bone_cache.bucket_count() < 128) bone_cache.reserve(128);




    const auto drainOptionalResults = [&](uint64_t generation) {
        auto opt = g_optional_results.Acquire();
        if (!opt || opt->generation != generation)
            return;
        const uint64_t mergeNowMs = frame_time_ms;

        g_phase.bones_ms += opt->bones_ms;
        g_phase.weapon_ms += opt->weapon_ms;
        g_phase.optional_budget_left_ms = opt->budget_left_ms;
        g_phase.bones_players += static_cast<int>(opt->bone_count);

        {
            std::unique_lock lock(g_cache_mutex);
            for (uint32_t i = 0; i < opt->bone_count && i < opt->bones.size(); ++i) {
                const OptionalBoneResult& r = opt->bones[i];
                if (!IsUserPointer(r.pawn) || !IsUserPointer(r.controller) ||
                    !r.bones_ok || !r.sample_ms) continue;
                if (r.sample_ms > mergeNowMs || mergeNowMs - r.sample_ms > 400u) continue;
                if (r.bones.size() * sizeof(r.bones[0]) < sizeof(CachedBones::joints)) continue;
                CachedBones candidate{};
                std::memcpy(candidate.joints, r.bones.data(), sizeof(candidate.joints));
                std::memcpy(candidate.origin, r.origin, sizeof(candidate.origin));
                bool valid = true;
                for (float coordinate : candidate.origin) valid = valid && std::isfinite(coordinate);
                for (const auto& joint : candidate.joints)
                    for (float coordinate : joint) valid = valid && std::isfinite(coordinate);
                if (!valid) continue;
                const auto liveIt = recent_players.find(r.pawn);
                if (liveIt != recent_players.end() && liveIt->second.entry_generation == generation &&
                    liveIt->second.confirmed_dead) continue;
                CachedBones& cb = bone_cache[r.pawn];
                if (cb.entry_generation != generation ||
                    (cb.controller && cb.controller != r.controller)) {
                    cb = CachedBones{};
                    cb.entry_generation = generation;
                }
                if (cb.last_valid_ms > r.sample_ms) continue;
                std::memcpy(cb.joints, candidate.joints, sizeof(cb.joints));
                cb.controller = r.controller;
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
                if (rp.entry_generation != generation ||
                    (rp.player.controller && rp.player.controller != r.controller)) {
                    rp = RecentPlayer{};
                    rp.player.pawn = r.pawn;
                    rp.entry_generation = generation;
                }
                if (rp.confirmed_dead) continue;
                rp.last_seen_ms = mergeNowMs;
                rp.player.controller = r.controller;
                std::snprintf(rp.player.name, sizeof(rp.player.name), "%.*s",
                    static_cast<int>(sizeof(r.name)), r.name);
                rp.player.is_bot = IsBotPlayerName(rp.player.name);
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
                if (rp.confirmed_dead) continue;
                rp.last_seen_ms = mergeNowMs;
                rp.weapon_received_ms = mergeNowMs;
                if (r.weapon_def > 0) rp.player.weapon_def = r.weapon_def;
                if (r.weapon[0])
                    std::snprintf(rp.player.weapon, sizeof(rp.player.weapon), "%.*s",
                        static_cast<int>(sizeof(r.weapon)), r.weapon);
                if (r.ammo_clip >= 0) rp.player.ammo_clip = r.ammo_clip;


                if (r.ammo_reserve >= 0) rp.player.ammo_reserve = r.ammo_reserve;
            }
        }

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
                if (rp.confirmed_dead) continue;
                rp.last_seen_ms = mergeNowMs;
                rp.visibility_received_ms = mergeNowMs;
                rp.player.visibility_known = true;
                rp.player.visible = r.visible;
                rp.player.bone_visibility_known_mask = r.point_known_mask;
                rp.player.bone_visibility_mask = r.point_visible_mask;
            }
        }
        {
            std::unique_lock lock(g_cache_mutex);


            if (frame_config.dropped_weapons && opt->dropped_weapons_valid) {
                runtime.dropped_weapons = opt->dropped_weapons;
                runtime.dropped_weapons.reserve(24);
            }
        }
    };
    drainOptionalResults(currentGen);





    const auto refresh_cached_liveness = [&](uint64_t now_ms) {
        static uint64_t s_nextRecoveryReadMs = 0;
        constexpr uint64_t kRecoveryIdentityMaxAgeMs = 2000;
        if (!last_good_ms || now_ms < last_good_ms ||
            now_ms - last_good_ms > kRecoveryIdentityMaxAgeMs) {
            last_good_players.clear();
            return false;
        }
        if (now_ms < s_nextRecoveryReadMs || last_good_players.empty() || !offsets.m_iHealth)
            return false;
        s_nextRecoveryReadMs = now_ms +
            (g_pressure_level.load(std::memory_order_relaxed) >= 2 ? 250u : 120u);
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
        for (size_t i = 0; i < count && liveness.count < liveness.players.size(); ++i) {
            if (!IsUserPointer(last_good_players[i].pawn)) continue;
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
            }
        }
        auto& telemetry = OmniGhost::Gameplay::DmaTelemetry::CS2();
        telemetry.pressure.last_good_age_ms.store(last_good_ms && now_ms >= last_good_ms
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




    runtime.spectators.clear();
    runtime.spectator_count = 0;
    if (!frame_config.dropped_weapons)
        runtime.dropped_weapons.clear();





    static char previous_map[64]{};
    static uint64_t next_map_refresh_ms = 0;
    const uint64_t map_now_ms = frame_time_ms;
    if (map_now_ms >= next_map_refresh_ms || g_force_map_refresh) {
        g_force_map_refresh = false;
        mem.SetDmaCallTag("CS2.MapRefresh");
        RefreshMapName();




        const int pressure = g_pressure_level.load(std::memory_order_relaxed);



        next_map_refresh_ms = map_now_ms + (runtime.in_match
            ? (pressure >= 2 ? 5000u : 2000u)
            : 1000u);
    }





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
            last_good_players.clear();
            last_good_ms = 0;
            recoveryCacheGeneration = currentGen;
        }
        runtime.dropped_weapons.clear();
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




    const bool needs_collision = (frame_config.esp_enabled &&
        (frame_config.visibility_colors || frame_config.visible_check)) ||
        (frame_config.aim_enabled &&
        (frame_config.aim_visibility_check || frame_config.aim_visible_bones_only));
    g_collision_loader.Request(runtime.map_name, needs_collision, map_now_ms);

    const bool map_reports_match = IsPlayableMapName(runtime.map_name);
    runtime.in_match = map_reports_match;
    int entity_probe_controller_count = 0;



    static bool was_in_match = false;





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



    if (runtime.in_match && !was_in_match) {
        g_pending_entity_list = 0;
        g_entity_list_confirmations = 0;
        runtime.entity_list_entry = 0;
        runtime.entity_list_addr = 0;
        g_cached_entity_root = 0;
        g_entity_list_next_validation_ms = 0;
        g_entity_list_next_full_probe_ms = 0;
        g_entity_list_force_refresh = true;
        makcu_wrapper::ForceClearButtons();
        const char* match_source = map_reports_match ? "mapa" : "probe de entidades";
        std::cout << "[CS2] Entrada em partida (" << match_source
                  << ", controllers=" << entity_probe_controller_count
                  << ") — a revalidar entity list" << std::endl;
    }

    if (was_in_match && !runtime.in_match) {
        std::cout << "[CS2] Saída de partida — a limpar cache de avatares\n";
    }
    was_in_match = runtime.in_match;


    if (!runtime.in_match) {
        runtime.players.clear();
        runtime.dropped_weapons.clear();
        runtime.bomb = BombState{};
        PublishRuntimeSnapshot();
        g_publish_runtime_this_scan = false;
        return;
    }



    if (!NeedsPlayerScan(frame_config)) {
        runtime.players.clear();
        runtime.player_count = 0;
        runtime.enemy_count = 0;
        runtime.controller_count = 0;
        runtime.pawn_count = 0;
        if (frame_config.bomb_timer && (runtime.frames % 2) == 0)
            UpdateBombStateThrottled();
        else if (!frame_config.bomb_timer)
            runtime.bomb = BombState{};







        if (frame_config.dropped_weapons)
            QueueOptionalDroppedOnly(frame_config);




        drainOptionalResults(currentGen);
        PublishRuntimeSnapshot();
        g_publish_runtime_this_scan = false;
        return;
    }




    auto viewIsFinite = [](const auto& matrix) {
        for (int i = 0; i < 16; ++i)
            if (!std::isfinite(matrix[i])) return false;
        return true;
    };
    bool haveView = false;
    if (const auto camera = Camera::Acquire(); camera && camera->timestamp_ms) {
        const uint64_t now = GetTickCount64();




        if (now >= camera->timestamp_ms && now - camera->timestamp_ms <= 120u &&
            viewIsFinite(camera->view_matrix)) {
            std::memcpy(runtime.view_matrix, camera->view_matrix, sizeof(runtime.view_matrix));
            haveView = true;
        }
    }
    static float s_lastViewMatrix[16]{};
    static uint64_t s_lastViewMatrixMs = 0;
    static uint64_t s_viewMatrixGen = 0;
    if (s_viewMatrixGen != currentGen) {
        s_viewMatrixGen = currentGen;
        s_lastViewMatrixMs = 0;
        std::fill(std::begin(s_lastViewMatrix), std::end(s_lastViewMatrix), 0.f);
    }
    float probedView[16];
    std::fill(std::begin(probedView), std::end(probedView),
        (std::numeric_limits<float>::quiet_NaN)());
    const bool probedFresh = !haveView && ProbeViewMatrix(probedView) && viewIsFinite(probedView);
    if (probedFresh)
        std::memcpy(runtime.view_matrix, probedView, sizeof(probedView));
    if (haveView || probedFresh) {
        std::memcpy(s_lastViewMatrix, runtime.view_matrix, sizeof(s_lastViewMatrix));
        s_lastViewMatrixMs = GetTickCount64();
    } else {
        const uint64_t viewFallbackNowMs = GetTickCount64();
        if (s_lastViewMatrixMs && viewFallbackNowMs >= s_lastViewMatrixMs &&
            viewFallbackNowMs - s_lastViewMatrixMs <= 500u) {
            std::memcpy(runtime.view_matrix, s_lastViewMatrix, sizeof(runtime.view_matrix));
        } else {
            if ((runtime.frames % 180) == 1)
                std::cout << "[CS2] ViewMatrix fail (fails=" << runtime.read_fails << ")" << std::endl;
            runtime.players.clear();
            PublishRuntimeSnapshot();
            g_publish_runtime_this_scan = false;
            return;
        }
    }

    runtime.players.reserve(64);








    static uint64_t s_lastLocalBootstrapMs = 0;
    static uint64_t s_localGen = 0;
    static ULONGLONG s_lastTeamMs = 0;
    static int previous_shots = -1;
    if (s_localGen != currentGen) {
        s_localGen = currentGen;
        s_lastLocalBootstrapMs = 0;
        s_lastTeamMs = 0;
        previous_shots = -1;
        runtime.local_pawn = 0;
        runtime.local_controller = 0;
        runtime.local_health = 0;
        runtime.local_team = 0;
    }
    const uint64_t localNowMs = frame_time_ms;
    const bool refreshLocalBootstrap = !IsUserPointer(runtime.local_pawn) ||
        !s_lastLocalBootstrapMs || localNowMs - s_lastLocalBootstrapMs >= 750u;
    uintptr_t localPawn = refreshLocalBootstrap ? 0 : runtime.local_pawn;
    uintptr_t localController = refreshLocalBootstrap ? 0 : runtime.local_controller;
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
        previous_shots = -1;
        s_lastTeamMs = 0;
        runtime.local_team = 0;
        runtime.local_has_defuser = false;
        runtime.local_scoped = false;
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



        if (runtime.local_health > 0 || refreshLocalBootstrap) {
            const ULONGLONG nowT = frame_time_ms;
            const bool refreshTeam = runtime.local_team < 2 || !s_lastTeamMs ||
                (nowT - s_lastTeamMs) > static_cast<ULONGLONG>(TEAM_INTERVAL_MS);

            uint8_t team8 = refreshTeam ? 0xFFu : static_cast<uint8_t>(runtime.local_team);
            uintptr_t scene = 0;
            uintptr_t item_services = 0;
            static uintptr_t cached_local_scene = 0;
            static uintptr_t cached_local_item_services = 0;
            static uintptr_t cached_local_pawn = 0;
            static uint64_t cached_local_scene_until_ms = 0;
            static uint64_t cached_local_item_services_until_ms = 0;
            static uint64_t cached_local_generation = 0;
            if (cached_local_pawn != runtime.local_pawn || cached_local_generation != currentGen) {
                cached_local_generation = currentGen;
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
            float view_angles[2] = {
                (std::numeric_limits<float>::quiet_NaN)(),
                (std::numeric_limits<float>::quiet_NaN)()
            };
            float local_position_sample[3] = {
                (std::numeric_limits<float>::quiet_NaN)(),
                (std::numeric_limits<float>::quiet_NaN)(),
                (std::numeric_limits<float>::quiet_NaN)()
            };
            bool scoped = false;
            int crosshair = 0;
            int shots = -1;
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



                    }
                }
            }



            if (sampled_health >= 0 && sampled_health <= 200)
                runtime.local_health = sampled_health;

            if (refreshTeam && IsPlayableTeam(static_cast<int>(team8))) {
                runtime.local_team = static_cast<int>(team8);
                s_lastTeamMs = nowT;
            }
            if (std::isfinite(view_angles[0]) && std::isfinite(view_angles[1])) {
                runtime.local_view_yaw = view_angles[1];
                runtime.local_angles[0] = view_angles[0];
                runtime.local_angles[1] = view_angles[1];
            } else if (offsets.dwViewAngles) {

                float fallback_angles[2]{};
                if (QRead(runtime.local_pawn + offsets.m_angEyeAngles, fallback_angles, sizeof(fallback_angles)) &&
                    std::isfinite(fallback_angles[0]) && std::isfinite(fallback_angles[1])) {
                    runtime.local_view_yaw = fallback_angles[1];
                    runtime.local_angles[0] = fallback_angles[0];
                    runtime.local_angles[1] = fallback_angles[1];
                }
            }
            runtime.local_scoped = scoped;
            runtime.local_crosshair_entity = (frame_config.trigger_enabled && frame_config.trigger_use_ident)
                ? crosshair : 0;

            if (refreshLocalSceneMetadata && IsUserPointer(scene) &&
                scene != cached_local_scene) {
                std::fill(std::begin(local_position_sample), std::end(local_position_sample),
                    (std::numeric_limits<float>::quiet_NaN)());
                (void)QRead(scene + offsets.m_vecAbsOrigin,
                    local_position_sample, sizeof(local_position_sample));
            }
            if (refreshLocalItemServices && IsUserPointer(item_services) &&
                item_services != cached_local_item_services && offsets.m_bHasDefuser) {
                runtime.local_has_defuser = false;
                (void)QReadT(item_services + offsets.m_bHasDefuser,
                    runtime.local_has_defuser);
            }
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
            if (!needRcsState) {
                previous_shots = -1;
                runtime.local_shots_fired = 0;
                runtime.local_aim_sample_ms = 0;
                std::memset(runtime.local_aim_punch, 0, sizeof(runtime.local_aim_punch));
            }
        } else {



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


        const uint64_t now_ms = frame_time_ms;
        if (!recoveryLivenessFresh)
            (void)refresh_cached_liveness(now_ms);






        g_publish_runtime_this_scan = false;
        return;
    }



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



    const int recoveryPressure = g_pressure_level.load(std::memory_order_relaxed);



    const uint64_t controllerNowMs = frame_time_ms;
    int collected = 0;
    if (cachedControllerCount > 0 &&
        cachedControllerListEntry == runtime.entity_list_entry &&
        controllerNowMs < nextControllerRefreshMs) {
        std::memcpy(controllers, cachedControllers, sizeof(controllers));
        collected = cachedControllerCount;
    } else {
        collected = CollectControllers(runtime.entity_list_entry, g_controller_stride, controllers, kMaxSlots);


        nextControllerRefreshMs = controllerNowMs + (recoveryPressure >= 1 ? 500u : 250u);
        if (collected > 0) {
            std::memcpy(cachedControllers, controllers, sizeof(controllers));
            cachedControllerCount = collected;
            cachedControllerListEntry = runtime.entity_list_entry;
            emptyEntryScans = 0;
        }
    }
    if (collected == 0) {

        const uintptr_t alternate = (g_controller_stride == kEntityStrideAlternate)
            ? kEntityStridePrimary : kEntityStrideAlternate;
        collected = CollectControllers(runtime.entity_list_entry, alternate, controllers, kMaxSlots);
        if (collected > 0) {
            g_controller_stride = alternate;
            g_pawn_stride = alternate;
            emptyEntryScans = 0;
        } else {



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






    int controller_span = 0;
    int controller_slots[kMaxSlots]{};
    for (int i = 0; i < kMaxSlots; ++i) {
        if (!IsUserPointer(controllers[i])) continue;
        controllers[controller_span] = controllers[i];
        controller_slots[controller_span] = i + 1;
        ++controller_span;
    }
    for (int i = controller_span; i < kMaxSlots; ++i)
        controllers[i] = 0;
    collected = controller_span;
    runtime.controller_count = collected;





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
        PublishRuntimeSnapshot();
        g_publish_runtime_this_scan = false;
        return;
    }


    if (runtime.local_controller && runtime.local_pawn)
        CalibratePawnStride(runtime.local_controller, runtime.local_pawn);





    UpdateSpectators(frame_config, controllers, collected);


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
        if (!fresh || fresh == 0xFFFFFFFFu) {
            if (cached.last_update_ms && controllerNowMs >= cached.last_update_ms &&
                controllerNowMs - cached.last_update_ms > 500u) {
                cached.handle = 0;
                cached.pawn = 0;
                handles[i] = 0;
                resolved_by_slot[i] = 0;
            }
            continue;
        }
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

        const uintptr_t alternateStride = (primaryStride == kEntityStrideAlternate)
            ? kEntityStridePrimary : kEntityStrideAlternate;
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

    uintptr_t resolved_pawns[kMaxSlots]{};
    int slot_index[kMaxSlots]{};
    int ent_slots[kMaxSlots]{};
    int candidate_count = 0;
    for (int i = 0; i < collected && candidate_count < kMaxSlots; ++i) {
        if (!controllers[i] || !handles[i]) continue;
        uintptr_t pawn = resolved_by_slot[i];
        if (!pawn) continue;
        resolved_pawns[candidate_count] = pawn;
        slot_index[candidate_count] = i;
        ent_slots[candidate_count] = controller_slots[i];
        ++candidate_count;
    }
    runtime.pawn_count = candidate_count;
    runtime.pawn_stride = g_pawn_stride;






    OmniGhost::Gameplay::UnifiedESP::FeatureFlags unifiedFlags;
    unifiedFlags.box = frame_config.box;
    unifiedFlags.corner_box = frame_config.box_corner;
    unifiedFlags.box_fill = frame_config.box_fill;
    unifiedFlags.skeleton = frame_config.skeleton;
    unifiedFlags.skeleton_joints = frame_config.skeleton_joints;
    unifiedFlags.head_dot = frame_config.head_dot;
    unifiedFlags.head_halo = frame_config.head_halo;
    unifiedFlags.health_bar = frame_config.health_bar;
    unifiedFlags.armor_bar = frame_config.armor_bar;
    unifiedFlags.health_value = frame_config.health_value;
    unifiedFlags.armor_value = frame_config.armor_value;
    unifiedFlags.name = frame_config.name;
    unifiedFlags.distance = frame_config.distance;
    unifiedFlags.weapon_name = frame_config.weapon_name || frame_config.c4_carrier_esp;
    unifiedFlags.weapon_ammo = frame_config.weapon_ammo;
    unifiedFlags.snaplines = frame_config.snaplines;
    unifiedFlags.trails = frame_config.trails;
    unifiedFlags.chinese_hat = frame_config.chinese_hat;
    unifiedFlags.angel_wings = frame_config.angel_wings;
    unifiedFlags.devil_horns = frame_config.devil_horns;
    unifiedFlags.floating_crown = frame_config.floating_crown;
    unifiedFlags.look_direction = frame_config.look_direction;
    unifiedFlags.self_esp = frame_config.self_esp;
    unifiedFlags.show_dead = frame_config.show_dead;
    unifiedFlags.show_bots = frame_config.show_bots;
    unifiedFlags.bomb_timer = frame_config.bomb_timer;
    unifiedFlags.spectator_list = frame_config.spectator_list;
    unifiedFlags.dropped_weapons = frame_config.dropped_weapons;
    unifiedFlags.webradar_enabled = frame_config.webradar_enabled;
    unifiedFlags.aim_enabled = frame_config.aim_enabled;
    unifiedFlags.trigger_enabled = frame_config.trigger_enabled;
    unifiedFlags.aim_visibility_check = frame_config.aim_visibility_check || frame_config.aim_visible_bones_only;
    unifiedFlags.aim_prediction = frame_config.aim_prediction;
    unifiedFlags.aim_ignore_bots = frame_config.aim_ignore_bots;
    const auto fields = unifiedFlags.RequiredFields();

    const bool need_armor = OmniGhost::Gameplay::UnifiedESP::Has(
        fields, OmniGhost::Gameplay::UnifiedESP::DataField::Armor);



    const bool need_yaw =
        OmniGhost::Gameplay::UnifiedESP::Has(
            fields, OmniGhost::Gameplay::UnifiedESP::DataField::Facing);
    const bool track_velocity = frame_config.esp_enabled ||
        OmniGhost::Gameplay::UnifiedESP::Has(
        fields, OmniGhost::Gameplay::UnifiedESP::DataField::Velocity);
    const bool flagsEnabled = frame_config.flag_blind || frame_config.flag_scoped;
    const bool need_scoped = frame_config.trigger_scoped_only ||
        (flagsEnabled && frame_config.flag_scoped);
    const bool need_flash = frame_config.flag_blind;

    const uint64_t scan_now_ms = frame_time_ms;
    struct CachedSceneBone {
        uintptr_t scene = 0;
        uint8_t team = 0;
        int armor = 0;
        uintptr_t weapon_services = 0;
        bool spotted = true;
        bool spotted_known = false;
        uint64_t scene_expires_ms = 0;
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
    const bool need_spotted = (frame_config.aim_enabled &&
        (frame_config.aim_visibility_check || frame_config.aim_visible_bones_only)) ||
        (frame_config.trigger_enabled && frame_config.aim_visibility_check);
    const bool need_weapon_services = need_weapon || need_spotted;


    mem.SetDmaCallTag("CS2.PawnCore");
    const auto _posBegin = std::chrono::steady_clock::now();
    const uint64_t position_read_begin_ms = CS2::SampleClockMs();
    ScatterReadPawnCore(resolved_pawns, candidateControllers, core, candidate_count, need_armor,
                        need_scoped, need_flash, need_yaw, need_weapon_services,
                        need_spotted, cachedTeams, refreshTeams,
                        cachedArmors, refreshArmors,
                        cachedScenes, refreshScenes, positions, track_velocity,
                        cachedWeaponServices, refreshWeaponServices,
                        cachedSpotted, refreshSpotted);
    const uint64_t position_read_end_ms = CS2::SampleClockMs();
    const uint64_t position_sample_ms = position_read_begin_ms +
        (position_read_end_ms - position_read_begin_ms) / 2u;
    runtime.world_sample_ms = position_sample_ms;
    g_phase.positions_ms = OmniGhost::Gameplay::TimeMs(_posBegin);
    NotePossibleDeviceStall(g_phase.positions_ms);
    for (int c = 0; c < candidate_count; ++c) {
        auto& cached = sceneBoneCache[resolved_pawns[c]];
        cached.entry_generation = currentGen;
        cached.last_seen_ms = scan_now_ms;
        if (refreshTeams[c] && core[c].read_ok &&
            IsPlayableTeam(static_cast<int>(core[c].team))) {
            cached.team = core[c].team;
            cached.team_expires_ms = scan_now_ms + 500u;
        } else {
            core[c].team = cached.team;
        }
        if (need_armor && refreshArmors[c] && core[c].read_ok &&
            core[c].armor >= 0 && core[c].armor <= 200) {
            cached.armor = core[c].armor;
            cached.armor_expires_ms = scan_now_ms + 75u;
        } else if (need_armor) {
            core[c].armor = cached.armor;
        }
        if (refreshScenes[c] && core[c].read_ok && IsUserPointer(core[c].scene)) {
            cached.scene = core[c].scene;
            cached.scene_expires_ms = scan_now_ms + 500u;
        } else {
            core[c].scene = cached.scene;
        }

        if (need_weapon_services && refreshWeaponServices[c] && core[c].read_ok &&
            IsUserPointer(core[c].weapon_services)) {
            cached.weapon_services = core[c].weapon_services;
            cached.weapon_services_expires_ms = scan_now_ms + 500u;
        }

        if (need_spotted && refreshSpotted[c] && core[c].read_ok && core[c].spotted_known) {
            cached.spotted = core[c].spotted;
            cached.spotted_known = true;
            cached.spotted_expires_ms = scan_now_ms + 75u;
        } else if (need_spotted) {
            core[c].spotted = cached.spotted;
            core[c].spotted_known = cached.spotted_known;
        }
    }




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
        if (!readOk || (!healthKnown && !controllerKnown))
            continue;
        auto& sample = liveness.players[liveness.count++];
        sample.pawn = resolved_pawns[c];
        sample.sample_ms = position_sample_ms;
        sample.generation = liveness.generation;
        sample.alive = !dead;
        if (dead) {
            confirmedDead[confirmedDeadCount++] = sample.pawn;


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
            sceneBoneCache.erase(pawn);
            last_good_players.erase(std::remove_if(last_good_players.begin(),
                last_good_players.end(), [pawn](const RecoveryIdentity& p) { return p.pawn == pawn; }),
                last_good_players.end());
        }
    }

    int validCoreSamples = 0;
    for (int c = 0; c < candidate_count; ++c)
        validCoreSamples += (core[c].read_ok &&
            IsPlayableTeam(static_cast<int>(core[c].team))) ? 1 : 0;
    if (candidate_count > 0 && validCoreSamples == 0) {


        g_entity_list_force_refresh = true;
        g_entity_list_next_validation_ms = 0;
        for (int c = 0; c < candidate_count; ++c)
            controllerPawnCache.erase(controllers[slot_index[c]]);




        g_publish_runtime_this_scan = false;
        return;
    }






    int eligible_count = 0;

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

        if (!IsFinitePosition(positions[c]))
            continue;
        if (eligible_count != c) {
            resolved_pawns[eligible_count] = resolved_pawns[c];
            slot_index[eligible_count] = slot_index[c];
            ent_slots[eligible_count] = ent_slots[c];
            candidateControllers[eligible_count] = candidateControllers[c];
            core[eligible_count] = core[c];
            std::memcpy(positions[eligible_count], positions[c], sizeof(positions[c]));
        }
        ++eligible_count;
    }
    candidate_count = eligible_count;








    {
        ScopedPhase _phEarly(&g_phase.early_publish_ms);
        runtime.players.clear();
        runtime.enemy_count = 0;
        runtime.world_sample_ms = position_sample_ms ? position_sample_ms : frame_time_ms;
        for (int c = 0; c < candidate_count; ++c) {
            const uintptr_t pawn = resolved_pawns[c];
            const uintptr_t controller = candidateControllers[c];
            if (!IsUserPointer(pawn)) continue;
            if (!core[c].read_ok) continue;
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



            corePlayer.ent_index = ent_slots[c];



            corePlayer.spotted = core[c].spotted_known ? core[c].spotted : true;
            corePlayer.is_flashed = need_flash && core[c].flash > 0.f;



            {
                const float yaw = core[c].eye_angles[1];
                corePlayer.view_yaw = std::isfinite(yaw) ? yaw : 0.f;
            }
            corePlayer.pos[0] = positions[c][0];
            corePlayer.pos[1] = positions[c][1];
            corePlayer.pos[2] = positions[c][2];
            corePlayer.position_sample_ms = position_sample_ms;




            {
                std::unique_lock lock(g_cache_mutex);
                if (auto aliveIt = recent_players.find(pawn);
                    aliveIt != recent_players.end() &&
                    aliveIt->second.entry_generation == currentGen &&
                    aliveIt->second.confirmed_dead) {
                    aliveIt->second.confirmed_dead = false;
                    aliveIt->second.last_seen_ms = frame_time_ms;
                    bone_cache.erase(pawn);
                    sceneBoneCache.erase(pawn);
                }
            }

            const float dx_dist = corePlayer.pos[0] - runtime.local_pos[0];
            const float dy_dist = corePlayer.pos[1] - runtime.local_pos[1];
            const float dz_dist = corePlayer.pos[2] - runtime.local_pos[2];


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


            {
                std::unique_lock lock(g_cache_mutex);
                if (auto bit = bone_cache.find(pawn); bit != bone_cache.end() &&
                    bit->second.entry_generation == currentGen &&
                    bit->second.last_valid_ms &&
                    frame_time_ms >= bit->second.last_valid_ms &&
                    frame_time_ms - bit->second.last_valid_ms <= kBoneVisualMaxAgeMs &&
                    bit->second.controller == controller) {
                std::memcpy(corePlayer.bones, bit->second.joints, sizeof(corePlayer.bones));
                const float cachedShift[3] = {
                    corePlayer.pos[0] - bit->second.origin[0],
                    corePlayer.pos[1] - bit->second.origin[1],
                    corePlayer.pos[2] - bit->second.origin[2]
                };
                for (std::size_t bone = 0; bone < kBoneSlotCount; ++bone) {
                    const auto& joint = corePlayer.bones[bone];
                    if (joint[0] == 0.f && joint[1] == 0.f && joint[2] == 0.f)
                        continue;
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
                constexpr uint64_t kOptionalNameMaxAgeMs = 5000;
                constexpr uint64_t kOptionalWeaponMaxAgeMs = 600;
                constexpr uint64_t kOptionalVisibilityMaxAgeMs = 250;
                const bool freshEntry = !rit->second.confirmed_dead &&
                    rit->second.last_seen_ms && scan_now_ms >= rit->second.last_seen_ms &&
                    scan_now_ms - rit->second.last_seen_ms <= kOptionalNameMaxAgeMs;
                if (sameController && freshEntry && rit->second.player.name[0]) {
                    std::snprintf(corePlayer.name, sizeof(corePlayer.name), "%s",
                        rit->second.player.name);
                    corePlayer.is_bot = corePlayer.is_bot || rit->second.player.is_bot;
                }
                if (sameController && freshEntry && rit->second.weapon_received_ms &&
                    scan_now_ms >= rit->second.weapon_received_ms &&
                    scan_now_ms - rit->second.weapon_received_ms <= kOptionalWeaponMaxAgeMs) {
                    if (rit->second.player.weapon[0])
                        std::snprintf(corePlayer.weapon, sizeof(corePlayer.weapon), "%s",
                            rit->second.player.weapon);
                    corePlayer.weapon_def = rit->second.player.weapon_def;
                    corePlayer.ammo_clip = rit->second.player.ammo_clip;
                    corePlayer.ammo_reserve = rit->second.player.ammo_reserve;
                }
                if (sameController && freshEntry && rit->second.visibility_received_ms &&
                    scan_now_ms >= rit->second.visibility_received_ms &&
                    scan_now_ms - rit->second.visibility_received_ms <= kOptionalVisibilityMaxAgeMs) {
                    corePlayer.visibility_known = rit->second.player.visibility_known;
                    corePlayer.visible = rit->second.player.visible;
                    corePlayer.bone_visibility_known_mask = rit->second.player.bone_visibility_known_mask;
                    corePlayer.bone_visibility_mask = rit->second.player.bone_visibility_mask;
                }
            }
        }


        if (!corePlayer.is_local && corePlayer.team != runtime.local_team)
            ++runtime.enemy_count;
        runtime.players.push_back(corePlayer);
        }
        runtime.player_count = static_cast<int>(runtime.players.size());
    }









    {
        const uint64_t nowMs = frame_time_ms;
        if (!runtime.players.empty()) {
            last_good_players.clear();
            last_good_players.reserve(runtime.players.size());
            for (const auto& player : runtime.players)
                last_good_players.push_back({player.pawn, player.controller});
            last_good_ms = nowMs;
            OmniGhost::Gameplay::DmaTelemetry::CS2().pressure.last_good_age_ms.store(
                0, std::memory_order_relaxed);
        }


        if (!runtime.in_match || !frame_config.bomb_timer)
            runtime.bomb = BombState{};
        else if (g_pressure_level.load(std::memory_order_relaxed) < 2)
            UpdateBombStateThrottled();


        const bool need_local_weapon = frame_config.aim_enabled ||
            frame_config.trigger_enabled || frame_config.aim_rcs_standalone;
        if (need_local_weapon && g_pressure_level.load(std::memory_order_relaxed) < 2)
            UpdateLocalWeaponDefinitionThrottled();
    }


    {
        constexpr uint64_t kGraceMs = 5000;
        const uint64_t nowMs = frame_time_ms;
        std::unique_lock lock(g_cache_mutex);
        auto prune = [&](auto& map, auto getLast) {
            for (auto it = map.begin(); it != map.end();) {
                const uint64_t last = getLast(it->second);
                if (!last || nowMs < last || nowMs - last > kGraceMs)
                    it = map.erase(it);
                else
                    ++it;
            }
        };
        auto cap = [](auto& map, size_t limit, auto getLast) {
            while (map.size() > limit) {
                auto oldest = map.begin();
                for (auto it = std::next(oldest); it != map.end(); ++it)
                    if (getLast(it->second) < getLast(oldest->second)) oldest = it;
                map.erase(oldest);
            }
        };
        prune(recent_players, [](const RecentPlayer& e) { return e.last_seen_ms; });
        prune(bone_cache, [](const CachedBones& e) { return e.last_valid_ms; });
        prune(sceneBoneCache, [](const CachedSceneBone& e) { return e.last_seen_ms; });
        prune(controllerPawnCache, [](const CachedControllerPawn& e) { return e.last_seen_ms; });
        cap(recent_players, kMaxRecentPlayers, [](const RecentPlayer& e) { return e.last_seen_ms; });
        cap(bone_cache, kMaxBoneCache, [](const CachedBones& e) { return e.last_valid_ms; });
        cap(sceneBoneCache, 128u, [](const CachedSceneBone& e) { return e.last_seen_ms; });
        cap(controllerPawnCache, 128u, [](const CachedControllerPawn& e) { return e.last_seen_ms; });
    }
    OmniGhost::Gameplay::DmaTelemetry::CS2().pressure.last_good_age_ms.store(
        last_good_ms && frame_time_ms >= last_good_ms ? frame_time_ms - last_good_ms : 0u,
        std::memory_order_relaxed);
    auto& cacheMetrics = CacheManager::Metrics();
    cacheMetrics.recent_players.store(recent_players.size(), std::memory_order_relaxed);
    cacheMetrics.bones.store(bone_cache.size(), std::memory_order_relaxed);
    cacheMetrics.controller_pawns.store(controllerPawnCache.size(), std::memory_order_relaxed);
    cacheMetrics.scene_bones.store(sceneBoneCache.size(), std::memory_order_relaxed);
    QueueOptionalEnrichment(frame_config);





    if (g_publish_runtime_this_scan) {
        PublishRuntimeSnapshot();
        g_publish_runtime_this_scan = false;
    }
    return;
}
