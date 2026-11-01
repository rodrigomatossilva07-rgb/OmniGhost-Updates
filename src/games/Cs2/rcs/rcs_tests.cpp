#include "rcs_controller.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace {
int failures = 0;

void Expect(bool condition, const char* name) {
    if (condition) return;
    ++failures;
    std::cerr << "[FAIL] " << name << '\n';
}

bool Near(float actual, float expected, float epsilon = .0001f) {
    return std::fabs(actual - expected) <= epsilon;
}

CS2::RCS::RecoilSample Sample(uint64_t time, float pitch, float yaw,
                              int shots = 2, int weapon = 7,
                              uintptr_t pawn = 0x100000) {
    return {pawn, 100, weapon, shots, pitch, yaw, time, time};
}
}

int main() {
    using namespace CS2::RCS;
    RCSConfig config{};
    config.enabled = true;
    config.smoothing = 0.f;
    config.maxCorrection = 100.f;
    RCSController controller;

    auto first = controller.Update(config, Sample(1000, 1.f, .5f), 1000);
    Expect(!first.produced, "first sample establishes baseline");
    auto delta = controller.Update(config, Sample(1010, 1.5f, .7f, 3), 1010);
    Expect(delta.produced && Near(delta.x, -.2f) && Near(delta.y, -.5f),
           "delta compensation is mathematically correct");
    auto duplicate = controller.Update(config, Sample(1010, 1.5f, .7f, 3), 1010);
    Expect(!duplicate.produced && duplicate.reason == ResetReason::DuplicateSample,
           "duplicate subtick sample is ignored");

    auto stopped = Sample(1300, 1.5f, .7f, 3);
    stopped.lastShotMs = 1000;
    Expect(!controller.Update(config, stopped, 1300).produced,
           "stale firing state resets controller");
    Expect(!controller.Update(config, Sample(1310, 2.f, 1.f, 4), 1310).produced,
           "first sample after reset is baseline only");

    Expect(!controller.Update(config, Sample(1320, 2.2f, 1.1f, 5, 16), 1320).produced,
           "weapon change resets controller");
    Expect(!controller.Update(config, Sample(1330, 2.3f, 1.2f, 6, 16, 0x200000), 1330).produced,
           "pawn change resets controller");

    controller.Reset();
    config.maxCorrection = .25f;
    (void)controller.Update(config, Sample(2000, 0.f, 0.f), 2000);
    auto limited = controller.Update(config, Sample(2010, 10.f, 10.f, 3), 2010);
    Expect(limited.produced && Near(limited.x, -.25f) && Near(limited.y, -.25f),
           "maximum correction clamps abrupt output");

    controller.Reset();
    config.maxCorrection = 100.f;
    config.weaponScale[7] = .5f;
    (void)controller.Update(config, Sample(3000, 0.f, 0.f), 3000);
    auto scaled = controller.Update(config, Sample(3010, 2.f, 1.f, 3), 3010);
    Expect(scaled.produced && Near(scaled.x, -.5f) && Near(scaled.y, -1.f),
           "weapon-specific multiplier is applied");

    auto invalid = Sample(3020, (std::numeric_limits<float>::quiet_NaN)(), 0.f, 4);
    Expect(!controller.Update(config, invalid, 3020).produced,
           "NaN input is rejected without a crash");

    config.enabled = false;
    Expect(!controller.Update(config, Sample(4000, 1.f, 1.f), 4000).produced,
           "disabled controller emits no correction");

    std::cout << "RCS tests: " << (failures ? "FAILED" : "PASSED")
              << " (failures=" << failures << ")\n";
    return failures == 0 ? 0 : 1;
}
