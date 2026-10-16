
void UpdateBombState() {
    if (!offsets.dwPlantedC4) {
        runtime.bomb = BombState{};
        return;
    }





    mem.SetDmaCallTag("CS2.Bomb");
    uintptr_t candidate = 0;
    if (!QReadT(runtime.client_base + offsets.dwPlantedC4, candidate, "CS2.Bomb.Ptr"))
        return;
    if (!IsUserPointer(candidate)) {
        runtime.bomb = BombState{};
        return;
    }

    uintptr_t entity = 0;
    uint8_t ticking = 0;
    uintptr_t list_entity = 0;
    uint8_t list_ticking = 0;

    EnsureScatter();
    if (g_scatter_full && offsets.m_bBombTicking) {
        mem.AddScatterReadRequest(g_scatter_full, candidate + offsets.m_bBombTicking,
                                  &ticking, sizeof(ticking));
        mem.AddScatterReadRequest(g_scatter_full, candidate, &list_entity, sizeof(list_entity));
        CS2::GiveCameraPriorityBeforeOptionalDma();
        mem.ExecuteReadScatter(g_scatter_full);
        if (ticking) {
            entity = candidate;
        } else if (IsUserPointer(list_entity)) {
            mem.AddScatterReadRequest(g_scatter_full, list_entity + offsets.m_bBombTicking,
                                      &list_ticking, sizeof(list_ticking));
            CS2::GiveCameraPriorityBeforeOptionalDma();
            mem.ExecuteReadScatter(g_scatter_full);
            ticking = list_ticking;
            entity = list_entity;
        }
    } else {
        if (offsets.m_bBombTicking)
            QReadT(candidate + offsets.m_bBombTicking, ticking, "CS2.Bomb.Tick");
        if (ticking) {
            entity = candidate;
        } else {
            if (!QReadT(candidate, list_entity, "CS2.Bomb.List") || !IsUserPointer(list_entity))
                return;
            if (offsets.m_bBombTicking)
                QReadT(list_entity + offsets.m_bBombTicking, ticking, "CS2.Bomb.ListTick");
            entity = list_entity;
        }
    }
    if (!ticking || !IsUserPointer(entity)) {


        if (entity && ticking == 0) runtime.bomb = BombState{};
        return;
    }

    BombState next = runtime.bomb;

    float absolute_blow_time = 0.f;
    float absolute_defuse_time = 0.f;
    float timer_length = 0.f;
    uint8_t defused = 0;
    uint8_t defusing = 0;
    uintptr_t globals = 0;
    uintptr_t scene = 0;
    float cur_slots[1]{};
    const uintptr_t kCurOff[1] = { offsets.global_vars_current_time };

    EnsureScatter();
    if (g_scatter_full) {
        if (offsets.m_flC4Blow)
            mem.AddScatterReadRequest(g_scatter_full, entity + offsets.m_flC4Blow,
                                      &absolute_blow_time, sizeof(absolute_blow_time));
        if (offsets.m_flTimerLength)
            mem.AddScatterReadRequest(g_scatter_full, entity + offsets.m_flTimerLength,
                                      &timer_length, sizeof(timer_length));
        if (offsets.m_bBombDefused)
            mem.AddScatterReadRequest(g_scatter_full, entity + offsets.m_bBombDefused,
                                      &defused, sizeof(defused));
        if (offsets.m_bBeingDefused)
            mem.AddScatterReadRequest(g_scatter_full, entity + offsets.m_bBeingDefused,
                                      &defusing, sizeof(defusing));
        if (offsets.m_flDefuseCountDown)
            mem.AddScatterReadRequest(g_scatter_full, entity + offsets.m_flDefuseCountDown,
                                      &absolute_defuse_time, sizeof(absolute_defuse_time));
        if (offsets.m_hBombDefuser)
            mem.AddScatterReadRequest(g_scatter_full, entity + offsets.m_hBombDefuser,
                                      &next.defuser_handle, sizeof(next.defuser_handle));
        if (offsets.m_pGameSceneNode)
            mem.AddScatterReadRequest(g_scatter_full, entity + offsets.m_pGameSceneNode,
                                      &scene, sizeof(scene));
        if (offsets.dwGlobalVars)
            mem.AddScatterReadRequest(g_scatter_full, runtime.client_base + offsets.dwGlobalVars,
                                      &globals, sizeof(globals));
        CS2::GiveCameraPriorityBeforeOptionalDma();
        mem.ExecuteReadScatter(g_scatter_full);

        if (IsUserPointer(globals)) {
            for (int i = 0; i < 1; ++i)
                mem.AddScatterReadRequest(g_scatter_full, globals + kCurOff[i],
                                          &cur_slots[i], sizeof(float));
            CS2::GiveCameraPriorityBeforeOptionalDma();
            mem.ExecuteReadScatter(g_scatter_full);
        }
        if (IsUserPointer(scene) && offsets.m_vecAbsOrigin) {
            mem.AddScatterReadRequest(g_scatter_full, scene + offsets.m_vecAbsOrigin,
                                      next.pos, sizeof(next.pos));
            CS2::GiveCameraPriorityBeforeOptionalDma();
            mem.ExecuteReadScatter(g_scatter_full);
        }
    } else {
        if (offsets.m_flC4Blow)
            QReadT(entity + offsets.m_flC4Blow, absolute_blow_time, "CS2.Bomb.Blow");
        if (offsets.m_flTimerLength)
            QReadT(entity + offsets.m_flTimerLength, timer_length, "CS2.Bomb.Timer");
        if (offsets.m_bBombDefused)
            QReadT(entity + offsets.m_bBombDefused, defused, "CS2.Bomb.Defused");
        if (offsets.m_bBeingDefused)
            QReadT(entity + offsets.m_bBeingDefused, defusing, "CS2.Bomb.Defusing");
        if (offsets.m_flDefuseCountDown)
            QReadT(entity + offsets.m_flDefuseCountDown, absolute_defuse_time, "CS2.Bomb.DefuseCD");
        if (offsets.m_hBombDefuser)
            QReadT(entity + offsets.m_hBombDefuser, next.defuser_handle, "CS2.Bomb.Defuser");
        if (offsets.dwGlobalVars)
            QReadT(runtime.client_base + offsets.dwGlobalVars, globals, "CS2.Bomb.Globals");
        if (IsUserPointer(globals)) {
            for (int i = 0; i < 1; ++i)
                QReadT(globals + kCurOff[i], cur_slots[i], "CS2.Bomb.CurTime");
        }
        if (offsets.m_pGameSceneNode &&
            QReadT(entity + offsets.m_pGameSceneNode, scene, "CS2.Bomb.Scene") &&
            IsUserPointer(scene))
            QRead(scene + offsets.m_vecAbsOrigin, next.pos, sizeof(next.pos), "CS2.Bomb.Pos");
    }

    float current_time = 0.f;
    bool have_time = false;
    for (int i = 0; i < 1; ++i) {
        const float t = cur_slots[i];
        if (std::isfinite(t) && t > 1.f && t < 1.0e7f) {
            current_time = t;
            have_time = true;
            break;
        }
    }

    float blow = 0.f;
    if (have_time && std::isfinite(absolute_blow_time) && absolute_blow_time > 1.f)
        blow = absolute_blow_time - current_time;



    if (!std::isfinite(blow) || blow < 0.05f || blow > 45.f) {
        if (std::isfinite(timer_length) && timer_length > 0.05f && timer_length <= 45.f)
            blow = timer_length;
        else
            return;
    }

    float defuse = 0.f;
    if (have_time && std::isfinite(absolute_defuse_time) && absolute_defuse_time > 1.f)
        defuse = absolute_defuse_time - current_time;
    if (!std::isfinite(defuse) || defuse < 0.f || defuse > 15.f)
        defuse = 0.f;

    next.planted = true;
    next.sample_timestamp_ms = GetTickCount64();
    next.blow_time = blow;
    next.defuse_time = defuse;
    next.defused = defused != 0;
    next.defusing = defusing != 0 && defuse > 0.05f;
    runtime.bomb = next;
}

void UpdateBombStateThrottled() {
    static uint64_t next_update_ms = 0;
    const uint64_t now = GetTickCount64();
    if (now < next_update_ms) return;



    next_update_ms = now + (runtime.bomb.planted ? 50u : 250u);
    UpdateBombState();
}
