#pragma once

#include <string>

namespace logger {
inline constexpr int info = 0;
inline constexpr int success = 1;
inline constexpr int fail = 2;
void log(const std::string& message, int type = info);
}
