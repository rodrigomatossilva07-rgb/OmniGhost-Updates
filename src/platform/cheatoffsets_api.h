#pragma once
#include <string>

namespace OmniGhost {
namespace CheatOffsets {

// Games published on https://www.cheatoffsets.com/api/games
// slug matches API products supported by an installed runtime adapter.
bool FetchGame(const char* slug, const char* out_json_path, std::string* status = nullptr);
bool FetchGameIfChanged(const char* slug, const char* out_json_path, std::string* status = nullptr);

// Convenience: known OmniGhost games → data/<name>_offsets.json next to EXE
bool FetchRust(std::string* status = nullptr);
bool FetchCS2(std::string* status = nullptr);
bool FetchApex(std::string* status = nullptr);
bool FetchFiveM(std::string* status = nullptr);
bool FetchWarzone(std::string* status = nullptr);

// Fetch all supported games (best-effort). Returns how many succeeded.
int FetchAll(std::string* status = nullptr);

} // namespace CheatOffsets
} // namespace OmniGhost
