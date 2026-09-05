#pragma once

#include <string>

namespace LauncherUpdates::UpdateTime {

[[nodiscard]] bool ParseIsoDatePrefix(const std::string& iso, int& year, int& month, int& day);
[[nodiscard]] std::string DateDisplay(const std::string& iso);
[[nodiscard]] std::string RelativePublishedTime(const std::string& iso);

} // namespace LauncherUpdates::UpdateTime
