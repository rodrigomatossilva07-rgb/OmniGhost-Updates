#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace OmniGhost::Diagnostics {

// Creates a bounded, redacted support ZIP. It intentionally includes only the
// caller-provided summary and current log streams; settings/auth/licence files
// are never added.
bool CreatePackage(std::string_view summary,
                   std::filesystem::path& outputPath,
                   std::string& error);

} // namespace OmniGhost::Diagnostics
