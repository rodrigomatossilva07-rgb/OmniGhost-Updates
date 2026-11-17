#pragma once

#include "platform/expected.h"
#include <chrono>

namespace OmniGhost::Platform {

// =============================================================================
// IHardwareManager - Interface for hardware detection/management
// =============================================================================

struct HardwareState {
    enum class ProbeStage : uint8_t {
        Idle = 0,
        CheckingMakcu = 1,
        CheckingDma = 2,
        Complete = 3
    };

    ProbeStage stage = ProbeStage::Idle;
    bool makcuFinished = false;
    bool makcuAvailable = false;
    bool dmaFinished = false;
    bool dmaAvailable = false;
};

struct TimeoutConfig {
    std::chrono::milliseconds makcuTimeout{6500};
    std::chrono::milliseconds dmaTimeout{2000};
};

class IHardwareManager {
public:
    virtual ~IHardwareManager() = default;

    virtual void SetTimeouts(const TimeoutConfig& config) noexcept = 0;
    [[nodiscard]] virtual TimeoutConfig GetTimeouts() const noexcept = 0;

    virtual void BeginStartupProbe() = 0;
    virtual void Shutdown() noexcept = 0;
    virtual void Poll() = 0;
    [[nodiscard]] virtual HardwareState Snapshot() const = 0;
    [[nodiscard]] virtual bool Complete() const noexcept = 0;
};

} // namespace OmniGhost::Platform