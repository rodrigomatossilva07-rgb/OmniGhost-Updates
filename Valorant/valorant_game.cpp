#include "valorant_game.h"
#include "../DMALibrary/Memory/Memory.h"
#include "../src/makcu/makcu_wrapper.h"
#include "../src/platform/app_paths.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <string>
#include "../src/platform/match_lifecycle.h"

namespace Valorant {

Offsets offsets{};
Runtime runtime{};
Config config{};

namespace {

bool IsUserPtr(uintptr_t p) {
    return p >= 0x10000ULL && p < 0x00007FFFFFFFFFFFULL;
}

template<typename T>
bool R(uintptr_t addr, T& out) {
    if (!addr) return false;
    return mem.Read(addr, &out, sizeof(T));
}

uintptr_t HexOrDec(const std::string& s) {
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        return (uintptr_t)std::stoull(s, nullptr, 16);
    return (uintptr_t)std::stoull(s, nullptr, 0);
}

bool JsonU64(const std::string& j, const char* key, uintptr_t& out) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = j.find(k);
    if (p == std::string::npos) return false;
    p = j.find(':', p);
    if (p == std::string::npos) return false;
    ++p;
    while (p < j.size() && (j[p] == ' ' || j[p] == '\t' || j[p] == '\"')) ++p;
    size_t e = p;
    while (e < j.size() && (isalnum((unsigned char)j[e]) || j[e] == 'x' || j[e] == 'X')) ++e;
    if (e == p) return false;
    try {
        out = HexOrDec(j.substr(p, e - p));
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[Valorant] JsonU64 parse failed for key '" << key << "': " << ex.what() << "\n";
        return false;
    } catch (...) {
        std::cerr << "[Valorant] JsonU64 unknown exception for key '" << key << "'\n";
        return false;
    }
}

// Simple camera basis → approximate view matrix for W2S (no engine ProjectWorldToScreen).
void BuildViewMatrix(const float pos[3], const float rot[3], float fov, float out[16]) {
    // rot: pitch, yaw, roll (degrees) — common UE camera order
    const float deg = 0.01745329251f;
    const float pitch = rot[0] * deg;
    const float yaw = rot[1] * deg;
    const float cp = cosf(pitch), sp = sinf(pitch);
    const float cy = cosf(yaw), sy = sinf(yaw);

    // Forward / right / up
    float f[3] = { cp * cy, cp * sy, sp };
    float r[3] = { -sy, cy, 0.f };
    // normalize right
    float rl = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    if (rl > 1e-6f) { r[0] /= rl; r[1] /= rl; r[2] /= rl; }
    float u[3] = {
        r[1] * f[2] - r[2] * f[1],
        r[2] * f[0] - r[0] * f[2],
        r[0] * f[1] - r[1] * f[0]
    };

    // Column-major-ish for our W2S (same convention as CS2 helpers)
    memset(out, 0, sizeof(float) * 16);
    out[0] = r[0]; out[1] = u[0]; out[2] = f[0];
    out[4] = r[1]; out[5] = u[1]; out[6] = f[1];
    out[8] = r[2]; out[9] = u[2]; out[10] = f[2];
    out[12] = -(r[0] * pos[0] + r[1] * pos[1] + r[2] * pos[2]);
    out[13] = -(u[0] * pos[0] + u[1] * pos[1] + u[2] * pos[2]);
    out[14] = -(f[0] * pos[0] + f[1] * pos[1] + f[2] * pos[2]);
    out[15] = 1.f;
    (void)fov;
}

} // namespace

bool LoadOffsetsJson(const char* path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string j((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto set = [&](const char* k, uintptr_t& f) { JsonU64(j, k, f); };
    set("uworld_state", offsets.uworld_state);
    set("persistent_level", offsets.persistent_level);
    set("owning_game_instance", offsets.owning_game_instance);
    set("local_players", offsets.local_players);
    set("player_controller", offsets.player_controller);
    set("acknowledged_pawn", offsets.acknowledged_pawn);
    set("player_camera", offsets.player_camera);
    set("root_component", offsets.root_component);
    set("relative_location", offsets.relative_location);
    set("current_mesh", offsets.current_mesh);
    set("damage_handler", offsets.damage_handler);
    set("current_health", offsets.current_health);
    set("team_component", offsets.team_component);
    set("team_id", offsets.team_id);
    set("actor_array", offsets.actor_array);
    set("camera_cache", offsets.camera_cache);
    offsets.loaded = true;
    return true;
}

bool SaveOffsetsJson(const char* path) {
    std::ofstream o(path);
    if (!o) return false;
    o << "{\n"
      << "  \"schema_version\": 1,\n"
      << "  \"process\": \"VALORANT-Win64-Shipping.exe\",\n"
      << "  \"uworld_state\": \"0x" << std::hex << offsets.uworld_state << "\",\n"
      << "  \"persistent_level\": \"0x" << offsets.persistent_level << "\",\n"
      << "  \"owning_game_instance\": \"0x" << offsets.owning_game_instance << "\",\n"
      << "  \"local_players\": \"0x" << offsets.local_players << "\",\n"
      << "  \"player_controller\": \"0x" << offsets.player_controller << "\",\n"
      << "  \"acknowledged_pawn\": \"0x" << offsets.acknowledged_pawn << "\",\n"
      << "  \"player_camera\": \"0x" << offsets.player_camera << "\",\n"
      << "  \"root_component\": \"0x" << offsets.root_component << "\",\n"
      << "  \"relative_location\": \"0x" << offsets.relative_location << "\",\n"
      << "  \"current_mesh\": \"0x" << offsets.current_mesh << "\",\n"
      << "  \"damage_handler\": \"0x" << offsets.damage_handler << "\",\n"
      << "  \"current_health\": \"0x" << offsets.current_health << "\",\n"
      << "  \"team_component\": \"0x" << offsets.team_component << "\",\n"
      << "  \"team_id\": \"0x" << offsets.team_id << "\",\n"
      << "  \"actor_array\": \"0x" << offsets.actor_array << "\",\n"
      << "  \"camera_cache\": \"0x" << offsets.camera_cache << "\"\n"
      << "}\n";
    return true;
}

bool Attach() {
    runtime = Runtime{};
    runtime.process_name = "VALORANT-Win64-Shipping.exe";
    runtime.status = "A procurar VALORANT...";

    // A passive launcher probe does not create a VMM session. Open the device
    // here, on the explicit game-launch path, before enumerating remote
    // processes or modules.
    if (!mem.Init(std::string(), true, false) && !mem.GetDiagnosticsSnapshot().deviceOpen) {
        runtime.status = "DMA/FPGA indisponivel";
        std::cerr << "[Valorant] DMA open failed\n";
        return false;
    }

    // Tester may override compiled values from source JSON. Customer builds do
    // not probe data/ and therefore never depend on or recreate that directory.
#if defined(OMNIGHOST_DEV_EXTERNAL_OFFSETS)
    {
        char path[MAX_PATH]{};
        if (GetModuleFileNameA(nullptr, path, MAX_PATH)) {
            std::filesystem::path p(path);
            p = p.parent_path() / "data" / "valorant_offsets.json";
            LoadOffsetsJson(p.string().c_str());
        }
        LoadOffsetsJson("data/valorant_offsets.json");
        LoadOffsetsJson("Valorant/data/valorant_offsets.json");
    }
#endif

    DWORD pid = mem.GetPidFromName("VALORANT-Win64-Shipping.exe");
    if (!pid) {
        pid = mem.GetPidFromName("VALORANT.exe");
        if (pid)
            runtime.process_name = "VALORANT.exe";
    }
    if (!pid) {
        runtime.status = "Offsets: ok | Processo: a aguardar VALORANT";
        return false;
    }

    // Module lookup is scoped to Memory::current_process. Bind the process
    // explicitly instead of relying on whichever game happened to run before.
    if (!mem.Init(runtime.process_name, false, false)) {
        runtime.status = mem.last_attach_result == Memory::AttachResult::Waiting
            ? "Offsets: ok | Memoria: a preparar"
            : "Offsets: ok | Memoria: falhou";
        std::cerr << "[Valorant] process bind failed pid=" << pid
                  << " process=" << runtime.process_name
                  << " result=" << mem.LastAttachResultName() << "\n";
        return false;
    }

    runtime.base = mem.GetBaseDaddy(runtime.process_name);
    if (!runtime.base || runtime.base < 0x10000) {
        runtime.status = "Offsets: ok | Base: falhou (Vanguard/CR3?)";
        return false;
    }

    runtime.attached = true;
    runtime.status = "Offsets: ok | Base: ok | A ler UWorld...";
    if (!makcu_wrapper::IsConnected())
        makcu_wrapper::MakcuInitialize("");
    return true;
}


// Lobby-safe: process base + UWorld slot readable (decrypt may still be needed in-match).
bool SoftProbeLobbyOffsets() {
    if (!runtime.attached || !runtime.base) {
        std::cout << "[Valorant] SoftProbe: not attached\n";
        return false;
    }
    if (!offsets.uworld_state) {
        std::cout << "[Valorant] SoftProbe: UWorld RVA missing in offsets\n";
        return false;
    }
    const uintptr_t rva = offsets.uworld_state;
    uintptr_t raw = 0;
    const bool readable = mem.Read(runtime.base + rva, &raw, sizeof(raw));
    // Accept readable slot even if value is encrypted (non-canonical) — wrong RVA usually fails read
    const bool ok = readable;
    std::cout << "[Valorant] SoftProbe UWorldRVA read=" << (readable ? "OK" : "FAIL")
              << " raw=0x" << std::hex << raw << std::dec
              << " => " << (ok ? "PASS" : "FAIL") << std::endl;
    return ok;
}

bool ValidateLiveOffsets() {
    // Use soft probe for lobby-safe validation; if that passes, offsets are not outdated.
    return SoftProbeLobbyOffsets();
}

bool IsGameProcessAlive() {
    DWORD pid = mem.GetPidFromName("VALORANT-Win64-Shipping.exe");
    if (!pid) pid = mem.GetPidFromName("VALORANT.exe");
    return pid != 0;
}

void Detach() {
    runtime = Runtime{};
    runtime.status = "Valorant detached";
}

void Tick() {
    if (!runtime.attached || !runtime.base) {
        // soft re-attach attempt
        static uint64_t last = 0;
        uint64_t now = GetTickCount64();
        if (now - last > 2000) {
            last = now;
            Attach();
        }
        return;
    }

    runtime.players.clear();
    runtime.in_game = false;

    // UWorld — many builds encrypt; try raw pointer first
    uintptr_t uworld = 0;
    if (!R(runtime.base + offsets.uworld_state, uworld) || !IsUserPtr(uworld)) {
        runtime.status = "Lobby / a aguardar partida (UWorld...)";
        runtime.uworld = 0;
        runtime.in_game = false;
        return;
    }
    runtime.uworld = uworld;

    uintptr_t gi = 0, lp_arr = 0, local_player = 0, pc = 0, pawn = 0;
    R(uworld + offsets.owning_game_instance, gi);
    if (!IsUserPtr(gi)) {
        runtime.status = "Lobby / a aguardar partida (GameInstance...)";
        runtime.in_game = false;
        return;
    }
    R(gi + offsets.local_players, lp_arr);
    if (IsUserPtr(lp_arr))
        R(lp_arr, local_player); // TArray.Data[0]
    if (!IsUserPtr(local_player)) {
        runtime.status = "Lobby / a aguardar partida (LocalPlayer...)";
        runtime.in_game = false;
        return;
    }
    // PlayerController — probe a few common ULocalPlayer slots (dumps sometimes leave this unresolved)
    {
        const uintptr_t pc_offs[] = {
            offsets.player_controller ? offsets.player_controller : 0x30ull,
            0x30ull, 0x38ull, 0x40ull, 0x28ull
        };
        for (uintptr_t off : pc_offs) {
            uintptr_t cand = 0;
            if (R(local_player + off, cand) && IsUserPtr(cand)) {
                pc = cand;
                offsets.player_controller = off;
                break;
            }
        }
    }
    if (!IsUserPtr(pc)) {
        runtime.status = "Lobby / a aguardar partida (PlayerController...)";
        runtime.in_game = false;
        return;
    }
    R(pc + offsets.acknowledged_pawn, pawn);
    // Fallback: AController::Pawn if AcknowledgedPawn is null (spectate / transition).
    // The current Offsets layout has no dedicated `pawn` field, so probe the
    // common controller pawn slot directly instead of referencing a non-existent member.
    if (!IsUserPtr(pawn)) {
        uintptr_t alt = 0;
        if (R(pc + 0x2B0, alt) && IsUserPtr(alt))
            pawn = alt;
    }
    runtime.local_pawn = IsUserPtr(pawn) ? pawn : 0;

    // Camera
    uintptr_t cam = 0;
    R(pc + offsets.player_camera, cam);
    if (IsUserPtr(cam)) {
        // FMinimalViewInfo at camera_cache + pov
        float loc[3]{}, rot[3]{}, fov = 90.f;
        const uintptr_t pov = cam + offsets.camera_cache + offsets.camera_pov;
        R(pov + 0x0, loc[0]);
        R(pov + 0x4, loc[1]);
        R(pov + 0x8, loc[2]);
        R(pov + 0xC, rot[0]);
        R(pov + 0x10, rot[1]);
        R(pov + 0x14, rot[2]);
        R(pov + 0x18, fov);
        memcpy(runtime.cam_pos, loc, sizeof(loc));
        memcpy(runtime.cam_rot, rot, sizeof(rot));
        runtime.cam_fov = fov > 1.f ? fov : 90.f;
        BuildViewMatrix(runtime.cam_pos, runtime.cam_rot, runtime.cam_fov, runtime.view_matrix);
    }

    // Local team
    if (runtime.local_pawn) {
        uintptr_t ps = 0, tc = 0;
        int tid = 0;
        R(runtime.local_pawn + offsets.player_state, ps);
        if (IsUserPtr(ps)) {
            R(ps + offsets.team_component, tc);
            if (IsUserPtr(tc))
                R(tc + offsets.team_id, tid);
        }
        runtime.local_team = tid;
    }

    // Actors from persistent level
    uintptr_t level = 0, actors = 0;
    int count = 0;
    R(uworld + offsets.persistent_level, level);
    if (!IsUserPtr(level)) {
        runtime.status = "Lobby / a aguardar partida (Level...)";
        runtime.in_game = false;
        return;
    }
    R(level + offsets.actor_array, actors);
    R(level + offsets.actor_count, count);
    if (!IsUserPtr(actors) || count <= 0 || count > 4096) {
        runtime.status = "Lobby / a aguardar partida (Actors...)";
        runtime.in_game = false;
        return;
    }

    const int cap = config.max_actors > 0 ? config.max_actors : 64;
    int added = 0;
    for (int i = 0; i < count && added < cap; ++i) {
        uintptr_t actor = 0;
        R(actors + (uintptr_t)i * sizeof(uintptr_t), actor);
        if (!IsUserPtr(actor)) continue;

        // Heuristic: must have mesh + damage handler
        uintptr_t mesh = 0, dmg = 0, root = 0;
        R(actor + offsets.current_mesh, mesh);
        R(actor + offsets.damage_handler, dmg);
        R(actor + offsets.root_component, root);
        if (!IsUserPtr(mesh) || !IsUserPtr(dmg) || !IsUserPtr(root))
            continue;

        float hp = 0.f, maxhp = 0.f;
        R(dmg + offsets.current_health, hp);
        R(dmg + offsets.max_health, maxhp);
        if (!std::isfinite(hp) || hp <= 0.f || hp > 500.f)
            continue;

        Player p{};
        p.actor = actor;
        p.pawn = actor;
        p.mesh = mesh;
        p.health = (int)hp;
        p.max_health = maxhp > 1.f ? (int)maxhp : 100;
        p.alive = true;
        p.is_local = (actor == runtime.local_pawn);

        float pos[3]{};
        R(root + offsets.relative_location, pos[0]);
        R(root + offsets.relative_location + 4, pos[1]);
        R(root + offsets.relative_location + 8, pos[2]);
        memcpy(p.pos, pos, sizeof(pos));
        // Head approx — UE capsule origin is typically near feet/center.
        // Prefer mesh bones when available; synthetic head still updates every
        // frame so held aim follows if pos Z drops on crouch.
        float head_up = 75.f;
        // Half-height heuristic: very low Z delta vs local often means crouch prop
        // is already baked into pos; keep 75 as stand default.
        p.head[0] = pos[0];
        p.head[1] = pos[1];
        p.head[2] = pos[2] + head_up;

        uintptr_t ps = 0, tc = 0;
        int tid = 0;
        R(actor + offsets.player_state, ps);
        if (IsUserPtr(ps)) {
            R(ps + offsets.team_component, tc);
            if (IsUserPtr(tc))
                R(tc + offsets.team_id, tid);
        }
        p.team = tid;

        const float dx = pos[0] - runtime.cam_pos[0];
        const float dy = pos[1] - runtime.cam_pos[1];
        const float dz = pos[2] - runtime.cam_pos[2];
        p.distance = sqrtf(dx * dx + dy * dy + dz * dz);

        if (config.team_check && !p.is_local && runtime.local_team != 0 &&
            p.team == runtime.local_team)
            continue;
        if (p.distance > config.max_distance)
            continue;

        runtime.players.push_back(p);
        ++added;
    }

    static MatchLifecycle::State s_match{};
    const bool has_local = IsUserPtr(runtime.local_pawn);
    const int player_count = static_cast<int>(runtime.players.size());
    const auto phase = MatchLifecycle::Update(
        s_match, true, has_local, player_count, runtime.uworld);
    runtime.in_game = (phase == MatchLifecycle::Phase::InMatch
        || phase == MatchLifecycle::Phase::EnteredMatch
        || phase == MatchLifecycle::Phase::WaitingEntities);

    if (phase == MatchLifecycle::Phase::EnteredMatch) {
        std::cout << "[Valorant] Entrada em partida — players=" << player_count << std::endl;
    } else if (phase == MatchLifecycle::Phase::LeftMatch) {
        std::cout << "[Valorant] Saida de partida / lobby\n";
    }

    char buf[160];
    MatchLifecycle::FormatStatus(buf, sizeof(buf), phase, player_count, nullptr);
    runtime.status = buf;
}

const char* StatusText() {
    return runtime.status.c_str();
}

bool ReinitDma() {
    std::cout << "[Valorant] Reinit DMA" << std::endl;
    Detach();
    return Attach();
}

} // namespace Valorant
