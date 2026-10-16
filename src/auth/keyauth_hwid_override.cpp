#include "hardware_fingerprint.h"

#include "third_party/keyauth/x64/lib/utils.hpp"




std::string utils::get_hwid() {
    static const std::string fingerprint = OmniGhost::Auth::ComputeHardwareFingerprint();
    return fingerprint.empty() ? "none" : fingerprint;
}
