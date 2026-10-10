#include "hardware_fingerprint.h"

#include "../../keyauth/x64/lib/utils.hpp"

// The prebuilt KeyAuth SDK resolves this symbol at link time. Supplying it in
// the application replaces the SDK's SID-only implementation without changing
// its authenticated request protocol.
std::string utils::get_hwid() {
    static const std::string fingerprint = OmniGhost::Auth::ComputeHardwareFingerprint();
    return fingerprint.empty() ? "none" : fingerprint;
}
