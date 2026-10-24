#include "rust_game.h"
#include "rust_esp.h"
#include "rust_offsets.h"

#include "third_party/DMALibrary/Memory/Memory.h"

#include <Windows.h>

#include <cstring>

extern Memory mem;

namespace Rust {

Config config{};
Runtime runtime{};
std::atomic_bool ready{false};

namespace {
constexpr const char* kProcessName = "RustClient.exe";

bool HasSuppliedCriticalOffsets() {
    using namespace Offsets::Build_2026_10_22;
    return Rvas::TypeManagerInstance != 0 && Rvas::TypeToObjectSet != 0 &&
           BaseNetworkableStatic::clientEntities != 0 && EntityRealm::entityList != 0 &&
           BasePlayer::_displayName != 0 && BaseCombatEntity::_health != 0;
}

void UpdateAvailability() {
    runtime.device_ok = mem.GetDiagnosticsSnapshot().deviceOpen;
    runtime.process_ok = mem.GetDiagnosticsSnapshot().processInitialized &&
                         mem.GetPidFromName(kProcessName) != 0;
    runtime.modules_ok = runtime.game_assembly > 0x10000 && runtime.unity_player > 0x10000;
    runtime.offset_catalog_ok = HasSuppliedCriticalOffsets();

    // The supplied dump deliberately records these two required roots as zero.
    // Keep the backend truthful until a current dump supplies them or a tested
    // dynamic resolver is implemented for this Unity/IL2CPP build.
    runtime.entity_source_ok = false;
    runtime.camera_ok = false;
    runtime.acquisition_ok = runtime.entity_source_ok && runtime.camera_ok;
    runtime.players.clear();

    if (!runtime.device_ok) runtime.status = "DMA/FPGA indisponível";
    else if (!runtime.process_ok) runtime.status = "RustClient.exe não encontrado";
    else if (!runtime.modules_ok) runtime.status = "GameAssembly.dll/UnityPlayer.dll indisponíveis";
    else if (!runtime.offset_catalog_ok) runtime.status = "Catálogo de offsets Rust inválido";
    else if (!runtime.entity_source_ok) runtime.status = "A aguardar BaseNetworkable_TypeInfo válido";
    else if (!runtime.camera_ok) runtime.status = "A aguardar viewMatrix válida";
    else runtime.status = "Aquisição Rust ativa";
}
}

bool Attach() {
    Shutdown();
    runtime.status = "A abrir DMA e a procurar Rust...";
    if (!mem.Init(std::string(), true, false) && !mem.GetDiagnosticsSnapshot().deviceOpen) {
        UpdateAvailability();
        return false;
    }
    runtime.pid = mem.GetPidFromName(kProcessName);
    if (!runtime.pid) {
        UpdateAvailability();
        return false;
    }
    if (!mem.Init(kProcessName, false, false)) {
        UpdateAvailability();
        return false;
    }
    runtime.game_assembly = mem.GetBaseDaddy("GameAssembly.dll");
    runtime.unity_player = mem.GetBaseDaddy("UnityPlayer.dll");
    UpdateAvailability();
    // Opening the diagnostic/menu session is useful even while a critical
    // runtime root is missing; the System page exposes the exact blocker.
    ready.store(runtime.process_ok && runtime.modules_ok, std::memory_order_release);
    return ready.load(std::memory_order_acquire);
}

void Shutdown() noexcept {
    ready.store(false, std::memory_order_release);
    runtime = Runtime{};
}

void Tick() {
    if (!ready.load(std::memory_order_acquire)) return;
    const std::uint64_t now = GetTickCount64();
    if (now - runtime.last_probe_ms >= 1000u) {
        runtime.last_probe_ms = now;
        UpdateAvailability();
        if (!runtime.process_ok)
            ready.store(false, std::memory_order_release);
    }
    ++runtime.frames;
    ESP::Draw(runtime, config);
}

bool IsGameProcessAlive() {
    return mem.GetPidFromName(kProcessName) != 0;
}

bool ValidateLiveOffsets() {
    UpdateAvailability();
    return runtime.modules_ok && runtime.offset_catalog_ok;
}

const char* StatusLine() {
    return runtime.status.c_str();
}

}
