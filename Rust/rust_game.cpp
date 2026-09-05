#include "rust_game.h"
#include "rust_internal.h"
#include "rust_decrypt.h"
#include "../src/platform/offset_auto.h"
#include "rust_offset_api.h"
#include "../src/platform/cheatoffsets_api.h"
#include "../Fivem/aimbot/aim_type.h"
#include "gameplay/esp_core.h"
#include "../DMALibrary/Memory/Memory.h"
#include "../src/makcu/makcu_wrapper.h"
#include "../src/platform/app_paths.h"
#include "../src/platform/thread_utils.h"
#include "../src/platform/scope_exit.h"
#include "../src/platform/session_log.h"
#include "../src/platform/match_lifecycle.h"
#include "platform/offset_source.h"

#include <Windows.h>

#include <filesystem>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

namespace Rust {

Offsets offsets{};
Runtime runtime{};
Config config{};
bool ready = false;
std::string status = "Rust idle";

std::atomic<int> device_phase{(int)DevicePhase::Closed};
std::atomic<int> process_phase{(int)ProcessPhase::Detached};
std::atomic<int> runtime_phase{(int)RuntimePhase::Idle};
std::atomic<int> backend_state{(int)BackendState::Idle};
std::atomic_bool backend_busy{false};

namespace {

std::mutex g_backend_mu;
std::atomic<uint64_t> g_operation_id{0};
std::atomic<uint64_t> g_operation_start_ms{0};
std::atomic<uint64_t> g_session_generation{1};
std::atomic<uint64_t> g_process_generation{0};
std::atomic<int> g_backend_reason{(int)BackendReason::None};
char g_operation_name[48] = {};
std::atomic_bool g_cancel{false};
std::jthread g_worker;

} // namespace

namespace detail {

void SetBackendReason(BackendReason reason) {
    g_backend_reason.store((int)reason, std::memory_order_release);
}

BackendReason CurrentBackendReason() {
    return static_cast<BackendReason>(g_backend_reason.load(std::memory_order_acquire));
}

const char* UserFacingReason(BackendReason r) {
    switch (r) {
    case BackendReason::None: return "OK";
    case BackendReason::DeviceOpenFailed: return "Dispositivo DMA indisponivel";
    case BackendReason::DeviceDataPathFailed: return "Leitura fisica DMA sem resposta";
    case BackendReason::VmmInitializationFailed: return "Falha a inicializar sessao VMM";
    case BackendReason::VmmSessionRecoveryFailed: return "Sessao VMM perdida (nao e problema de offsets)";
    case BackendReason::PluginInitializationFailed: return "Plugins VMM falharam";
    case BackendReason::ProcessNotFound: return "Jogo nao encontrado (RustClient.exe)";
    case BackendReason::ProcInfoGenerating: return "Jogo encontrado; a gerar ProcInfo/DTB...";
    case BackendReason::ProcInfoTimeout: return "ProcInfo/DTB excedeu o tempo";
    case BackendReason::ProcInfoStuck: return "ProcInfo preso em 0%";
    case BackendReason::ProcInfoCancelled: return "ProcInfo cancelado";
    case BackendReason::MappingUnavailable: return "Mapeamento de memoria indisponivel";
    case BackendReason::ModuleValidationFailed: return "Jogo encontrado; a aguardar validacao de modulo";
    case BackendReason::RuntimeUnavailable: return "Jogo anexado; a aguardar GameAssembly/local/matrix";
    case BackendReason::OperationSuperseded: return "Operacao cancelada/substituida";
    case BackendReason::DependencyMismatch: return "Dependencias DMA invalidas";
    case BackendReason::BackendBusy: return "Motor ocupado";
    }
    return "Estado desconhecido";
}

void InvalidateProcessContext(const char* why) {
    runtime.local_player = 0;
    runtime.local_team = 0;
    runtime.local_pos[0] = runtime.local_pos[1] = runtime.local_pos[2] = 0.f;
    std::memset(runtime.view_matrix, 0, sizeof(runtime.view_matrix));
    runtime.players.clear();
    runtime.world_entities.clear();
    runtime.player_count = 0;
    runtime.world_count = 0;
    runtime.in_game = false;
    runtime.matrix_ok = false;
    runtime.list_ok = false;
    runtime.self_test_ok = false;
    runtime.last_cache_ms = 0;
    runtime.last_pos_ms = 0;
    runtime.last_world_ms = 0;
    std::cout << "[Rust] InvalidateProcessContext reason=\""
              << (why ? why : "?") << "\"\n";
}

void SyncGenerationsOrInvalidate() {
    // Generation tracking is updated on attach/reinit.
}

} // namespace detail

#include "rust_offset_loading.inl"

int64_t MonotonicMilliseconds() {
    return static_cast<int64_t>(detail::NowMs());
}

uint64_t BeginBackendOperation(const char* name) {
    const uint64_t id = g_operation_id.fetch_add(1, std::memory_order_acq_rel) + 1;
    g_operation_start_ms.store(detail::NowMs(), std::memory_order_release);
    if (name)
        std::snprintf(g_operation_name, sizeof(g_operation_name), "%s", name);
    else
        g_operation_name[0] = '\0';
    backend_busy.store(true, std::memory_order_release);
    return id;
}

void EndBackendOperation(uint64_t /*id*/, const char* outcome) {
    backend_busy.store(false, std::memory_order_release);
    if (outcome)
        std::cout << "[Rust] backend op done: " << outcome << "\n";
}

BackendSnapshot GetBackendSnapshot() {
    BackendSnapshot snap{};
    snap.state = static_cast<BackendState>(backend_state.load(std::memory_order_acquire));
    snap.reason = static_cast<BackendReason>(g_backend_reason.load(std::memory_order_acquire));
    snap.device = static_cast<DevicePhase>(device_phase.load(std::memory_order_acquire));
    snap.process = static_cast<ProcessPhase>(process_phase.load(std::memory_order_acquire));
    snap.runtime = static_cast<RuntimePhase>(runtime_phase.load(std::memory_order_acquire));
    snap.busy = backend_busy.load(std::memory_order_acquire);
    snap.operation_id = g_operation_id.load(std::memory_order_acquire);
    const uint64_t start = g_operation_start_ms.load(std::memory_order_acquire);
    const uint64_t now = detail::NowMs();
    snap.operation_elapsed_ms = (start && now >= start) ? (now - start) : 0;
    std::snprintf(snap.operation, sizeof(snap.operation), "%s", g_operation_name);
    snap.session_generation = g_session_generation.load(std::memory_order_acquire);
    snap.process_generation = g_process_generation.load(std::memory_order_acquire);
    snap.procinfo_state = 0;
    snap.procinfo_progress = -1;
    snap.procinfo_recovery_recommended = false;
    snap.procinfo_dtb_size = 0;
    snap.vmm_maintenance = false;
    snap.blocked_data_calls = 0;
    return snap;
}

const char* BackendStateName() {
    switch (static_cast<BackendState>(backend_state.load())) {
    case BackendState::Idle: return "Idle";
    case BackendState::Starting: return "Starting";
    case BackendState::Waiting: return "Waiting";
    case BackendState::Ready: return "Ready";
    case BackendState::Error: return "Error";
    case BackendState::Cancelled: return "Cancelled";
    }
    return "?";
}

const char* BackendReasonName(BackendReason reason) {
    return detail::UserFacingReason(reason);
}

const char* DevicePhaseName() {
    switch (static_cast<DevicePhase>(device_phase.load())) {
    case DevicePhase::Closed: return "Closed";
    case DevicePhase::Opening: return "Opening";
    case DevicePhase::Open: return "Open";
    case DevicePhase::Lost: return "Lost";
    }
    return "?";
}

const char* ProcessPhaseName() {
    switch (static_cast<ProcessPhase>(process_phase.load())) {
    case ProcessPhase::Detached: return "Detached";
    case ProcessPhase::Searching: return "Searching";
    case ProcessPhase::Found: return "Found";
    case ProcessPhase::Attaching: return "Attaching";
    case ProcessPhase::Attached: return "Attached";
    case ProcessPhase::Waiting: return "Waiting";
    }
    return "?";
}

const char* RuntimePhaseName() {
    switch (static_cast<RuntimePhase>(runtime_phase.load())) {
    case RuntimePhase::Idle: return "Idle";
    case RuntimePhase::Resolving: return "Resolving";
    case RuntimePhase::Ready: return "Ready";
    case RuntimePhase::Unavailable: return "Unavailable";
    }
    return "?";
}

bool SleepCancelable(unsigned long ms) {
    const auto end = detail::NowMs() + ms;
    while (detail::NowMs() < end) {
        if (g_cancel.load(std::memory_order_acquire))
            return false;
        Sleep(15);
    }
    return !g_cancel.load(std::memory_order_acquire);
}

bool IsGameProcessAlive() {
    return mem.GetPidFromName("RustClient.exe") != 0;
}

static bool OpenDeviceOnly() {
    device_phase = (int)DevicePhase::Opening;
    if (!mem.Init(std::string(), true, false)) {
        device_phase = (int)DevicePhase::Lost;
        detail::SetBackendReason(BackendReason::DeviceOpenFailed);
        status = "Dispositivo DMA indisponivel";
        return false;
    }
    device_phase = (int)DevicePhase::Open;
    return true;
}

static bool BindRustProcess() {
    process_phase = (int)ProcessPhase::Searching;
    detail::SetBackendReason(BackendReason::ProcessNotFound);
    status = "A procurar RustClient.exe...";

    for (int attempt = 0; attempt < 40 && !g_cancel.load(); ++attempt) {
        if (mem.Init("RustClient.exe", true, false)) {
            process_phase = (int)ProcessPhase::Attached;
            g_process_generation.fetch_add(1, std::memory_order_acq_rel);
            detail::SetBackendReason(BackendReason::ModuleValidationFailed);
            status = "Processo anexado; a resolver GameAssembly...";
            return true;
        }
        Sleep(250);
    }
    process_phase = (int)ProcessPhase::Detached;
    detail::SetBackendReason(BackendReason::ProcessNotFound);
    status = "RustClient.exe nao encontrado";
    return false;
}

static bool ResolveGameAssembly() {
    runtime_phase = (int)RuntimePhase::Resolving;
    runtime.game_assembly = mem.GetBaseDaddy("GameAssembly.dll");
    if (!runtime.game_assembly) {
        detail::SetBackendReason(BackendReason::ModuleValidationFailed);
        status = "GameAssembly.dll ainda nao disponivel";
        runtime_phase = (int)RuntimePhase::Unavailable;
        return false;
    }
    detail::SetBackendReason(BackendReason::RuntimeUnavailable);
    status = "GameAssembly OK; a aguardar local/matrix";
    return true;
}

bool Attach() {
    std::lock_guard<std::mutex> lock(g_backend_mu);
    if (backend_busy.load()) {
        detail::SetBackendReason(BackendReason::BackendBusy);
        return false;
    }
    const uint64_t op = BeginBackendOperation("attach");
    g_cancel.store(false, std::memory_order_release);
    backend_state = (int)BackendState::Starting;
    ready = false;
    detail::InvalidateProcessContext("attach");

    if (!OpenDeviceOnly()) {
        backend_state = (int)BackendState::Error;
        EndBackendOperation(op, "device-failed");
        return false;
    }
    if (!BindRustProcess()) {
        backend_state = (int)BackendState::Waiting;
        EndBackendOperation(op, "waiting-process");
        return true;
    }
    if (!ResolveGameAssembly()) {
        backend_state = (int)BackendState::Waiting;
        EndBackendOperation(op, "waiting-module");
        return true;
    }

    backend_state = (int)BackendState::Ready;
    process_phase = (int)ProcessPhase::Attached;
    runtime_phase = (int)RuntimePhase::Ready;
    ready = true;
    detail::SetBackendReason(BackendReason::None);
    status = "Rust ligado";
    EndBackendOperation(op, "ok");
    return true;
}

bool StartBackendAsync() {
    if (backend_busy.load())
        return false;
    g_cancel.store(false, std::memory_order_release);
    g_worker = std::jthread([](std::stop_token) {
        (void)Attach();
    });
    return true;
}

bool ReinitDmaCore() {
    std::lock_guard<std::mutex> lock(g_backend_mu);
    const uint64_t op = BeginBackendOperation("reinit");
    g_cancel.store(false, std::memory_order_release);
    detail::InvalidateProcessContext("reinit");
    ready = false;
    runtime_phase = (int)RuntimePhase::Resolving;

    if (device_phase.load() != (int)DevicePhase::Open) {
        if (!OpenDeviceOnly()) {
            backend_state = (int)BackendState::Error;
            EndBackendOperation(op, "device-failed");
            return false;
        }
    }

    if (!mem.Init("RustClient.exe", true, false)) {
        process_phase = (int)ProcessPhase::Searching;
        detail::SetBackendReason(BackendReason::ProcessNotFound);
        backend_state = (int)BackendState::Waiting;
        EndBackendOperation(op, "waiting-process");
        return false;
    }
    process_phase = (int)ProcessPhase::Attached;
    g_process_generation.fetch_add(1, std::memory_order_acq_rel);

    if (!ResolveGameAssembly()) {
        backend_state = (int)BackendState::Waiting;
        EndBackendOperation(op, "waiting-module");
        return false;
    }

    (void)mem.FixCr3();

    backend_state = (int)BackendState::Ready;
    runtime_phase = (int)RuntimePhase::Ready;
    ready = true;
    detail::SetBackendReason(BackendReason::None);
    status = "Rust reconectado";
    EndBackendOperation(op, "ok");
    return true;
}

bool ReinitDma() {
    return ReinitDmaCore();
}

bool ReinitDmaAsync() {
    if (backend_busy.load())
        return false;
    g_worker = std::jthread([](std::stop_token) {
        (void)ReinitDmaCore();
    });
    return true;
}

void CancelBackendOperation() {
    g_cancel.store(true, std::memory_order_release);
    detail::SetBackendReason(BackendReason::OperationSuperseded);
}

void Shutdown() {
    g_cancel.store(true, std::memory_order_release);
    if (g_worker.joinable())
        g_worker = std::jthread{};
    std::lock_guard<std::mutex> lock(g_backend_mu);
    backend_state = (int)BackendState::Cancelled;
    ready = false;
    detail::InvalidateProcessContext("shutdown");
    process_phase = (int)ProcessPhase::Detached;
    runtime_phase = (int)RuntimePhase::Idle;
    device_phase = (int)DevicePhase::Closed;
    mem.InvalidateProcess();
    backend_state = (int)BackendState::Idle;
    backend_busy.store(false, std::memory_order_release);
    status = "Rust idle";
    detail::SetBackendReason(BackendReason::None);
}

void RunFrame() {
    if (!ready && backend_state.load() == (int)BackendState::Waiting) {
        static uint64_t s_last_wait_try = 0;
        const uint64_t now = detail::NowMs();
        if (now - s_last_wait_try > 2000 && !backend_busy.load()) {
            s_last_wait_try = now;
            (void)ReinitDmaAsync();
        }
        return;
    }
    if (!ready)
        return;

    ++runtime.frames;
    const uint64_t now = detail::NowMs();
    detail::SyncGenerationsOrInvalidate();

    static uint64_t s_last_probe = 0;
    if (now - s_last_probe >= 3000) {
        s_last_probe = now;
        if (!runtime.game_assembly)
            runtime.game_assembly = mem.GetBaseDaddy("GameAssembly.dll");
        if (runtime.game_assembly) {
            float mx[16]{};
            if (detail::ResolveViewMatrix(mx)) {
                std::memcpy(runtime.view_matrix, mx, sizeof(mx));
                runtime.matrix_ok = true;
            }
            if (!runtime.local_player)
                runtime.local_player = detail::ResolveLocalPlayer();
            runtime_phase = (int)RuntimePhase::Ready;
            if (backend_state.load() == (int)BackendState::Waiting && runtime.matrix_ok) {
                backend_state = (int)BackendState::Ready;
                ready = true;
            }
        } else if (process_phase.load() == (int)ProcessPhase::Attached) {
            runtime_phase = (int)RuntimePhase::Unavailable;
        }
    }

    static MatchLifecycle::State s_match{};
    const bool has_world = runtime.game_assembly != 0;

    if (!runtime.local_player || !runtime.matrix_ok) {
        runtime.players.clear();
        runtime.player_count = 0;
        runtime.world_entities.clear();
        runtime.world_count = 0;
        const auto phase = MatchLifecycle::Update(s_match, has_world, false, 0, runtime.game_assembly);
        runtime.in_game = false;
        if (std::strstr(runtime.status, "MC k=") == nullptr) {
            char extra[96];
            std::snprintf(extra, sizeof(extra), "local=%s matrix=%s list=%s GA=0x%llX",
                runtime.local_player ? "ok" : "0",
                runtime.matrix_ok ? "ok" : "0",
                runtime.list_ok ? "ok" : "0",
                (unsigned long long)runtime.game_assembly);
            MatchLifecycle::FormatStatus(runtime.status, sizeof(runtime.status), phase, 0, extra);
        }
        status = runtime.status;
        return;
    }

    uintptr_t localModel = 0;
    if (detail::ReadU64(runtime.local_player + offsets.playerModel, localModel))
        mem.Read(localModel + offsets.modelPosition, runtime.local_pos, sizeof(float) * 3);
    detail::R(runtime.local_player + offsets.currentTeam, runtime.local_team);

    if (now - runtime.last_cache_ms > 2000) {
        detail::CachePlayers();
        runtime.last_cache_ms = now;
    } else if (now - runtime.last_pos_ms > 30) {
        detail::UpdatePositions();
        runtime.last_pos_ms = now;
    }
    if (now - runtime.last_world_ms > 2500) {
        detail::CacheWorldEntities();
        runtime.last_world_ms = now;
    }

    detail::TryMiscWrites();

    const auto phase = MatchLifecycle::Update(
        s_match, true, true, runtime.player_count, runtime.game_assembly);
    runtime.in_game = (phase == MatchLifecycle::Phase::InMatch
        || phase == MatchLifecycle::Phase::EnteredMatch
        || phase == MatchLifecycle::Phase::WaitingEntities);

    if (phase == MatchLifecycle::Phase::EnteredMatch) {
        runtime.last_cache_ms = 0;
        runtime.last_world_ms = 0;
        detail::CachePlayers();
        runtime.last_cache_ms = now;
    }

    char extra[96];
    std::snprintf(extra, sizeof(extra), "world=%d matrix=OK list=%s team=%d",
        runtime.world_count,
        runtime.list_ok ? "OK" : "wait",
        runtime.local_team);
    MatchLifecycle::FormatStatus(runtime.status, sizeof(runtime.status), phase, runtime.player_count, extra);
    status = runtime.status;
}



} // namespace Rust
