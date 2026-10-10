#pragma once

#include <string>

namespace OmniGhost::Auth {

// Returns only a domain-separated SHA-256 digest. Raw hardware identifiers
// never leave this function and are never persisted by OmniGhost.
[[nodiscard]] std::string ComputeHardwareFingerprint() noexcept;

} // namespace OmniGhost::Auth
