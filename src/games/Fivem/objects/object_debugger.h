#pragma once

#include "math/math.h"
#include "gameplay/snapshot_exchange.h"

#include <cstdint>
#include <string>
#include <vector>

namespace FiveM::ObjectDebugger {

enum class ObjectState : std::uint8_t {
    Valid,
    InvalidPosition,
    MissingModel,
    MissingLocalPlayer
};

struct ObjectRecord {
    std::uintptr_t address = 0;
    std::uint32_t slot = 0;
    std::uint32_t modelHash = 0;
    Vec3 position{};
    float distance = 0.f;
    ObjectState state = ObjectState::InvalidPosition;
    std::string modelName;
};

enum class AcquisitionState : std::uint8_t {
    Idle,
    WaitingForGame,
    UnsupportedBuild,
    LayoutUnavailable,
    MissingInterface,
    InvalidPool,
    NoObjects,
    CandidateData,
    ReadError
};

struct Snapshot {
    std::vector<ObjectRecord> objects;
    AcquisitionState state = AcquisitionState::Idle;
    std::uint32_t reportedCount = 0;
    std::uint32_t inspectedCount = 0;
    std::uint32_t rejectedPointers = 0;
    std::uint32_t validPositions = 0;
    std::uint64_t sampledAtMs = 0;
    float acquisitionMs = 0.f;
    int build = 0;
    bool candidateLayout = true;
};

using SnapshotExchange = OmniGhost::Gameplay::SnapshotExchange<Snapshot, 3>;
using SnapshotLease = SnapshotExchange::ReadLease;

// Keeps acquisition disabled unless the debugger page is actually being viewed.
void RequestRefresh() noexcept;
[[nodiscard]] bool RequestedRecently() noexcept;

// Called from FiveM's existing optional DMA worker. It never renders ESP elements.
void Collect(const Vec3& localPosition, std::uint64_t sessionGeneration);
[[nodiscard]] SnapshotLease AcquireSnapshot() noexcept;
void Shutdown() noexcept;

[[nodiscard]] const char* StateText(AcquisitionState state) noexcept;
[[nodiscard]] const char* ObjectStateText(ObjectState state) noexcept;

}
