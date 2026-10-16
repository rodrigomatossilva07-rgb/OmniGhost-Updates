
bool ResolveDataPath(std::string& out_dir) {
    const std::string exe = ExeDir();
    const std::vector<fs::path> search_bases = {
        fs::path(exe),
        fs::path(exe).parent_path(),
        fs::path(exe).parent_path().parent_path(),
        fs::path(exe).parent_path().parent_path().parent_path(),
        fs::current_path(),
    };
    const char* candidates[] = { "data" };
    for (const fs::path& base : search_bases) {
        if (base.empty()) continue;
        for (const char* rel : candidates) {
            fs::path p = base / rel;
            if (fs::exists(p / "cs2_offsets.json")) {
                out_dir = p.string();
                std::cout << "[CS2] Usando offsets de: " << out_dir << std::endl;
                return true;
            }
        }
    }
    out_dir = (fs::path(exe) / "data").string();
    std::cout << "[CS2] data/cs2_offsets.json nao encontrado: " << out_dir << std::endl;
    return false;
}

bool LoadOffsetsFromJson(const char* path) {
#if !defined(OMNIGHOST_DEV_EXTERNAL_OFFSETS)
    (void)path;
    return LoadEmbeddedOffsets();
#else
    const bool explicit_path = path && *path;
    const Offsets previous = offsets;
    std::string file;
    if (path && *path) {
        file = path;
    } else {
        std::string dir;
        ResolveDataPath(dir);
        std::cout << "[CS2] Resolved data path: " << dir << std::endl;
        file = (fs::path(dir) / "cs2_offsets.json").string();
    }

    std::cout << "[CS2] Loading offsets from: " << file << std::endl;
    std::string data;
    if (!ReadTextFile(file, data)) {
        status = "cs2_offsets.json nao encontrado (coloca em data/ ao lado do exe)";
        std::cout << "[CS2] " << status << "\n    procurou: " << file << std::endl;
        return false;
    }

    offsets.dwEntityList = 0;
    offsets.dwLocalPlayerPawn = 0;
    offsets.dwLocalPlayerController = 0;
    offsets.dwViewMatrix = 0;
    offsets.dwViewAngles = 0;
    offsets.dwGlobalVars = 0;
    offsets.dwBuildNumber = 0;
    offsets.dwPlantedC4 = 0;


    auto sub = data.find("\"client.dll\"");
    const std::string slice = (sub != std::string::npos) ? data.substr(sub) : data;

    JsonU64(slice, "dwEntityList", offsets.dwEntityList);
    JsonU64(slice, "dwLocalPlayerPawn", offsets.dwLocalPlayerPawn);
    JsonU64(slice, "dwLocalPlayerController", offsets.dwLocalPlayerController);
    JsonU64(slice, "dwViewMatrix", offsets.dwViewMatrix);
    JsonU64(slice, "dwViewAngles", offsets.dwViewAngles);
    JsonU64(slice, "dwGlobalVars", offsets.dwGlobalVars);
    JsonU64(slice, "dwPlantedC4", offsets.dwPlantedC4);
    JsonU64(data, "entity_identity_stride", offsets.entity_identity_stride);
    JsonU64(data, "entity_identity_pointer", offsets.entity_identity_pointer);
    JsonU64(data, "entity_identity_name", offsets.entity_identity_name);
    JsonU64(data, "entity_page_size", offsets.entity_page_size);
    JsonU64(data, "entity_page_table_offset", offsets.entity_page_table_offset);
    JsonU64(data, "global_vars_current_time", offsets.global_vars_current_time);
    JsonU64(data, "global_vars_current_map", offsets.global_vars_current_map);
    bool bone_mapping_loaded = true;
    {
        static constexpr const char* kBoneNames[kBoneSlotCount] = {
            "Head", "Neck", "SpineUpper", "SpineMiddle", "SpineLower", "Pelvis",
            "ClavicleLeft", "ShoulderLeft", "ElbowLeft", "HandLeft",
            "ClavicleRight", "ShoulderRight", "ElbowRight", "HandRight",
            "HipLeft", "KneeLeft", "AnkleLeft", "HipRight", "KneeRight", "AnkleRight"
        };
        g_boneMapping = BoneMapping{};
        for (std::size_t i = 0; i < kBoneSlotCount; ++i) {
            uintptr_t value = 0;
            const bool ok = JsonClassU64(data, "bone_mapping", kBoneNames[i], value) && value < 28;
            bone_mapping_loaded &= ok;
            if (ok)
                g_boneMapping.jointIndex[i] = static_cast<int>(value);
        }
    }

    auto eng = data.find("\"engine2.dll\"");
    if (eng != std::string::npos) {
        const std::string es = data.substr(eng);
        JsonU64(es, "dwBuildNumber", offsets.dwBuildNumber);
    }



    auto grab_cpp = [&](const char* key, uintptr_t& out) {
        if (out) return;
        const std::string k(key);
        size_t p = 0;
        while ((p = data.find(k, p)) != std::string::npos) {

            if (p > 0) {
                const char c = data[p - 1];
                if (std::isalnum((unsigned char)c) || c == '_' || c == '"') {
                    p += k.size();
                    continue;
                }
            }
            size_t eq = data.find('=', p + k.size());
            size_t semi = data.find(';', p + k.size());
            if (eq == std::string::npos || semi == std::string::npos || eq > semi || eq - p > 96) {
                p += k.size();
                continue;
            }
            size_t hx = data.find("0x", eq);
            if (hx == std::string::npos || hx > semi) {
                p += k.size();
                continue;
            }
            size_t e = hx + 2;
            while (e < data.size() && std::isxdigit((unsigned char)data[e])) ++e;
            try {
                out = (uintptr_t)std::stoull(data.substr(hx, e - hx), nullptr, 16);
            } catch (const std::exception& ex) {
                std::cerr << "[CS2] LoadOffsetsFromJson: Failed to parse hex value at offset " << hx << ": " << ex.what() << "\n";
                out = 0;
            } catch (...) {
                std::cerr << "[CS2] LoadOffsetsFromJson: Unknown exception parsing hex value at offset " << hx << "\n";
                out = 0;
            }
            return;
        }
    };
    grab_cpp("dwEntityList", offsets.dwEntityList);
    grab_cpp("dwLocalPlayerPawn", offsets.dwLocalPlayerPawn);
    grab_cpp("dwLocalPlayerController", offsets.dwLocalPlayerController);
    grab_cpp("dwViewMatrix", offsets.dwViewMatrix);
    grab_cpp("dwViewAngles", offsets.dwViewAngles);
    grab_cpp("dwGlobalVars", offsets.dwGlobalVars);
    grab_cpp("dwPlantedC4", offsets.dwPlantedC4);
    grab_cpp("dwBuildNumber", offsets.dwBuildNumber);


    {
        uintptr_t v = 0;
        if (!offsets.dwEntityList && JsonU64(data, "client.dwEntityList", v)) offsets.dwEntityList = v;
        v = 0; if (!offsets.dwLocalPlayerPawn && JsonU64(data, "client.dwLocalPlayerPawn", v)) offsets.dwLocalPlayerPawn = v;
        v = 0; if (!offsets.dwViewMatrix && JsonU64(data, "client.dwViewMatrix", v)) offsets.dwViewMatrix = v;
    }

    LoadSchemaOffsets(data);
    std::string validation_error;
    offsets.loaded = bone_mapping_loaded && offsets.Validate(&validation_error);
    if (!bone_mapping_loaded && validation_error.empty())
        validation_error = "bone_mapping incompleto";

    const bool loaded_from_file = offsets.loaded;
    if (loaded_from_file) {
        char buf[192];
        std::snprintf(buf, sizeof(buf),
            "Offsets OK  EL=0x%llX  LPP=0x%llX  VM=0x%llX",
            (unsigned long long)offsets.dwEntityList,
            (unsigned long long)offsets.dwLocalPlayerPawn,
            (unsigned long long)offsets.dwViewMatrix);
        status = buf;
        offsets_source = file;
        std::cout << "[CS2] Loaded: " << file << std::endl;
        std::cout << "[CS2] Schema  m_hPlayerPawn=0x" << std::hex << offsets.m_hPlayerPawn
                  << " m_iHealth=0x" << offsets.m_iHealth
                  << " m_iTeamNum=0x" << offsets.m_iTeamNum
                  << " m_vOldOrigin=0x" << offsets.m_vOldOrigin
                  << " m_ArmorValue=0x" << offsets.m_ArmorValue
                  << std::dec << std::endl;
    } else {
        status = validation_error.empty() ? "cs2_offsets.json incompleto" : validation_error;
        offsets_source = status;
        if (explicit_path)
            offsets = previous;
    }
    std::cout << "[CS2] " << status << std::endl;
    return loaded_from_file;
#endif
}

bool LoadEmbeddedOffsets() {
    OmniGhost::EmbeddedOffsets::Snapshot snapshot;
    OmniGhost::EmbeddedOffsets::Diagnostics diagnostics;
    if (!OmniGhost::EmbeddedOffsets::Load(
            OmniGhost::EmbeddedOffsets::Game::CS2, snapshot, diagnostics)) {
        offsets = {};
        offsets_source = "recurso embedded inválido";
        std::clog << "[RESOURCE] game=CS2 source=embedded id=IDR_OFFSETS_CS2 load=FAIL reason="
                  << diagnostics.error << '\n';
        return false;
    }
    offsets = Offsets{};
    auto read = [&](const char* path, uintptr_t& value, bool required = true) {
        std::uint64_t loaded = 0;
        const bool ok = snapshot.TryGet(path, loaded) && loaded != 0;
        if (ok) value = static_cast<uintptr_t>(loaded);
        return ok || !required;
    };
    bool required = true;
    required &= read("client.dll.dwEntityList", offsets.dwEntityList);
    required &= read("client.dll.dwLocalPlayerPawn", offsets.dwLocalPlayerPawn);
    read("client.dll.dwLocalPlayerController", offsets.dwLocalPlayerController, false);
    required &= read("client.dll.dwViewMatrix", offsets.dwViewMatrix);
    read("client.dll.dwViewAngles", offsets.dwViewAngles, false);
    read("client.dll.dwGlobalVars", offsets.dwGlobalVars, false);
    read("client.dll.dwPlantedC4", offsets.dwPlantedC4, false);
    read("engine2.dll.dwBuildNumber", offsets.dwBuildNumber, false);
    required &= read("layout.entity_identity_stride", offsets.entity_identity_stride);
    required &= read("layout.entity_identity_pointer", offsets.entity_identity_pointer);
    required &= read("layout.entity_identity_name", offsets.entity_identity_name);
    required &= read("layout.entity_page_size", offsets.entity_page_size);
    required &= read("layout.entity_page_table_offset", offsets.entity_page_table_offset);
    read("layout.global_vars_current_time", offsets.global_vars_current_time, false);
    read("layout.global_vars_current_map", offsets.global_vars_current_map, false);
    {
        static constexpr const char* kBoneNames[kBoneSlotCount] = {
            "Head", "Neck", "SpineUpper", "SpineMiddle", "SpineLower", "Pelvis",
            "ClavicleLeft", "ShoulderLeft", "ElbowLeft", "HandLeft",
            "ClavicleRight", "ShoulderRight", "ElbowRight", "HandRight",
            "HipLeft", "KneeLeft", "AnkleLeft", "HipRight", "KneeRight", "AnkleRight"
        };
        g_boneMapping = BoneMapping{};
        for (std::size_t i = 0; i < kBoneSlotCount; ++i) {
            std::uint64_t value = 0;
            const std::string path = std::string("bone_mapping.") + kBoneNames[i];
            const bool ok = snapshot.TryGet(path, value) && value < 28;
            required &= ok;
            if (ok)
                g_boneMapping.jointIndex[i] = static_cast<int>(value);
        }
    }




    read("schemas.C_BaseEntity.m_iHealth", offsets.m_iHealth, false);
    read("schemas.C_BaseEntity.m_iTeamNum", offsets.m_iTeamNum, false);
    read("schemas.C_BaseEntity.m_pGameSceneNode", offsets.m_pGameSceneNode, false);
    read("schemas.C_BaseEntity.m_fFlags", offsets.m_fFlags, false);
    read("schemas.C_BaseEntity.m_vecVelocity", offsets.m_vecVelocity, false);
    read("schemas.C_BasePlayerPawn.m_vOldOrigin", offsets.m_vOldOrigin, false);
    read("schemas.CCSPlayerController.m_hPlayerPawn", offsets.m_hPlayerPawn, false);
    read("schemas.CCSPlayerController.m_hObserverPawn", offsets.m_hObserverPawn, false);
    read("schemas.C_BasePlayerPawn.m_pObserverServices", offsets.m_pObserverServices, false);
    read("schemas.CPlayer_ObserverServices.m_hObserverTarget", offsets.m_hObserverTarget, false);
    read("schemas.CCSPlayerController.m_iPawnHealth", offsets.m_iPawnHealth, false);
    read("schemas.CCSPlayerController.m_iPawnArmor", offsets.m_iPawnArmor, false);
    read("schemas.CBasePlayerController.m_iszPlayerName", offsets.m_iszPlayerName, false);
    read("schemas.CBasePlayerController.m_steamID", offsets.m_steamID, false);
    read("schemas.CCSPlayerController.m_sSanitizedPlayerName", offsets.m_sSanitizedPlayerName, false);
    read("schemas.CCSPlayerController.m_bPawnIsAlive", offsets.m_bPawnIsAlive, false);
    read("schemas.CCSPlayerController.m_pInGameMoneyServices", offsets.m_pInGameMoneyServices, false);
    read("schemas.CCSPlayerController_InGameMoneyServices.m_iAccount", offsets.m_iAccount, false);
    read("schemas.C_CSPlayerPawn.m_ArmorValue", offsets.m_ArmorValue, false);
    read("schemas.C_CSPlayerPawn.m_angEyeAngles", offsets.m_angEyeAngles, false);
    read("schemas.C_CSPlayerPawn.m_iIDEntIndex", offsets.m_iIDEntIndex, false);
    read("schemas.C_CSPlayerPawn.m_iShotsFired", offsets.m_iShotsFired, false);
    read("schemas.C_CSPlayerPawn.m_bIsDefusing", offsets.m_bIsDefusing, false);
    read("schemas.C_CSPlayerPawn.m_bIsScoped", offsets.m_bIsScoped, false);
    read("schemas.C_CSPlayerPawnBase.m_flFlashDuration", offsets.m_flFlashDuration, false);
    read("schemas.CGameSceneNode.m_vecAbsOrigin", offsets.m_vecAbsOrigin, false);
    read("schemas.CGameSceneNode.m_bDormant", offsets.m_bDormant, false);
    read("schemas.CSkeletonInstance.m_modelState", offsets.m_modelState, false);
    uintptr_t boneArrayInModelState = 0;
    read("schemas.CModelState.m_boneArray", boneArrayInModelState, true);
    offsets.BoneArray = offsets.m_modelState + boneArrayInModelState;
    read("schemas.C_PlantedC4.m_bBombTicking", offsets.m_bBombTicking, false);
    read("schemas.C_PlantedC4.m_flC4Blow", offsets.m_flC4Blow, false);
    read("schemas.C_PlantedC4.m_flTimerLength", offsets.m_flTimerLength, false);
    read("schemas.C_PlantedC4.m_bBombDefused", offsets.m_bBombDefused, false);
    read("schemas.C_PlantedC4.m_bBeingDefused", offsets.m_bBeingDefused, false);
    read("schemas.C_PlantedC4.m_flDefuseCountDown", offsets.m_flDefuseCountDown, false);
    read("schemas.C_PlantedC4.m_hBombDefuser", offsets.m_hBombDefuser, false);
    read("schemas.C_BasePlayerPawn.m_pWeaponServices", offsets.m_pWeaponServices, false);
    read("schemas.C_BasePlayerPawn.m_pItemServices", offsets.m_pItemServices, false);
    read("schemas.CCSPlayer_ItemServices.m_bHasDefuser", offsets.m_bHasDefuser, false);
    read("schemas.CPlayer_WeaponServices.m_hActiveWeapon", offsets.m_hActiveWeapon, false);
    read("schemas.C_EconEntity.m_AttributeManager", offsets.m_AttributeManager, false);
    read("schemas.C_AttributeContainer.m_Item", offsets.m_Item, false);
    read("schemas.C_EconItemView.m_iItemDefinitionIndex", offsets.m_iItemDefinitionIndex, false);
    read("schemas.C_BasePlayerWeapon.m_iClip1", offsets.m_iClip1, false);
    read("schemas.C_BasePlayerWeapon.m_pReserveAmmo", offsets.m_pReserveAmmo, false);
    read("schemas.C_CSPlayerPawn.m_entitySpottedState", offsets.m_entitySpottedState, false);
    read("schemas.EntitySpottedState_t.m_bSpotted", offsets.m_bSpotted, false);
    std::string validation_error;
    offsets.loaded = required && offsets.Validate(&validation_error);
    if (offsets.loaded) {
        offsets_source = "offsets embedded";
        std::cout << "[RESOURCE] game=CS2 source=embedded id=IDR_OFFSETS_CS2 load=PASS build="
                  << snapshot.metadata.build << std::endl;
    } else if (!validation_error.empty()) {
        status = validation_error;
        offsets_source = "offsets embedded inválidos";
    }
    return offsets.loaded;
}

static std::string LowerString(const std::string& s) {
    std::string out;
    out.resize(s.size());
    std::transform(s.begin(), s.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

static bool EndsWith(const std::string& value, const std::string& ending) {
    if (ending.size() > value.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

static bool ResolveModuleBases() {
    runtime.client_base = mem.GetBaseDaddy("client.dll");
    runtime.engine_base = mem.GetBaseDaddy("engine2.dll");
    if (runtime.client_base && runtime.engine_base)
        return true;

    auto module_names = mem.GetModuleList("");
    for (const auto& module : module_names) {
        const std::string lower = LowerString(module);
        if (!runtime.client_base && EndsWith(lower, "client.dll")) {
            runtime.client_base = mem.GetBaseDaddy(module);
            std::cout << "[CS2] Found client module as " << module << "" << std::endl;
        }
        if (!runtime.engine_base && EndsWith(lower, "engine2.dll")) {
            runtime.engine_base = mem.GetBaseDaddy(module);
            std::cout << "[CS2] Found engine module as " << module << "" << std::endl;
        }
        if (runtime.client_base && runtime.engine_base)
            break;
    }

    if (!runtime.client_base) {
        std::cout << "[CS2] Falha ao encontrar client.dll via GetBaseDaddy; modulos atuais:" << std::endl;
        for (const auto& module : module_names)
            std::cout << "    " << module << std::endl;
    }
    return runtime.client_base != 0;
}

bool WaitForProcess(int timeout_sec) {
    std::cout << "[CS2] Aguardando cs2.exe..." << std::endl;
    status = "Aguardando cs2.exe";
    const int steps = (std::max)(1, timeout_sec);
    for (int i = 0; i < steps; ++i) {
        DWORD pid = 0;
        if (mem.vHandle) {
            pid = mem.GetPidFromName("cs2.exe");
            if (!pid) pid = mem.GetPidFromName("CS2.exe");
        }


        if (pid) {
            status = "cs2.exe encontrado";
            std::cout << "[CS2] " << status << " (pid " << pid << ")" << std::endl;
            return true;
        }
        if (i % 5 == 0)
            std::cout << "[CS2] ... ainda a aguardar (" << i << "s)" << std::endl;
        Sleep(1000);
    }
    status = "Timeout - cs2.exe nao encontrado";
    std::cout << "[CS2] " << status << std::endl;
    return false;
}

bool Attach() {
    status = "A anexar cs2.exe";
    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Adapter,
        "CS2 attach begin",
        {{"game", "CS2"}, {"vHandle", mem.vHandle ? "yes" : "no"}});


    if (!mem.vHandle) {
        status = "Inicializando DMA";
        std::cout << "[CS2] " << status << std::endl;
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Info,
            OmniGhost::SessionLog::Subsystem::DMA,
            "CS2 opening FPGA session", {});
        try {
            if (!mem.Init("", true, false)) {
                status = "DMA offline (FPGA nao abriu sessao funcional)";
                std::cout << "[CS2] " << status << std::endl;
                OmniGhost::SessionLog::Write(
                    OmniGhost::SessionLog::Severity::Error,
                    OmniGhost::SessionLog::Subsystem::DMA,
                    "CS2 DMA offline after mem.Init",
                    {{"status", status}});
                return false;
            }
            OmniGhost::SessionLog::Write(
                OmniGhost::SessionLog::Severity::Info,
                OmniGhost::SessionLog::Subsystem::DMA,
                "CS2 FPGA session open",
                {{"vHandle", mem.vHandle ? "yes" : "no"}});
        } catch (const std::exception& ex) {
            status = "Excecao ao inicializar DMA: " + std::string(ex.what());
            std::cerr << "[CS2] CRASH em mem.Init: " << ex.what() << std::endl;
            OmniGhost::SessionLog::Write(
                OmniGhost::SessionLog::Severity::Error,
                OmniGhost::SessionLog::Subsystem::DMA,
                "CS2 mem.Init exception",
                {{"what", ex.what()}});
            return false;
        } catch (...) {
            status = "Erro desconhecido ao inicializar DMA";
            std::cerr << "[CS2] CRASH desconhecido em mem.Init" << std::endl;
            OmniGhost::SessionLog::Write(
                OmniGhost::SessionLog::Severity::Error,
                OmniGhost::SessionLog::Subsystem::DMA,
                "CS2 mem.Init unknown exception", {});
            return false;
        }
    }


    if (mem.GetPidFromName("cs2.exe") || mem.GetPidFromName("CS2.exe")) {
        status = "cs2.exe encontrado";
        std::cout << "[CS2] " << status << " (DMA)" << std::endl;
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Info,
            OmniGhost::SessionLog::Subsystem::Process,
            "CS2 process present (DMA)", {});
    } else if (!WaitForProcess(10)) {
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Error,
            OmniGhost::SessionLog::Subsystem::Process,
            "CS2 process wait timeout",
            {{"status", status}});
        return false;
    }


    bool attached = false;
    for (int a = 0; a < 40; ++a) {
        try {
            if (mem.Init("cs2.exe", false, false) || mem.Init("CS2.exe", false, false)) {
                attached = true;
                break;
            }
        } catch (const std::exception& ex) {
            std::cerr << "[CS2] Attach retry " << a << " exception: " << ex.what() << std::endl;
            OmniGhost::SessionLog::Write(
                OmniGhost::SessionLog::Severity::Warning,
                OmniGhost::SessionLog::Subsystem::Process,
                "CS2 process bind retry exception",
                {{"attempt", std::to_string(a)}, {"what", ex.what()}});
        } catch (...) {
            std::cerr << "[CS2] Attach retry " << a << " unknown exception caught and logged" << std::endl;
        }
        status = "A anexar cs2.exe...";
        if ((a % 5) == 0) {
            std::cout << "[CS2] Attach retry " << a << std::endl;
            OmniGhost::SessionLog::Write(
                OmniGhost::SessionLog::Severity::Info,
                OmniGhost::SessionLog::Subsystem::Process,
                "CS2 process bind retry",
                {{"attempt", std::to_string(a)}});
        }
        Sleep(a < 10 ? 250 : 500);
    }
    if (!attached) {
        status = "Falha ao anexar cs2.exe (DMA aberto mas processo nao ligado)";
        std::cout << "[CS2] " << status << std::endl;
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Error,
            OmniGhost::SessionLog::Subsystem::Process,
            "CS2 process bind failed",
            {{"status", status}});
        return false;
    }
    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Process,
        "CS2 process bound", {});


    for (int attempt = 0; attempt < 120; ++attempt) {
        try {
            if (ResolveModuleBases())
                break;
        } catch (const std::exception& ex) {
            std::cerr << "[CS2] ResolveModuleBases attempt " << attempt << " exception: " << ex.what() << std::endl;
        } catch (...) {
            std::cerr << "[CS2] ResolveModuleBases attempt " << attempt << " unknown exception caught and logged" << std::endl;
        }
        if (attempt % 5 == 0)
            std::cout << "[CS2] client.dll ainda nao resolvido (" << attempt << ")" << std::endl;
        status = "A aguardar client.dll...";
        Sleep(500);

        if (attempt > 0 && (attempt % 10) == 0) {
            try {
                mem.Init("cs2.exe", false, false) || mem.Init("CS2.exe", false, false);
            } catch (const std::exception& ex) {
                std::cerr << "[CS2] Re-bind attempt " << attempt << " exception: " << ex.what() << std::endl;
            } catch (...) {
                std::cerr << "[CS2] Re-bind attempt " << attempt << " unknown exception caught and logged" << std::endl;
            }
        }
    }
    if (!runtime.client_base) {
        status = "client.dll nao encontrado (abre o CS2 e espera o menu principal)";
        std::cout << "[CS2] " << status << std::endl;
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Error,
            OmniGhost::SessionLog::Subsystem::Process,
            "CS2 client.dll not found",
            {{"status", status}});
        return false;
    }
    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Process,
        "CS2 modules resolved",
        {{"client", std::to_string(runtime.client_base)}});


    if (!offsets.loaded) {
        LoadOffsetsFromJson(nullptr);
    }


    if (!offsets.loaded) {
        LoadEmbeddedOffsets();
    }
    if (!offsets.loaded) {
        status = "Sem offsets validos";
        return false;
    }

    std::cout << "[CS2] Offsets source final: " << offsets_source
              << "  EL=0x" << std::hex << offsets.dwEntityList
              << "  LPC=0x" << offsets.dwLocalPlayerController
              << "  LPP=0x" << offsets.dwLocalPlayerPawn
              << std::dec << std::endl;


    if (!OmniGhost::Gameplay::WeaponNameRegistry::Instance().LoadFromJson("data/weapons.json")) {
        std::cout << "[CS2] Warning: Failed to load data/weapons.json, trying embedded resource..." << std::endl;
        if (!OmniGhost::Gameplay::WeaponNameRegistry::Instance().LoadFromEmbedded()) {
            std::cout << "[CS2] Warning: Weapon name registry not loaded, using fallback maps" << std::endl;
        }
    }

    if (runtime.engine_base && offsets.dwBuildNumber)
        QReadT(runtime.engine_base + offsets.dwBuildNumber, runtime.build_number);



    if (!ProbeViewMatrix()) {
        std::cout << "[CS2] ViewMatrix ilegível; offsets podem estar desatualizados" << std::endl;
        status = "Offsets incompatíveis com o build atual; atualiza data/cs2_offsets.json";
        OmniGhost::OffsetAuto::MarkOutdated(OmniGhost::ActiveGame::CS2, status);
        std::cout << "[CS2] " << status << std::endl;
        return false;
    }

    if (runtime.engine_base && offsets.dwBuildNumber)
        QReadT(runtime.engine_base + offsets.dwBuildNumber, runtime.build_number);

    ready = true;
    OmniGhost::OffsetAuto::MarkLiveValid(OmniGhost::ActiveGame::CS2,
        "View matrix e módulos críticos validados no build em execução");

    char buf[160];
    std::snprintf(buf, sizeof(buf), "CS2 OK  client=0x%llX  build=%u",
        (unsigned long long)runtime.client_base, runtime.build_number);
    status = buf;
    std::cout << "[CS2] " << status << std::endl;


    if (!aim_type::IsConnected()) {
        std::cout << "[CS2] A procurar Makcu em todas as portas COM..." << std::endl;
        makcu_wrapper::MakcuInitialize("");
        if (makcu_wrapper::IsConnected()) {
            aim_type::config.active = aim_type::DeviceType::Makcu;
            aim_type::config.makcu_connected = true;
            std::cout << "[CS2] Makcu ligado automaticamente." << std::endl;
        } else {
            std::cout << "[CS2] Makcu nao encontrado (aimbot usa fallback software)." << std::endl;
        }
    } else if (makcu_wrapper::IsConnected() &&
               aim_type::config.active == aim_type::DeviceType::Makcu) {
        aim_type::config.active = aim_type::DeviceType::Makcu;
        aim_type::config.makcu_connected = true;
    }
    return true;
}
