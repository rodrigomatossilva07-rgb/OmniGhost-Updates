#pragma once
#include <string>

namespace Rust {
namespace OffsetApi {

// Downloads https://www.cheatoffsets.com/api/games/rust/current
// Parses markdown C++ fences into data/rust_offsets.json next to the EXE (or path).
// Returns true if a newer/valid dump was written.
// Uses WinHTTP; requires network on the OmniGhost PC (not the game PC).
bool FetchAndSave(const char* out_json_path, std::string* status_out = nullptr);

// Optional: If-None-Match ETag stored in data/rust_offsets.etag
bool FetchIfChanged(const char* out_json_path, std::string* status_out = nullptr);

} // namespace OffsetApi
} // namespace Rust
