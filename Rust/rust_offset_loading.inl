// Rust offset source parsing/loading; included inside namespace Rust.

namespace {
    bool JsonU64(const std::string& src, const char* key, uintptr_t& out) {
        const std::string pat = std::string("\"") + key + "\"";
        auto pos = src.find(pat);
        if (pos == std::string::npos) return false;
        pos = src.find(':', pos);
        if (pos == std::string::npos) return false;
        ++pos;
        while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\r' || src[pos] == '\n'))
            ++pos;
        if (pos < src.size() && src[pos] == '"') ++pos;
        char* end = nullptr;
        unsigned long long v = strtoull(src.c_str() + pos, &end, 0);
        if (end == src.c_str() + pos) return false;
        out = static_cast<uintptr_t>(v);
        return true;
    }

    std::vector<std::filesystem::path> OffsetSearchPaths(const char* explicitPath) {
        std::vector<std::filesystem::path> paths;
        if (explicitPath && *explicitPath) {
            paths.push_back(std::filesystem::path(explicitPath));
            return paths;
        }
        // Search in data directory relative to executable
        std::filesystem::path exeDir;
        char buf[MAX_PATH]{};
        if (GetModuleFileNameA(nullptr, buf, MAX_PATH)) {
            exeDir = std::filesystem::path(buf).parent_path();
            paths.push_back(exeDir / "data" / "rust_offsets.json");
            paths.push_back(exeDir / "rust_offsets.json");
        }
        // Search in current working directory
        paths.push_back(std::filesystem::path("data") / "rust_offsets.json");
        paths.push_back(std::filesystem::path("rust_offsets.json"));
        return paths;
    }
}
void ApplyEmbeddedDefaults() {
    std::snprintf(offsets.source, sizeof(offsets.source), "fallback");
    // Aug 2026 dump (cheatoffsets / user paste). LocalPlayer TypeInfo absent this build.
    if (!offsets.MainCamera_TypeInfo) offsets.MainCamera_TypeInfo = 0x115BB318ull;
    if (!offsets.BaseNetworkable_TypeInfo) offsets.BaseNetworkable_TypeInfo = 0x115B1A70ull;
    if (!offsets.Il2CppHandle_RVA) offsets.Il2CppHandle_RVA = 0x11A88F30ull;
    if (!offsets.playerModel) offsets.playerModel = 0x2E8;
    if (!offsets.movement) offsets.movement = 0x510;
    if (!offsets.currentTeam) offsets.currentTeam = 0x550;
    if (!offsets.heldEntity) offsets.heldEntity = 0x5D0;
    if (!offsets.lifestate) offsets.lifestate = 0x2A8;
    if (!offsets._health) offsets._health = 0x2B4;
    if (!offsets._maxHealth) offsets._maxHealth = 0x2B8;
    if (!offsets.playerFlags) offsets.playerFlags = 0x6D0;
    if (!offsets.inventory) offsets.inventory = 0x308;
    if (!offsets.displayName) offsets.displayName = 0x390;
    if (!offsets.playerEyes) offsets.playerEyes = 0x708;
    if (!offsets.clActiveItem) offsets.clActiveItem = 0x580;
    if (!offsets.visiblePlayerList) offsets.visiblePlayerList = 0x10;
    if (!offsets.staticFieldsAlt) offsets.staticFieldsAlt = 0x90;
    if (!offsets.modelPosition) offsets.modelPosition = 0x148;
    if (!offsets.staticFields) offsets.staticFields = 0xB8;
    if (!offsets.mainCamera) offsets.mainCamera = 0x0;
    if (!offsets.cameraGameObject) offsets.cameraGameObject = 0x30;
    if (!offsets.viewMatrix) offsets.viewMatrix = 0x2FC;
    if (!offsets.bufferList) offsets.bufferList = 0x10;
    if (!offsets.buffer) offsets.buffer = 0x10;
    if (!offsets.bufferSize) offsets.bufferSize = 0x18;
    if (!offsets.destroyed) offsets.destroyed = 0x40;
    if (!offsets.newVelocity) offsets.newVelocity = 0x164;
    if (!offsets.groundAngle) offsets.groundAngle = 0xC4;
    if (!offsets.groundAngleNew) offsets.groundAngleNew = 0xC8;
    if (!offsets.maxAngleWalking) offsets.maxAngleWalking = 0xD0;
    if (!offsets.recoil) offsets.recoil = 0x400;
    // Loaded when camera + networkable exist (BasePlayer TypeInfo optional this build)
    offsets.loaded = offsets.MainCamera_TypeInfo && offsets.BaseNetworkable_TypeInfo;
    std::cout << "[Rust] Embedded defaults BN=0x" << std::hex
              << offsets.BaseNetworkable_TypeInfo << " MC=0x" << offsets.MainCamera_TypeInfo
              << " IL2=0x" << offsets.Il2CppHandle_RVA
              << std::dec << " loaded=" << offsets.loaded << std::endl;
}

bool LoadOffsetsFromJson(const char* path) {
#if !defined(OMNIGHOST_DEV_EXTERNAL_OFFSETS)
    (void)path;
    // Prefer RCDATA built from data/rust_offsets.json; seed only if resource missing.
    {
        using OmniGhost::OffsetSource::GameId;
        using OmniGhost::OffsetSource::LoadSnapshot;
        using OmniGhost::OffsetSource::TryGetAny;
        const auto loaded = LoadSnapshot(GameId::Rust, nullptr);
        if (loaded.ok) {
            auto set = [&](std::initializer_list<const char*> keys, uintptr_t& dst) {
                std::uint64_t v = 0;
                if (TryGetAny(loaded.snapshot, keys, v) && v) dst = static_cast<uintptr_t>(v);
            };
            set({"BaseNetworkable_TypeInfo", "globals.BaseNetworkable_TypeInfo"}, offsets.BaseNetworkable_TypeInfo);
            set({"MainCamera_TypeInfo", "globals.MainCamera_TypeInfo"}, offsets.MainCamera_TypeInfo);
            set({"BasePlayer_TypeInfo", "globals.BasePlayer_TypeInfo"}, offsets.BasePlayer_TypeInfo);
            set({"LocalPlayer_TypeInfo", "globals.LocalPlayer_TypeInfo"}, offsets.LocalPlayer_TypeInfo);
            set({"il2cpphandle", "globals.il2cpphandle", "Il2CppHandle_RVA"}, offsets.Il2CppHandle_RVA);
            set({"playerFlags", "structs.base_player.playerFlags"}, offsets.playerFlags);
            set({"movement", "structs.base_player.movement"}, offsets.movement);
            set({"inventory", "structs.base_player.inventory"}, offsets.inventory);
            set({"playerModel", "structs.base_player.playerModel"}, offsets.playerModel);
            set({"displayName", "structs.base_player.displayName"}, offsets.displayName);
            set({"currentTeam", "structs.base_player.currentTeam"}, offsets.currentTeam);
            set({"lifestate", "structs.base_combat_entity.lifestate"}, offsets.lifestate);
            set({"_health", "structs.base_combat_entity._health"}, offsets._health);
            set({"_maxHealth", "structs.base_combat_entity._maxHealth"}, offsets._maxHealth);
            set({"viewMatrix", "camera.view_matrix", "structs.camera.view_matrix"}, offsets.viewMatrix);
            set({"staticFields", "base_networkable.static_fields"}, offsets.staticFields);
            offsets.loaded = offsets.MainCamera_TypeInfo && offsets.BaseNetworkable_TypeInfo;
            if (offsets.loaded) {
                std::snprintf(offsets.source, sizeof(offsets.source), "embedded-json");
                return true;
            }
        }
    }
    ApplyEmbeddedDefaults();
    if (offsets.loaded) std::snprintf(offsets.source, sizeof(offsets.source), "embedded-seed");
    return offsets.loaded;
#else
    const bool explicit_path = path && *path;
    const Offsets previous = offsets;
    if (explicit_path)
        offsets = Offsets{};
    for (const auto& p : OffsetSearchPaths(path)) {
        std::ifstream f(p);
        if (!f) continue;
        std::ostringstream ss; ss << f.rdbuf();
        const std::string j = ss.str();
        if (j.empty()) continue;
        auto grab = [&](const char* k, uintptr_t& dst) {
            uintptr_t v = 0; if (JsonU64(j, k, v) && v) dst = v;
        };
        grab("BasePlayer_TypeInfo", offsets.BasePlayer_TypeInfo);
        grab("LocalPlayer_TypeInfo", offsets.LocalPlayer_TypeInfo);
        grab("MainCamera_TypeInfo", offsets.MainCamera_TypeInfo);
        grab("BaseNetworkable_TypeInfo", offsets.BaseNetworkable_TypeInfo);
        grab("TOD_Sky_TypeInfo", offsets.TOD_Sky_TypeInfo);
        grab("il2cpphandle", offsets.Il2CppHandle_RVA);
        grab("playerFlags", offsets.playerFlags);
        grab("movement", offsets.movement);
        grab("inventory", offsets.inventory);
        grab("playerModel", offsets.playerModel);
        grab("displayName", offsets.displayName);
        grab("currentTeam", offsets.currentTeam);
        grab("userId", offsets.userId);
        grab("heldEntity", offsets.heldEntity);
        grab("playerEyes", offsets.playerEyes);
        grab("clActiveItem", offsets.clActiveItem);
        grab("modelState", offsets.modelState);
        grab("visiblePlayerList", offsets.visiblePlayerList);
        grab("_health", offsets._health);
        grab("_maxHealth", offsets._maxHealth);
        grab("lifestate", offsets.lifestate);
        grab("modelPosition", offsets.modelPosition);
        grab("isNpc", offsets.isNpc);
        grab("newVelocity", offsets.newVelocity);
        grab("groundAngle", offsets.groundAngle);
        grab("groundAngleNew", offsets.groundAngleNew);
        grab("maxAngleWalking", offsets.maxAngleWalking);
        grab("staticFields", offsets.staticFields);
        grab("mainCamera", offsets.mainCamera);
        grab("cameraGameObject", offsets.cameraGameObject);
        grab("viewMatrix", offsets.viewMatrix);
        grab("bufferList", offsets.bufferList);
        grab("buffer", offsets.buffer);
        grab("bufferSize", offsets.bufferSize);
        grab("destroyed", offsets.destroyed);
        grab("recoil", offsets.recoil);
        // Nested struct aliases from current dump
        grab("base_networkable", offsets.BaseNetworkable_TypeInfo);
        grab("main_camera_c", offsets.MainCamera_TypeInfo);
        Decrypt::LoadDecryptFromJson(j);
        Decrypt::ApplyBuiltinDefaults();
        // BN + camera are enough to mark loaded (BasePlayer TypeInfo absent this build)
        offsets.loaded = offsets.MainCamera_TypeInfo && offsets.BaseNetworkable_TypeInfo;
        std::cout << "[Rust] Offsets from " << p.string() << " BP=0x" << std::hex
                  << offsets.BasePlayer_TypeInfo << std::dec << std::endl;
        if (offsets.loaded)
        std::snprintf(offsets.source, sizeof(offsets.source), "json");
    return offsets.loaded;
    }
    if (explicit_path) {
        offsets = previous;
        return false;
    }
    ApplyEmbeddedDefaults();
    if (offsets.loaded)
        std::snprintf(offsets.source, sizeof(offsets.source), "json");
    return offsets.loaded;
#endif
}

