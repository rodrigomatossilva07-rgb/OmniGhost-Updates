#pragma once
#include <optional>

namespace OmniGhost::Update {


std::optional<int> RunUpdaterModeIfRequested(int argc, wchar_t** argv);
}
