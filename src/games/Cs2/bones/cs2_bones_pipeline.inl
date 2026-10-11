// Adaptive optional enrichment: bones, metadata, weapons and visibility jobs.
static void RunOptionalEnrichment(const Config& frame_config,
                                  std::vector<OptionalTarget> targets,
                                  const bool in_match, const uintptr_t client_base,
                                  const bool dropped_only, const uint64_t jobGen) {
    if (dropped_only ? !in_match : (!in_match || targets.empty()))
        return;
    const int pressure = g_pressure_level.load(std::memory_order_relaxed);
    const bool isRecovery = pressure >= 3;
    // Sole producer of g_optional_results. All deltas are local until the
    // final publish; the acquisition thread merges them into its own caches.
    OptionalResults results{};
    results.generation = jobGen;

    OmniGhost::Gameplay::OptionalPhaseBudget budget;
    {
        // IDENTICAL to FiveM: 6ms base budget, shed at pressure 2/3
        float budgetMs = kOptionalBudgetMs; // 6.0f
        if (isRecovery) budgetMs = 2.f;     // minimal for critical bones only
        else if (pressure == 2) budgetMs = 3.5f; // ~58%
        else if (pressure == 1) budgetMs = 4.5f; // ~75%
        budget.Reset(budgetMs);
    }

    g_scatter_optional.Ensure();
    if (!g_scatter_optional)
        return;

    OmniGhost::Gameplay::UnifiedConfig::CS2Config unifiedConfig;
    // Update with frame-specific values
    unifiedConfig.esp_enabled = frame_config.esp_enabled;
    unifiedConfig.box = frame_config.box;
    unifiedConfig.skeleton = frame_config.skeleton;
    unifiedConfig.head_dot = frame_config.head_dot;
    unifiedConfig.head_halo = frame_config.head_halo;
    unifiedConfig.chinese_hat = frame_config.chinese_hat;
    unifiedConfig.angel_wings = frame_config.angel_wings;
    unifiedConfig.devil_horns = frame_config.devil_horns;
    unifiedConfig.floating_crown = frame_config.floating_crown;
    unifiedConfig.health_bar = frame_config.health_bar;
    unifiedConfig.armor_bar = frame_config.armor_bar;
    unifiedConfig.health_value = frame_config.health_value;
    unifiedConfig.armor_value = frame_config.armor_value;
    unifiedConfig.visibility_colors = frame_config.visibility_colors;
    unifiedConfig.visible_check = frame_config.visible_check;
    unifiedConfig.weapon_name = frame_config.weapon_name;
    unifiedConfig.weapon_ammo = frame_config.weapon_ammo;
    unifiedConfig.name = frame_config.name;
    unifiedConfig.distance = frame_config.distance;
    unifiedConfig.snaplines = frame_config.snaplines;
    unifiedConfig.trails = frame_config.trails;
    unifiedConfig.head_halo = frame_config.head_halo;
    unifiedConfig.look_direction = frame_config.look_direction;
    unifiedConfig.chinese_hat = frame_config.chinese_hat;
    unifiedConfig.angel_wings = frame_config.angel_wings;
    unifiedConfig.devil_horns = frame_config.devil_horns;
    unifiedConfig.floating_crown = frame_config.floating_crown;
    unifiedConfig.rgb_mode = frame_config.rgb_mode;
    unifiedConfig.visibility_colors = frame_config.visibility_colors;
    unifiedConfig.visible_check = frame_config.visible_check;
    unifiedConfig.team_check = frame_config.team_check;
    unifiedConfig.show_bots = frame_config.show_bots;
    unifiedConfig.max_distance = frame_config.max_distance;
    unifiedConfig.optional_lane_budget_ms = frame_config.optional_lane_budget_ms;
    unifiedConfig.performance_mode = frame_config.performance_mode;
    unifiedConfig.aim_enabled = frame_config.aim_enabled;
    unifiedConfig.trigger_enabled = frame_config.trigger_enabled;
    unifiedConfig.aim_visibility_check = frame_config.aim_visibility_check;
    unifiedConfig.trigger_head_only = frame_config.trigger_head_only;
    unifiedConfig.trigger_scoped_only = frame_config.trigger_scoped_only;
    unifiedConfig.aim_prediction = frame_config.aim_prediction;
    unifiedConfig.radar_2d = frame_config.radar_2d;
    unifiedConfig.webradar_enabled = frame_config.webradar_enabled;
    unifiedConfig.dropped_weapons = frame_config.dropped_weapons;
    unifiedConfig.radar_2d = frame_config.radar_2d;
    unifiedConfig.webradar_enabled = frame_config.webradar_enabled;
    unifiedConfig.performance_mode = frame_config.performance_mode;
    
    // Copy colors
    for (int i = 0; i < 4; ++i) {
        unifiedConfig.col_box[i] = frame_config.col_box[i];
        unifiedConfig.col_box_corner[i] = frame_config.col_box_corner[i];
        unifiedConfig.col_box_fill[i] = frame_config.col_box_fill[i];
        unifiedConfig.col_skeleton[i] = frame_config.col_skeleton[i];
        unifiedConfig.col_joints[i] = frame_config.col_joints[i];
        unifiedConfig.col_head[i] = frame_config.col_head[i];
        unifiedConfig.col_halo[i] = frame_config.col_halo[i];
        unifiedConfig.col_health[i] = frame_config.col_health[i];
        unifiedConfig.col_armor[i] = frame_config.col_armor[i];
        unifiedConfig.col_name[i] = frame_config.col_name[i];
        unifiedConfig.col_distance[i] = frame_config.col_distance[i];
        unifiedConfig.col_weapon[i] = frame_config.col_weapon[i];
        unifiedConfig.col_snaplines[i] = frame_config.col_snaplines[i];
        unifiedConfig.col_trail[i] = frame_config.col_trail[i];
        unifiedConfig.col_fun_effects[i] = frame_config.col_fun_effects[i];
        unifiedConfig.col_look[i] = frame_config.col_look[i];
        unifiedConfig.col_text_outline[i] = frame_config.col_text_outline[i];
        unifiedConfig.col_visible[i] = frame_config.col_visible[i];
        unifiedConfig.col_occluded[i] = frame_config.col_occluded[i];
        unifiedConfig.col_team[i] = frame_config.col_team[i];
        unifiedConfig.col_target[i] = frame_config.col_target[i];
    }
    
    // Use unified FeatureFlags for RequiredFields
    const auto unifiedFlags = unifiedConfig.GetFeatureFlags();
    const auto fields = unifiedFlags.RequiredFields();
    const bool need_bones = OmniGhost::Gameplay::UnifiedESP::Has(
        fields, OmniGhost::Gameplay::UnifiedESP::DataField::Skeleton);
    const bool need_full_bones = frame_config.skeleton ||
        (frame_config.trigger_enabled && !frame_config.trigger_head_only);
    const bool need_names = frame_config.name || frame_config.radar_2d ||
        (frame_config.esp_enabled && !frame_config.show_bots) ||
        (frame_config.aim_enabled && frame_config.aim_ignore_bots);
    const bool need_weapons = frame_config.weapon_name || frame_config.weapon_ammo;
    const bool allow_weapon = pressure == 0 && !HeavyDmaCooldownActive();

    // The job snapshot was captured by the acquisition thread at queue time;
    // no reads from the global runtime.player list happen on this thread.
    if (dropped_only) {
        if (frame_config.dropped_weapons) {
            mem.SetDmaPriority(Memory::DmaPri::OtherMetadata);
            CS2::GiveCameraPriorityBeforeOptionalDma();
            CollectDroppedWeapons(frame_config, in_match, client_base, results.dropped_weapons);
            results.dropped_weapons_valid = true;
        }
        if (jobGen != g_session_generation.load(std::memory_order_acquire))
            return;
        if (auto slot = g_optional_results.TryBeginWrite()) {
            *slot.value = std::move(results);
            g_optional_results.Publish(slot.index);
        }
        return;
    }

    // ── Bones micro-batches ────────────────────────────────────────────
    if (need_bones && !!budget.Remaining() && pressure < 3) {
        mem.SetDmaPriority(Memory::DmaPri::Bones);
        const auto bonesPhaseBegin = std::chrono::steady_clock::now();
        // Optional work must yield between small batches; pressure 2 still
        // refreshes one pose at a time instead of letting every skeleton age out.
        constexpr int kBatch = 2;
        constexpr int kJointCount = 28;
        struct BoneJointSnapshot { float x, y, z, scale; char pad[0x10]; };
        static_assert(sizeof(BoneJointSnapshot) == 32, "BoneJointSnapshot");

        if (pressure >= 2 && targets.size() > 1) {
            static size_t recoveryCursor = 0; // optional worker is the sole caller
            recoveryCursor %= targets.size();
            std::rotate(targets.begin(), targets.begin() + recoveryCursor, targets.end());
            ++recoveryCursor;
        }

        for (size_t base = 0; base < targets.size() && !!budget.Remaining();) {
            if (g_acqState.IsStoppingOrStopped()) return;
            if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;
            // Quick check first - if camera is pending or deadline imminent, do full yield
            if (CS2::ShouldYieldToCameraQuick()) {
                if (!CS2::GiveCameraPriorityBeforeOptionalDma())
                    break; // camera still busy after timeout — defer remaining batches
            }
            const size_t end = (std::min)(targets.size(), base + static_cast<size_t>(pressure >= 2 ? 1 : kBatch));
            uintptr_t boneBases[kBatch]{};
            BoneJointSnapshot joints[kBatch][kJointCount]{};
            float sampledOrigins[kBatch][3];
            for (auto& origin : sampledOrigins)
                std::fill(std::begin(origin), std::end(origin),
                    (std::numeric_limits<float>::quiet_NaN)());
            bool eligible[kBatch]{};
            int queued = 0;
            for (size_t i = base; i < end; ++i) {
                const size_t bi = i - base;
                uintptr_t scene = targets[i].scene;
                if (!IsUserPointer(scene)) {
                    // Try live scene pointer from pawn.
                    QReadT(targets[i].pawn + offsets.m_pGameSceneNode, scene, "CS2.Opt.Scene");
                    targets[i].scene = IsUserPointer(scene) ? scene : 0;
                }
                if (!IsUserPointer(targets[i].scene)) continue;
                uintptr_t boneBase = 0;
                if (!QReadT(targets[i].scene + offsets.BoneArray, boneBase, "CS2.Opt.BoneBase") ||
                    !IsUserPointer(boneBase))
                    continue;
                boneBases[bi] = boneBase;
                eligible[bi] = true;
                mem.AddScatterReadRequest(g_scatter_optional, boneBase,
                    joints[bi], sizeof(BoneJointSnapshot) * (need_full_bones ? kJointCount : 8));
                mem.AddScatterReadRequest(g_scatter_optional,
                    targets[i].scene + offsets.m_vecAbsOrigin,
                    sampledOrigins[bi], sizeof(sampledOrigins[bi]));
                ++queued;
            }
            if (queued > 0) {
                CS2::GiveCameraPriorityBeforeOptionalDma();
                mem.SetDmaCallTag("CS2.Optional.Bones");
                const uint64_t bone_read_begin_ms = GetTickCount64();
                const bool boneReadOk = mem.ExecuteReadScatter(g_scatter_optional);
                const uint64_t bone_read_end_ms = GetTickCount64();
                // P0-3: Check generation immediately after DMA to avoid processing stale data
                if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;
                // Midpoint of the actual scatter execute — this is the real
                // acquisition instant used for bones_age in presentation.
                const uint64_t sampleMs = bone_read_begin_ms +
                    (bone_read_end_ms - bone_read_begin_ms) / 2u;
                for (size_t i = base; i < end; ++i) {
                    const size_t bi = i - base;
                    if (!boneReadOk || !eligible[bi] || !IsFinitePosition(sampledOrigins[bi])) continue;
                    if (results.bone_count >= results.bones.size()) break;
                    OptionalBoneResult& r = results.bones[results.bone_count];
                    r = OptionalBoneResult{}; // rejected candidate must not contaminate the next slot
                    r.pawn = targets[i].pawn;
                    r.bone_base = boneBases[bi];
                    // Translation must use the origin sampled with this pose,
                    // not the older origin captured when the job was queued.
                    std::memcpy(r.origin, sampledOrigins[bi], sizeof(r.origin));
                    const auto validJoint = [&](const BoneJointSnapshot& j, size_t boneSlot) {
                        if (!std::isfinite(j.x) || !std::isfinite(j.y) || !std::isfinite(j.z))
                            return false;
                        if (j.x == 0.f && j.y == 0.f && j.z == 0.f)
                            return false;
                        const float dx = j.x - r.origin[0];
                        const float dy = j.y - r.origin[1];
                        const float dz = j.z - r.origin[2];
                        // Allow ankles/feet to be lower (below -50) since they can be far from pelvis
                        float minDz = -50.f;
                        if (boneSlot >= 14 && boneSlot <= 19) minDz = -120.f;
                        return dx * dx + dy * dy < 180.f * 180.f &&
                            dz > minDz && dz < 200.f;
                    };
                    // Compact upper-body mapping using runtime-resolved bone indices.
                    // Uses g_boneMapping (loaded from the canonical offset snapshot).
                    const int kIdx[4] = {
                        CS2::GetBoneJointIndex(CS2::BoneSlot::Head),
                        CS2::GetBoneJointIndex(CS2::BoneSlot::Neck),
                        CS2::GetBoneJointIndex(CS2::BoneSlot::SpineUpper),
                        CS2::GetBoneJointIndex(CS2::BoneSlot::SpineLower)
                    };
                    bool upperPoseValid = true;
                    for (int s = 0; s < 4; ++s) {
                        const auto& j = joints[bi][kIdx[s]];
                        const size_t boneSlot = (s == 3) ? 
                            static_cast<size_t>(CS2::BoneSlot::SpineLower) : s; // head=0, neck=1, chest=2, stomach=4
                        if (!validJoint(j, boneSlot)) {
                            upperPoseValid = false;
                            continue;
                        }
                        r.bones[boneSlot][0] = j.x;
                        r.bones[boneSlot][1] = j.y;
                        r.bones[boneSlot][2] = j.z;
                    }
                    bool completePose = false;
                    if (need_full_bones) {
                        // Map extended slots from joint stream using runtime-resolved bone indices.
                        // g_boneMapping is loaded from the canonical offset snapshot.
                        completePose = true;
                        for (size_t b = 0; b < kBoneSlotCount; ++b) {
                            int jointIdx = CS2::GetBoneJointIndex(static_cast<CS2::BoneSlot>(b));
                            if (jointIdx < 0 || jointIdx >= static_cast<int>(kJointCount)) continue;
                            const auto& j = joints[bi][jointIdx];
                            if (!validJoint(j, b)) continue; // renderer omits that segment
                            r.bones[b][0] = j.x; r.bones[b][1] = j.y; r.bones[b][2] = j.z;
                        }
                        // A pose is drawable only when the torso has coherent
                        // joints. A corrupt limb cannot invalidate the torso or
                        // turn into a long line from the head.
                        for (size_t b : {size_t{0}, size_t{1}, size_t{2}, size_t{5}})
                            if (r.bones[b][0] == 0.f && r.bones[b][1] == 0.f && r.bones[b][2] == 0.f)
                                completePose = false;
                        if (completePose) {
                            const float dx = r.bones[0][0] - r.bones[5][0];
                            const float dy = r.bones[0][1] - r.bones[5][1];
                            const float dz = r.bones[0][2] - r.bones[5][2];
                            const float torsoSq = dx * dx + dy * dy + dz * dz;
                            const float hnX = r.bones[0][0] - r.bones[1][0];
                            const float hnY = r.bones[0][1] - r.bones[1][1];
                            const float hnZ = r.bones[0][2] - r.bones[1][2];
                            completePose = torsoSq > 100.f && torsoSq < 140.f * 140.f &&
                                hnX * hnX + hnY * hnY + hnZ * hnZ < 55.f * 55.f &&
                                r.bones[0][2] > r.origin[2] + 5.f;
                        }
                        r.full_bones_ok = completePose;
                    }
                    if (upperPoseValid && (!need_full_bones || completePose)) {
                        r.bones_ok = true;
                        r.sample_ms = sampleMs;
                        ++results.bone_count;
                    }
                }
            } // approx per micro-batch
            base = end;
        }
        results.bones_ms = OmniGhost::Gameplay::TimeMs(bonesPhaseBegin);
    }

    // ── Names (low rate) ───────────────────────────────────────────────
    if (need_names && !!budget.Remaining() && pressure < 2) {
        mem.SetDmaPriority(Memory::DmaPri::WeaponNameTeam);
        for (size_t i = 0; i < targets.size() && !!budget.Remaining(); ++i) {
            // P0-3: Check generation before each name read
            if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;
            if (!IsUserPointer(targets[i].controller)) continue;
            if (targets[i].has_name) continue; // job snapshot already has a name
            if (!CS2::GiveCameraPriorityBeforeOptionalDma())
                break; // camera still busy — abort names pass
            char name[64]{};
            if (QRead(targets[i].controller + offsets.m_iszPlayerName, name, sizeof(name) - 1, "CS2.Opt.Name")
                && name[0] && results.name_count < results.names.size()) {
                // P0-3: Verify generation still valid after DMA read
                if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;
                auto& r = results.names[results.name_count++];
                r.pawn = targets[i].pawn;
                r.controller = targets[i].controller;
                std::snprintf(r.name, sizeof(r.name), "%s", name);
            }
        }
    }

    // Item discovery must not starve behind per-player weapon metadata.
    if (frame_config.dropped_weapons && pressure < 1) {
        mem.SetDmaPriority(Memory::DmaPri::OtherMetadata);
        if (CS2::GiveCameraPriorityBeforeOptionalDma()) {
            // P0-3: Check generation before dropped weapons scan
            if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;
            const auto droppedPhaseBegin = std::chrono::steady_clock::now();
            CollectDroppedWeapons(frame_config, in_match, client_base, results.dropped_weapons);
            // P0-3: Verify generation after dropped weapons scan
            if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;
            results.dropped_weapons_valid = true;
            results.dropped_ms = OmniGhost::Gameplay::TimeMs(droppedPhaseBegin);
        }
    }

    // ── Weapons (pressure 0 only) ──────────────────────────────────────
    if (need_weapons && allow_weapon && !!budget.Remaining()) {
        mem.SetDmaPriority(Memory::DmaPri::WeaponNameTeam);
        const auto weaponPhaseBegin = std::chrono::steady_clock::now();
        
        // P1-4: Batch weapon metadata reads using scatter to reduce round-trips
        // Phase 1: Read weapon services for all targets in one scatter
        std::vector<uintptr_t> weaponServices(targets.size(), 0);
        {
            auto h = mem.CreateScatterHandle();
            if (h) {
                for (size_t i = 0; i < targets.size(); ++i) {
                    mem.AddScatterReadRequest(h, targets[i].pawn + offsets.m_pWeaponServices,
                                              &weaponServices[i], sizeof(uintptr_t));
                }
                mem.ExecuteReadScatter(h);
                mem.CloseScatterHandle(h);
            }
        }
        if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;

        // Phase 2: Read weapon handles for valid services
        std::vector<uint32_t> weaponHandles(targets.size(), 0);
        {
            auto h = mem.CreateScatterHandle();
            if (h) {
                for (size_t i = 0; i < targets.size(); ++i) {
                    if (!IsUserPointer(weaponServices[i])) continue;
                    mem.AddScatterReadRequest(h, weaponServices[i] + offsets.m_hActiveWeapon,
                                              &weaponHandles[i], sizeof(uint32_t));
                }
                mem.ExecuteReadScatter(h);
                mem.CloseScatterHandle(h);
            }
        }
        if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;

        // Phase 3: Resolve weapon entity handles in batch
        std::vector<uintptr_t> weaponEntities(targets.size(), 0);
        {
            const uintptr_t primaryStride = offsets.entity_identity_stride;
            const uintptr_t alternateStride = offsets.entity_identity_stride;
            CS2::ScatterResolvePawnHandles(weaponHandles.data(), weaponEntities.data(),
                static_cast<int>(targets.size()), primaryStride, alternateStride,
                true, g_scatter_optional, client_base);
        }
        if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;

        // Phase 4: Read definitions and ammo in batch
        std::vector<uint16_t> weaponDefs(targets.size(), 0);
        std::vector<int> weaponClips(targets.size(), -1);
        {
            auto h = mem.CreateScatterHandle();
            if (h) {
                for (size_t i = 0; i < targets.size(); ++i) {
                    if (!IsUserPointer(weaponEntities[i])) continue;
                    uintptr_t attr = weaponEntities[i] + offsets.m_AttributeManager;
                    mem.AddScatterReadRequest(h, attr + offsets.m_Item + offsets.m_iItemDefinitionIndex,
                                              &weaponDefs[i], sizeof(uint16_t));
                    if (frame_config.weapon_ammo && offsets.m_iClip1) {
                        mem.AddScatterReadRequest(h, weaponEntities[i] + offsets.m_iClip1,
                                                  &weaponClips[i], sizeof(int));
                    }
                }
                mem.ExecuteReadScatter(h);
                mem.CloseScatterHandle(h);
            }
        }
        if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;

        // Collect results
        for (size_t i = 0; i < targets.size() && !!budget.Remaining(); ++i) {
            if (!IsUserPointer(weaponEntities[i])) continue;
            const bool haveDef = weaponDefs[i] > 0 && weaponDefs[i] < 6000;
            const bool haveClip = frame_config.weapon_ammo && weaponClips[i] >= 0 && weaponClips[i] < 500;
            if ((haveDef || haveClip) && results.weapon_count < results.weapons.size()) {
                auto& r = results.weapons[results.weapon_count++];
                r.pawn = targets[i].pawn;
                if (haveDef) {
                    r.weapon_def = weaponDefs[i];
                    const char* nm = CS2_Weapons::NameFromDefIndex(static_cast<int>(weaponDefs[i]));
                    if (nm)
                        std::snprintf(r.weapon, sizeof(r.weapon), "%s", nm);
                }
                if (haveClip)
                    r.ammo_clip = weaponClips[i];
            }
        }
        results.weapon_ms = OmniGhost::Gameplay::TimeMs(weaponPhaseBegin);
    }

    // P2-8: Track optional budget left for telemetry correlation.
    results.budget_left_ms = budget.Remaining();

    // ── Visibility (raycast against static map collision) ───────────────
    if (OmniGhost::Gameplay::UnifiedESP::Has(
            fields, OmniGhost::Gameplay::UnifiedESP::DataField::Visibility) &&
        !!budget.Remaining() && pressure < 2) {
        // Match the queued world points to a camera sample and an immutable
        // map identity; projection row 3 is not the camera's world position.
        auto& collision = Trajectory::CollisionCache();
        const auto world = AcquireRuntimeSnapshot();
        const CameraSnapshot camera = SelectCameraSnapshot(targets.front().position_sample_ms);
        const auto cameraOrigin = Visibility::CameraOrigin(
            camera.timestamp_ms ? camera.view_matrix : nullptr);
        if (world && world->session_generation == jobGen && cameraOrigin &&
            collision.IsLoadedFor(world->map_name)) {
            // Prepare visibility targets - use head + body points for each pawn
            std::vector<VisibilityPipeline::Target> visTargets;
            visTargets.reserve(targets.size());
            for (const auto& t : targets) {
                VisibilityPipeline::Target vt{};
                vt.pawn = t.pawn;
                // Point 0: head (origin + ~70 units up)
                vt.points[0] = Trajectory::Vec3(t.origin[0], t.origin[1], t.origin[2] + 70.f);
                // Point 1: chest
                vt.points[1] = Trajectory::Vec3(t.origin[0], t.origin[1], t.origin[2] + 40.f);
                // Point 2: pelvis
                vt.points[2] = Trajectory::Vec3(t.origin[0], t.origin[1], t.origin[2] + 10.f);
                // Point 3: feet
                vt.points[3] = Trajectory::Vec3(t.origin[0], t.origin[1], t.origin[2] - 10.f);
                visTargets.push_back(vt);
            }

            if (!visTargets.empty()) {
                mem.SetDmaPriority(Memory::DmaPri::OtherMetadata);
                CS2::GiveCameraPriorityBeforeOptionalDma();
                const auto visPhaseBegin = std::chrono::steady_clock::now();
                // Submit to visibility worker (async)
                g_visibility_worker.Submit(
                    collision.WorldSnapshot(), *cameraOrigin, std::move(visTargets), jobGen);
                // Note: visibility worker runs async; results will be queried on next optional job
                // For now, we query any already-completed results from previous submissions
                for (const auto& t : targets) {
                    if (!IsUserPointer(t.pawn)) continue;
                    auto visResult = g_visibility_worker.Get(t.pawn, jobGen);
                    if (visResult.known && results.visibility_count < results.visibility.size()) {
                        auto& r = results.visibility[results.visibility_count++];
                        r.pawn = t.pawn;
                        r.visible = visResult.visible;
                        r.known = true;
                        r.checked_ms = visResult.checked_ms;
                    }
                }
                results.visibility_ms = OmniGhost::Gameplay::TimeMs(visPhaseBegin);
            }
        }
    }

    if (jobGen != g_session_generation.load(std::memory_order_acquire))
        return;
    // Publish deltas; the acquisition thread merges and owns the runtime publish.
    if (auto slot = g_optional_results.TryBeginWrite()) {
        *slot.value = std::move(results);
        g_optional_results.Publish(slot.index);
    }
}


