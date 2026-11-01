#pragma once

#include <mutex>
#include <string>

namespace CS2::RCS {

class RCSDebug {
public:
    void Set(bool enabled, const char* stage, const char* detail);
    [[nodiscard]] std::string Snapshot() const;

private:
    mutable std::mutex mutex_;
    std::string line_ = "RCS idle";
};

} // namespace CS2::RCS
