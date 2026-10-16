
static void CollectDroppedWeapons(const Config& frame_config, bool in_match,
                                  uintptr_t client_base, std::vector<DroppedWeapon>& out) {
    struct CachedDroppedWeapon { DroppedWeapon weapon{}; uint64_t last_seen_ms = 0; };
    static std::unordered_map<uintptr_t, CachedDroppedWeapon> weapon_cache;
    if (!frame_config.esp_enabled || !frame_config.dropped_weapons || !in_match) {
        weapon_cache.clear();
        out.clear();
        return;
    }
    static uint64_t next_scan_ms = 0;
    static size_t next_page = 0;
    struct CachedClassName { std::array<char, 48> name{}; uint64_t last_seen_ms = 0; };
    static std::unordered_map<uintptr_t, CachedClassName> class_cache;
    static uint64_t classCacheGeneration = 0;
    static uint64_t nextClassCachePruneMs = 0;
    const uint64_t sessionGeneration = g_session_generation.load(std::memory_order_acquire);
    if (classCacheGeneration != sessionGeneration) {
        class_cache.clear();
        weapon_cache.clear();
        next_scan_ms = 0;
        next_page = 0;
        nextClassCachePruneMs = 0;
        classCacheGeneration = sessionGeneration;
    }
    if (class_cache.bucket_count() < 2048) class_cache.reserve(2048);
    const uint64_t now = GetTickCount64();
    const auto publishCachedWeapons = [&] {
        out.clear();
        for (auto it = weapon_cache.begin(); it != weapon_cache.end();) {
            if (now < it->second.last_seen_ms || now - it->second.last_seen_ms > 3000u) {
                it = weapon_cache.erase(it);
                continue;
            }
            if (out.size() < 24u)
                out.push_back(it->second.weapon);
            ++it;
        }
        CacheManager::Metrics().dropped_weapons.store(
            weapon_cache.size(), std::memory_order_relaxed);
    };
    if (now >= nextClassCachePruneMs) {
        nextClassCachePruneMs = now + 5000u;
        int pruned = 0;
        for (auto it = class_cache.begin(); it != class_cache.end() && pruned < 64;) {
            if (now >= it->second.last_seen_ms && now - it->second.last_seen_ms > 30000u) {
                it = class_cache.erase(it);
                ++pruned;
            } else {
                ++it;
            }
        }
    }
    if (now < next_scan_ms) { publishCachedWeapons(); return; }
    if (g_pressure_level.load(std::memory_order_relaxed) >= 1) {
        next_scan_ms = now + 900;
        publishCachedWeapons();
        return;
    }
    next_scan_ms = now + 250;

    constexpr size_t kPages = 4;
    const size_t kSlotsPerPage = static_cast<size_t>(offsets.entity_page_size);
    const size_t kSlots = kPages * kSlotsPerPage;



    const size_t first_page = (next_page += kPages) % 8;
    std::array<uintptr_t, kPages> pages{};
    std::vector<uintptr_t> entities(kSlots), identities(kSlots), name_ptrs(kSlots), scenes(kSlots);
    std::vector<std::array<char, 48>> names(kSlots);
    std::vector<std::array<float, 3>> positions(kSlots);
    std::vector<int> ammo(kSlots);
    uintptr_t root = g_cached_entity_root;
    if (!IsUserPointer(root) && (!QReadT(client_base + offsets.dwEntityList, root) || !IsUserPointer(root))) {
        publishCachedWeapons();
        return;
    }
    g_cached_entity_root = root;
    g_scatter_optional.Ensure();
    if (!g_scatter_optional) {
        publishCachedWeapons();
        return;
    }

    mem.SetDmaCallTag("CS2.DroppedWeapons.Pages");
    for (size_t page = 0; page < kPages; ++page)
        mem.AddScatterReadRequest(g_scatter_optional, root + offsets.entity_page_table_offset + sizeof(uintptr_t) * (first_page + page), &pages[page], sizeof(uintptr_t));
    CS2::GiveCameraPriorityBeforeOptionalDma();
    mem.ExecuteReadScatter(g_scatter_optional);
    mem.SetDmaCallTag("CS2.DroppedWeapons.Entities");
    bool queuedEntities = false;
    for (size_t page = 0; page < kPages; ++page) if (IsUserPointer(pages[page]))
        for (size_t slot = 0; slot < kSlotsPerPage; ++slot)
            { mem.AddScatterReadRequest(g_scatter_optional, pages[page] + offsets.entity_identity_stride * slot,
                &entities[page * kSlotsPerPage + slot], sizeof(uintptr_t)); queuedEntities = true; }
    if (!queuedEntities) { publishCachedWeapons(); return; }
    CS2::GiveCameraPriorityBeforeOptionalDma();
    mem.ExecuteReadScatter(g_scatter_optional);
    mem.SetDmaCallTag("CS2.DroppedWeapons.Identity");
    bool queuedIdentity = false;
    for (size_t i = 0; i < kSlots; ++i) if (IsUserPointer(entities[i]))
        { mem.AddScatterReadRequest(g_scatter_optional, entities[i] + offsets.entity_identity_pointer, &identities[i], sizeof(uintptr_t)); queuedIdentity = true; }
    if (!queuedIdentity) { publishCachedWeapons(); return; }
    CS2::GiveCameraPriorityBeforeOptionalDma();
    mem.ExecuteReadScatter(g_scatter_optional);
    mem.SetDmaCallTag("CS2.DroppedWeapons.Names");
    bool queuedNames = false;
    for (size_t i = 0; i < kSlots; ++i) {
        const auto cached = class_cache.find(identities[i]);
        if (cached != class_cache.end()) {
            names[i] = cached->second.name;
            cached->second.last_seen_ms = now;
        }
        else if (IsUserPointer(identities[i]))
            { mem.AddScatterReadRequest(g_scatter_optional, identities[i] + offsets.entity_identity_name, &name_ptrs[i], sizeof(uintptr_t)); queuedNames = true; }
    }
    if (queuedNames) {
        CS2::GiveCameraPriorityBeforeOptionalDma();
        mem.ExecuteReadScatter(g_scatter_optional);
    }
    mem.SetDmaCallTag("CS2.DroppedWeapons.NameText");
    bool queuedNameText = false;
    for (size_t i = 0; i < kSlots; ++i) if (IsUserPointer(name_ptrs[i]))
        { mem.AddScatterReadRequest(g_scatter_optional, name_ptrs[i], names[i].data(), names[i].size() - 1); queuedNameText = true; }
    if (queuedNameText) {
        CS2::GiveCameraPriorityBeforeOptionalDma();
        mem.ExecuteReadScatter(g_scatter_optional);
    }

    mem.SetDmaCallTag("CS2.DroppedWeapons.Data");
    bool queuedData = false;
    for (size_t i = 0; i < kSlots; ++i) {
        if (!IsDroppedWeaponClass(names[i].data()) || !DroppedWeaponAllowed(names[i].data(), frame_config)) continue;
        class_cache.insert_or_assign(identities[i], CachedClassName{names[i], now});
        mem.AddScatterReadRequest(g_scatter_optional, entities[i] + offsets.m_pGameSceneNode, &scenes[i], sizeof(uintptr_t));
        queuedData = true;
        if (frame_config.dropped_weapon_ammo && offsets.m_iClip1)
            mem.AddScatterReadRequest(g_scatter_optional, entities[i] + offsets.m_iClip1, &ammo[i], sizeof(int));
    }
    if (!queuedData) { publishCachedWeapons(); return; }
    CS2::GiveCameraPriorityBeforeOptionalDma();
    mem.ExecuteReadScatter(g_scatter_optional);
    mem.SetDmaCallTag("CS2.DroppedWeapons.Positions");
    bool queuedPositions = false;
    for (size_t i = 0; i < kSlots; ++i) if (IsUserPointer(scenes[i]))
        { mem.AddScatterReadRequest(g_scatter_optional, scenes[i] + offsets.m_vecAbsOrigin, positions[i].data(), sizeof(float) * 3); queuedPositions = true; }
    if (!queuedPositions) { publishCachedWeapons(); return; }
    CS2::GiveCameraPriorityBeforeOptionalDma();
    const uint64_t positionReadBeginMs = GetTickCount64();
    mem.ExecuteReadScatter(g_scatter_optional);
    const uint64_t positionReadEndMs = GetTickCount64();
    const uint64_t positionSampleMs = positionReadBeginMs +
        (positionReadEndMs - positionReadBeginMs) / 2u;

    for (size_t i = 0; i < kSlots; ++i) {
        if (!IsDroppedWeaponClass(names[i].data()) || !IsFinitePosition(positions[i].data())) continue;
        DroppedWeapon weapon{};
        weapon.entity = entities[i]; weapon.ammo_clip = ammo[i]; weapon.sample_timestamp_ms = positionSampleMs;
        std::memcpy(weapon.pos, positions[i].data(), sizeof(weapon.pos));
        FormatDroppedWeaponName(names[i].data(), weapon.name, sizeof(weapon.name));
        if (IsUserPointer(weapon.entity))
            weapon_cache.insert_or_assign(weapon.entity, CachedDroppedWeapon{weapon, now});
    }
    publishCachedWeapons();
    if (class_cache.size() > 2048) {
        int removed = 0;
        for (auto it = class_cache.begin(); it != class_cache.end() && removed < 32;) {
            it = class_cache.erase(it);
            ++removed;
        }
    }
}
