
static void CalibratePawnStride(uintptr_t local_controller, uintptr_t local_pawn) {
    static bool calibrated = false;
    static uintptr_t last_local_pawn = 0;
    if (!local_controller || !local_pawn)
        return;
    if (calibrated && last_local_pawn == local_pawn)
        return;

    uint32_t handle = 0;
    if (!QReadT(local_controller + offsets.m_hPlayerPawn, handle) || !handle || handle == 0xFFFFFFFF)
        return;

    const uintptr_t with70 = ResolveEntityByHandle(handle, kEntityStrideAlternate);
    if (with70 == local_pawn) {
        g_pawn_stride = kEntityStrideAlternate;
        calibrated = true;
        last_local_pawn = local_pawn;
        std::cout << "[CS2] Pawn stride calibrated: 0x70 (handle=0x"
                  << std::hex << handle << std::dec << ")" << std::endl;
        return;
    }
    const uintptr_t with78 = ResolveEntityByHandle(handle, kEntityStridePrimary);
    if (with78 == local_pawn) {
        g_pawn_stride = kEntityStridePrimary;
        calibrated = true;
        last_local_pawn = local_pawn;
        std::cout << "[CS2] Pawn stride calibrated: 0x78 (handle=0x"
                  << std::hex << handle << std::dec << ")" << std::endl;
        return;
    }


    static bool dumped = false;
    if (!dumped) {
        dumped = true;
        std::cout << "[CS2] Pawn stride calibration FAILED"
                  << " localCtrl=0x" << std::hex << local_controller
                  << " localPawn=0x" << local_pawn
                  << " handle=0x" << handle
                  << " got70=0x" << with70
                  << " got78=0x" << with78
                  << " m_hPlayerPawn=0x" << offsets.m_hPlayerPawn
                  << std::dec << std::endl;
    }
}

static uintptr_t ResolvePawnFromHandle(uint32_t handle, uintptr_t expected_local_pawn) {
    if (!handle || handle == 0xFFFFFFFF) return 0;

    const uintptr_t primary = g_pawn_stride ? g_pawn_stride : offsets.entity_identity_stride;

    const uintptr_t alternate = (primary == kEntityStrideAlternate)
        ? kEntityStridePrimary : kEntityStrideAlternate;


    const uintptr_t preferred = ResolveEntityByHandle(handle, primary);
    if (preferred) {
        if (expected_local_pawn && preferred == expected_local_pawn)
            return preferred;

        if (IsUserPointer(preferred))
            return preferred;
    }

    const uintptr_t alt = ResolveEntityByHandle(handle, alternate);
    if (alt) {
        if (expected_local_pawn && alt == expected_local_pawn) {
            g_pawn_stride = alternate;
            return alt;
        }
        if (IsUserPointer(alt)) {
            g_pawn_stride = alternate;
            return alt;
        }
    }
    return 0;
}

static void UpdateLocalWeaponDefinitionThrottled() {
    static uint64_t next_update_ms = 0;
    static uintptr_t cached_pawn = 0;
    static uintptr_t cached_services = 0;
    static uint32_t cached_handle = 0;
    static uintptr_t cached_entity = 0;
    static uint16_t cached_definition = 0;
    const uint64_t now = GetTickCount64();
    if (now < next_update_ms || !IsUserPointer(runtime.local_pawn) || !offsets.m_pWeaponServices || !offsets.m_hActiveWeapon)
        return;
    if (cached_pawn != runtime.local_pawn) {
        cached_pawn = runtime.local_pawn;
        cached_services = 0;
        cached_handle = 0;
        cached_entity = 0;
        cached_definition = 0;
        runtime.local_weapon_definition = 0;
    }
    CS2::GiveCameraPriorityBeforeOptionalDma();
    uintptr_t services = cached_services;
    uint32_t handle = 0;
    bool sampled = false;
    EnsureScatter();
    if (g_scatter_full) {
        mem.AddScatterReadRequest(g_scatter_full, runtime.local_pawn + offsets.m_pWeaponServices,
                                  &services, sizeof(services));
        if (IsUserPointer(cached_services))
            mem.AddScatterReadRequest(g_scatter_full, cached_services + offsets.m_hActiveWeapon,
                                      &handle, sizeof(handle));
        mem.SetDmaCallTag("CS2.LocalWeaponState");
        mem.ExecuteReadScatter(g_scatter_full);


        if (!handle && IsUserPointer(services) && services != cached_services) {
            cached_services = services;
            next_update_ms = now + 40u;
            return;
        }
        sampled = true;
    } else {
        sampled = QReadT(runtime.local_pawn + offsets.m_pWeaponServices, services);
        if (sampled && IsUserPointer(services))
            sampled = QReadT(services + offsets.m_hActiveWeapon, handle);
    }
    if (!sampled || !IsUserPointer(services) || !handle || handle == 0xFFFFFFFF) {
        if (sampled) runtime.local_weapon_definition = 0;
        next_update_ms = now + 50u;
        return;
    }
    cached_services = services;
    if (handle == cached_handle && cached_definition) {
        runtime.local_weapon_definition = cached_definition;
        next_update_ms = now + 150u;
        return;
    }
    const uintptr_t entity = ResolveEntityByHandle(handle, g_pawn_stride ? g_pawn_stride : offsets.entity_identity_stride);
    uint16_t definition = 0;
    if (IsUserPointer(entity) && QReadT(entity + offsets.m_AttributeManager + offsets.m_Item +
        offsets.m_iItemDefinitionIndex, definition) && definition > 0 && definition < 6000) {
        cached_handle = handle;
        cached_entity = entity;
        cached_definition = definition;
        runtime.local_weapon_definition = definition;
        next_update_ms = now + 150u;
    } else {
        cached_handle = handle;
        cached_entity = 0;
        cached_definition = 0;
        runtime.local_weapon_definition = 0;
        next_update_ms = now + 50u;
    }
}

static uint32_t ReadPawnHandle(uintptr_t controller) {
    if (!controller) return 0;
    uint32_t handle = 0;
    if (QReadT(controller + offsets.m_hPlayerPawn, handle) && handle && handle != 0xFFFFFFFF)
        return handle;

    if (offsets.m_hObserverPawn &&
        QReadT(controller + offsets.m_hObserverPawn, handle) && handle && handle != 0xFFFFFFFF)
        return handle;
    return 0;
}


static int CollectControllers(uintptr_t list_entry, uintptr_t stride, uintptr_t* out, int max_count) {
    int count = 0;
    if (!list_entry || !out || max_count <= 0) return 0;

    EnsureScatter();
    if (g_scatter_full) {
        for (int i = 0; i < max_count; ++i) {
            out[i] = 0;
            mem.AddScatterReadRequest(
                g_scatter_full,
                list_entry + static_cast<uintptr_t>(i + 1) * stride,
                &out[i],
                sizeof(uintptr_t));
        }
        mem.ExecuteReadScatter(g_scatter_full);
        for (int i = 0; i < max_count; ++i) {
            if (IsUserPointer(out[i]))
                ++count;
            else
                out[i] = 0;
        }
        return count;
    }

    for (int i = 0; i < max_count; ++i) {
        uintptr_t controller = 0;
        if (QReadT(list_entry + static_cast<uintptr_t>(i + 1) * stride, controller) &&
            IsUserPointer(controller)) {
            out[i] = controller;
            ++count;
        } else {
            out[i] = 0;
        }
    }
    return count;
}


static void ScatterReadPawnHandles(const uintptr_t* controllers, uint32_t* handles, int count) {
    if (!controllers || !handles || count <= 0) return;
    EnsureScatter();
    if (!g_scatter_full) {
        for (int i = 0; i < count; ++i)
            handles[i] = controllers[i] ? ReadPawnHandle(controllers[i]) : 0;
        return;
    }
    for (int i = 0; i < count; ++i) {
        handles[i] = 0;
        if (!controllers[i]) continue;
        mem.AddScatterReadRequest(
            g_scatter_full,
            controllers[i] + offsets.m_hPlayerPawn,
            &handles[i],
            sizeof(uint32_t));
    }
    mem.ExecuteReadScatter(g_scatter_full);
    for (int i = 0; i < count; ++i) {
        if (handles[i] == 0xFFFFFFFF)
            handles[i] = 0;
    }
}




static int ScatterResolvePawnHandles(const uint32_t* handles, uintptr_t* pawns,
                                     int count, uintptr_t stride,
                                     uintptr_t alternate_stride,
                                     bool optionalLane = false,
                                     VMMDLL_SCATTER_HANDLE workerScatter = nullptr,
                                     uintptr_t workerClientBase = 0) {
    if (!handles || !pawns || count <= 0) return 0;
    std::fill(pawns, pawns + count, 0);
    bool anyValidHandle = false;
    for (int i = 0; i < count; ++i) {
        if (DecodeEntityHandle(handles[i]).valid) {
            anyValidHandle = true;
            break;
        }
    }
    if (!anyValidHandle) return 0;

    uintptr_t root = 0;
    if (workerScatter) {
        if (!IsUserPointer(workerClientBase) ||
            !QReadT(workerClientBase + offsets.dwEntityList, root) || !IsUserPointer(root))
            return 0;
    } else {
        root = g_cached_entity_root;
        if (!IsUserPointer(root)) {
            if (!QReadT(runtime.client_base + offsets.dwEntityList, root) || !IsUserPointer(root))
                return 0;
            g_cached_entity_root = root;
        }
    }

    constexpr int kMaxPages = 64;
    uintptr_t chunks[kMaxPages]{};
    bool pageUsed[kMaxPages]{};
    static thread_local uintptr_t cachedChunks[kMaxPages]{};
    static thread_local uintptr_t cachedChunkRoot = 0;
    static thread_local uint64_t cachedChunksUntilMs = 0;
    static thread_local uint64_t cachedChunkGeneration = 0;
    const uint64_t nowMs = GetTickCount64();
    const uint64_t generation = g_session_generation.load(std::memory_order_acquire);
    if (cachedChunkGeneration != generation || cachedChunkRoot != root ||
        nowMs >= cachedChunksUntilMs) {
        std::fill(std::begin(cachedChunks), std::end(cachedChunks), 0);
        cachedChunkGeneration = generation;
        cachedChunkRoot = root;
        cachedChunksUntilMs = nowMs + 500u;
    }
    for (int i = 0; i < count; ++i) {
        const EntityHandleParts parts = DecodeEntityHandle(handles[i]);
        if (!parts.valid) continue;
        const int page = static_cast<int>(parts.page);
        if (page >= 0 && page < kMaxPages) pageUsed[page] = true;
    }

    if (!workerScatter) EnsureScatter();
    const VMMDLL_SCATTER_HANDLE scatter = workerScatter ? workerScatter :
        static_cast<VMMDLL_SCATTER_HANDLE>(g_scatter_full);
    if (!scatter) {
        if (optionalLane) return 0;
        int resolved = 0;
        for (int i = 0; i < count; ++i) {
            pawns[i] = ResolvePawnFromHandle(handles[i], runtime.local_pawn);
            if (IsUserPointer(pawns[i])) ++resolved;
        }
        return resolved;
    }

    bool queuedChunkPointers = false;
    bool usedCachedChunkPointers = false;
    for (int page = 0; page < kMaxPages; ++page) {
        if (!pageUsed[page]) continue;
        if (IsUserPointer(cachedChunks[page])) {
            chunks[page] = cachedChunks[page];
            usedCachedChunkPointers = true;
            continue;
        }
        mem.AddScatterReadRequest(scatter,
            root + offsets.entity_page_table_offset + sizeof(uintptr_t) * static_cast<uintptr_t>(page),
            &chunks[page], sizeof(uintptr_t));
        queuedChunkPointers = true;
    }
    if (queuedChunkPointers) {
        if (optionalLane) CS2::GiveCameraPriorityBeforeOptionalDma();
        mem.SetDmaCallTag("CS2.ResolveHandles.Pages");
        mem.ExecuteReadScatter(scatter);
        for (int page = 0; page < kMaxPages; ++page)
            if (IsUserPointer(chunks[page])) cachedChunks[page] = chunks[page];
    }


    bool queuedEntityPointers = false;
    for (int i = 0; i < count; ++i) {
        const EntityHandleParts parts = DecodeEntityHandle(handles[i]);
        if (!parts.valid) continue;
        const int page = static_cast<int>(parts.page);
        if (page < 0 || page >= kMaxPages || !IsUserPointer(chunks[page])) continue;
        mem.AddScatterReadRequest(scatter, chunks[page] + stride * parts.index,
                                  &pawns[i], sizeof(uintptr_t));
        queuedEntityPointers = true;
    }
    if (queuedEntityPointers) {
        if (optionalLane) CS2::GiveCameraPriorityBeforeOptionalDma();
        mem.SetDmaCallTag("CS2.ResolveHandles.Entities");
        mem.ExecuteReadScatter(scatter);
    }


    int resolved = 0;
    for (int i = 0; i < count; ++i) {
        if (IsUserPointer(pawns[i])) {
            ++resolved;
        } else {
            pawns[i] = 0;
        }
    }



    if (alternate_stride && alternate_stride != stride) {
        queuedEntityPointers = false;
        for (int i = 0; i < count; ++i) {
            if (IsUserPointer(pawns[i])) continue;
            const EntityHandleParts parts = DecodeEntityHandle(handles[i]);
            if (!parts.valid) continue;
            const int page = static_cast<int>(parts.page);
            if (page < 0 || page >= kMaxPages || !IsUserPointer(chunks[page])) continue;
            mem.AddScatterReadRequest(scatter, chunks[page] + alternate_stride * parts.index,
                                      &pawns[i], sizeof(uintptr_t));
            queuedEntityPointers = true;
        }
        if (queuedEntityPointers) {
            if (optionalLane) CS2::GiveCameraPriorityBeforeOptionalDma();
            mem.SetDmaCallTag("CS2.ResolveHandles.Entities.Alt");
            mem.ExecuteReadScatter(scatter);
        }
        resolved = 0;
        for (int i = 0; i < count; ++i) {
            if (IsUserPointer(pawns[i])) ++resolved;
            else pawns[i] = 0;
        }
    }
    if (resolved == 0 && count > 0 && usedCachedChunkPointers) {
        std::fill(std::begin(cachedChunks), std::end(cachedChunks), 0);
        cachedChunksUntilMs = 0;
    }
    return resolved;
}


struct PawnCoreFields {


    int health = -1;
    uint8_t controller_alive = 0xFF;
    uint64_t controller_steam_id = UINT64_MAX;
    uint8_t team = 0;
    int armor = 0;
    uintptr_t scene = 0;
    float velocity[3]{};
    bool spotted = true;
    bool spotted_known = false;
    uint8_t scoped = 0;
    float flash = 0.f;
    float eye_angles[2]{};
    uintptr_t weapon_services = 0;


    bool read_ok = false;
};

static void ScatterReadPawnCore(const uintptr_t* pawns, const uintptr_t* controllers,
                                PawnCoreFields* fields, int count,
                                bool need_armor, bool need_scoped, bool need_flash,
                                 bool need_yaw, bool need_weapon_services, bool need_spotted,
                                 const uint8_t* cached_teams,
                                 const bool* refresh_teams,
                                 const int* cached_armors,
                                 const bool* refresh_armors,
                                const uintptr_t* cached_scenes,
                                const bool* refresh_scenes,
                                float (*positions)[3], bool need_velocity,
                                const uintptr_t* cached_weapon_services,
                                const bool* refresh_weapon_services,
                                const bool* cached_spotted,
                                const bool* refresh_spotted) {
    if (!pawns || !fields || count <= 0) return;
    EnsureScatter();


    if (!g_scatter_full) {
        for (int i = 0; i < count; ++i) {
            fields[i] = PawnCoreFields{};
        }
        return;
    }
    static thread_local uint8_t spotted_buf[128];
    if (count > 128) count = 128;
    for (int i = 0; i < count; ++i) {
        const uintptr_t cachedScene = cached_scenes ? cached_scenes[i] : 0;
        const uintptr_t cachedWepSvc = cached_weapon_services ? cached_weapon_services[i] : 0;
        const bool cachedSpot = cached_spotted ? cached_spotted[i] : true;
        fields[i] = {};
        fields[i].health = -1;
        fields[i].controller_alive = 0xFF;
        fields[i].controller_steam_id = UINT64_MAX;
        fields[i].team = cached_teams ? cached_teams[i] : 0;
        fields[i].armor = cached_armors ? cached_armors[i] : 0;
        fields[i].scene = cachedScene;
        fields[i].weapon_services = cachedWepSvc;
        fields[i].spotted = cachedSpot;
        spotted_buf[i] = 1;
        if (!pawns[i]) continue;
        mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_iHealth,
                                  &fields[i].health, sizeof(int));
        if (controllers && IsUserPointer(controllers[i]) && offsets.m_bPawnIsAlive)
            mem.AddScatterReadRequest(g_scatter_full, controllers[i] + offsets.m_bPawnIsAlive,
                                      &fields[i].controller_alive, sizeof(uint8_t));
        if (controllers && IsUserPointer(controllers[i]) && offsets.m_steamID)
            mem.AddScatterReadRequest(g_scatter_full, controllers[i] + offsets.m_steamID,
                                      &fields[i].controller_steam_id, sizeof(uint64_t));
        if (!IsPlayableTeam(static_cast<int>(fields[i].team)) ||
            (refresh_teams && refresh_teams[i]))
            mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_iTeamNum,
                                      &fields[i].team, sizeof(uint8_t));
        if (need_armor && (!cached_armors || (refresh_armors && refresh_armors[i])))
            mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_ArmorValue,
                                      &fields[i].armor, sizeof(int));
        if (!IsUserPointer(cachedScene) || (refresh_scenes && refresh_scenes[i])) {


            fields[i].scene = 0;
            mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_pGameSceneNode,
                                      &fields[i].scene, sizeof(uintptr_t));
        }

        if (need_weapon_services && (!cached_weapon_services || (refresh_weapon_services && refresh_weapon_services[i]))) {
            fields[i].weapon_services = 0;
            mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_pWeaponServices,
                                      &fields[i].weapon_services, sizeof(uintptr_t));
        }

        if (need_spotted && offsets.m_entitySpottedState && (!cached_spotted || (refresh_spotted && refresh_spotted[i]))) {
            spotted_buf[i] = 0xFF;
            mem.AddScatterReadRequest(g_scatter_full,
                pawns[i] + offsets.m_entitySpottedState + offsets.m_bSpotted,
                &spotted_buf[i], sizeof(uint8_t));
        } else if (need_spotted) {
            spotted_buf[i] = cachedSpot ? 1 : 0;
        } else {
            spotted_buf[i] = 1;
        }
        if (positions) {
            if (IsUserPointer(cachedScene))
                mem.AddScatterReadRequest(g_scatter_full, cachedScene + offsets.m_vecAbsOrigin,
                                          positions[i], sizeof(float) * 3);
            else
                mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_vOldOrigin,
                                          positions[i], sizeof(float) * 3);
        }
        if (need_velocity && offsets.m_vecVelocity)
            mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_vecVelocity,
                                      fields[i].velocity, sizeof(fields[i].velocity));
        if (need_scoped && offsets.m_bIsScoped)
            mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_bIsScoped,
                                      &fields[i].scoped, sizeof(fields[i].scoped));
        if (need_flash && offsets.m_flFlashDuration)
            mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_flFlashDuration,
                                      &fields[i].flash, sizeof(fields[i].flash));
        if (need_yaw)
            mem.AddScatterReadRequest(g_scatter_full, pawns[i] + offsets.m_angEyeAngles,
                                      fields[i].eye_angles, sizeof(fields[i].eye_angles));
    }
    // Keep the projection matrix current before the largest position batch.
    // Otherwise every ESP element visibly trails together while the camera turns.
    CS2::GiveCameraPriorityBeforeOptionalDma();
    if (!mem.ExecuteReadScatter(g_scatter_full)) {
        for (int i = 0; i < count; ++i) {
            fields[i] = PawnCoreFields{};
            if (positions)
                std::fill(std::begin(positions[i]), std::end(positions[i]),
                    (std::numeric_limits<float>::quiet_NaN)());
        }
        g_read_fails.fetch_add(count, std::memory_order_relaxed);
        return;
    }
    for (int i = 0; i < count; ++i) {
        fields[i].spotted = (spotted_buf[i] != 0);
        fields[i].read_ok = true;
    }
}
