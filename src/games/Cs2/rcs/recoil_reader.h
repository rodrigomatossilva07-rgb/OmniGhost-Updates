#pragma once

#include "../cs2_game.h"
#include "rcs_controller.h"

namespace CS2::RCS {

enum class ReadStatus { Ready, NoLocalPlayer, Dead, MissingWeapon, MissingSample,
                        UnresolvedSample, StaleSample, InvalidAngles };

struct ReadResult {
    RecoilSample sample{};
    ReadStatus status = ReadStatus::MissingSample;
    explicit operator bool() const noexcept { return status == ReadStatus::Ready; }
};

class RecoilReader {
public:
    [[nodiscard]] static ReadResult Read(const Runtime& snapshot, uint64_t nowMs) noexcept;
};

[[nodiscard]] const char* ToString(ReadStatus status) noexcept;

}
