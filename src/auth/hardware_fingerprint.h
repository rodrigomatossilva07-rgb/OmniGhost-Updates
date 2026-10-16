#pragma once

#include <string>

namespace OmniGhost::Auth {



[[nodiscard]] std::string ComputeHardwareFingerprint() noexcept;

}
