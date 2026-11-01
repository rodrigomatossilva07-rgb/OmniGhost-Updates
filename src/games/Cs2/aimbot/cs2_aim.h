#pragma once
#include "cs2_game.h"

namespace CS2_Aim {
struct RCSTelemetry {
    bool enabled = false;
    bool standalone = false;
    bool finalPunch = false;
    bool firing = false;
    bool correctionProduced = false;
    int shots = 0;
    int weaponDefinition = 0;
    uintptr_t pawn = 0;
    uint64_t sampleMs = 0;
    uint64_t observedMs = 0;
    float pitch = 0.f;
    float yaw = 0.f;
    float deltaPitch = 0.f;
    float deltaYaw = 0.f;
    float correctionX = 0.f;
    float correctionY = 0.f;
    float pendingX = 0.f;
    float pendingY = 0.f;
    int sentX = 0;
    int sentY = 0;
    char stage[32]{};
    char reason[96]{};
};

void Run(const CS2::Runtime& rt, const CS2::Config& cfg);
int ActiveTargetIndex();
const char* DebugStatus();
const char* RCSDebugStatus();
RCSTelemetry AcquireRCSTelemetry();
}
