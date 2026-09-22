#include "warzone_game.h"
#include "warzone_aim.h"
#include "warzone_esp.h"
#include "warzone_decrypt.h"
#include "../src/platform/match_lifecycle.h"
#include "../src/platform/embedded_offsets.h"
#include "../DMALibrary/Memory/Memory.h"
#include <Windows.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <cmath>
#include <cstring>
 #include <algorithm>
#include <limits>

namespace fs = std::filesystem;

extern Memory mem;

namespace Warzone {

Offsets offsets{};
Runtime runtime{};
Config config{};
bool ready = false;
std::string status = "Warzone idle";
std::atomic_bool backend_busy{false};

namespace {

std::string ExeDir() {
    char buf[MAX_PATH]{};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path().string();
}

bool JsonHex(const std::string& src, const char* key, uintptr_t& out) {
    const std::string pat = std::string("\"") + key + "\"";
    size_t p = src.find(pat);
    if (p == std::string::npos) return false;
    p = src.find(':', p + pat.size());
    if (p == std::string::npos) return false;
    ++p;
    while (p < src.size() && (src[p] == ' ' || src[p] == '\t' || src[p] == '"')) ++p;
    char* end = nullptr;
    unsigned long long v = strtoull(src.c_str() + p, &end, 0);
    if (end == src.c_str() + p) return false;
    out = (uintptr_t)v;
    return true;
}

bool LooksLikeMatrix(const float* m) {
    int finite = 0;
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(m[i])) return false;
        if (std::fabs(m[i]) > 1e-6f) ++finite;
    }
    return finite >= 6;
}

} // namespace

namespace {

bool PopulateFromEmbedded(const OmniGhost::EmbeddedOffsets::Snapshot& snapshot) {
    Offsets next{};
    auto get = [&](std::string_view key, uintptr_t& output, bool required = false) {
        std::uint64_t value = 0;
        const bool ok = snapshot.TryGet(key, value);
        if (ok) output = static_cast<uintptr_t>(value);
        return ok || !required;
    };
    auto getInt = [&](std::string_view key, int& output) {
        std::uint64_t value = 0;
        if (!snapshot.TryGet(key, value) || value > static_cast<std::uint64_t>((std::numeric_limits<int>::max)())) return false;
        output = static_cast<int>(value);
        return true;
    };

    bool required = true;
    required &= get("offsets.timestamp", next.timestamp, true);
    required &= get("offsets.ref_def", next.ref_def, true);
    required &= get("offsets.name_array", next.name_array, true);
    required &= get("offsets.camera_base", next.camera_base, true);
    get("offsets.game_mode", next.game_mode);
    required &= get("offsets.view_matrix", next.view_matrix, true);
    get("offsets.entity_list", next.entity_list);
    get("offsets.entity_size", next.entity_size);
    get("offsets.max_player_count", next.max_player_count);
    get("offsets.loot_ptr", next.loot_ptr);
    get("offsets.distribute", next.distribute);
    get("offsets.lobby_data", next.lobby_data);

    required &= get("decrypt_rva.client_info_enc", next.client_info_enc, true);
    required &= get("decrypt_rva.client_info_key", next.client_info_key, true);
    required &= get("decrypt_rva.client_base_enc", next.client_base_enc, true);
    required &= get("decrypt_rva.client_base_key", next.client_base_key, true);
    required &= get("decrypt_rva.bone_enc", next.bone_enc, true);
    required &= get("decrypt_rva.bone_key", next.bone_key, true);
    get("decrypt_rva.stub_client_info", next.stub_client_info);
    get("decrypt_rva.stub_client_base", next.stub_client_base);
    get("decrypt_rva.stub_bone_base", next.stub_bone_base);

    required &= get("camera.pos", next.camera_pos, true);
    get("camera.pos_enc", next.camera_pos_enc);
    get("camera.pos_key", next.camera_pos_key);

    required &= get("local.index", next.local_index_off, true);
    required &= get("local.index_pos", next.local_index_pos, true);
    get("local.visible_client_bits", next.visible_client_bits);
    get("local.recoil", next.recoil);
    get("local.scoreboard", next.scoreboard);

    required &= get("player.size", next.player_size, true);
    required &= get("player.valid", next.player_valid, true);
    required &= get("player.pos", next.player_pos, true);
    required &= get("player.pos_data", next.player_pos_data, true);
    required &= get("player.team", next.player_team, true);
    get("player.stance", next.player_stance);
    required &= get("player.health", next.player_health, true);
    get("player.weapon_index", next.player_weapon_index);

    required &= get("bone.base_pos", next.bone_base_pos, true);
    required &= get("bone.size", next.bone_size, true);
    required &= get("bone.offset", next.bone_offset, true);
    getInt("bone.head", next.bone_head);
    getInt("bone.chest", next.bone_chest);

    required &= get("name_array.pos", next.name_array_pos, true);
    required &= get("name_array.size", next.name_entry_size, true);
    required &= get("name_array.entry_name", next.name_entry_name, true);
    required &= get("name_array.entry_health", next.name_entry_health, true);
    required &= get("name_array.entry_alive", next.name_entry_alive, true);

    next.client_info = next.client_info_enc;
    next.client_base = next.client_base_enc;
    next.bone_base = next.bone_enc;
    next.local_index = next.local_index_off;
    next.ref_def_ptr = next.ref_def;
    next.loaded = required;
    std::snprintf(next.source, sizeof(next.source), "%s", required ? "embedded" : "embedded-invalid");
    offsets = next;
    return required;
}

#if defined(OMNIGHOST_DEV_EXTERNAL_OFFSETS)
bool LoadOffsetsFromExternalJson(const char* path) {
    std::string p;
    if (path && path[0]) p = path;
    else {
        p = ExeDir() + "\\data\\warzone_offsets.json";
        if (!fs::exists(p)) p = ExeDir() + "\\warzone_offsets.json";
    }
    std::ifstream in(p);
    if (!in) return false;
    std::stringstream ss; ss << in.rdbuf();
    const std::string src = ss.str();
    Offsets next{};
    auto grab = [&](const char* k, uintptr_t& o) { JsonHex(src, k, o); };
    grab("timestamp", next.timestamp); grab("ref_def", next.ref_def); grab("name_array", next.name_array);
    grab("camera_base", next.camera_base); grab("game_mode", next.game_mode); grab("view_matrix", next.view_matrix);
    grab("entity_list", next.entity_list); grab("entity_size", next.entity_size); grab("max_player_count", next.max_player_count);
    grab("loot_ptr", next.loot_ptr); grab("distribute", next.distribute); grab("lobby_data", next.lobby_data);
    grab("client_info_enc", next.client_info_enc); grab("client_info_key", next.client_info_key);
    grab("client_base_enc", next.client_base_enc); grab("client_base_key", next.client_base_key);
    grab("bone_enc", next.bone_enc); grab("bone_key", next.bone_key);
    grab("stub_client_info", next.stub_client_info); grab("stub_client_base", next.stub_client_base); grab("stub_bone_base", next.stub_bone_base);
    grab("index", next.local_index_off); grab("index_pos", next.local_index_pos); grab("visible_client_bits", next.visible_client_bits); grab("recoil", next.recoil);
    grab("entry_name", next.name_entry_name); grab("entry_health", next.name_entry_health); grab("entry_alive", next.name_entry_alive);
    grab("pos_data", next.player_pos_data); grab("weapon_index", next.player_weapon_index); grab("base_pos", next.bone_base_pos);
    next.loaded = next.view_matrix != 0 && next.name_array != 0 && next.client_info_enc != 0 && next.camera_base != 0;
    if (!next.loaded) return false;
    std::snprintf(next.source, sizeof(next.source), "external-dev");
    offsets = next;
    return true;
}
#endif

} // namespace

bool LoadOffsetsFromJson(const char* path) {
#if defined(OMNIGHOST_DEV_EXTERNAL_OFFSETS)
    if (path && path[0] && LoadOffsetsFromExternalJson(path))
        return true;
#else
    (void)path;
#endif
    OmniGhost::EmbeddedOffsets::Snapshot snapshot;
    OmniGhost::EmbeddedOffsets::Diagnostics diag;
    const bool resourceOk = OmniGhost::EmbeddedOffsets::Load(
        OmniGhost::EmbeddedOffsets::Game::Warzone, snapshot, diag);

    std::cout << "[OFFSETS] storage=EMBEDDED\n"
              << "[OFFSETS] game=Warzone\n"
              << "[OFFSETS] resource=" << (diag.resourceFound ? "FOUND" : "MISSING") << "\n"
              << "[OFFSETS] resource_size=" << diag.resourceSize << "\n"
              << "[OFFSETS] header=" << (diag.headerValid ? "PASS" : "FAIL") << "\n"
              << "[OFFSETS] size=" << (diag.sizeValid ? "PASS" : "FAIL") << "\n"
              << "[OFFSETS] decompress=" << (diag.decompressed ? "PASS" : "FAIL") << "\n"
              << "[OFFSETS] integrity=" << (diag.integrityValid ? "PASS" : "FAIL") << "\n"
              << "[OFFSETS] parse=" << (diag.parsed ? "PASS" : "FAIL") << "\n";
    if (!resourceOk) {
        offsets = {};
        std::snprintf(offsets.source, sizeof(offsets.source), "embedded-failed");
        std::cout << "[OFFSETS] activation_allowed=FALSE error=" << diag.error << "\n";
#if defined(OMNIGHOST_DEV_EXTERNAL_OFFSETS)
        if ((!path || !path[0]) && LoadOffsetsFromExternalJson(nullptr)) {
            std::cout << "[OFFSETS] development_external_fallback=PASS\n";
            return true;
        }
#endif
        return false;
    }

    std::cout << "[OFFSETS] build=" << snapshot.metadata.build << "\n";
    if (!snapshot.metadata.cl.empty()) std::cout << "[OFFSETS] cl=" << snapshot.metadata.cl << "\n";
    const bool snapshotIdentityOk = OmniGhost::EmbeddedOffsets::MetadataMatches(
        snapshot, "warzone", "Steam 0x6A6D4AEF", "", "cod.exe");
    std::cout << "[OFFSETS] snapshot_identity=" << (snapshotIdentityOk ? "PASS" : "FAIL") << "\n";
    if (!snapshotIdentityOk) {
        offsets = {};
        std::snprintf(offsets.source, sizeof(offsets.source), "embedded-identity-failed");
        std::cout << "[OFFSETS] schema=FAIL\n[OFFSETS] activation_allowed=FALSE\n";
        return false;
    }
    const bool populated = PopulateFromEmbedded(snapshot);
    std::cout << "[OFFSETS] schema=" << (populated ? "PASS" : "FAIL") << "\n"
              << "[OFFSETS] activation_allowed=" << (populated ? "PENDING_RUNTIME_PROBE" : "FALSE") << "\n";
    return populated;
}

bool ReloadOffsets() {
    return LoadOffsetsFromJson(nullptr);
}

bool Attach() {
    backend_busy = true;
    ready = false;
    runtime = {};
    status = "Warzone: a abrir DMA...";

    // Device first (same as FiveM/CS2)
    if (!mem.Init(std::string(), true, false)) {
        status = "Warzone: falha FPGA/DMA";
        std::cout << "[Warzone] " << status << std::endl;
        backend_busy = false;
        return false;
    }
    std::cout << "[Warzone] DMA device OK" << std::endl;

    // Canonical CoD process names (avoid duplicate case variants that double the FixCr3 cost)
    static const char* kNames[] = { "cod.exe", "ModernWarfare.exe", "iw8_ship.exe", "COD.exe", nullptr };

    DWORD found_pid = 0;
    const char* found_name = nullptr;
    for (int i = 0; kNames[i]; ++i) {
        DWORD p = mem.GetPidFromName(kNames[i]);
        if (p) {
            found_pid = p;
            found_name = kNames[i];
            break;
        }
    }

    if (!found_pid) {
        status = "Warzone: cod.exe nao encontrado — abre o jogo";
        std::cout << "[Warzone] " << status << std::endl;
        backend_busy = false;
        ready = true; // UI stays open; RunFrame retries with cooldown
        runtime.decrypt_needed = true;
        std::snprintf(runtime.status, sizeof(runtime.status), "%s", status.c_str());
        return true;
    }

    // PID exists — attempt full bind (FixCr3 + MZ). Distinguish "not found" from "CR3/MZ fail".
    bool attached = false;
    if (mem.Init(found_name, false, false)) {
        attached = true;
        std::cout << "[Warzone] Anexado a " << found_name << " pid=" << found_pid << std::endl;
    } else {
        // One more try with alternate casing only if primary failed
        if (found_name && _stricmp(found_name, "cod.exe") == 0) {
            if (mem.Init("COD.exe", false, false)) {
                attached = true;
                found_name = "COD.exe";
                std::cout << "[Warzone] Anexado a COD.exe pid=" << found_pid << std::endl;
            }
        }
    }

    if (!attached) {
        char buf[160];
        std::snprintf(buf, sizeof(buf),
            "Warzone: cod.exe PID %u encontrado, CR3/MZ ainda invalido — a tentar",
            (unsigned)found_pid);
        status = buf;
        std::cout << "[Warzone] " << status << std::endl;
        backend_busy = false;
        ready = true;
        runtime.decrypt_needed = true;
        runtime.last_bind_attempt_ms = GetTickCount64();
        std::snprintf(runtime.status, sizeof(runtime.status), "%s", status.c_str());
        return true; // UI open; RunFrame will retry on cooldown
    }

    runtime.module_base = mem.GetBaseDaddy(found_name ? found_name : "cod.exe");
    if (!runtime.module_base)
        runtime.module_base = mem.GetBaseDaddy("cod.exe");
    if (!runtime.module_base)
        runtime.module_base = mem.GetBaseDaddy("COD.exe");
    if (!runtime.module_base)
        runtime.module_base = mem.GetBaseDaddy("ModernWarfare.exe");

    if (!LoadOffsetsFromJson(nullptr)) {
        status = "Warzone: recurso de offsets embedded indisponivel/invalido";
        std::cout << "[Warzone] " << status << std::endl;
    } else {
#if defined(OMNIGHOST_VERBOSE_OFFSET_DIAGNOSTICS)
        std::cout << "[Warzone] Offsets OK view_matrix=0x" << std::hex << offsets.view_matrix
                  << " entity_list=0x" << offsets.entity_list << std::dec << std::endl;
#else
        std::cout << "[Warzone] Offsets embedded carregados; runtime probe pendente" << std::endl;
#endif
    }

    // Quick MZ/base validation. Getting a valid module base is only the attach stage;
    // the next stage is validating that the shipped offsets actually read sensible data.
    bool module_mz_ok = false;
    size_t module_size = 0;
    if (runtime.module_base) {
        uint16_t mz = 0;
        module_mz_ok = mem.Read(runtime.module_base, &mz, sizeof(mz)) && mz == 0x5A4D;
        if (module_mz_ok) {
            module_size = mem.GetBaseSize(found_name ? found_name : "cod.exe");
            if (!module_size) module_size = mem.GetBaseSize("cod.exe");
            std::cout << "[Warzone] Module base OK 0x" << std::hex << runtime.module_base
                      << " size=0x" << module_size << std::dec << std::endl;
            // PE TimeDateStamp — must match offsets.timestamp (Steam 0x6A6D4AEF) or decrypt RVAs are stale.
            uint32_t pe_ts = 0;
            uint32_t e_lfanew = 0;
            if (mem.Read(runtime.module_base + 0x3C, &e_lfanew, sizeof(e_lfanew)) &&
                e_lfanew > 0 && e_lfanew < 0x1000 &&
                mem.Read(runtime.module_base + e_lfanew + 8, &pe_ts, sizeof(pe_ts))) {
                const uint32_t expect = offsets.timestamp ? (uint32_t)offsets.timestamp : 0x6A6D4AEFu;
                std::cout << "[Warzone] PE TimeDateStamp=0x" << std::hex << pe_ts
                          << " expected=0x" << expect << std::dec
                          << (pe_ts == expect ? " MATCH" : " MISMATCH — atualiza offsets/decrypt")
                          << std::endl;
            }
        } else {
            std::cout << "[Warzone] Module MZ fail — CR3/base pode estar errado" << std::endl;
            runtime.decrypt_needed = true;
        }
    }

    bool matrix_read_ok = false;
    bool matrix_plausible = false;
    bool matrix_offset_in_image = true;
    if (module_mz_ok && offsets.loaded && offsets.view_matrix) {
        if (module_size) {
            matrix_offset_in_image = offsets.view_matrix < module_size &&
                offsets.view_matrix + sizeof(runtime.view_matrix) <= module_size;
        }

        if (matrix_offset_in_image) {
            float vm[16]{};
            matrix_read_ok = mem.Read(runtime.module_base + offsets.view_matrix, vm, sizeof(vm));
            matrix_plausible = matrix_read_ok && LooksLikeMatrix(vm);
            if (matrix_plausible) {
                std::memcpy(runtime.view_matrix, vm, sizeof(vm));
                runtime.matrix_ok = true;
            }
        }
    }

    // This backend currently has the attach/base and raw offset probes wired, but the
    // encrypted entity/client layout is intentionally reported as a separate stage.
    // Do not make a valid module base look like a hang.
    runtime.decrypt_needed = offsets.entity_list != 0 || offsets.client_info != 0 || offsets.client_base != 0;
    ready = true;
    runtime.last_bind_attempt_ms = GetTickCount64();

    if (!runtime.module_base || !module_mz_ok) {
        status = "Warzone: module base invalido";
        std::snprintf(runtime.status, sizeof(runtime.status), "%s", status.c_str());
    } else if (!offsets.loaded) {
        status = "Warzone: module OK; offsets em falta";
        std::snprintf(runtime.status, sizeof(runtime.status), "%s", status.c_str());
    } else if (!matrix_offset_in_image) {
        status = "Warzone: module OK; view_matrix fora do modulo — offsets incompatíveis";
        std::snprintf(runtime.status, sizeof(runtime.status), "%s", status.c_str());
        std::cout << "[Warzone] Falha de offsets: view_matrix fica fora da imagem atual de cod.exe" << std::endl;
    } else if (!matrix_read_ok) {
        status = "Warzone: module OK; view_matrix nao legivel — partida/offsets";
        std::snprintf(runtime.status, sizeof(runtime.status), "%s", status.c_str());
        std::cout << "[Warzone] Backend ativo: module base OK; view_matrix ainda nao legivel" << std::endl;
    } else if (!matrix_plausible) {
        status = "Warzone: module OK; matriz ainda invalida — entra numa partida/verifica offsets";
        std::snprintf(runtime.status, sizeof(runtime.status), "%s", status.c_str());
        std::cout << "[Warzone] Backend ativo: module base OK; a aguardar matriz valida" << std::endl;
    } else {
        status = "Warzone: base+matrix OK; entidades aguardam decrypt/layout";
        std::snprintf(runtime.status, sizeof(runtime.status), "%s", status.c_str());
        std::cout << "[Warzone] Backend ativo: module base + view matrix OK" << std::endl;
    }

    std::clog << "[DEBUG][Warzone] attach_probe base=0x" << std::hex << runtime.module_base
              << " size=0x" << module_size
#if defined(OMNIGHOST_VERBOSE_OFFSET_DIAGNOSTICS)
              << " view_matrix_rva=0x" << offsets.view_matrix << std::dec
#endif
              << std::dec
              << " mz=" << (module_mz_ok ? "OK" : "FAIL")
              << " matrix_offset=" << (matrix_offset_in_image ? "IN_IMAGE" : "OUT_OF_IMAGE")
              << " matrix_read=" << (matrix_read_ok ? "OK" : "FAIL")
              << " matrix_shape=" << (matrix_plausible ? "OK" : "FAIL")
              << " decrypt_layout=" << (runtime.decrypt_needed ? "PENDING" : "N/A")
              << std::endl;

    backend_busy = false;
    return true;
}

bool InitializeMenuShell() {
    // Prefer real attach when called from launcher
    return Attach();
}

bool IsGameProcessAlive() {
    DWORD pid = mem.GetPidFromName("cod.exe");
    if (!pid) pid = mem.GetPidFromName("COD.exe");
    if (!pid) pid = mem.GetPidFromName("ModernWarfare.exe");
    return pid != 0;
}


// Lobby-safe: module base is MZ and view_matrix RVA sits inside the image.
// Full matrix validity may wait for a match — that is NOT treated as outdated offsets.
bool SoftProbeLobbyOffsets() {
    if (!ready || !runtime.module_base) {
        std::cout << "[Warzone] SoftProbe: module not ready\n";
        return false;
    }
    uint16_t mz = 0;
    if (!mem.Read(runtime.module_base, &mz, sizeof(mz)) || mz != 0x5A4D) {
        std::cout << "[Warzone] SoftProbe: MZ fail\n";
        return false;
    }
    if (!offsets.view_matrix) {
        std::cout << "[Warzone] SoftProbe: view_matrix RVA=0\n";
        return false;
    }
    // PE SizeOfImage
    uint32_t pe_off = 0, size = 0;
    mem.Read(runtime.module_base + 0x3C, &pe_off, sizeof(pe_off));
    if (pe_off && pe_off < 0x1000)
        mem.Read(runtime.module_base + pe_off + 0x50, &size, sizeof(size));
    const bool in_image = !size || (offsets.view_matrix < size);
    // Readable slot (may be zeros in lobby)
    float vm[16]{};
    const bool readable = mem.Read(runtime.module_base + offsets.view_matrix, vm, sizeof(vm));
    const bool ok = in_image && readable;
    std::cout << "[Warzone] SoftProbe MZ=OK in_image=" << (in_image ? 1 : 0)
              << " readable=" << (readable ? 1 : 0)
              << " => " << (ok ? "PASS" : "FAIL") << std::endl;
    return ok;
}

bool ValidateLiveOffsets() {
    // Use soft probe for lobby-safe validation; if that passes, offsets are not outdated.
    return SoftProbeLobbyOffsets();
}

void RunFrame() {
    if (!ready) return;
    ++runtime.frames;

    // Retry bind only on cooldown. FixCr3 + procinfo costs ~3s; never spam every 60 frames.
    // Cooldown grows with consecutive failures so the menu stays responsive.
    if (!runtime.module_base) {
        const uint64_t now = GetTickCount64();
        const uint64_t cooldown_ms = 4000ull + (uint64_t)(std::min)(runtime.bind_fail_count, 8) * 1500ull;
        const bool cooldown_ok = (runtime.last_bind_attempt_ms == 0) ||
            (now - runtime.last_bind_attempt_ms >= cooldown_ms);

        if (cooldown_ok) {
            DWORD pid = mem.GetPidFromName("cod.exe");
            if (!pid) pid = mem.GetPidFromName("COD.exe");
            if (!pid) pid = mem.GetPidFromName("ModernWarfare.exe");

            if (!pid) {
                std::snprintf(runtime.status, sizeof(runtime.status),
                    "wait: cod.exe nao encontrado");
            } else {
                runtime.last_bind_attempt_ms = now;
                const bool ok = mem.Init("cod.exe", false, false) ||
                                mem.Init("COD.exe", false, false);
                if (ok) {
                    runtime.module_base = mem.GetBaseDaddy("cod.exe");
                    if (!runtime.module_base) runtime.module_base = mem.GetBaseDaddy("COD.exe");
                    if (!runtime.module_base) runtime.module_base = mem.GetBaseDaddy("ModernWarfare.exe");
                    runtime.bind_fail_count = 0;
                    std::cout << "[Warzone] Process bind late base=0x" << std::hex
                              << runtime.module_base << std::dec << std::endl;
                    if (!offsets.loaded)
                        LoadOffsetsFromJson(nullptr);
                } else {
                    ++runtime.bind_fail_count;
                    std::snprintf(runtime.status, sizeof(runtime.status),
                        "PID %u ok, CR3/MZ fail (tentativa %d, cooldown %llus)",
                        (unsigned)pid, runtime.bind_fail_count,
                        (unsigned long long)(cooldown_ms / 1000ull));
                    std::cout << "[Warzone] " << runtime.status << std::endl;
                }
            }
        }
    }

    if (!runtime.module_base || !offsets.loaded) {
        if (runtime.module_base == 0 && runtime.status[0] == '\0') {
            std::snprintf(runtime.status, sizeof(runtime.status),
                "wait module/offsets base=0x%llX off=%d",
                (unsigned long long)runtime.module_base, offsets.loaded ? 1 : 0);
        }
        runtime.in_game = false;
        runtime.matrix_ok = false;
        runtime.list_ok = false;
        return;
    }

    // --- Performance pipeline (CS2/FiveM style) ---
    // Matrix: every frame (cheap single read).
    // Decrypt + player enum: every N frames via scatter, keep last good list between refreshes.
    static DecryptState s_dec{};
    static bool s_dec_ok = false;
    static uint64_t s_last_enum_ms = 0;
    static std::vector<Player> s_cached_players;
    static int s_cached_local_index = -1;
    static int s_cached_local_team = 0;
    static float s_cached_local_pos[3]{};

    runtime.matrix_ok = false;
    bool matrix_read_ok = false;
    if (offsets.view_matrix) {
        float vm[16]{};
        matrix_read_ok = mem.Read(runtime.module_base + offsets.view_matrix, vm, sizeof(vm));
        if (matrix_read_ok && LooksLikeMatrix(vm)) {
            std::memcpy(runtime.view_matrix, vm, sizeof(vm));
            runtime.matrix_ok = true;
        }
    }

    const uint64_t now_ms = GetTickCount64();
    // Lobby: refresh every 250ms. In-match with clients: every 50ms (~20Hz).
    const uint64_t enum_interval_ms = s_dec.client_base_ok ? 50ull : 250ull;
    const bool need_enum = (s_last_enum_ms == 0) || (now_ms - s_last_enum_ms >= enum_interval_ms);

    if (need_enum) {
        s_last_enum_ms = now_ms;
        s_dec = {};
        s_dec_ok = RunDecrypt(runtime.module_base, s_dec);
        s_cached_players.clear();

        if (s_dec_ok && (s_dec.client_base_ok || s_dec.name_array_ok)) {
            const int max_slots = s_dec.client_base_ok ? 120 : 64;
            const uintptr_t nsize = offsets.name_entry_size ? offsets.name_entry_size : 0xD0;
            const uintptr_t npos  = offsets.name_array_pos ? offsets.name_array_pos : 0x3038;
            const uintptr_t nname = offsets.name_entry_name ? offsets.name_entry_name : 0x10;
            const uintptr_t nhealth = offsets.name_entry_health ? offsets.name_entry_health : 0x90;
            const uintptr_t nalive = offsets.name_entry_alive ? offsets.name_entry_alive : 0x78;
            const uintptr_t psize = offsets.player_size ? offsets.player_size : 0x2808;

            // Phase 1: scatter-read name entries (names + health + alive)
            struct NameSlot {
                char name[64]{};
                int health = 0;
                int alive = 0;
            };
            std::vector<NameSlot> names(max_slots);
            if (s_dec.name_array_ok && s_dec.name_array_base) {
                auto h = mem.CreateScatterHandle();
                for (int i = 0; i < max_slots; ++i) {
                    uintptr_t entry = s_dec.name_array_base + npos + (uintptr_t)i * nsize;
                    mem.AddScatterReadRequest(h, entry + nname, names[i].name, 32);
                    mem.AddScatterReadRequest(h, entry + nhealth, &names[i].health, sizeof(int));
                    mem.AddScatterReadRequest(h, entry + nalive, &names[i].alive, sizeof(int));
                }
                mem.ExecuteReadScatter(h);
                mem.CloseScatterHandle(h);
            }

            // Phase 2: scatter-read player valid/team/pos when client_base is up.
            // pos at player+0xEB0 may be either float3 or a pointer to a data block
            // (actual floats at ptr + pos_data/0x80). We read both layouts in two passes.
            struct PosSlot {
                int valid = 0;
                int team = 0;
                int stance = 0; // 0 stand, 1 crouch, 2 prone
                float pos[3]{};
                uintptr_t pos_ptr = 0;
            };
            std::vector<PosSlot> poss(max_slots);
            if (s_dec.client_base_ok && s_dec.client_base) {
                const uintptr_t pvalid = offsets.player_valid ? offsets.player_valid : 0x13FC;
                const uintptr_t pteam  = offsets.player_team  ? offsets.player_team  : 0xB0;
                const uintptr_t ppos   = offsets.player_pos   ? offsets.player_pos   : 0xEB0;
                const uintptr_t pdata  = offsets.player_pos_data ? offsets.player_pos_data : 0x80;
                const uintptr_t pstance = offsets.player_stance ? offsets.player_stance : 0x19D0;
                auto h = mem.CreateScatterHandle();
                for (int i = 0; i < max_slots; ++i) {
                    uintptr_t player = s_dec.client_base + (uintptr_t)i * psize;
                    mem.AddScatterReadRequest(h, player + pvalid, &poss[i].valid, sizeof(int));
                    mem.AddScatterReadRequest(h, player + pteam,  &poss[i].team,  sizeof(int));
                    mem.AddScatterReadRequest(h, player + pstance, &poss[i].stance, sizeof(int));
                    // First attempt: treat as pointer (common for this build)
                    mem.AddScatterReadRequest(h, player + ppos,   &poss[i].pos_ptr, sizeof(uintptr_t));
                }
                mem.ExecuteReadScatter(h);
                mem.CloseScatterHandle(h);

                // Second pass: resolve positions via pointer indirection, fallback to direct float3
                auto h2 = mem.CreateScatterHandle();
                std::vector<float> direct_pos(max_slots * 3, 0.f);
                for (int i = 0; i < max_slots; ++i) {
                    uintptr_t player = s_dec.client_base + (uintptr_t)i * psize;
                    if (poss[i].pos_ptr > 0x10000ULL && poss[i].pos_ptr < 0x00007FFFFFFFFFFFULL) {
                        mem.AddScatterReadRequest(h2, poss[i].pos_ptr + pdata, poss[i].pos, sizeof(float) * 3);
                    } else {
                        mem.AddScatterReadRequest(h2, player + ppos, &direct_pos[i * 3], sizeof(float) * 3);
                    }
                }
                mem.ExecuteReadScatter(h2);
                mem.CloseScatterHandle(h2);
                for (int i = 0; i < max_slots; ++i) {
                    const bool ptr_ok = poss[i].pos_ptr > 0x10000ULL && poss[i].pos_ptr < 0x00007FFFFFFFFFFFULL;
                    if (!ptr_ok) {
                        poss[i].pos[0] = direct_pos[i * 3 + 0];
                        poss[i].pos[1] = direct_pos[i * 3 + 1];
                        poss[i].pos[2] = direct_pos[i * 3 + 2];
                    }
                }
            }

            float local_pos[3]{};
            bool have_local_pos = false;
            s_cached_players.reserve(64);

            for (int i = 0; i < max_slots; ++i) {
                // sanitize name
                char* nm = names[i].name;
                for (int c = 0; c < 63 && nm[c]; ++c) {
                    if ((unsigned char)nm[c] < 32 || (unsigned char)nm[c] > 126) {
                        nm[c] = '\0';
                        break;
                    }
                }
                // Verified players require client_base + plausible position.
                // Name alone must NEVER enter runtime.players.
                if (!s_dec.client_base_ok) continue;
                // valid flag is authoritative when non-zero; still accept strong positions
                // if the flag layout drifts on a future build.
                if (poss[i].valid == 0) {
                    // skip empty slots early
                }
                const bool has_pos = std::isfinite(poss[i].pos[0]) && std::isfinite(poss[i].pos[1]) &&
                    std::isfinite(poss[i].pos[2]) &&
                    (std::fabs(poss[i].pos[0]) > 1.f || std::fabs(poss[i].pos[1]) > 1.f) &&
                    std::fabs(poss[i].pos[0]) < 1e6f &&
                    std::fabs(poss[i].pos[1]) < 1e6f;
                if (!has_pos) continue;
                if (poss[i].valid == 0 && !has_pos) continue;

                Player pl{};
                pl.index = i;
                pl.address = s_dec.client_base + (uintptr_t)i * psize;
                pl.pos[0] = poss[i].pos[0];
                pl.pos[1] = poss[i].pos[1];
                pl.pos[2] = poss[i].pos[2];
                // Live stance → head/chest height. Fixes free-cheat bug where aim
                // stays at standing head when the enemy crouches/prones.
                // CoD: 0/stand, 1/crouch, 2/prone (also accept bit-ish values).
                int st = poss[i].stance;
                if (st < 0 || st > 8) st = 0;
                pl.stance = (st >= 2) ? 2 : st; // clamp 0..2 for known range
                float head_dz = 70.f;
                float chest_dz = 40.f;
                if (pl.stance == 1) { head_dz = 42.f; chest_dz = 28.f; }      // crouch
                else if (pl.stance == 2) { head_dz = 18.f; chest_dz = 12.f; } // prone
                pl.head[0] = pl.pos[0]; pl.head[1] = pl.pos[1]; pl.head[2] = pl.pos[2] + head_dz;
                pl.chest[0] = pl.pos[0]; pl.chest[1] = pl.pos[1]; pl.chest[2] = pl.pos[2] + chest_dz;
                pl.team = poss[i].team;
                if (names[i].health > 0 && names[i].health <= 200)
                    pl.health = (float)names[i].health;
                else
                    pl.health = 0.f; // never invent 100 HP
                pl.valid = true;
                pl.alive = (names[i].alive != 0) || (pl.health > 0.f);
                pl.downed = pl.alive && pl.health > 0.f && pl.health <= 30.f;
                pl.is_local = (s_dec.local_index >= 0 && i == s_dec.local_index);
                if (nm[0]) std::strncpy(pl.name, nm, sizeof(pl.name) - 1);

                if (pl.is_local && has_pos) {
                    local_pos[0] = pl.pos[0]; local_pos[1] = pl.pos[1]; local_pos[2] = pl.pos[2];
                    have_local_pos = true;
                    s_cached_local_team = pl.team;
                    s_cached_local_pos[0] = pl.pos[0];
                    s_cached_local_pos[1] = pl.pos[1];
                    s_cached_local_pos[2] = pl.pos[2];
                }
                s_cached_players.push_back(pl);
            }

            if (have_local_pos) {
                for (auto& pl : s_cached_players) {
                    if (!pl.valid) continue;
                    const float dx = pl.pos[0] - local_pos[0];
                    const float dy = pl.pos[1] - local_pos[1];
                    const float dz = pl.pos[2] - local_pos[2];
                    pl.distance = std::sqrt(dx * dx + dy * dy + dz * dz) * 0.0254f;
                }
            }
            s_cached_local_index = s_dec.local_index;
        }
    }

    // Publish cached list every frame (no DMA)
    runtime.decrypt_ok = s_dec_ok;
    runtime.decrypt_needed = !s_dec.client_base_ok;
    std::snprintf(runtime.decrypt_detail, sizeof(runtime.decrypt_detail), "%s", s_dec.detail);
    runtime.local_index = s_cached_local_index;
    runtime.local_team = s_cached_local_team;
    std::memcpy(runtime.local_pos, s_cached_local_pos, sizeof(runtime.local_pos));
    runtime.players = s_cached_players;
    runtime.player_count = (int)runtime.players.size();
    runtime.list_ok = runtime.player_count > 0;

    // Count name_array candidates for diagnostics only (never as verified players)
    int name_candidates = 0;
    if (s_dec.name_array_ok && s_dec.name_array_base && !s_dec.client_base_ok) {
        const uintptr_t nsize = offsets.name_entry_size ? offsets.name_entry_size : 0xD0;
        const uintptr_t npos  = offsets.name_array_pos ? offsets.name_array_pos : 0x3038;
        const uintptr_t nname = offsets.name_entry_name ? offsets.name_entry_name : 0x10;
        for (int i = 0; i < 64; ++i) {
            char nm[32]{};
            mem.Read(s_dec.name_array_base + npos + (uintptr_t)i * nsize + nname, nm, 31);
            if (nm[0] >= 32 && nm[0] <= 126) ++name_candidates;
        }
    }

    // Lobby → match lifecycle (always probing; never treat lobby as terminal failure)
    static MatchLifecycle::State s_match{};
    const bool has_world = runtime.module_base != 0 && runtime.matrix_ok;
    const bool has_local = s_cached_local_index >= 0 || runtime.local_index >= 0;
    const auto phase = MatchLifecycle::Update(
        s_match, has_world, has_local, runtime.player_count, runtime.module_base);
    runtime.in_game = (phase == MatchLifecycle::Phase::InMatch
        || phase == MatchLifecycle::Phase::EnteredMatch
        || phase == MatchLifecycle::Phase::WaitingEntities
        || (runtime.matrix_ok && s_dec.client_base_ok));

    if (phase == MatchLifecycle::Phase::EnteredMatch) {
        // Clear decrypt/player cache so the next enum rebuilds for the new match
        s_dec_ok = false;
        s_cached_players.clear();
        s_last_enum_ms = 0;
        std::cout << "[Warzone] Entrada em partida — a revalidar client_info/lista\n";
    } else if (phase == MatchLifecycle::Phase::LeftMatch) {
        std::cout << "[Warzone] Saida de partida / lobby\n";
    }

    if (!matrix_read_ok) {
        std::snprintf(runtime.status, sizeof(runtime.status),
            "Lobby / a aguardar | Matrix FAIL");
    } else if (!runtime.matrix_ok) {
        std::snprintf(runtime.status, sizeof(runtime.status),
            "Lobby / a aguardar partida (matrix invalid)");
    } else if (s_dec.client_base_ok && runtime.list_ok) {
        std::snprintf(runtime.status, sizeof(runtime.status),
            "Em jogo | Players %d verified", runtime.player_count);
    } else if (s_dec.client_base_ok) {
        std::snprintf(runtime.status, sizeof(runtime.status),
            "Em partida — a resolver lista | Players 0");
    } else if (s_dec.client_info_ok) {
        std::snprintf(runtime.status, sizeof(runtime.status),
            "Lobby / a aguardar | ClientBase FAIL | Names %d", name_candidates);
    } else if (std::strstr(s_dec.detail, "NULL_ENC")) {
        std::snprintf(runtime.status, sizeof(runtime.status),
            "Lobby / a aguardar partida (NULL_ENC / pos-patch?)");
    } else {
        std::snprintf(runtime.status, sizeof(runtime.status),
            "Lobby / a aguardar | ClientInfo FAIL | Names %d", name_candidates);
    }

    // Full diagnostics go to the single logs.txt via std::clog.
    {
        static bool last_matrix_ok = false;
        static bool last_list_ok = false;
        static int last_players = -1;
        static uint64_t last_probe_log_ms = 0;

        const uint64_t now = GetTickCount64();
        const bool changed = runtime.matrix_ok != last_matrix_ok ||
            runtime.list_ok != last_list_ok ||
            runtime.player_count != last_players;
        const bool periodic = last_probe_log_ms == 0 || now - last_probe_log_ms >= 5000;

        if (changed || periodic) {
            std::clog << "[DEBUG][Warzone] runtime_probe frame=" << runtime.frames
                      << " base=0x" << std::hex << runtime.module_base
                      << " ci=0x" << s_dec.client_info
                      << " cb=0x" << s_dec.client_base
                      << " na=0x" << s_dec.name_array_base << std::dec
                      << " matrix=" << (runtime.matrix_ok ? "OK" : "FAIL")
                      << " list=" << (runtime.list_ok ? "OK" : "0")
                      << " players=" << runtime.player_count
                      << " idx=" << runtime.local_index
                      << " decrypt=" << runtime.decrypt_detail
                      << std::endl;
            last_probe_log_ms = now;
            last_matrix_ok = runtime.matrix_ok;
            last_list_ok = runtime.list_ok;
            last_players = runtime.player_count;
        }
    }

    // Draw / aim only when we have matrix + players
    Warzone_ESP::Draw(runtime, config);
    Warzone_Aim::Run(runtime, config);
}

void Shutdown() {
    ready = false;
    runtime = {};
    status = "Warzone parado";
}

const char* StatusLine() {
    return runtime.status[0] ? runtime.status : status.c_str();
}

} // namespace Warzone
