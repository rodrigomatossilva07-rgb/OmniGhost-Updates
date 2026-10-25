#include "object_debugger.h"

#include "../game/offsets.h"
#include "third_party/DMALibrary/Memory/Memory.h"
#include "globals.h"

#include <Windows.h>

#include <atomic>

namespace FiveM::ObjectDebugger {
namespace {

constexpr ULONGLONG kRequestLifetimeMs = 750;
constexpr ULONGLONG kCollectionIntervalMs = 250;

SnapshotExchange g_snapshots;
std::atomic<ULONGLONG> g_requestedAt{0};
std::atomic<ULONGLONG> g_lastCollectionAt{0};

void Publish(Snapshot&& snapshot) {
    if (auto slot = g_snapshots.TryBeginWrite()) {
        *slot.value = std::move(snapshot);
        g_snapshots.Publish(slot.index);
    }
}

Snapshot BaseSnapshot(AcquisitionState state) {
    Snapshot snapshot;
    snapshot.state = state;
    snapshot.build = offset::buildVersion;
    snapshot.sampledAtMs = GetTickCount64();
    snapshot.candidateLayout = false;
    return snapshot;
}

}

void RequestRefresh() noexcept {
    g_requestedAt.store(GetTickCount64(), std::memory_order_release);
}

bool RequestedRecently() noexcept {
    const ULONGLONG requested = g_requestedAt.load(std::memory_order_acquire);
    const ULONGLONG now = GetTickCount64();
    return requested != 0 && now >= requested && now - requested <= kRequestLifetimeMs;
}

void Collect(const Vec3&, std::uint64_t) {
    if (!RequestedRecently()) return;
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG previous = g_lastCollectionAt.load(std::memory_order_relaxed);
    if (previous && now >= previous && now - previous < kCollectionIntervalMs) return;
    g_lastCollectionAt.store(now, std::memory_order_relaxed);

    if (!FiveM::IsBuildSupported()) {
        Publish(BaseSnapshot(AcquisitionState::UnsupportedBuild));
        return;
    }
    if (!mem.vHandle || !offset::replay) {
        Publish(BaseSnapshot(AcquisitionState::WaitingForGame));
        return;
    }

    // The project currently has a module-level object_pool candidate for b3258,
    // but no verified pool/list/count/entry/model layout. Reading through offsets
    // copied from an old external client would violate the DMA offset policy and
    // could make random memory look like valid objects. Keep the debugger honest:
    // it becomes a consumer of real records only after a build-specific layout is
    // added to the verified offset source.
    Publish(BaseSnapshot(AcquisitionState::LayoutUnavailable));
}

SnapshotLease AcquireSnapshot() noexcept {
    return g_snapshots.Acquire();
}

void Shutdown() noexcept {
    g_requestedAt.store(0, std::memory_order_release);
    g_lastCollectionAt.store(0, std::memory_order_release);
    g_snapshots.ResetProducerThread();
    Publish(BaseSnapshot(AcquisitionState::Idle));
    g_snapshots.ResetProducerThread();
}

const char* StateText(AcquisitionState state) noexcept {
    switch (state) {
    case AcquisitionState::Idle: return "Em espera";
    case AcquisitionState::WaitingForGame: return "A aguardar FiveM / DMA";
    case AcquisitionState::UnsupportedBuild: return "Build sem suporte";
    case AcquisitionState::LayoutUnavailable: return "Layout CObject ainda nao validado";
    case AcquisitionState::MissingInterface: return "Interface CObject indisponivel";
    case AcquisitionState::InvalidPool: return "Pool rejeitada pela validacao";
    case AcquisitionState::NoObjects: return "Pool valida, sem objetos";
    case AcquisitionState::CandidateData: return "Dados candidatos recebidos";
    case AcquisitionState::ReadError: return "Falha de leitura DMA";
    }
    return "Desconhecido";
}

const char* ObjectStateText(ObjectState state) noexcept {
    switch (state) {
    case ObjectState::Valid: return "Valido";
    case ObjectState::InvalidPosition: return "Posicao invalida";
    case ObjectState::MissingModel: return "Hash indisponivel";
    case ObjectState::MissingLocalPlayer: return "Sem jogador local";
    }
    return "Desconhecido";
}

}
