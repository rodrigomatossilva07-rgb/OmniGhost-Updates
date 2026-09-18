#include "fortnite_game.h"
#include "../DMALibrary/Memory/Memory.h"
#include "platform/offset_source.h"
#include "imgui.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstring>
#include <Windows.h>

extern Memory mem;

namespace Fortnite {

Config config{};
Offsets offsets{};
Runtime runtime{};

static const char* kProcessCandidates[] = {
    "FortniteClient-Win64-Shipping.exe",
    "FortniteClient-Win64-Shipping_EAC_EOS.exe",
    "Fortnite.exe",
};

static bool g_logged_once = false;

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

// cheatoffsets decrypt_world: rotl64(encoded - sub, rol) ^ xor_key
static uintptr_t DecryptGWorld(uint64_t encoded) {
    if (!offsets.gworld_encoded)
        return static_cast<uintptr_t>(encoded);
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

void FlushDiagnosticsToLog() {
#if !defined(OMNIGHOST_VERBOSE_OFFSET_DIAGNOSTICS)
    // Throttle identical SUMMARY lines (Tick resolves every frame).
    static std::string last_status;
    static ULONGLONG last_ts = 0;
    const ULONGLONG now = GetTickCount64();
    if (runtime.status == last_status && (now - last_ts) < 2000)
        return;
    last_status = runtime.status;
    last_ts = now;
    // SessionLog owns the only logs.txt sink. Customer builds intentionally log
    // outcomes rather than addresses/RVAs or the full offset table.
    std::clog << "[FORTNITE] diagnostics=SUMMARY"
              << " attached=" << (runtime.attached ? "YES" : "NO")
              << " chain=" << (runtime.chain_ok ? "PASS" : "FAIL")
              << " camera=" << (runtime.camera_ok ? "PASS" : "FAIL")
              << " local_pawn=" << (runtime.local_pawn_ok ? "PASS" : "WAIT")
              << " w2s=" << (runtime.w2s_ok ? "PASS" : "FAIL")
              << " status=\"" << runtime.status << "\"\n";
#else
    std::ostringstream o;
    o << std::fixed << std::setprecision(3);
    o << "======== FORTNITE LOCALPLAYER DIAG " << offsets.build << "-CL-" << offsets.cl << " ========\n";
    o << "[FORTNITE] Build expected: " << offsets.build << "-CL-" << offsets.cl << "\n";
    o << "[ROOT] ModuleBase = " << Hex(runtime.base) << "\n";
    o << "[ROOT] GWorld RVA = " << Hex(offsets.gworld) << "\n";
    o << "[ROOT] GWorld address = " << Hex(runtime.base + offsets.gworld) << "\n";
    o << "[ROOT] GEngine RVA = " << Hex(offsets.gengine) << " (optional)\n";
    o << "[ROOT] World read success = " << (runtime.d_world.read_ok ? "YES" : "NO") << "\n";
    o << "[ROOT] World = " << Hex(runtime.world)
      << " canonical=" << (runtime.d_world.canonical ? "YES" : "NO") << "\n";
    if (runtime.d_world.fail)
        o << "[ROOT] World Fail = " << runtime.d_world.fail << "\n";
    if (runtime.engine)
        o << "[ROOT] Engine (fallback path) = " << Hex(runtime.engine) << "\n";

    o << "[CHAIN] GameViewport = " << Hex(runtime.viewport)
      << " read=" << (runtime.d_viewport.read_ok ? "OK" : "FAIL")
      << " canonical=" << (runtime.d_viewport.canonical ? "YES" : "NO") << "\n";
    o << "[CHAIN] World = " << Hex(runtime.world)
      << " read=" << (runtime.d_world.read_ok ? "OK" : "FAIL")
      << " canonical=" << (runtime.d_world.canonical ? "YES" : "NO") << "\n";
    o << "[CHAIN] Viewport.GameInstance = " << Hex(runtime.viewport_gi) << "\n";
    o << "[CHAIN] World.GameInstance = " << Hex(runtime.world_gi) << "\n";
    o << "[CHECK] GameInstance match = " << (runtime.gi_match ? "PASS" : "FAIL") << "\n";

    o << "[LOCAL] LocalPlayers.Data = " << Hex(runtime.local_players_data) << "\n";
    o << "[LOCAL] LocalPlayers.Count = " << runtime.local_players_count << "\n";
    o << "[LOCAL] LocalPlayer[0] = " << Hex(runtime.local_player) << "\n";
    o << "[LOCAL] PlayerController = " << Hex(runtime.player_controller) << "\n";
    o << "[LOCAL] AcknowledgedPawn = " << Hex(runtime.local_pawn)
      << (runtime.local_pawn_ok ? "" : " (NOT SPAWNED)") << "\n";

    o << "[CAMERA] PCM = " << Hex(runtime.camera_manager) << "\n";
    o << "[CAMERA] Location = "
      << runtime.cam_loc.x << ", " << runtime.cam_loc.y << ", " << runtime.cam_loc.z << "\n";
    o << "[CAMERA] Rotation = "
      << runtime.cam_rot.pitch << ", " << runtime.cam_rot.yaw << ", " << runtime.cam_rot.roll << "\n";
    o << "[CAMERA] FOV = " << runtime.cam_fov << "\n";
    o << "[CAMERA] valid = " << (runtime.camera_ok ? "PASS" : "FAIL") << "\n";

    if (runtime.local_pawn_ok) {
        o << "[PAWN] XYZ = "
          << runtime.local_pos.x << ", " << runtime.local_pos.y << ", " << runtime.local_pos.z << "\n";
        o << "[W2S] " << (runtime.w2s_ok ? "PASS" : "FAIL")
          << " screen=" << runtime.screen_x << "," << runtime.screen_y
          << " dist=" << runtime.distance_to_cam << "\n";
    }
    o << "[STATUS] " << runtime.status << "\n";
    o << "================================================================\n";

    const std::string block = o.str();
    std::clog << block;
#endif
}

static void ResolveLocalChain() {
    runtime.chain_ok = false;
    runtime.camera_ok = false;
    runtime.local_pawn_ok = false;
    runtime.w2s_ok = false;
    runtime.gi_match = false;
    runtime.engine = runtime.viewport = runtime.world = 0;
    runtime.viewport_gi = runtime.world_gi = runtime.game_instance = 0;
    runtime.local_players_data = 0;
    runtime.local_players_count = 0;
    runtime.local_player = runtime.player_controller = 0;
    runtime.local_pawn = runtime.root_component = runtime.camera_manager = 0;
    runtime.local_pos = {};
    runtime.cam_loc = {};
    runtime.cam_rot = {};
    runtime.cam_fov = 0.f;
    runtime.last_fail.clear();

    if (!runtime.base) {
        runtime.last_fail = "no module base";
        runtime.status = "FAIL: no module base";
        return;
    }

    // Prove process VA reads work (PE MZ at module base).
    uint16_t mz = 0;
    if (!mem.Read(runtime.base, &mz, sizeof(mz)) || mz != 0x5A4D) {
        runtime.last_fail = "module base not readable (MZ)";
        runtime.status = "FAIL: leitura do modulo falhou (base invalida ou DMA)";
        return;
    }

    const uint32_t image_size = ReadPeSizeOfImage(runtime.base);

    // -------------------------------------------------------------------------
    // PRIMARY ROOT — cheatoffsets 42.00: GWorld plain pointer.
    // Capware CL-57316517 UWorld members applied after pointer resolves.
    // OPTIONAL fallback: GEngine -> GameViewport -> World when GWorld fails.
    // -------------------------------------------------------------------------
    bool have_world = false;
    if (offsets.gworld) {
        if (image_size && offsets.gworld >= image_size) {
            runtime.last_fail = "GWorld RVA outside SizeOfImage";
            runtime.status = "FAIL GWorld: RVA fora da imagem (offsets/modulo)";
            return;
        }
        // Encoded path: read u64 at GWorld RVA, then decrypt_world().
        // Plain path: treat value as pointer directly (gworld_encoded=false).
        const uintptr_t slot = runtime.base + offsets.gworld;
        uint64_t encoded = 0;
        size_t got = 0;
        const bool slot_ok = mem.ReadWithCount(slot, &encoded, sizeof(encoded), got) && got == sizeof(encoded);
        if (!slot_ok)
            (void)mem.Read(slot, &encoded, sizeof(encoded));

        runtime.d_world.address = slot;
        runtime.d_world.bytes_read = got ? got : (encoded ? sizeof(encoded) : 0);
        runtime.d_world.read_ok = slot_ok || (encoded != 0);

        if (runtime.d_world.read_ok) {
            const uintptr_t world = offsets.gworld_encoded
                ? DecryptGWorld(encoded)
                : static_cast<uintptr_t>(encoded);
            runtime.d_world.value = world;
            runtime.d_world.canonical = IsCanonicalUserPtr(world);
            if (!runtime.d_world.canonical)
                runtime.d_world.fail = offsets.gworld_encoded ? "decrypt non-canonical" : "non-canonical pointer";
            else {
                runtime.world = world;
                have_world = true;
            }
        } else {
            runtime.d_world.fail = "READ FAILED";
        }
    }
    // Fallback: GEngine -> GameViewport -> World (cheatoffsets documents this path).
    if (!have_world && offsets.gengine) {
        runtime.d_engine = ReadPtrDiag(runtime.base + offsets.gengine);
        if (runtime.d_engine.read_ok && runtime.d_engine.canonical) {
            runtime.engine = runtime.d_engine.value;
            runtime.d_viewport = ReadPtrDiag(runtime.engine + offsets.engine_game_viewport);
            if (runtime.d_viewport.read_ok && runtime.d_viewport.canonical) {
                runtime.viewport = runtime.d_viewport.value;
                runtime.d_world = ReadPtrDiag(runtime.viewport + offsets.viewport_world);
                if (runtime.d_world.read_ok && runtime.d_world.canonical) {
                    runtime.world = runtime.d_world.value;
                    have_world = true;
                }
            }
        }
    }
    if (!have_world) {
        if (offsets.gworld && runtime.d_world.read_ok && !runtime.d_world.canonical) {
            runtime.last_fail = runtime.d_world.fail ? runtime.d_world.fail : "GWorld non-canonical";
            runtime.status = std::string("FAIL GWorld: ") + runtime.last_fail;
        } else if (offsets.gworld) {
            runtime.last_fail = runtime.d_world.fail ? runtime.d_world.fail : "READ FAILED";
            runtime.status = std::string("FAIL GWorld: ") + runtime.last_fail;
        } else {
            runtime.last_fail = "GWorld offset missing";
            runtime.status = "FAIL: GWorld offset em falta";
        }
        return;
    }

    // GameInstance from World (official). Viewport GI is optional cross-check only.
    runtime.d_gi = ReadPtrDiag(runtime.world + offsets.world_owning_game_instance);
    if (!runtime.d_gi.read_ok || !runtime.d_gi.canonical) {
        runtime.last_fail = runtime.d_gi.fail ? runtime.d_gi.fail : "GameInstance";
        runtime.status = std::string("FAIL GameInstance: ") + runtime.last_fail;
        return;
    }
    runtime.game_instance = runtime.d_gi.value;
    runtime.world_gi = runtime.game_instance;
    if (runtime.viewport && offsets.viewport_game_instance) {
        uintptr_t vgi = 0;
        if (ReadU64(runtime.viewport + offsets.viewport_game_instance, vgi) &&
            IsCanonicalUserPtr(vgi)) {
            runtime.viewport_gi = vgi;
            runtime.gi_match = (vgi == runtime.world_gi);
        }
    } else {
        runtime.gi_match = true; // N/A on pure GWorld path
    }

    // LocalPlayers TArray: data @ +0, count @ +8
    uintptr_t lp_data = 0;
    int32_t lp_count = 0;
    if (!ReadU64(runtime.game_instance + offsets.gi_local_players, lp_data) ||
        !IsCanonicalUserPtr(lp_data)) {
        runtime.last_fail = "LocalPlayers.Data invalid";
        runtime.status = "FAIL LocalPlayers.Data";
        return;
    }
    if (!ReadI32(runtime.game_instance + offsets.gi_local_players + 8, lp_count) ||
        lp_count < 1 || lp_count > 8) {
        runtime.last_fail = "LocalPlayers.Count invalid";
        runtime.status = "FAIL LocalPlayers.Count";
        return;
    }
    runtime.local_players_data = lp_data;
    runtime.local_players_count = lp_count;

    uintptr_t local_player = 0;
    if (!ReadU64(lp_data, local_player) || !IsCanonicalUserPtr(local_player)) {
        runtime.last_fail = "LocalPlayer[0] invalid";
        runtime.status = "FAIL LocalPlayer[0]";
        return;
    }
    runtime.local_player = local_player;
    runtime.d_lp.address = lp_data;
    runtime.d_lp.value = local_player;
    runtime.d_lp.read_ok = true;
    runtime.d_lp.canonical = true;
    runtime.d_lp.bytes_read = 8;

    // PlayerController
    runtime.d_pc = ReadPtrDiag(local_player + offsets.local_player_controller);
    if (!runtime.d_pc.read_ok || !runtime.d_pc.canonical) {
        runtime.last_fail = runtime.d_pc.fail ? runtime.d_pc.fail : "PlayerController";
        runtime.status = std::string("FAIL PlayerController: ") + runtime.last_fail;
        return;
    }
    runtime.player_controller = runtime.d_pc.value;

    // Camera manager (required for W2S / lobby camera)
    runtime.d_pcm = ReadPtrDiag(runtime.player_controller + offsets.pc_player_camera_manager);
    if (!runtime.d_pcm.read_ok || !runtime.d_pcm.canonical) {
        runtime.last_fail = runtime.d_pcm.fail ? runtime.d_pcm.fail : "PlayerCameraManager";
        runtime.status = std::string("FAIL PCM: ") + runtime.last_fail;
        return;
    }
    runtime.camera_manager = runtime.d_pcm.value;

    // Camera POV fields
    FVectorD cam_loc{};
    FRotatorD cam_rot{};
    float fov = 0.f;
    const bool loc_ok = ReadFVectorD(runtime.camera_manager + offsets.pcm_cam_location, cam_loc);
    const bool rot_ok = ReadFRotatorD(runtime.camera_manager + offsets.pcm_cam_rotation, cam_rot);
    const bool fov_ok = ReadFloat(runtime.camera_manager + offsets.pcm_cam_fov, fov);
    runtime.cam_loc = cam_loc;
    runtime.cam_rot = cam_rot;
    runtime.cam_fov = fov;
    runtime.camera_ok = loc_ok && rot_ok && fov_ok && fov > 20.f && fov < 170.f;
    if (!runtime.camera_ok) {
        runtime.last_fail = "camera Location/Rotation/FOV invalid";
        // still mark chain partially ok for PC/World
        runtime.chain_ok = true;
        runtime.status = "CHAIN OK | CAMERA FAIL";
        return;
    }

    // AcknowledgedPawn — may be null in lobby
    uintptr_t pawn = 0;
    const bool pawn_read = ReadU64(runtime.player_controller + offsets.pc_acknowledged_pawn, pawn);
    if (!pawn_read || !IsCanonicalUserPtr(pawn)) {
        runtime.local_pawn = 0;
        runtime.local_pawn_ok = false;
        runtime.chain_ok = true;
        runtime.status = "CHAIN OK | LOCAL PAWN: NOT SPAWNED";
        return;
    }
    runtime.local_pawn = pawn;

    uintptr_t root = 0;
    if (!ReadU64(pawn + offsets.actor_root_component, root) || !IsCanonicalUserPtr(root)) {
        runtime.local_pawn_ok = false;
        runtime.chain_ok = true;
        runtime.status = "CHAIN OK | RootComponent FAIL";
        return;
    }
    runtime.root_component = root;

    FVectorD pos{};
    if (!ReadFVectorD(root + offsets.scene_relative_location, pos)) {
        runtime.local_pawn_ok = false;
        runtime.chain_ok = true;
        runtime.status = "CHAIN OK | LocalPos FAIL";
        return;
    }
    runtime.local_pos = pos;
    runtime.local_pawn_ok = true;
    runtime.chain_ok = true;

    // Distance to camera
    const double dx = pos.x - cam_loc.x;
    const double dy = pos.y - cam_loc.y;
    const double dz = pos.z - cam_loc.z;
    runtime.distance_to_cam = static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));

    runtime.status = "CHAIN OK | LOCAL PAWN OK | CAMERA OK";
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

    // GWorld crypto (encoded by default when dump provides keys)
    {
        std::uint64_t sub = 0, xor_key = 0, rol = 0;
        if (TryGetAny(loaded.snapshot, {"gworld_crypto.sub", "gworld_crypto.sub_decimal"}, sub) && sub)
            next.gworld_sub = sub;
        if (TryGetAny(loaded.snapshot, {"gworld_crypto.xor_key"}, xor_key) && xor_key)
            next.gworld_xor = xor_key;
        if (TryGetAny(loaded.snapshot, {"gworld_crypto.rol_amt"}, rol) && rol)
            next.gworld_rol = static_cast<uint32_t>(rol);
        // Presence of crypto block => encoded; note field is boolean-ish in JSON
        std::uint64_t enc_flag = 0;
        if (TryGetAny(loaded.snapshot, {"gworld_crypto.encoded"}, enc_flag))
            next.gworld_encoded = (enc_flag != 0);
        else if (sub || xor_key)
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
    set({"GameStateBase.PlayerArray", "AGameStateBase.PlayerArray"}, next.game_state_player_array);
    set({"FortPawn.CurrentWeapon", "AFortPawn.CurrentWeapon"}, next.fort_pawn_current_weapon);
    set({"FortPlayerStateAthena.TeamIndex", "AFortPlayerStateAthena.TeamIndex"}, next.fort_ps_team_index);

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
              << " GEngine=0x" << offsets.gengine
              << " build=" << offsets.build << "-CL-" << offsets.cl << std::dec << "\n";
    return true;
}

bool ReloadOffsets() {
    return LoadOffsetsFromJson(nullptr);
}

bool Attach() {
    runtime = Runtime{};
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

    ResolveLocalChain();
    FlushDiagnosticsToLog();
    g_logged_once = true;
    return true;
}

void Detach() {
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
    ++runtime.frames;

    // Resolve every frame (lightweight pointer chain)
    ResolveLocalChain();

    // W2S using overlay display size
    runtime.w2s_ok = false;
    if (runtime.local_pawn_ok && runtime.camera_ok) {
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
    if (!g_logged_once || (now - last_log > 5000 && !runtime.chain_ok)) {
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

    // Local player screen marker
    if (runtime.local_pawn_ok && runtime.w2s_ok && config.show_local_marker) {
        const ImU32 col = IM_COL32(
            (int)(config.col_local[0] * 255),
            (int)(config.col_local[1] * 255),
            (int)(config.col_local[2] * 255),
            (int)(config.col_local[3] * 255));
        const ImVec2 p(runtime.screen_x, runtime.screen_y);
        dl->AddCircleFilled(p, 5.f, col, 12);
        dl->AddCircle(p, 8.f, col, 12, 1.5f);
        dl->AddText(ImVec2(p.x + 10.f, p.y - 10.f), col, "LOCAL PLAYER");
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

bool ReinitDma() {
    std::cout << "[Fortnite] Reinit DMA" << std::endl;
    Detach();
    return Attach();
}

} // namespace Fortnite
