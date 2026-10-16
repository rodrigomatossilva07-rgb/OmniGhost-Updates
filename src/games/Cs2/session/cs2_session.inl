


uintptr_t g_pending_entity_list = 0;
int g_entity_list_confirmations = 0;
uintptr_t g_controller_stride = 0;
uintptr_t g_pawn_stride = 0;
uint64_t g_entity_list_next_validation_ms = 0;
uint64_t g_entity_list_next_full_probe_ms = 0;
bool g_entity_list_force_refresh = true;








void ResetSessionState(const char* reason) {
    const uint64_t newGeneration = CacheManager::AdvanceGeneration();
    g_boneMapping.resolved = false;
    g_visibility_worker.Reset();
    g_collision_loader.Reset();
    Telemetry::ResetSession();
    Camera::Reset();
    RuntimePipeline::ResetPublishMetrics();
    CacheManager::ResetMetrics();
    g_pending_entity_list = 0;
    g_entity_list_confirmations = 0;
    g_entity_list_next_validation_ms = 0;
    g_entity_list_next_full_probe_ms = 0;
    g_entity_list_force_refresh = true;
    g_cached_entity_root = 0;
    if (reason) std::cout << "[CS2] Reset de sessao: " << reason
                          << " gen=" << newGeneration << std::endl;
}



void RefreshMapName() {
    if (!offsets.dwGlobalVars) {



        return;
    }


    static std::string cachedMapName;
    static uint64_t cachedMapNameMs = 0;
    const uint64_t nowMs = GetTickCount64();
    constexpr uint64_t kMapNameTtlMs = 30000;
    if (!cachedMapName.empty() && nowMs < cachedMapNameMs + kMapNameTtlMs) {

        if (cachedMapName.size() >= sizeof(runtime.map_name))
            cachedMapName.resize(sizeof(runtime.map_name) - 1);
        std::memset(runtime.map_name, 0, sizeof(runtime.map_name));
        std::memcpy(runtime.map_name, cachedMapName.data(), cachedMapName.size());
        return;
    }

    uintptr_t globals = 0;
    uintptr_t map_address = 0;
    char raw[128]{};
    mem.SetDmaCallTag("CS2.MapRefresh");
    EnsureScatter();
    bool mapOk = false;
    if (g_scatter_full) {
        mem.AddScatterReadRequest(g_scatter_full, runtime.client_base + offsets.dwGlobalVars,
                                  &globals, sizeof(globals));
        mem.ExecuteReadScatter(g_scatter_full);
        if (IsUserPointer(globals)) {
            mem.AddScatterReadRequest(g_scatter_full, globals + offsets.global_vars_current_map,
                                      &map_address, sizeof(map_address));
            mem.ExecuteReadScatter(g_scatter_full);
            if (IsUserPointer(map_address)) {
                mem.AddScatterReadRequest(g_scatter_full, map_address, raw, sizeof(raw) - 1);
                mem.ExecuteReadScatter(g_scatter_full);
                mapOk = true;
            }
        }
    } else {
        mapOk = QReadT(runtime.client_base + offsets.dwGlobalVars, globals, "CS2.MapRefresh") &&
                IsUserPointer(globals) &&
                QReadT(globals + offsets.global_vars_current_map, map_address, "CS2.MapRefresh") &&
                IsUserPointer(map_address) &&
                QRead(map_address, raw, sizeof(raw) - 1, "CS2.MapRefresh");
    }
    if (!mapOk) {


        return;
    }

    raw[sizeof(raw) - 1] = '\0';
    std::string clean(raw);
    std::replace(clean.begin(), clean.end(), '\\', '/');
    std::string lower = clean;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (lower.find("background") != std::string::npos ||
        lower.find("mainmenu") != std::string::npos) {
        runtime.map_name[0] = '\0';
        cachedMapName.clear();
        cachedMapNameMs = nowMs;
        return;
    }
    if (const size_t slash = clean.find_last_of('/'); slash != std::string::npos)
        clean.erase(0, slash + 1);
    if (const size_t extension = clean.find('.'); extension != std::string::npos)
        clean.erase(extension);
    clean.erase(std::remove_if(clean.begin(), clean.end(), [](unsigned char c) {
        return !(std::isalnum(c) || c == '_' || c == '-');
    }), clean.end());

    if (clean.size() >= sizeof(runtime.map_name))
        clean.resize(sizeof(runtime.map_name) - 1);
    std::memset(runtime.map_name, 0, sizeof(runtime.map_name));
    std::memcpy(runtime.map_name, clean.data(), clean.size());


    cachedMapName = clean;
    cachedMapNameMs = nowMs;
}


static bool ProbeListEntry(uintptr_t entry, uintptr_t stride) {
    if (!IsUserPointer(entry)) return false;

    uintptr_t ctrls[4]{};
    EnsureScatter();
    if (g_scatter_full) {
        mem.SetDmaCallTag("CS2.ProbeListEntry");
        for (int i = 1; i <= 4; ++i)
            mem.AddScatterReadRequest(g_scatter_full,
                entry + static_cast<uintptr_t>(i) * stride, &ctrls[i - 1], sizeof(uintptr_t));
        const auto t0 = std::chrono::steady_clock::now();
        mem.ExecuteReadScatter(g_scatter_full);
        NotePossibleDeviceStall(OmniGhost::Gameplay::TimeMs(t0));
    } else {
        for (int i = 1; i <= 4; ++i)
            QReadT(entry + static_cast<uintptr_t>(i) * stride, ctrls[i - 1], "CS2.ProbeListEntry");
    }
    int hits = 0;
    for (int i = 0; i < 4; ++i)
        if (IsUserPointer(ctrls[i])) ++hits;
    return hits >= 1;
}

static bool RefreshEntityListEntry() {
    ScopedPhase _phEntity(&g_phase.entity_ms);
    const uint64_t nowMs = GetTickCount64();
    const bool haveEntry = runtime.entity_list_entry != 0;



    if (haveEntry && IsUserPointer(g_cached_entity_root) &&
        !g_entity_list_force_refresh && nowMs < g_entity_list_next_validation_ms)
        return true;

    uintptr_t root = g_cached_entity_root;
    uintptr_t primaryEntry = 0;
    bool validationOk = false;
    EnsureScatter();
    if (g_scatter_full && IsUserPointer(root) && haveEntry) {



        mem.SetDmaCallTag("CS2.EntityListValidate");
        uintptr_t observedRoot = 0;
        mem.AddScatterReadRequest(g_scatter_full,
            runtime.client_base + offsets.dwEntityList, &observedRoot, sizeof(observedRoot));
        mem.AddScatterReadRequest(g_scatter_full,
            root + offsets.entity_page_table_offset, &primaryEntry, sizeof(primaryEntry));
        mem.ExecuteReadScatter(g_scatter_full);
        validationOk = observedRoot == root && primaryEntry == runtime.entity_list_entry;
    }
    if (validationOk && nowMs < g_entity_list_next_full_probe_ms) {
        g_entity_list_force_refresh = false;
        g_entity_list_next_validation_ms = nowMs + ENTITY_LIST_INTERVAL_MS;
        return true;
    }



    if (!validationOk) {
        root = 0;
        if (g_scatter_full) {
            mem.SetDmaCallTag("CS2.EntityRoot");
            mem.AddScatterReadRequest(g_scatter_full,
                runtime.client_base + offsets.dwEntityList, &root, sizeof(root));
            mem.ExecuteReadScatter(g_scatter_full);
        } else {
            QReadT(runtime.client_base + offsets.dwEntityList, root, "CS2.EntityRoot");
        }
        if (!IsUserPointer(root)) {
            g_cached_entity_root = 0;
            g_entity_list_next_validation_ms = 0;
            g_entity_list_next_full_probe_ms = 0;
            g_entity_list_force_refresh = true;
            return haveEntry;
        }
        g_cached_entity_root = root;
    }

    g_phase.entity_full_probe = 1;
    const uintptr_t kPageOffs[] = { offsets.entity_page_table_offset };

    uintptr_t best_entry = 0;
    uintptr_t best_stride = g_controller_stride ? g_controller_stride : offsets.entity_identity_stride;


    uintptr_t pageEntries[sizeof(kPageOffs) / sizeof(kPageOffs[0])]{};
    EnsureScatter();
    if (g_scatter_full) {
        mem.SetDmaCallTag("CS2.EntityPages");
        for (size_t pi = 0; pi < sizeof(kPageOffs) / sizeof(kPageOffs[0]); ++pi)
            mem.AddScatterReadRequest(g_scatter_full, root + kPageOffs[pi],
                                      &pageEntries[pi], sizeof(uintptr_t));
        const auto tPages = std::chrono::steady_clock::now();
        mem.ExecuteReadScatter(g_scatter_full);
        NotePossibleDeviceStall(OmniGhost::Gameplay::TimeMs(tPages));
    } else {
        for (size_t pi = 0; pi < sizeof(kPageOffs) / sizeof(kPageOffs[0]); ++pi)
            QReadT(root + kPageOffs[pi], pageEntries[pi], "CS2.EntityPages");
    }
    for (size_t pi = 0; pi < sizeof(kPageOffs) / sizeof(kPageOffs[0]); ++pi) {
        const uintptr_t entry = pageEntries[pi];
        if (!IsUserPointer(entry))
            continue;
        if (ProbeListEntry(entry, offsets.entity_identity_stride)) {
            best_entry = entry;
            best_stride = offsets.entity_identity_stride;
            break;
        }
        if (!best_entry)
            best_entry = entry;
    }

    if (!best_entry) {


        return runtime.entity_list_entry != 0;
    }

    if (best_entry == runtime.entity_list_entry) {
        g_controller_stride = best_stride;
        g_pawn_stride = best_stride;


        g_entity_list_next_validation_ms = nowMs + ENTITY_LIST_INTERVAL_MS;
        g_entity_list_next_full_probe_ms = nowMs + ENTITY_LIST_FULL_PROBE_INTERVAL_MS;
        g_entity_list_force_refresh = false;
        return true;
    }



    const int need = (runtime.entity_list_entry == 0) ? 1 : 2;
    if (best_entry != g_pending_entity_list) {
        g_pending_entity_list = best_entry;
        g_entity_list_confirmations = 1;
        if (need == 1) {
            runtime.entity_list_addr = runtime.client_base + offsets.dwEntityList;
            runtime.entity_list_entry = best_entry;
            g_controller_stride = best_stride;
            g_pawn_stride = best_stride;
            g_pending_entity_list = 0;
            g_entity_list_confirmations = 0;
            g_entity_list_next_validation_ms = nowMs + ENTITY_LIST_INTERVAL_MS;
            g_entity_list_next_full_probe_ms = nowMs + ENTITY_LIST_FULL_PROBE_INTERVAL_MS;
            g_entity_list_force_refresh = false;
            return true;
        }
        return runtime.entity_list_entry != 0;
    }
    if (++g_entity_list_confirmations < need)
        return runtime.entity_list_entry != 0;

    runtime.entity_list_addr = runtime.client_base + offsets.dwEntityList;
    runtime.entity_list_entry = best_entry;
    g_controller_stride = best_stride;
    g_pawn_stride = best_stride;
    g_pending_entity_list = 0;
    g_entity_list_confirmations = 0;
    g_entity_list_next_validation_ms = nowMs + ENTITY_LIST_INTERVAL_MS;
    g_entity_list_next_full_probe_ms = nowMs + ENTITY_LIST_FULL_PROBE_INTERVAL_MS;
    g_entity_list_force_refresh = false;
    return true;
}








static uintptr_t ResolveEntityByHandle(uint32_t handle, uintptr_t stride) {
    if (!handle || handle == 0xFFFFFFFF || !runtime.client_base || !offsets.dwEntityList)
        return 0;

    uintptr_t root = g_cached_entity_root;
    if (!IsUserPointer(root)) {
        if (!QReadT(runtime.client_base + offsets.dwEntityList, root) || !IsUserPointer(root))
            return 0;
        g_cached_entity_root = root;
    }

    const EntityHandleParts parts = DecodeEntityHandle(handle);

    uintptr_t chunk = 0;
    if (!QReadT(root + offsets.entity_page_table_offset + sizeof(uintptr_t) * parts.page, chunk) ||
        !IsUserPointer(chunk))
        return 0;

    uintptr_t entity = 0;
    if (!QReadT(chunk + stride * parts.index, entity) || !IsUserPointer(entity))
        return 0;
    return entity;
}
