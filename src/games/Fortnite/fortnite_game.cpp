#include "fortnite_game.h"
#include "../DMALibrary/Memory/Memory.h"
#include "platform/offset_source.h"
#include "gameplay/snapshot_exchange.h"
#include "gameplay/frame_pipeline.h"
#include "gameplay/dma_telemetry_log.h"
#include "imgui.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstring>
#include <atomic>
#include <thread>
#include <Windows.h>

extern Memory mem;

namespace Fortnite {

Config config{};
Offsets offsets{};
Runtime runtime{};

static const char* kProcessCandidates[] = {
    "FortniteClient-Win64-Shipping.exe",
};

static bool g_logged_once = false;

// ── P0 fix: DMA never runs on the render thread ──────────────────────────
// Acquisition thread owns g_acq_runtime and publishes immutable Runtime
// snapshots. CameraLoop refreshes only the 3 PCM fields at ~62 Hz from the
// chain-resolved camera manager pointer. Tick()/DrawESP() consume leases;
// the public `runtime` global is written exclusively by the render thread
// (single-writer — UI status pages read it without cross-thread races).
namespace {
OmniGhost::Gameplay::SnapshotExchange<Runtime, 3> g_snapshots;
OmniGhost::Gameplay::SnapshotExchange<Config, 3> g_config_snapshots;
struct CameraFrame {
    FVectorD loc{};
    FRotatorD rot{};
    float fov = 0.f;
    bool ok = false;
    uint64_t sample_ms = 0; // midpoint of the PCM fields read
    uintptr_t camera_manager = 0;
};
OmniGhost::Gameplay::SnapshotExchange<CameraFrame, 3> g_camera_frames;
Runtime g_acq_runtime{};                 // acquisition thread ONLY
std::thread g_acq_thread;
std::thread g_cam_thread;
std::atomic_bool g_threads_stop{false};
std::atomic_bool g_threads_running{false};
std::atomic<uintptr_t> g_camera_manager{0}; // chain → camera lane handoff
} // namespace

static bool IsCanonicalUserPtr(uintptr_t p) {
    // Usermode x64 canonical pointer, non-null, reasonable floor.
    if (p < 0x10000ULL) return false;
    if (p > 0x00007FFFFFFFFFFFULL) return false;
    // Reject common poison patterns
    if ((p & 0xFFFFULL) == 0xACDE) return false;
    return true;
}

static bool ReadU64(uintptr_t addr, uintptr_t& out) {
    out = 0;
    if (!addr) return false;
    uint64_t tmp = 0;
    if (!mem.Read(addr, &tmp, sizeof(tmp)))
        return false;
    out = static_cast<uintptr_t>(tmp);
    return true;
}

static bool ReadI32(uintptr_t addr, int32_t& out) {
    out = 0;
    if (!addr) return false;
    return mem.Read(addr, &out, sizeof(out));
}

static bool ReadFVectorD(uintptr_t addr, FVectorD& out) {
    out = {};
    if (!addr) return false;
    double buf[3]{};
    if (!mem.Read(addr, buf, sizeof(buf)))
        return false;
    out.x = buf[0];
    out.y = buf[1];
    out.z = buf[2];
    return out.finite();
}

static bool ReadFRotatorD(uintptr_t addr, FRotatorD& out) {
    out = {};
    if (!addr) return false;
    double buf[3]{};
    if (!mem.Read(addr, buf, sizeof(buf)))
        return false;
    out.pitch = buf[0];
    out.yaw = buf[1];
    out.roll = buf[2];
    return out.finite();
}

static bool ReadFloat(uintptr_t addr, float& out) {
    out = 0.f;
    if (!addr) return false;
    return mem.Read(addr, &out, sizeof(out));
}

static ChainDiag ReadPtrDiag(uintptr_t addr) {
    ChainDiag d{};
    d.address = addr;
    uint64_t tmp = 0;
    size_t got = 0;
    if (addr) {
        // Counted read first (surfaces partial page faults), then plain Read fallback.
        if (mem.ReadWithCount(addr, &tmp, sizeof(tmp), got) && got == sizeof(tmp)) {
            d.read_ok = true;
        } else if (mem.Read(addr, &tmp, sizeof(tmp))) {
            d.read_ok = true;
            got = sizeof(tmp);
        }
    }
    d.bytes_read = static_cast<uint32_t>(got);
    d.value = static_cast<uintptr_t>(tmp);
    d.canonical = d.read_ok && IsCanonicalUserPtr(d.value);
    if (!addr) d.fail = "null address";
    else if (!d.read_ok) d.fail = got ? "PARTIAL READ" : "READ FAILED";
    else if (!d.canonical) d.fail = "non-canonical pointer";
    return d;
}

// PE32+ OptionalHeader.SizeOfImage. Returns 0 on failure.
static uint32_t ReadPeSizeOfImage(uintptr_t module_base) {
    if (!module_base) return 0;
    uint32_t e_lfanew = 0;
    if (!mem.Read(module_base + 0x3C, &e_lfanew, sizeof(e_lfanew)))
        return 0;
    if (e_lfanew == 0 || e_lfanew > 0x1000)
        return 0;
    uint32_t size_of_image = 0;
    if (!mem.Read(module_base + e_lfanew + 0x50, &size_of_image, sizeof(size_of_image)))
        return 0;
    return size_of_image;
}

// Current dump: world = encoded * mul + add (uint64 modular arithmetic).
// Legacy: rotl64(encoded - sub, rol) ^ xor_key when mul/add are zero.
static uintptr_t DecryptGWorld(uint64_t encoded) {
    if (!offsets.gworld_encoded)
        return static_cast<uintptr_t>(encoded);
    if (offsets.gworld_mul != 0) {
        const uint64_t world =
            offsets.gworld_mul * encoded + offsets.gworld_add;
        return static_cast<uintptr_t>(world);
    }
    // Legacy rot_xor path
    const uint64_t v = encoded - offsets.gworld_sub;
    const uint32_t r = offsets.gworld_rol & 63u;
    const uint64_t rotated = (v << r) | (v >> ((64u - r) & 63u));
    return static_cast<uintptr_t>(rotated ^ offsets.gworld_xor);
}

// Standard UE-style W2S with degrees, camera at origin of view axes.
static bool WorldToScreen(const FVectorD& world, const FVectorD& cam_loc,
                          const FRotatorD& cam_rot, float fov_deg,
                          float screen_w, float screen_h,
                          float& out_x, float& out_y) {
    if (fov_deg <= 20.f || fov_deg >= 170.f) return false;
    if (!world.finite() || !cam_loc.finite() || !cam_rot.finite()) return false;
    if (screen_w < 1.f || screen_h < 1.f) return false;

    constexpr double kPi = 3.14159265358979323846;
    const double pitch = cam_rot.pitch * kPi / 180.0;
    const double yaw = cam_rot.yaw * kPi / 180.0;

    const double cp = std::cos(pitch), sp = std::sin(pitch);
    const double cy = std::cos(yaw), sy = std::sin(yaw);

    // Camera axes (UE: X forward, Y right, Z up)
    const double fx = cp * cy, fy = cp * sy, fz = sp;
    const double rx = -sy, ry = cy, rz = 0.0;
    const double ux = -sp * cy, uy = -sp * sy, uz = cp;

    const double dx = world.x - cam_loc.x;
    const double dy = world.y - cam_loc.y;
    const double dz = world.z - cam_loc.z;

    const double forward = dx * fx + dy * fy + dz * fz;
    if (forward < 1.0) return false; // behind / too close

    const double right = dx * rx + dy * ry + dz * rz;
    const double up = dx * ux + dy * uy + dz * uz;

    const double fov_rad = static_cast<double>(fov_deg) * kPi / 180.0;
    const double tan_half = std::tan(fov_rad * 0.5);
    if (tan_half <= 1e-6) return false;

    const double aspect = static_cast<double>(screen_w) / static_cast<double>(screen_h);
    const double ndc_x = (right / forward) / (tan_half * aspect);
    const double ndc_y = (up / forward) / tan_half;

    out_x = static_cast<float>((ndc_x + 1.0) * 0.5 * screen_w);
    out_y = static_cast<float>((1.0 - ndc_y) * 0.5 * screen_h);
    return std::isfinite(out_x) && std::isfinite(out_y);
}

static std::string Hex(uintptr_t v) {
    std::ostringstream o;
    o << "0x" << std::hex << std::uppercase << v;
    return o.str();
}

// Read only the replicated GameState player list. This data is diagnostic at
// this stage; the Fortnite ESP and aim paths do not consume runtime.players.
// Acquisition-thread only. Player slots are read with scatter passes instead
// of per-player sequential DMA: A=pawn/health/team, B=root/state validate,
// C=positions (world_sample_ms = midpoint of the position scatter).
static void ResolvePlayers(Runtime& rt, const Config& frameConfig) {
    rt.players.clear();
    rt.player_array_ok = false;
    rt.player_data_ok = false;
    rt.player_array_count = 0;
    rt.player_array_capacity = 0;
    rt.players_without_pawn = 0;
    rt.players_dying = 0;
    rt.players_out_of_range = 0;
    rt.players_same_team = 0;
    rt.players_invalid = 0;
    rt.players_local = 0;

    if (!rt.world) {
        rt.player_array_status = "A aguardar World";
        return;
    }
    uintptr_t game_state = 0;
    if (!ReadU64(rt.world + offsets.world_game_state, game_state) ||
        !IsCanonicalUserPtr(game_state)) {
        rt.player_array_status = "GameState indisponível (lobby ou offset inválido)";
        return;
    }

    // UE TArray: data pointer, Num and Max. Reject corrupt headers before
    // allocating or reading a potentially unbounded region over DMA.
    const uintptr_t array_addr = game_state + offsets.game_state_player_array;
    uintptr_t data = 0;
    int32_t count = 0, capacity = 0;
    if (!ReadU64(array_addr, data) ||
        !ReadI32(array_addr + 8, count) ||
        !ReadI32(array_addr + 12, capacity) ||
        count < 0 || count > 256 || capacity < count || capacity > 512 ||
        (count > 0 && !IsCanonicalUserPtr(data))) {
        rt.player_array_status = "PlayerArray: cabeçalho inválido";
        return;
    }
    rt.player_array_ok = true;
    rt.player_array_count = count;
    rt.player_array_capacity = capacity;
    if (count == 0) {
        rt.player_data_ok = true;
        rt.player_array_status = "PlayerArray válida, sem jogadores (lobby)";
        return;
    }

    std::vector<uintptr_t> states(static_cast<size_t>(count));
    if (!mem.Read(data, states.data(), states.size() * sizeof(uintptr_t))) {
        rt.player_array_status = "PlayerArray: falha ao ler entradas";
        return;
    }
    rt.player_data_ok = true;
    if (!rt.local_pawn_ok && !rt.camera_ok) {
        rt.player_array_status = "PlayerArray OK; a aguardar posição local/câmara";
        return;
    }

    uintptr_t local_state = 0;
    uint8_t local_team = 0;
    const bool have_local_state = rt.player_controller &&
        ReadU64(rt.player_controller + offsets.controller_player_state, local_state) &&
        IsCanonicalUserPtr(local_state);
    const bool have_local_team = have_local_state &&
        mem.Read(local_state + offsets.fort_ps_team_index, &local_team, sizeof(local_team));
    const double max_cm = static_cast<double>(frameConfig.max_distance) * 100.0;

    // ── Pass A: pawn ptr + health + team for every entry (scatter) ────────
    constexpr int kMaxPlayers = 128; // practical server cap; arrays stay stack-bound
    const int n = (std::min)(count, kMaxPlayers);
    std::array<uintptr_t, kMaxPlayers> pawns{};
    std::array<float, kMaxPlayers> healths{};
    std::array<uint8_t, kMaxPlayers> teams{};
    if (auto h = mem.CreateScatterHandle()) {
        for (int i = 0; i < n; ++i) {
            if (!IsCanonicalUserPtr(states[static_cast<size_t>(i)])) continue;
            mem.AddScatterReadRequest(h, states[static_cast<size_t>(i)] + offsets.player_state_pawn_private,
                                      &pawns[i], sizeof(uintptr_t));
            mem.AddScatterReadRequest(h, states[static_cast<size_t>(i)] + offsets.fort_ps_current_health,
                                      &healths[i], sizeof(float));
            mem.AddScatterReadRequest(h, states[static_cast<size_t>(i)] + offsets.fort_ps_team_index,
                                      &teams[i], sizeof(uint8_t));
        }
        mem.ExecuteReadScatter(h);
        mem.CloseScatterHandle(h);
    }

    // ── Pass B: root component + pong validate (candidates only) ──────────
    std::array<uintptr_t, kMaxPlayers> roots{};
    std::array<uintptr_t, kMaxPlayers> pawnStates{};
    std::array<bool, kMaxPlayers> candidate{};
    if (auto h = mem.CreateScatterHandle()) {
        for (int i = 0; i < n; ++i) {
            const uintptr_t pawn = pawns[i];
            if (!IsCanonicalUserPtr(pawn)) continue;
            if (rt.local_pawn_ok && pawn == rt.local_pawn) continue;   // local
            if (!std::isfinite(healths[i]) || healths[i] <= 0.f) continue; // dying
            candidate[i] = true;
            mem.AddScatterReadRequest(h, pawn + offsets.actor_root_component,
                                      &roots[i], sizeof(uintptr_t));
            mem.AddScatterReadRequest(h, pawn + offsets.pawn_player_state,
                                      &pawnStates[i], sizeof(uintptr_t));
        }
        mem.ExecuteReadScatter(h);
        mem.CloseScatterHandle(h);
    }

    // ── Pass C: positions (world sample timestamp = midpoint of this read) ─
    std::array<FVectorD, kMaxPlayers> positions{};
    bool queuedPos = false;
    {
        const uint64_t positionBeginMs = GetTickCount64();
        if (auto h = mem.CreateScatterHandle()) {
            for (int i = 0; i < n; ++i) {
                if (!candidate[i]) continue;
                if (!IsCanonicalUserPtr(roots[i])) continue;
                if (pawnStates[i] != states[static_cast<size_t>(i)]) continue;
                mem.AddScatterReadRequest(h, roots[i] + offsets.scene_relative_location,
                                          &positions[i], sizeof(FVectorD));
                queuedPos = true;
            }
            mem.ExecuteReadScatter(h);
            mem.CloseScatterHandle(h);
        }
        const uint64_t positionEndMs = GetTickCount64();
        if (queuedPos)
            rt.world_sample_ms = positionBeginMs + (positionEndMs - positionBeginMs) / 2u;
    }

    for (int i = 0; i < n; ++i) {
        const uintptr_t state = states[static_cast<size_t>(i)];
        if (!IsCanonicalUserPtr(state)) {
            ++rt.players_invalid;
            continue;
        }
        if (have_local_state && state == local_state) {
            ++rt.players_local;
            continue;
        }
        const uintptr_t pawn = pawns[i];
        if (!IsCanonicalUserPtr(pawn)) {
            ++rt.players_without_pawn;
            continue;
        }
        if (rt.local_pawn_ok && pawn == rt.local_pawn) {
            ++rt.players_local;
            continue;
        }
        if (pawnStates[i] != state && pawnStates[i] != 0) {
            ++rt.players_invalid;
            continue;
        }
        const float health = healths[i];
        if (!std::isfinite(health)) {
            ++rt.players_invalid;
            continue;
        }
        if (health <= 0.f) {
            ++rt.players_dying;
            continue;
        }
        const FVectorD position = positions[i];
        if (!IsCanonicalUserPtr(roots[i]) || !position.finite() ||
            std::abs(position.x) > 1e8 || std::abs(position.y) > 1e8 ||
            std::abs(position.z) > 1e8) {
            ++rt.players_invalid;
            continue;
        }
        const FVectorD& origin = rt.local_pawn_ok ? rt.local_pos : rt.cam_loc;
        const double dx = position.x - origin.x;
        const double dy = position.y - origin.y;
        const double dz = position.z - origin.z;
        const double distance_cm = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!std::isfinite(distance_cm)) {
            ++rt.players_invalid;
            continue;
        }
        if (distance_cm > max_cm) {
            ++rt.players_out_of_range;
            continue;
        }
        const uint8_t team = teams[i];
        const bool team_known = true;
        if (frameConfig.team_check && have_local_team && team_known && team == local_team) {
            ++rt.players_same_team;
            continue;
        }
        Runtime::Player player{};
        player.player_state = state;
        player.pawn = pawn;
        player.position = position;
        player.distance_m = static_cast<float>(distance_cm / 100.0);
        player.team_index = team;
        player.team_known = team_known;
        rt.players.push_back(player);
    }
    if (!rt.players.empty())
        rt.player_array_status = "PlayerArray OK | jogadores filtrados disponíveis";
    else if (rt.players_invalid > count / 2)
        rt.player_array_status = "PlayerArray legível; muitas entradas inválidas (verificar offsets)";
    else
        rt.player_array_status = "PlayerArray válida; nenhum jogador passou os filtros";
}

static void FlushDiagnosticsToLogImpl(const Runtime& rt) {
#if !defined(OMNIGHOST_VERBOSE_OFFSET_DIAGNOSTICS)
    // Throttle identical SUMMARY lines (Tick resolves every frame).
    static std::string last_status;
    static ULONGLONG last_ts = 0;
    const ULONGLONG now = GetTickCount64();
    const std::string summary = rt.status + " | " + rt.player_array_status +
        " | " + std::to_string(rt.player_array_count) + "/" +
        std::to_string(rt.players.size());
    if (summary == last_status && (now - last_ts) < 2000)
        return;
    last_status = summary;
    last_ts = now;
    // SessionLog owns the only logs.txt sink. Customer builds intentionally log
    // outcomes rather than addresses/RVAs or the full offset table.
    std::clog << "[FORTNITE] diagnostics=SUMMARY"
              << " attached=" << (rt.attached ? "YES" : "NO")
              << " chain=" << (rt.chain_ok ? "PASS" : "FAIL")
              << " camera=" << (rt.camera_ok ? "PASS" : "FAIL")
              << " local_pawn=" << (rt.local_pawn_ok ? "PASS" : "WAIT")
              << " w2s=" << (rt.w2s_ok ? "PASS" : "FAIL")
              << " player_array=" << (rt.player_array_ok ? "PASS" : "FAIL")
              << " entries=" << rt.player_array_count
              << " selected=" << rt.players.size()
              << " player_status=\"" << rt.player_array_status << "\""
              << " status=\"" << rt.status << "\"\n";
#else
    std::ostringstream o;
    o << std::fixed << std::setprecision(3);
    o << "======== FORTNITE LOCALPLAYER DIAG " << offsets.build << "-CL-" << offsets.cl << " ========\n";
    o << "[FORTNITE] Build expected: " << offsets.build << "-CL-" << offsets.cl << "\n";
    o << "[ROOT] ModuleBase = " << Hex(rt.base) << "\n";
    o << "[ROOT] GWorld RVA = " << Hex(offsets.gworld) << "\n";
    o << "[ROOT] GWorld address = " << Hex(rt.base + offsets.gworld) << "\n";
    o << "[ROOT] GEngine RVA = " << Hex(offsets.gengine) << " (optional)\n";
    o << "[ROOT] World read success = " << (rt.d_world.read_ok ? "YES" : "NO") << "\n";
    o << "[ROOT] World = " << Hex(rt.world)
      << " canonical=" << (rt.d_world.canonical ? "YES" : "NO") << "\n";
    if (rt.d_world.fail)
        o << "[ROOT] World Fail = " << rt.d_world.fail << "\n";
    if (rt.engine)
        o << "[ROOT] Engine (fallback path) = " << Hex(rt.engine) << "\n";

    o << "[CHAIN] GameViewport = " << Hex(rt.viewport)
      << " read=" << (rt.d_viewport.read_ok ? "OK" : "FAIL")
      << " canonical=" << (rt.d_viewport.canonical ? "YES" : "NO") << "\n";
    o << "[CHAIN] World = " << Hex(rt.world)
      << " read=" << (rt.d_world.read_ok ? "OK" : "FAIL")
      << " canonical=" << (rt.d_world.canonical ? "YES" : "NO") << "\n";
    o << "[CHAIN] Viewport.GameInstance = " << Hex(rt.viewport_gi) << "\n";
    o << "[CHAIN] World.GameInstance = " << Hex(rt.world_gi) << "\n";
    o << "[CHECK] GameInstance match = " << (rt.gi_match ? "PASS" : "FAIL") << "\n";

    o << "[LOCAL] LocalPlayers.Data = " << Hex(rt.local_players_data) << "\n";
    o << "[LOCAL] LocalPlayers.Count = " << rt.local_players_count << "\n";
    o << "[LOCAL] LocalPlayer[0] = " << Hex(rt.local_player) << "\n";
    o << "[LOCAL] PlayerController = " << Hex(rt.player_controller) << "\n";
    o << "[LOCAL] AcknowledgedPawn = " << Hex(rt.local_pawn)
      << (rt.local_pawn_ok ? "" : " (NOT SPAWNED)") << "\n";

    o << "[CAMERA] PCM = " << Hex(rt.camera_manager) << "\n";
    o << "[CAMERA] Location = "
      << rt.cam_loc.x << ", " << rt.cam_loc.y << ", " << rt.cam_loc.z << "\n";
    o << "[CAMERA] Rotation = "
      << rt.cam_rot.pitch << ", " << rt.cam_rot.yaw << ", " << rt.cam_rot.roll << "\n";
    o << "[CAMERA] FOV = " << rt.cam_fov << "\n";
    o << "[CAMERA] valid = " << (rt.camera_ok ? "PASS" : "FAIL") << "\n";

    if (rt.local_pawn_ok) {
        o << "[PAWN] XYZ = "
          << rt.local_pos.x << ", " << rt.local_pos.y << ", " << rt.local_pos.z << "\n";
        o << "[W2S] " << (rt.w2s_ok ? "PASS" : "FAIL")
          << " screen=" << rt.screen_x << "," << rt.screen_y
          << " dist=" << rt.distance_to_cam << "\n";
    }
    o << "[PLAYERS] PlayerArray = " << (rt.player_array_ok ? "PASS" : "FAIL")
      << " count=" << rt.player_array_count
      << " selected=" << rt.players.size()
      << " reason=" << rt.player_array_status << "\n";
    o << "[STATUS] " << rt.status << "\n";
    o << "================================================================\n";

    const std::string block = o.str();
    std::clog << block;
#endif
}

// UI-thread diagnostics entry point (reads the presentation-owned runtime).
void FlushDiagnosticsToLog() { FlushDiagnosticsToLogImpl(runtime); }

static void ResolveLocalChain(Runtime& rt) {
    rt.chain_ok = false;
    rt.camera_ok = false;
    rt.local_pawn_ok = false;
    rt.w2s_ok = false;
    rt.gi_match = false;
    rt.engine = rt.viewport = rt.world = 0;
    rt.viewport_gi = rt.world_gi = rt.game_instance = 0;
    rt.local_players_data = 0;
    rt.local_players_count = 0;
    rt.local_player = rt.player_controller = 0;
    rt.local_pawn = rt.root_component = rt.camera_manager = 0;
    rt.local_pos = {};
    rt.cam_loc = {};
    rt.cam_rot = {};
    rt.cam_fov = 0.f;
    rt.last_fail.clear();

    if (!rt.base) {
        rt.last_fail = "no module base";
        rt.status = "FAIL: no module base";
        return;
    }

    // Prove process VA reads work (PE MZ at module base).
    uint16_t mz = 0;
    if (!mem.Read(rt.base, &mz, sizeof(mz)) || mz != 0x5A4D) {
        rt.last_fail = "module base not readable (MZ)";
        rt.status = "FAIL: leitura do modulo falhou (base invalida ou DMA)";
        return;
    }

    const uint32_t image_size = ReadPeSizeOfImage(rt.base);

    // -------------------------------------------------------------------------
    // PRIMARY ROOT — cheatoffsets 42.00: GWorld plain pointer.
    // Capware CL-57316517 UWorld members applied after pointer resolves.
    // OPTIONAL fallback: GEngine -> GameViewport -> World when GWorld fails.
    // -------------------------------------------------------------------------
    bool have_world = false;
    if (offsets.gworld) {
        if (image_size && offsets.gworld >= image_size) {
            rt.last_fail = "GWorld RVA outside SizeOfImage";
            rt.status = "FAIL GWorld: RVA fora da imagem (offsets/modulo)";
            return;
        }
        // Encoded path: read u64 at GWorld RVA, then decrypt_world().
        // Plain path: treat value as pointer directly (gworld_encoded=false).
        const uintptr_t slot = rt.base + offsets.gworld;
        uint64_t encoded = 0;
        size_t got = 0;
        const bool slot_ok = mem.ReadWithCount(slot, &encoded, sizeof(encoded), got) && got == sizeof(encoded);
        if (!slot_ok)
            (void)mem.Read(slot, &encoded, sizeof(encoded));

        rt.d_world.address = slot;
        rt.d_world.bytes_read = got ? got : (encoded ? sizeof(encoded) : 0);
        rt.d_world.read_ok = slot_ok || (encoded != 0);

        if (rt.d_world.read_ok) {
            const uintptr_t world = offsets.gworld_encoded
                ? DecryptGWorld(encoded)
                : static_cast<uintptr_t>(encoded);
            rt.d_world.value = world;
            rt.d_world.canonical = IsCanonicalUserPtr(world);
            if (!rt.d_world.canonical)
                rt.d_world.fail = offsets.gworld_encoded ? "decrypt non-canonical" : "non-canonical pointer";
            else {
                rt.world = world;
                have_world = true;
            }
        } else {
            rt.d_world.fail = "READ FAILED";
        }
    }
    // Fallback: GEngine -> GameViewport -> World (cheatoffsets documents this path).
    if (!have_world && offsets.gengine) {
        rt.d_engine = ReadPtrDiag(rt.base + offsets.gengine);
        if (rt.d_engine.read_ok && rt.d_engine.canonical) {
            rt.engine = rt.d_engine.value;
            rt.d_viewport = ReadPtrDiag(rt.engine + offsets.engine_game_viewport);
            if (rt.d_viewport.read_ok && rt.d_viewport.canonical) {
                rt.viewport = rt.d_viewport.value;
                rt.d_world = ReadPtrDiag(rt.viewport + offsets.viewport_world);
                if (rt.d_world.read_ok && rt.d_world.canonical) {
                    rt.world = rt.d_world.value;
                    have_world = true;
                }
            }
        }
    }
    if (!have_world) {
        if (offsets.gworld && rt.d_world.read_ok && !rt.d_world.canonical) {
            rt.last_fail = rt.d_world.fail ? rt.d_world.fail : "GWorld non-canonical";
            rt.status = std::string("FAIL GWorld: ") + rt.last_fail;
        } else if (offsets.gworld) {
            rt.last_fail = rt.d_world.fail ? rt.d_world.fail : "READ FAILED";
            rt.status = std::string("FAIL GWorld: ") + rt.last_fail;
        } else {
            rt.last_fail = "GWorld offset missing";
            rt.status = "FAIL: GWorld offset em falta";
        }
        return;
    }

    // GameInstance from World (official). Viewport GI is optional cross-check only.
    rt.d_gi = ReadPtrDiag(rt.world + offsets.world_owning_game_instance);
    if (!rt.d_gi.read_ok || !rt.d_gi.canonical) {
        rt.last_fail = rt.d_gi.fail ? rt.d_gi.fail : "GameInstance";
        rt.status = std::string("FAIL GameInstance: ") + rt.last_fail;
        return;
    }
    rt.game_instance = rt.d_gi.value;
    rt.world_gi = rt.game_instance;
    if (rt.viewport && offsets.viewport_game_instance) {
        uintptr_t vgi = 0;
        if (ReadU64(rt.viewport + offsets.viewport_game_instance, vgi) &&
            IsCanonicalUserPtr(vgi)) {
            rt.viewport_gi = vgi;
            rt.gi_match = (vgi == rt.world_gi);
        }
    } else {
        rt.gi_match = true; // N/A on pure GWorld path
    }

    // LocalPlayers TArray: data @ +0, count @ +8
    uintptr_t lp_data = 0;
    int32_t lp_count = 0;
    if (!ReadU64(rt.game_instance + offsets.gi_local_players, lp_data) ||
        !IsCanonicalUserPtr(lp_data)) {
        rt.last_fail = "LocalPlayers.Data invalid";
        rt.status = "FAIL LocalPlayers.Data";
        return;
    }
    if (!ReadI32(rt.game_instance + offsets.gi_local_players + 8, lp_count) ||
        lp_count < 1 || lp_count > 8) {
        rt.last_fail = "LocalPlayers.Count invalid";
        rt.status = "FAIL LocalPlayers.Count";
        return;
    }
    rt.local_players_data = lp_data;
    rt.local_players_count = lp_count;

    uintptr_t local_player = 0;
    if (!ReadU64(lp_data, local_player) || !IsCanonicalUserPtr(local_player)) {
        rt.last_fail = "LocalPlayer[0] invalid";
        rt.status = "FAIL LocalPlayer[0]";
        return;
    }
    rt.local_player = local_player;
    rt.d_lp.address = lp_data;
    rt.d_lp.value = local_player;
    rt.d_lp.read_ok = true;
    rt.d_lp.canonical = true;
    rt.d_lp.bytes_read = 8;

    // PlayerController
    rt.d_pc = ReadPtrDiag(local_player + offsets.local_player_controller);
    if (!rt.d_pc.read_ok || !rt.d_pc.canonical) {
        rt.last_fail = rt.d_pc.fail ? rt.d_pc.fail : "PlayerController";
        rt.status = std::string("FAIL PlayerController: ") + rt.last_fail;
        return;
    }
    rt.player_controller = rt.d_pc.value;

    // Camera manager (required for W2S / lobby camera)
    rt.d_pcm = ReadPtrDiag(rt.player_controller + offsets.pc_player_camera_manager);
    if (!rt.d_pcm.read_ok || !rt.d_pcm.canonical) {
        rt.last_fail = rt.d_pcm.fail ? rt.d_pcm.fail : "PlayerCameraManager";
        rt.status = std::string("FAIL PCM: ") + rt.last_fail;
        return;
    }
    rt.camera_manager = rt.d_pcm.value;

    // Camera POV fields
    FVectorD cam_loc{};
    FRotatorD cam_rot{};
    float fov = 0.f;
    const bool loc_ok = ReadFVectorD(rt.camera_manager + offsets.pcm_cam_location, cam_loc);
    const bool rot_ok = ReadFRotatorD(rt.camera_manager + offsets.pcm_cam_rotation, cam_rot);
    const bool fov_ok = ReadFloat(rt.camera_manager + offsets.pcm_cam_fov, fov);
    rt.cam_loc = cam_loc;
    rt.cam_rot = cam_rot;
    rt.cam_fov = fov;
    rt.camera_ok = loc_ok && rot_ok && fov_ok && fov > 20.f && fov < 170.f;
    if (!rt.camera_ok) {
        rt.last_fail = "camera Location/Rotation/FOV invalid";
        // still mark chain partially ok for PC/World
        rt.chain_ok = true;
        rt.status = "CHAIN OK | CAMERA FAIL";
        return;
    }

    // Local pawn resolution — AcknowledgedPawn is often null in lobby.
    // Try several UE paths so lobby / spectate still yield a usable actor.
    auto try_pawn = [&](uintptr_t addr, const char* tag) -> uintptr_t {
        uintptr_t p = 0;
        if (!addr || !ReadU64(addr, p) || !IsCanonicalUserPtr(p))
            return 0;
        (void)tag;
        return p;
    };

    uintptr_t pawn = 0;
    const char* pawn_source = "none";
    if ((pawn = try_pawn(rt.player_controller + offsets.pc_acknowledged_pawn, "AcknowledgedPawn")))
        pawn_source = "AcknowledgedPawn";
    else if (offsets.controller_pawn &&
             (pawn = try_pawn(rt.player_controller + offsets.controller_pawn, "Controller.Pawn")))
        pawn_source = "Controller.Pawn";
    else if (offsets.controller_character &&
             (pawn = try_pawn(rt.player_controller + offsets.controller_character, "Controller.Character")))
        pawn_source = "Controller.Character";
    else if (offsets.pc_spectator_pawn &&
             (pawn = try_pawn(rt.player_controller + offsets.pc_spectator_pawn, "SpectatorPawn")))
        pawn_source = "SpectatorPawn";
    else if (offsets.pcm_view_target && rt.camera_manager) {
        // TViewTarget.Target at ViewTarget+0
        if ((pawn = try_pawn(rt.camera_manager + offsets.pcm_view_target, "ViewTarget.Target")))
            pawn_source = "ViewTarget";
    }

    rt.chain_ok = true;

    if (!pawn) {
        // Lobby without possessed pawn: still drive W2S from the camera so the
        // overlay is not "disconnected". Marker sits at the camera origin.
        rt.local_pawn = 0;
        rt.local_pawn_ok = false;
        rt.root_component = 0;
        rt.local_pos = cam_loc;
        rt.distance_to_cam = 0.f;
        rt.status = "CHAIN OK | CAMERA OK | LOCAL PAWN: LOBBY (sem pawn)";
        return;
    }

    rt.local_pawn = pawn;

    uintptr_t root = 0;
    if (!ReadU64(pawn + offsets.actor_root_component, root) || !IsCanonicalUserPtr(root)) {
        rt.local_pawn_ok = false;
        rt.root_component = 0;
        rt.local_pos = cam_loc;
        rt.distance_to_cam = 0.f;
        rt.status = std::string("CHAIN OK | CAMERA OK | RootComponent FAIL (") + pawn_source + ")";
        return;
    }
    rt.root_component = root;

    FVectorD pos{};
    if (!ReadFVectorD(root + offsets.scene_relative_location, pos) || !pos.finite()) {
        rt.local_pawn_ok = false;
        rt.local_pos = cam_loc;
        rt.distance_to_cam = 0.f;
        rt.status = std::string("CHAIN OK | CAMERA OK | LocalPos FAIL (") + pawn_source + ")";
        return;
    }
    rt.local_pos = pos;
    rt.local_pawn_ok = true;

    const double dx = pos.x - cam_loc.x;
    const double dy = pos.y - cam_loc.y;
    const double dz = pos.z - cam_loc.z;
    rt.distance_to_cam = static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));

    rt.status = std::string("CHAIN OK | LOCAL PAWN OK (") + pawn_source + ") | CAMERA OK";
}

bool LoadOffsetsFromJson(const char* path) {
    using namespace OmniGhost::OffsetSource;
    const LoadResult loaded = LoadSnapshot(GameId::Fortnite, path);
    if (!loaded.ok) {
        std::cerr << "[Fortnite] offsets load failed: " << loaded.detail << "\n";
        return false;
    }

    Offsets next = offsets; // keep seeds, overwrite from snapshot
    auto set = [&](std::initializer_list<const char*> keys, uintptr_t& dst) {
        std::uint64_t v = 0;
        if (TryGetAny(loaded.snapshot, keys, v) && v != 0)
            dst = static_cast<uintptr_t>(v);
    };

    set({"globals.GEngine", "engine.GEngine", "GEngine"}, next.gengine);
    set({"globals.GWorld", "engine.GWorld", "GWorld", "gworld_crypto.rva"}, next.gworld);
    set({"globals.GNames", "engine.GNames", "GNames"}, next.gnames);
    set({"globals.ProcessEvent", "engine.ProcessEvent", "ProcessEvent"}, next.process_event);

    // GWorld crypto (mul_add preferred; rot_xor legacy)
    {
        std::uint64_t mul = 0, add = 0, sub = 0, xor_key = 0, rol = 0;
        if (TryGetAny(loaded.snapshot, {"gworld_crypto.world_mul", "engine.world_mul", "Engine.world_mul"}, mul) && mul)
            next.gworld_mul = mul;
        if (TryGetAny(loaded.snapshot, {"gworld_crypto.world_add", "engine.world_add", "Engine.world_add"}, add))
            next.gworld_add = add;
        if (TryGetAny(loaded.snapshot, {"gworld_crypto.sub", "gworld_crypto.sub_decimal"}, sub) && sub)
            next.gworld_sub = sub;
        if (TryGetAny(loaded.snapshot, {"gworld_crypto.xor_key"}, xor_key) && xor_key)
            next.gworld_xor = xor_key;
        if (TryGetAny(loaded.snapshot, {"gworld_crypto.rol_amt"}, rol) && rol)
            next.gworld_rol = static_cast<uint32_t>(rol);
        std::uint64_t enc_flag = 0;
        if (TryGetAny(loaded.snapshot, {"gworld_crypto.encoded"}, enc_flag))
            next.gworld_encoded = (enc_flag != 0);
        else if (mul || sub || xor_key)
            next.gworld_encoded = true;
    }

    set({"Engine.GameViewport"}, next.engine_game_viewport);
    set({"GameViewportClient.World"}, next.viewport_world);
    set({"GameViewportClient.GameInstance"}, next.viewport_game_instance);

    set({"World.PersistentLevel", "UWorld.PersistentLevel"}, next.world_persistent_level);
    set({"World.NetDriver", "UWorld.NetDriver"}, next.world_net_driver);
    set({"World.GameState", "UWorld.GameState"}, next.world_game_state);
    set({"World.Levels", "UWorld.Levels"}, next.world_levels);
    set({"World.OwningGameInstance", "UWorld.OwningGameInstance"}, next.world_owning_game_instance);

    set({"ULevel.ActorCluster", "fn_esp_quick.ULevel_ActorCluster"}, next.level_actor_cluster);
    set({"ULevel.WorldSettings", "fn_esp_quick.ULevel_WorldSettings"}, next.level_world_settings);
    set({"ULevelActorContainer.Actors", "fn_esp_quick.ULevelActorContainer_Actors"}, next.level_actors);

    set({"GameInstance.LocalPlayers", "UGameInstance.LocalPlayers", "fn_esp_quick.UGameInstance_LocalPlayers"}, next.gi_local_players);
    set({"Player.PlayerController", "UPlayer.PlayerController", "LocalPlayer.PlayerController",
         "fn_esp_quick.UPlayer_PlayerController"}, next.local_player_controller);
    set({"LocalPlayer.ViewportClient", "ULocalPlayer.ViewportClient"}, next.local_viewport_client);

    set({"PlayerController.AcknowledgedPawn", "APlayerController.AcknowledgedPawn"}, next.pc_acknowledged_pawn);
    set({"PlayerController.MyHUD", "APlayerController.MyHUD"}, next.pc_my_hud);
    set({"PlayerController.PlayerCameraManager", "APlayerController.PlayerCameraManager"}, next.pc_player_camera_manager);
    set({"PlayerController.SpectatorPawn", "APlayerController.SpectatorPawn",
         "aplayer_controller.SpectatorPawn", "dump_structs.aplayer_controller.SpectatorPawn"},
        next.pc_spectator_pawn);
    set({"Controller.Pawn", "AController.Pawn", "apawn.Controller",
         "acontroller.Pawn", "dump_structs.acontroller.Pawn"}, next.controller_pawn);
    set({"Controller.PlayerState", "AController.PlayerState",
         "acontroller.PlayerState", "dump_structs.acontroller.PlayerState"},
        next.controller_player_state);
    set({"Controller.Character", "AController.Character",
         "acontroller.Character", "dump_structs.acontroller.Character"}, next.controller_character);
    set({"PlayerCameraManager.ViewTarget", "APlayerCameraManager.ViewTarget",
         "aplayer_camera_manager.ViewTarget", "dump_structs.aplayer_camera_manager.ViewTarget"},
        next.pcm_view_target);

    set({"Pawn.PlayerState", "APawn.PlayerState"}, next.pawn_player_state);
    set({"Pawn.Controller", "APawn.Controller"}, next.pawn_controller);
    set({"Character.Mesh", "ACharacter.Mesh"}, next.character_mesh);
    set({"Actor.RootComponent", "AActor.RootComponent"}, next.actor_root_component);
    set({"SceneComponent.RelativeLocation", "USceneComponent.RelativeLocation"}, next.scene_relative_location);
    set({"SceneComponent.RelativeRotation", "USceneComponent.RelativeRotation"}, next.scene_relative_rotation);
    set({"SceneComponent.ComponentVelocity", "USceneComponent.ComponentVelocity"}, next.scene_component_velocity);

    set({"PlayerCameraManager.CameraCachePrivate", "APlayerCameraManager.CameraCachePrivate"}, next.pcm_camera_cache_private);
    // Derived camera POV fields stay relative to CameraCachePrivate unless explicitly present
    if (next.pcm_camera_cache_private) {
        next.pcm_cam_location = next.pcm_camera_cache_private + 0x10; // + POV + Location
        next.pcm_cam_rotation = next.pcm_camera_cache_private + 0x28;
        next.pcm_cam_fov = next.pcm_camera_cache_private + 0x40;
        // Prefer explicit absolute fields from previous layout if JSON has MinimalViewInfo only as relative 0
    }
    // Keep proven absolute PCM camera fields when present as full offsets in prior seeds
    set({"GameStateBase.PlayerArray", "AGameStateBase.PlayerArray",
         "agame_state_base.PlayerArray", "dump_structs.agame_state_base.PlayerArray"},
        next.game_state_player_array);
    set({"PlayerState.PawnPrivate", "APlayerState.PawnPrivate",
         "aplayer_state.PawnPrivate", "dump_structs.aplayer_state.PawnPrivate"},
        next.player_state_pawn_private);
    set({"FortPlayerState.CurrentHealth", "AFortPlayerState.CurrentHealth",
         "afort_player_state.CurrentHealth", "dump_structs.afort_player_state.CurrentHealth"},
        next.fort_ps_current_health);
    set({"FortPawn.CurrentWeapon", "AFortPawn.CurrentWeapon",
         "afort_pawn.CurrentWeapon", "dump_structs.afort_pawn.CurrentWeapon"},
        next.fort_pawn_current_weapon);
    set({"FortPlayerStateAthena.TeamIndex", "AFortPlayerStateAthena.TeamIndex",
         "afort_player_state_athena.TeamIndex", "dump_structs.afort_player_state_athena.TeamIndex"},
        next.fort_ps_team_index);

    // dump_structs paths: update data/fortnite_offsets.json only after patches
    set({"dump_structs.uworld.PersistentLevel", "uworld.PersistentLevel"}, next.world_persistent_level);
    set({"dump_structs.uworld.GameState", "uworld.GameState"}, next.world_game_state);
    set({"dump_structs.uworld.Levels", "uworld.Levels"}, next.world_levels);
    set({"dump_structs.uworld.OwningGameInstance", "uworld.OwningGameInstance"}, next.world_owning_game_instance);
    set({"dump_structs.ugame_instance.LocalPlayers", "ugame_instance.LocalPlayers"}, next.gi_local_players);
    set({"dump_structs.aplayer_controller.AcknowledgedPawn", "aplayer_controller.AcknowledgedPawn"}, next.pc_acknowledged_pawn);
    set({"dump_structs.aplayer_controller.PlayerCameraManager", "aplayer_controller.PlayerCameraManager"}, next.pc_player_camera_manager);
    set({"dump_structs.aactor.RootComponent", "aactor.RootComponent"}, next.actor_root_component);
    set({"dump_structs.uscene_component.RelativeLocation", "uscene_component.RelativeLocation"}, next.scene_relative_location);
    set({"dump_structs.uscene_component.RelativeRotation", "uscene_component.RelativeRotation"}, next.scene_relative_rotation);
    set({"dump_structs.uscene_component.ComponentVelocity", "uscene_component.ComponentVelocity"}, next.scene_component_velocity);
    set({"dump_structs.acharacter.Mesh", "acharacter.Mesh"}, next.character_mesh);
    set({"dump_structs.apawn.PlayerState", "apawn.PlayerState"}, next.pawn_player_state);
    set({"dump_structs.apawn.Controller", "apawn.Controller"}, next.pawn_controller);
    set({"dump_structs.aplayer_camera_manager.CameraCachePrivate", "aplayer_camera_manager.CameraCachePrivate"}, next.pcm_camera_cache_private);
    if (next.pcm_camera_cache_private) {
        next.pcm_cam_location = next.pcm_camera_cache_private + 0x10;
        next.pcm_cam_rotation = next.pcm_camera_cache_private + 0x28;
        next.pcm_cam_fov = next.pcm_camera_cache_private + 0x40;
    }

    // Static string pointers for build/cl — store into thread-local buffers
    static char build_buf[32] = {};
    static char cl_buf[32] = {};
    if (!loaded.snapshot.metadata.build.empty()) {
        std::snprintf(build_buf, sizeof(build_buf), "%s", loaded.snapshot.metadata.build.c_str());
        next.build = build_buf;
    }
    if (!loaded.snapshot.metadata.cl.empty()) {
        std::snprintf(cl_buf, sizeof(cl_buf), "%s", loaded.snapshot.metadata.cl.c_str());
        next.cl = cl_buf;
    }

    if (!next.gworld) {
        std::cerr << "[Fortnite] offsets missing required GWorld\n";
        return false;
    }
    offsets = next;
    std::cout << "[Fortnite] offsets loaded storage=" << loaded.storage
              << " GWorld=0x" << std::hex << offsets.gworld
              << " encoded=" << (offsets.gworld_encoded ? "YES" : "NO")
              << " mul=0x" << offsets.gworld_mul
              << " add=0x" << offsets.gworld_add
              << " GS=0x" << offsets.world_game_state
              << " GI=0x" << offsets.world_owning_game_instance
              << " Team=0x" << offsets.fort_ps_team_index
              << " Weapon=0x" << offsets.fort_pawn_current_weapon
              << " build=" << offsets.build << "-CL-" << offsets.cl << std::dec << "\n";
    if (loaded.from_external_json)
        std::cout << "[Fortnite] source=data/fortnite_offsets.json (edit JSON only)\n";
    else
        std::cout << "[Fortnite] source=embedded — edit data/fortnite_offsets.json + rebuild Publish\n";
    return true;
}

bool ReloadOffsets() {
    return LoadOffsetsFromJson(nullptr);
}

// Camera lane: reads ONLY the PlayerCameraManager POV block from the cached
// chain pointer at ~62 Hz. Never re-walks the deep UE chain — that belongs to
// the acquisition lane. sample_ms = midpoint of the POV read.
static void CameraLoop() {
    uint64_t nextSampleMs = 0;
    while (!g_threads_stop.load(std::memory_order_acquire)) {
        const uint64_t nowMs = GetTickCount64();
        if (nowMs < nextSampleMs) {
            const uint64_t remaining = nextSampleMs - nowMs;
            std::this_thread::sleep_for(std::chrono::milliseconds((std::min)(remaining, uint64_t{8})));
            continue;
        }
        const uintptr_t pcm = g_camera_manager.load(std::memory_order_acquire);
        if (!pcm || !mem.vHandle) {
            nextSampleMs = nowMs + 50u;
            continue;
        }
        // POV fields are contiguous in CameraCachePrivate on current builds:
        // loc(24B) @ pcm_cam_location, rot(24B), fov(4B). Single DMA read.
        struct alignas(8) PovBlock { double loc[3]; double rot[3]; float fov; };
        const bool contiguous =
            offsets.pcm_cam_location + 24 == offsets.pcm_cam_rotation &&
            offsets.pcm_cam_rotation + 24 == offsets.pcm_cam_fov;
        const uint64_t beginMs = GetTickCount64();
        PovBlock blk{};
        bool ok = false;
        if (contiguous) {
            ok = mem.Read(pcm + offsets.pcm_cam_location, &blk, sizeof(blk));
        } else {
            ok = mem.Read(pcm + offsets.pcm_cam_location, blk.loc, sizeof(blk.loc)) &&
                 mem.Read(pcm + offsets.pcm_cam_rotation, blk.rot, sizeof(blk.rot)) &&
                 mem.Read(pcm + offsets.pcm_cam_fov, &blk.fov, sizeof(blk.fov));
        }
        const uint64_t endMs = GetTickCount64();
        OmniGhost::Gameplay::DmaTelemetry::ObserveCameraRead(
            OmniGhost::Gameplay::DmaTelemetry::Fortnite(),
            static_cast<float>(endMs - beginMs));
        if (ok && blk.fov > 20.f && blk.fov < 170.f) {
            if (auto slot = g_camera_frames.TryBeginWrite()) {
                slot.value->loc = { blk.loc[0], blk.loc[1], blk.loc[2] };
                slot.value->rot = { blk.rot[0], blk.rot[1], blk.rot[2] };
                slot.value->fov = blk.fov;
                slot.value->ok = true;
                slot.value->sample_ms = beginMs + (endMs - beginMs) / 2u;
                slot.value->camera_manager = pcm;
                g_camera_frames.Publish(slot.index);
            }
        }
        nextSampleMs += 16u;
        if (nextSampleMs <= GetTickCount64())
            nextSampleMs = GetTickCount64() + 1u;
    }
}

// Acquisition lane: deep chain re-resolve (~20 Hz) + player list (~4 Hz),
// identical cadence to before but now OFF the render thread. Publishes an
// immutable Runtime snapshot per cycle.
static void AcquisitionLoop() {
    OmniGhost::Gameplay::FixedRateScheduler scheduler;
    uint64_t lastPlayerScanMs = 0;
    uint64_t scanId = 0;
    while (!g_threads_stop.load(std::memory_order_acquire)) {
        const auto cycleBegin = std::chrono::steady_clock::now();
        const uint64_t now = GetTickCount64();
        try {
            ResolveLocalChain(g_acq_runtime);
            g_camera_manager.store(g_acq_runtime.camera_manager,
                                   std::memory_order_release);
            if (now - lastPlayerScanMs >= 250) {
                lastPlayerScanMs = now;
                if (auto options = g_config_snapshots.Acquire())
                    ResolvePlayers(g_acq_runtime, *options);
            }
            // Camera freshness comes from the camera lane; embed the latest.
            if (auto cam = g_camera_frames.Acquire(); cam && cam->ok &&
                cam->camera_manager == g_acq_runtime.camera_manager && cam->sample_ms &&
                GetTickCount64() >= cam->sample_ms && GetTickCount64() - cam->sample_ms <= 100u) {
                g_acq_runtime.cam_loc = cam->loc;
                g_acq_runtime.cam_rot = cam->rot;
                g_acq_runtime.cam_fov = cam->fov;
                g_acq_runtime.camera_ok = true;
                g_acq_runtime.camera_sample_ms = cam->sample_ms;
            } else {
                g_acq_runtime.camera_ok = false;
            }
            g_acq_runtime.snapshot_timestamp_ms = GetTickCount64();
            if (auto slot = g_snapshots.TryBeginWrite()) {
                *slot.value = g_acq_runtime;
                g_snapshots.Publish(slot.index);
            }
        } catch (...) {
            // Never kill the acquisition lane on a single bad cycle.
        }
        const float cycleMs = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - cycleBegin).count();
        auto& telemetry = OmniGhost::Gameplay::DmaTelemetry::Fortnite();
        const bool active = g_acq_runtime.attached && g_acq_runtime.local_pawn != 0;
        OmniGhost::Gameplay::DmaTelemetry::ObserveAcquire(telemetry, cycleMs, active, ++scanId);
        telemetry.entities.store(static_cast<int>(g_acq_runtime.players.size()), std::memory_order_relaxed);
        if (g_acq_runtime.camera_sample_ms) {
            const uint64_t measuredAt = GetTickCount64();
            OmniGhost::Gameplay::DmaTelemetry::ObserveCameraAge(telemetry,
                static_cast<float>(measuredAt >= g_acq_runtime.camera_sample_ms
                    ? measuredAt - g_acq_runtime.camera_sample_ms : 0u));
        }
        OmniGhost::Gameplay::DmaTelemetry::Tick("FORTNITE");
        // If a cycle overran its whole budget the device is stalling — do not
        // stack another heavy scan immediately; one short recovery beat.
        scheduler.Wait(std::chrono::milliseconds(cycleMs > 60.f ? 100 : 50));
    }
}

static void EnsureThreadsStarted() {
    bool expected = false;
    if (!g_threads_running.compare_exchange_strong(expected, true,
            std::memory_order_acq_rel))
        return;
    g_threads_stop.store(false, std::memory_order_release);
    g_cam_thread = std::thread(CameraLoop);
    g_acq_thread = std::thread(AcquisitionLoop);
}

static void StopThreads() {
    if (!g_threads_running.exchange(false, std::memory_order_acq_rel))
        return;
    g_threads_stop.store(true, std::memory_order_release);
    if (g_cam_thread.joinable()) g_cam_thread.join();
    if (g_acq_thread.joinable()) g_acq_thread.join();
    g_camera_frames.ResetProducerThread();
    g_snapshots.ResetProducerThread();
    g_camera_manager.store(0, std::memory_order_release);
}

bool Attach() {
    StopThreads(); // safety: never leave a previous session's threads alive
    runtime = Runtime{};
    g_acq_runtime = Runtime{};
    runtime.visual_only = false;
    runtime.process_name = kProcessCandidates[0];
    runtime.status = "A abrir DMA / a procurar Fortnite...";
    g_logged_once = false;

    if (!LoadOffsetsFromJson(nullptr)) {
        runtime.status = "Offsets Fortnite em falta (data/fortnite_offsets.json ou resource)";
        std::cerr << "[Fortnite] offset table unavailable\n";
        return false;
    }

    if (!mem.Init(std::string(), true, false) && !mem.GetDiagnosticsSnapshot().deviceOpen) {
        runtime.status = "DMA/FPGA indisponivel";
        std::cerr << "[Fortnite] DMA open failed\n";
        return false;
    }

    DWORD pid = 0;
    for (const char* name : kProcessCandidates) {
        pid = mem.GetPidFromName(name);
        if (pid) {
            runtime.process_name = name;
            break;
        }
    }
    if (!pid) {
        runtime.status = "Processo Fortnite nao encontrado (abre o jogo no PC principal)";
        std::cerr << "[Fortnite] process not found\n";
        return false;
    }
    runtime.pid = pid;

    // GetBaseDaddy() resolves modules against Memory::current_process. Merely
    // finding a PID does not bind that context, so the old code queried PID 0
    // (or the previously selected game) and could never resolve Fortnite's
    // image. Perform the same validated process bind used by the other game
    // adapters before asking for a module base.
    if (!mem.Init(runtime.process_name, false, false)) {
        runtime.status = mem.last_attach_result == Memory::AttachResult::Waiting
            ? "Processo ok | Memoria ainda a preparar"
            : "Processo ok | Falha ao anexar memoria";
        std::cerr << "[Fortnite] process bind failed pid=" << pid
                  << " process=" << runtime.process_name
                  << " result=" << mem.LastAttachResultName() << "\n";
        return false;
    }

    runtime.base = mem.GetBaseDaddy(runtime.process_name.c_str());
    if (!runtime.base || runtime.base < 0x10000) {
        for (const char* name : kProcessCandidates) {
            runtime.base = mem.GetBaseDaddy(name);
            if (runtime.base && runtime.base >= 0x10000) {
                runtime.process_name = name;
                break;
            }
        }
    }
    if (!runtime.base || runtime.base < 0x10000) {
        runtime.status = "Processo ok | Base do modulo falhou";
        std::cerr << "[Fortnite] module base failed pid=" << pid << "\n";
        return false;
    }

    runtime.attached = true;
    const uint32_t image_size = ReadPeSizeOfImage(runtime.base);
    std::cout << "[FORTNITE] Build expected: " << offsets.build << "-CL-" << offsets.cl << "\n"
              << "[ROOT] ModuleBase = 0x" << std::hex << runtime.base
              << " SizeOfImage=0x" << image_size
              << " pid=" << std::dec << runtime.pid << "\n"
              << "[ROOT] GWorld RVA = 0x" << std::hex << offsets.gworld
              << " GNames RVA = 0x" << offsets.gnames
              << " GEngine RVA = 0x" << offsets.gengine << " (optional)" << std::dec << "\n";
    if (image_size && offsets.gworld >= image_size) {
        std::cerr << "[FORTNITE] GWorld RVA exceeds SizeOfImage — offsets/module mismatch\n";
    }

    // First synchronous resolution (attach UI context, threads not yet
    // started — safe to touch both runtimes here).
    ResolveLocalChain(g_acq_runtime);
    ResolvePlayers(g_acq_runtime, config);
    runtime = g_acq_runtime;
    if (auto slot = g_snapshots.TryBeginWrite()) {
        *slot.value = g_acq_runtime;
        g_snapshots.Publish(slot.index);
    }
    g_snapshots.ResetProducerThread(); // main-thread attach -> acquisition worker
    if (auto options = g_config_snapshots.TryBeginWrite()) {
        *options.value = config;
        g_config_snapshots.Publish(options.index);
    }
    g_config_snapshots.ResetProducerThread(); // attach caller -> UI/render publisher
    EnsureThreadsStarted(); // from here on, DMA runs only on worker threads
    FlushDiagnosticsToLog();
    g_logged_once = true;
    return true;
}

void Detach() {
    StopThreads(); // join workers BEFORE clearing shared state
    g_acq_runtime = Runtime{};
    runtime = Runtime{};
    runtime.status = "Fortnite detached";
    g_logged_once = false;
}

bool IsGameProcessAlive() {
    if (!runtime.attached) return false;
    DWORD pid = mem.GetPidFromName(runtime.process_name.c_str());
    if (!pid) {
        for (const char* name : kProcessCandidates) {
            pid = mem.GetPidFromName(name);
            if (pid) return true;
        }
        return false;
    }
    return true;
}

bool ValidateLiveOffsets() {
    // Fortnite's offsets are simple (no complex encryption); if the process is
    // attached and the base module is readable, offsets are considered valid.
    return IsGameProcessAlive() && runtime.base != 0;
}

void Tick() {
    if (!runtime.attached) {
        static ULONGLONG last = 0;
        const ULONGLONG now = GetTickCount64();
        if (now - last > 2500) {
            last = now;
            Attach();
        }
        return;
    }
    // UI/render thread is the sole writer of Config snapshots. Acquisition
    // never reads mutable UI configuration directly.
    if (auto options = g_config_snapshots.TryBeginWrite()) {
        *options.value = config;
        g_config_snapshots.Publish(options.index);
    }
    EnsureThreadsStarted(); // idempotent; workers own all DMA from here

    // Presentation merge (render thread is the ONLY writer of `runtime`):
    // world state from the acquisition lane, camera from the camera lane.
    if (auto snap = g_snapshots.Acquire()) {
        const uint64_t keepFrames = runtime.frames;
        runtime = *snap;
        runtime.frames = keepFrames + 1;
    } else {
        ++runtime.frames;
    }
    if (auto cam = g_camera_frames.Acquire(); cam && cam->ok &&
        cam->camera_manager == runtime.camera_manager && cam->sample_ms &&
        GetTickCount64() >= cam->sample_ms && GetTickCount64() - cam->sample_ms <= 100u) {
        runtime.cam_loc = cam->loc;
        runtime.cam_rot = cam->rot;
        runtime.cam_fov = cam->fov;
        runtime.camera_ok = true;
        runtime.camera_sample_ms = cam->sample_ms;
    } else {
        runtime.camera_ok = false;
    }

    // W2S using overlay display size. In lobby without a pawn we still project
    // local_pos (camera origin) so the pipeline is not "disconnected".
    runtime.w2s_ok = false;
    if (runtime.camera_ok && runtime.local_pos.finite()) {
        const ImGuiIO& io = ImGui::GetIO();
        float sx = 0.f, sy = 0.f;
        if (WorldToScreen(runtime.local_pos, runtime.cam_loc, runtime.cam_rot, runtime.cam_fov,
                          io.DisplaySize.x, io.DisplaySize.y, sx, sy)) {
            runtime.screen_x = sx;
            runtime.screen_y = sy;
            runtime.w2s_ok = true;
        }
    }

    // Re-log every ~5s if still failing, or once when recovered
    static ULONGLONG last_log = 0;
    const ULONGLONG now = GetTickCount64();
    if (!g_logged_once || (now - last_log > 5000 && (!runtime.chain_ok || !runtime.player_array_ok))) {
        FlushDiagnosticsToLog();
        g_logged_once = true;
        last_log = now;
    }
}

void DrawESP() {
    if (!runtime.attached || !config.esp_enabled)
        return;
    if (!config.show_local_marker && !config.show_local_coords && !config.show_camera_debug)
        return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;

    // Debug HUD (top-left)
    if (config.show_debug_panel) {
        float y = 12.f;
        auto line = [&](const char* s) {
            dl->AddText(ImVec2(12.f, y), IM_COL32(200, 220, 255, 230), s);
            y += 16.f;
        };
        char buf[256];
        std::snprintf(buf, sizeof(buf), "FN %s-CL-%s | %s", offsets.build, offsets.cl, runtime.status.c_str());
        line(buf);
        std::snprintf(buf, sizeof(buf), "GEngine %s  Viewport %s  World %s",
                      Hex(runtime.engine).c_str(), Hex(runtime.viewport).c_str(), Hex(runtime.world).c_str());
        line(buf);
        std::snprintf(buf, sizeof(buf), "GI %s match=%s  LP %s  PC %s",
                      Hex(runtime.game_instance).c_str(), runtime.gi_match ? "PASS" : "FAIL",
                      Hex(runtime.local_player).c_str(), Hex(runtime.player_controller).c_str());
        line(buf);
        std::snprintf(buf, sizeof(buf), "Pawn %s  PCM %s  W2S %s",
                      runtime.local_pawn_ok ? Hex(runtime.local_pawn).c_str() : "NOT SPAWNED",
                      Hex(runtime.camera_manager).c_str(),
                      runtime.w2s_ok ? "PASS" : "FAIL");
        line(buf);
        if (runtime.camera_ok) {
            std::snprintf(buf, sizeof(buf), "Cam %.1f %.1f %.1f | P/Y/R %.1f %.1f %.1f | FOV %.1f",
                          runtime.cam_loc.x, runtime.cam_loc.y, runtime.cam_loc.z,
                          runtime.cam_rot.pitch, runtime.cam_rot.yaw, runtime.cam_rot.roll,
                          runtime.cam_fov);
            line(buf);
        }
        if (runtime.local_pawn_ok) {
            std::snprintf(buf, sizeof(buf), "PawnXYZ %.1f %.1f %.1f | dist %.1f",
                          runtime.local_pos.x, runtime.local_pos.y, runtime.local_pos.z,
                          runtime.distance_to_cam);
            line(buf);
        }
    }

    // Local / camera screen marker (pawn when available; camera origin in lobby)
    if (runtime.w2s_ok && config.show_local_marker) {
        const ImU32 col = IM_COL32(
            (int)(config.col_local[0] * 255),
            (int)(config.col_local[1] * 255),
            (int)(config.col_local[2] * 255),
            (int)(config.col_local[3] * 255));
        const ImVec2 p(runtime.screen_x, runtime.screen_y);
        dl->AddCircleFilled(p, 5.f, col, 12);
        dl->AddCircle(p, 8.f, col, 12, 1.5f);
        dl->AddText(ImVec2(p.x + 10.f, p.y - 10.f), col,
                    runtime.local_pawn_ok ? "LOCAL PLAYER" : "CAMERA (lobby)");
        if (config.show_local_coords) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%.0f %.0f %.0f | %.0fm",
                          runtime.local_pos.x, runtime.local_pos.y, runtime.local_pos.z,
                          runtime.distance_to_cam / 100.f); // cm -> m-ish
            dl->AddText(ImVec2(p.x + 10.f, p.y + 6.f), IM_COL32(220, 220, 220, 220), buf);
        }
    }
}

void RunAim() {
    // Intentionally empty — no aim in this phase.
}

const char* StatusText() {
    return runtime.status.c_str();
}

} // namespace Fortnite
