#include "rcs_debug.h"

namespace CS2::RCS {

void RCSDebug::Set(bool enabled, const char* stage, const char* detail) {
    if (!enabled) return;
    std::scoped_lock lock(mutex_);
    line_ = stage ? stage : "RCS";
    if (detail && *detail) {
        line_ += ": ";
        line_ += detail;
    }
}

std::string RCSDebug::Snapshot() const {
    std::scoped_lock lock(mutex_);
    return line_;
}

} // namespace CS2::RCS
