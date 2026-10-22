
static void RunOptionalEnrichment(const Config& frame_config,
                                  std::vector<OptionalTarget> targets,
                                  const bool in_match, const uintptr_t client_base,
                                  const bool dropped_only, const uint64_t jobGen) {
    if (dropped_only ? !in_match : (!in_match || targets.empty()))
        return;
    const int pressure = g_pressure_level.load(std::memory_order_relaxed);
    const bool isRecovery = pressure >= 3;


    OptionalResults results{};
    results.generation = jobGen;

    OmniGhost::Gameplay::OptionalPhaseBudget budget;
    {

        float budgetMs = kOptionalBudgetMs;
        if (isRecovery) budgetMs = 2.f;
        else if (pressure == 2) budgetMs = 3.5f;
        else if (pressure == 1) budgetMs = 4.5f;
        budget.Reset(budgetMs);
    }

    g_scatter_optional.Ensure();
    if (!g_scatter_optional)
        return;




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
    const bool need_bones = OmniGhost::Gameplay::UnifiedESP::Has(
        fields, OmniGhost::Gameplay::UnifiedESP::DataField::Skeleton);
    const bool need_full_bones = frame_config.skeleton ||
        (frame_config.trigger_enabled && !frame_config.trigger_head_only);
    const bool need_names = frame_config.name ||
        (frame_config.esp_enabled && !frame_config.show_bots) ||
        (frame_config.aim_enabled && frame_config.aim_ignore_bots);
    const bool need_weapons = frame_config.weapon_name || frame_config.weapon_ammo || frame_config.c4_carrier_esp;
    const bool allow_weapon = pressure == 0 && !HeavyDmaCooldownActive();



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


    if (need_bones && !!budget.Remaining() && pressure < 3) {
        mem.SetDmaPriority(Memory::DmaPri::Bones);
        const auto bonesPhaseBegin = std::chrono::steady_clock::now();


        constexpr int kBatch = 8;
        constexpr int kJointCount = 28;
        struct BoneJointSnapshot { float x, y, z, scale; char pad[0x10]; };
        static_assert(sizeof(BoneJointSnapshot) == 32, "BoneJointSnapshot");





        uint64_t scanJointsEvaluated = 0;
        uint64_t scanJointsRejected = 0;
        int scanQueuedBatches = 0;

        if (pressure >= 2 && targets.size() > 1) {
            static size_t recoveryCursor = 0;
            recoveryCursor %= targets.size();
            std::rotate(targets.begin(), targets.begin() + recoveryCursor, targets.end());
            ++recoveryCursor;
        }

        for (size_t base = 0; base < targets.size() && !!budget.Remaining();) {
            if (g_acqState.IsStoppingOrStopped()) return;
            if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;

            if (CS2::ShouldYieldToCameraQuick()) {
                if (!CS2::GiveCameraPriorityBeforeOptionalDma())
                    break;
            }
            const size_t batchSize = pressure >= 3 ? 2u : pressure == 2 ? 4u : static_cast<size_t>(kBatch);
            const size_t end = (std::min)(targets.size(), base + batchSize);
            uintptr_t boneBases[kBatch]{};
            BoneJointSnapshot joints32[kBatch][kJointCount]{};
            float sampledOrigins[kBatch][3];
            for (auto& origin : sampledOrigins)
                std::fill(std::begin(origin), std::end(origin),
                    (std::numeric_limits<float>::quiet_NaN)());
            bool eligible[kBatch]{};
            int queued = 0;



            uintptr_t sceneBuf[kBatch]{};
            uintptr_t boneBaseBuf[kBatch]{};
            bool resolveQueued = false;
            for (size_t i = base; i < end; ++i) {
                const size_t bi = i - base;
                if (IsUserPointer(targets[i].scene)) {
                    sceneBuf[bi] = targets[i].scene;
                } else {
                    mem.AddScatterReadRequest(g_scatter_optional,
                        targets[i].pawn + offsets.m_pGameSceneNode,
                        &sceneBuf[bi], sizeof(sceneBuf[bi]));
                    resolveQueued = true;
                }
            }
            if (resolveQueued) {
                CS2::GiveCameraPriorityBeforeOptionalDma();
                mem.SetDmaCallTag("CS2.Opt.SceneResolve");
                mem.ExecuteReadScatter(g_scatter_optional);
                if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;
            }
            bool boneBaseQueued = false;
            for (size_t i = base; i < end; ++i) {
                const size_t bi = i - base;
                const uintptr_t scene = sceneBuf[bi];
                if (!IsUserPointer(scene)) continue;
                targets[i].scene = scene;
                mem.AddScatterReadRequest(g_scatter_optional,
                    scene + offsets.BoneArray,
                    &boneBaseBuf[bi], sizeof(boneBaseBuf[bi]));
                boneBaseQueued = true;
            }
            if (boneBaseQueued) {
                CS2::GiveCameraPriorityBeforeOptionalDma();
                mem.SetDmaCallTag("CS2.Opt.BoneBaseResolve");
                mem.ExecuteReadScatter(g_scatter_optional);
                if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;
            }
            for (size_t i = base; i < end; ++i) {
                const size_t bi = i - base;
                const uintptr_t boneBase = boneBaseBuf[bi];
                if (!IsUserPointer(boneBase)) continue;
                boneBases[bi] = boneBase;
                eligible[bi] = true;

                mem.AddScatterReadRequest(g_scatter_optional, boneBase,
                    joints32[bi], sizeof(BoneJointSnapshot) * (need_full_bones ? kJointCount : 8));
                mem.AddScatterReadRequest(g_scatter_optional,
                    targets[i].scene + offsets.m_vecAbsOrigin,
                    sampledOrigins[bi], sizeof(sampledOrigins[bi]));
                ++queued;
            }
            if (queued > 0) {
                ++scanQueuedBatches;
                CS2::GiveCameraPriorityBeforeOptionalDma();
                mem.SetDmaCallTag("CS2.Optional.Bones");
                const uint64_t bone_read_begin_ms = GetTickCount64();
                const bool boneReadOk = mem.ExecuteReadScatter(g_scatter_optional);
                const uint64_t bone_read_end_ms = GetTickCount64();

                if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;


                const uint64_t sampleMs = bone_read_begin_ms +
                    (bone_read_end_ms - bone_read_begin_ms) / 2u;
                for (size_t i = base; i < end; ++i) {
                    const size_t bi = i - base;
                    if (!boneReadOk || !eligible[bi] || !IsFinitePosition(sampledOrigins[bi])) continue;
                    if (results.bone_count >= results.bones.size()) break;
                    OptionalBoneResult& r = results.bones[results.bone_count];
                    r = OptionalBoneResult{};
                    r.pawn = targets[i].pawn;
                    r.controller = targets[i].controller;
                    r.bone_base = boneBases[bi];


                    std::memcpy(r.origin, sampledOrigins[bi], sizeof(r.origin));

                    const auto jointPosOf = [&](int jointIdx, float out[3]) {
                        if (jointIdx < 0 || jointIdx >= static_cast<int>(kJointCount))
                            return false;
                        const auto& j = joints32[bi][jointIdx];
                        out[0] = j.x; out[1] = j.y; out[2] = j.z;
                        return true;
                    };
                    const auto validJoint = [&](const float pos[3], size_t boneSlot) {
                        ++scanJointsEvaluated;
                        if (!std::isfinite(pos[0]) || !std::isfinite(pos[1]) || !std::isfinite(pos[2])) {
                            ++scanJointsRejected;
                            return false;
                        }
                        if (pos[0] == 0.f && pos[1] == 0.f && pos[2] == 0.f) {
                            ++scanJointsRejected;
                            return false;
                        }
                        const float dx = pos[0] - r.origin[0];
                        const float dy = pos[1] - r.origin[1];
                        const float dz = pos[2] - r.origin[2];

                        float minDz = -50.f;
                        if (boneSlot >= 14 && boneSlot <= 19) minDz = -120.f;
                        const bool ok = dx * dx + dy * dy < 180.f * 180.f &&
                            dz > minDz && dz < 200.f;
                        if (!ok) ++scanJointsRejected;
                        return ok;
                    };


                    const int kIdx[4] = {
                        CS2::GetBoneJointIndex(CS2::BoneSlot::Head),
                        CS2::GetBoneJointIndex(CS2::BoneSlot::Neck),
                        CS2::GetBoneJointIndex(CS2::BoneSlot::SpineUpper),
                        CS2::GetBoneJointIndex(CS2::BoneSlot::SpineLower)
                    };
                    bool upperPoseValid = true;
                    for (int s = 0; s < 4; ++s) {




                        if (kIdx[s] < 0 || kIdx[s] >= static_cast<int>(kJointCount)) {
                            upperPoseValid = false;
                            continue;
                        }
                        float jpos[3]{};
                        if (!jointPosOf(kIdx[s], jpos)) {
                            upperPoseValid = false;
                            continue;
                        }
                        const size_t boneSlot = (s == 3) ?
                            static_cast<size_t>(CS2::BoneSlot::SpineLower) : s;
                        if (!validJoint(jpos, boneSlot)) {
                            upperPoseValid = false;
                            continue;
                        }
                        r.bones[boneSlot][0] = jpos[0];
                        r.bones[boneSlot][1] = jpos[1];
                        r.bones[boneSlot][2] = jpos[2];
                    }
                    bool completePose = false;
                    if (need_full_bones) {
                        std::size_t validBoneCount = 0;
                        for (size_t b = 0; b < kBoneSlotCount; ++b) {
                            int jointIdx = CS2::GetBoneJointIndex(static_cast<CS2::BoneSlot>(b));
                            if (jointIdx < 0 || jointIdx >= static_cast<int>(kJointCount))
                                continue;
                            float jpos[3]{};
                            if (!jointPosOf(jointIdx, jpos) || !validJoint(jpos, b))
                                continue;
                            r.bones[b][0] = jpos[0]; r.bones[b][1] = jpos[1]; r.bones[b][2] = jpos[2];
                            ++validBoneCount;
                        }
                        completePose = upperPoseValid && validBoneCount >= 8;
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
                            const auto segmentValid = [&](size_t a, size_t b, float minLength, float maxLength) {
                                const float sx = r.bones[a][0] - r.bones[b][0];
                                const float sy = r.bones[a][1] - r.bones[b][1];
                                const float sz = r.bones[a][2] - r.bones[b][2];
                                const float lengthSq = sx * sx + sy * sy + sz * sz;
                                return lengthSq >= minLength * minLength &&
                                    lengthSq <= maxLength * maxLength;
                            };
                            static constexpr std::pair<size_t, size_t> kAnatomyLinks[] = {
                                {0,1}, {1,2}, {2,3}, {3,4}, {4,5},
                                {1,6}, {6,7}, {7,8}, {8,9},
                                {1,10}, {10,11}, {11,12}, {12,13},
                                {5,14}, {14,15}, {15,16},
                                {5,17}, {17,18}, {18,19}
                            };
                            std::size_t validSegments = 0;
                            for (const auto& [a, b] : kAnatomyLinks) {
                                const bool hasA = r.bones[a][0] != 0.f || r.bones[a][1] != 0.f || r.bones[a][2] != 0.f;
                                const bool hasB = r.bones[b][0] != 0.f || r.bones[b][1] != 0.f || r.bones[b][2] != 0.f;
                                if (!hasA || !hasB)
                                    continue;
                                const float maxLength = (a >= 14 || b >= 14) ? 72.f : 58.f;
                                if (!segmentValid(a, b, .25f, maxLength)) {
                                    completePose = false;
                                    break;
                                }
                                ++validSegments;
                            }
                            completePose = completePose && validSegments >= 6;
                        }
                        r.full_bones_ok = completePose;
                    }
                    if (upperPoseValid) {
                        r.bones_ok = true;
                        r.sample_ms = sampleMs;
                        ++results.bone_count;
                    }
                }
            }
            base = end;
        }

        {
            auto& tel = OmniGhost::Gameplay::DmaTelemetry::CS2();
            tel.bones.joints_evaluated.fetch_add(scanJointsEvaluated, std::memory_order_relaxed);
            tel.bones.joints_rejected.fetch_add(scanJointsRejected, std::memory_order_relaxed);




            if (scanQueuedBatches > 0 && scanJointsEvaluated > 0 && results.bone_count == 0)
                OmniGhost::SessionLog::Write(
                    OmniGhost::SessionLog::Severity::Warning,
                    OmniGhost::SessionLog::Subsystem::Adapter,
                    "[CS2] Bones: amostra rejeitada; mantido o layout validado de 32B");
        }
        results.bones_ms = OmniGhost::Gameplay::TimeMs(bonesPhaseBegin);
    }


    if (need_names && !!budget.Remaining() && pressure < 2) {
        mem.SetDmaPriority(Memory::DmaPri::WeaponNameTeam);


        struct NameSlot { size_t target_index = 0; char name[64]{}; };
        std::vector<NameSlot> pending;
        pending.reserve(targets.size());
        for (size_t i = 0; i < targets.size(); ++i) {
            if (!IsUserPointer(targets[i].controller)) continue;
            if (targets[i].has_name) continue;
            pending.emplace_back();
            pending.back().target_index = i;
        }
        bool queuedAny = false;
        for (NameSlot& slot : pending) {
            mem.AddScatterReadRequest(g_scatter_optional,
                targets[slot.target_index].controller + offsets.m_iszPlayerName,
                slot.name, sizeof(slot.name) - 1);
            queuedAny = true;
        }
        if (queuedAny) {
            CS2::GiveCameraPriorityBeforeOptionalDma();
            mem.SetDmaCallTag("CS2.Opt.Names");
            if (!mem.ExecuteReadScatter(g_scatter_optional))
                return;
        }
        if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;
        for (NameSlot& slot : pending) {
            if (!slot.name[0] || results.name_count >= results.names.size()) continue;
            auto& r = results.names[results.name_count++];
            r.pawn = targets[slot.target_index].pawn;
            r.controller = targets[slot.target_index].controller;
            std::snprintf(r.name, sizeof(r.name), "%s", slot.name);
        }
    }


    if (frame_config.dropped_weapons && pressure < 1) {
        mem.SetDmaPriority(Memory::DmaPri::OtherMetadata);
        if (CS2::GiveCameraPriorityBeforeOptionalDma()) {

            if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;
            const auto droppedPhaseBegin = std::chrono::steady_clock::now();
            CollectDroppedWeapons(frame_config, in_match, client_base, results.dropped_weapons);

            if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;
            results.dropped_weapons_valid = true;
            results.dropped_ms = OmniGhost::Gameplay::TimeMs(droppedPhaseBegin);
        }
    }


    if (need_weapons && allow_weapon && !!budget.Remaining()) {
        mem.SetDmaPriority(Memory::DmaPri::WeaponNameTeam);
        const auto weaponPhaseBegin = std::chrono::steady_clock::now();



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


        std::vector<uintptr_t> weaponEntities(targets.size(), 0);
        {

            const uintptr_t primaryStride = offsets.entity_identity_stride;
            const uintptr_t alternateStride = (primaryStride == CS2::kEntityStrideAlternate)
                ? CS2::kEntityStridePrimary : CS2::kEntityStrideAlternate;
            CS2::ScatterResolvePawnHandles(weaponHandles.data(), weaponEntities.data(),
                static_cast<int>(targets.size()), primaryStride, alternateStride,
                true, g_scatter_optional, client_base);
        }
        if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;


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



        std::vector<int> weaponReserves(targets.size(), -1);
        if (frame_config.weapon_ammo && offsets.m_pReserveAmmo) {
            std::vector<uintptr_t> reservePtrs(targets.size(), 0);
            {
                auto h = mem.CreateScatterHandle();
                if (h) {
                    for (size_t i = 0; i < targets.size(); ++i) {
                        if (!IsUserPointer(weaponEntities[i])) continue;
                        mem.AddScatterReadRequest(h, weaponEntities[i] + offsets.m_pReserveAmmo,
                                                  &reservePtrs[i], sizeof(uintptr_t));
                    }
                    mem.ExecuteReadScatter(h);
                    mem.CloseScatterHandle(h);
                }
            }
            {
                auto h = mem.CreateScatterHandle();
                if (h) {
                    for (size_t i = 0; i < targets.size(); ++i) {
                        if (!IsUserPointer(reservePtrs[i])) continue;
                        mem.AddScatterReadRequest(h, reservePtrs[i],
                                                  &weaponReserves[i], sizeof(int));
                    }
                    mem.ExecuteReadScatter(h);
                    mem.CloseScatterHandle(h);
                }
            }
        }
        if (jobGen != g_session_generation.load(std::memory_order_acquire)) return;


        for (size_t i = 0; i < targets.size() && !!budget.Remaining(); ++i) {
            if (!IsUserPointer(weaponEntities[i])) continue;
            const bool haveDef = weaponDefs[i] > 0 && weaponDefs[i] < 6000;
            const bool haveClip = frame_config.weapon_ammo && weaponClips[i] >= 0 && weaponClips[i] < 500;
            const bool haveReserve = frame_config.weapon_ammo && weaponReserves[i] >= 0 && weaponReserves[i] < 10000;
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
                if (haveReserve)
                    r.ammo_reserve = weaponReserves[i];
            }
        }
        results.weapon_ms = OmniGhost::Gameplay::TimeMs(weaponPhaseBegin);
    }


    results.budget_left_ms = budget.Remaining();


    if (OmniGhost::Gameplay::UnifiedESP::Has(
            fields, OmniGhost::Gameplay::UnifiedESP::DataField::Visibility) &&
        !!budget.Remaining() && pressure < 2) {


        auto& collision = Trajectory::CollisionCache();
        const auto world = AcquireRuntimeSnapshot();
        const CameraSnapshot camera = SelectCameraSnapshot(targets.front().position_sample_ms);
        const auto cameraOrigin = Visibility::CameraOrigin(
            camera.timestamp_ms ? camera.view_matrix : nullptr);
        if (world && world->session_generation == jobGen && cameraOrigin &&
            collision.IsLoadedFor(world->map_name)) {

            std::vector<VisibilityPipeline::Target> visTargets;
            visTargets.reserve(targets.size());
            for (const auto& t : targets) {
                VisibilityPipeline::Target vt{};
                vt.pawn = t.pawn;
                const Player* cachedPlayer = nullptr;
                for (const auto& candidate : world->players) {
                    if (candidate.pawn == t.pawn) {
                        cachedPlayer = &candidate;
                        break;
                    }
                }
                constexpr std::array<std::size_t, 4> kAimSlots = { 0u, 1u, 2u, 4u };
                constexpr std::array<float, 4> kFallbackHeights = { 70.f, 55.f, 40.f, 22.f };
                for (std::size_t pointIndex = 0; pointIndex < vt.points.size(); ++pointIndex) {
                    const std::size_t slot = kAimSlots[pointIndex];
                    if (cachedPlayer && cachedPlayer->bones_ok && slot < kBoneSlotCount) {
                        const float* bone = cachedPlayer->bones[slot];
                        if (std::isfinite(bone[0]) && std::isfinite(bone[1]) && std::isfinite(bone[2]) &&
                            !(bone[0] == 0.f && bone[1] == 0.f && bone[2] == 0.f)) {
                            vt.points[pointIndex] = Trajectory::Vec3(bone[0], bone[1], bone[2]);
                            continue;
                        }
                    }
                    vt.points[pointIndex] = Trajectory::Vec3(
                        t.origin[0], t.origin[1], t.origin[2] + kFallbackHeights[pointIndex]);
                }
                visTargets.push_back(vt);
            }

            if (!visTargets.empty()) {
                mem.SetDmaPriority(Memory::DmaPri::OtherMetadata);
                CS2::GiveCameraPriorityBeforeOptionalDma();
                const auto visPhaseBegin = std::chrono::steady_clock::now();

                g_visibility_worker.Submit(
                    collision.WorldSnapshot(), *cameraOrigin, std::move(visTargets), jobGen);


                for (const auto& t : targets) {
                    if (!IsUserPointer(t.pawn)) continue;
                    auto visResult = g_visibility_worker.Get(t.pawn, jobGen);
                    if (visResult.known && results.visibility_count < results.visibility.size()) {
                        auto& r = results.visibility[results.visibility_count++];
                        r.pawn = t.pawn;
                        r.visible = visResult.visible;
                        r.known = true;
                        r.point_known_mask = visResult.point_known_mask;
                        r.point_visible_mask = visResult.point_visible_mask;
                        r.checked_ms = visResult.checked_ms;
                    }
                }
                results.visibility_ms = OmniGhost::Gameplay::TimeMs(visPhaseBegin);
            }
        }
    }

    if (jobGen != g_session_generation.load(std::memory_order_acquire))
        return;

    if (auto slot = g_optional_results.TryBeginWrite()) {
        *slot.value = std::move(results);
        g_optional_results.Publish(slot.index);
    }
}
