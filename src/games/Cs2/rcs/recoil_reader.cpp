#include "recoil_reader.h"
#include "local_player_resolver.h"

#include <cmath>

namespace CS2::RCS {

ReadResult RecoilReader::Read(const Runtime& snapshot, uint64_t nowMs) noexcept {
    ReadResult result{};
    const LocalPlayerState local = LocalPlayerResolver::Inspect(snapshot);
    if (!local.pawnValid) { result.status = ReadStatus::NoLocalPlayer; return result; }
    if (!local.alive) { result.status = ReadStatus::Dead; return result; }



    if (!snapshot.local_aim_sample_ms || nowMs < snapshot.local_aim_sample_ms) {
        result.status = ReadStatus::MissingSample; return result;
    }
    if (!snapshot.local_aim_punch_final) {
        result.status = ReadStatus::UnresolvedSample; return result;
    }
    if (nowMs - snapshot.local_aim_sample_ms > 100u) {
        result.status = ReadStatus::StaleSample; return result;
    }
    if (!std::isfinite(snapshot.local_aim_punch[0]) || !std::isfinite(snapshot.local_aim_punch[1]) ||
        std::fabs(snapshot.local_aim_punch[0]) > 89.f || std::fabs(snapshot.local_aim_punch[1]) > 180.f) {
        result.status = ReadStatus::InvalidAngles; return result;
    }
    result.sample = {snapshot.local_pawn, snapshot.local_health, snapshot.local_weapon_definition,
        snapshot.local_shots_fired, snapshot.local_aim_punch[0], snapshot.local_aim_punch[1],
        snapshot.local_aim_sample_ms, snapshot.local_last_shot_ms};
    result.status = ReadStatus::Ready;
    return result;
}

const char* ToString(ReadStatus status) noexcept {
    switch (status) {
    case ReadStatus::Ready: return "ready";
    case ReadStatus::NoLocalPlayer: return "local pawn unavailable";
    case ReadStatus::Dead: return "local player dead";
    case ReadStatus::MissingWeapon: return "weapon unavailable";
    case ReadStatus::MissingSample: return "aim punch unavailable";
    case ReadStatus::UnresolvedSample: return "final aim punch unavailable";
    case ReadStatus::StaleSample: return "aim punch stale";
    case ReadStatus::InvalidAngles: return "aim punch invalid";
    }
    return "unknown";
}

}
