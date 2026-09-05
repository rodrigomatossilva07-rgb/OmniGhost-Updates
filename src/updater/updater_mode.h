#pragma once
#include <optional>

namespace OmniGhost::Update {
// Returns no value during normal launcher startup. When --apply-update is
// present, runs the updater from a temporary copy of OmniGhost.exe.
std::optional<int> RunUpdaterModeIfRequested(int argc, wchar_t** argv);
}
