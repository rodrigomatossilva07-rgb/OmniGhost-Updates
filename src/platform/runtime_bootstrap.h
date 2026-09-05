#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace OmniGhost::RuntimeBootstrap {
enum class Result {
    Continue,
    Failed
};

// Verifies/repairs unavoidable native vendor files without copying or
// relaunching OmniGhost.exe. The caller always remains the main process.
Result Prepare();

// Stops and removes only the obsolete immutable installation created by older
// builds under %LOCALAPPDATA%\OmniGhost (old EXE + old libs). User data remains.
[[nodiscard]] bool RetireLegacyPrivateInstall(std::wstring& error);

// Resolves and validates a file from the generated private-runtime manifest.
// Callers never execute a same-named binary from PATH or beside the portable EXE.
[[nodiscard]] std::filesystem::path PrivateRuntimePath(std::wstring_view relativePath);
[[nodiscard]] bool ValidatePrivateRuntimeFile(std::wstring_view relativePath,
                                              std::wstring& error);
// Checks whether an immutable runtime entry is present inside the PE bundle.
// This does not create files and is safe for launcher readiness checks.
[[nodiscard]] bool EmbeddedRuntimeFileAvailable(std::wstring_view relativePath) noexcept;

// Materializes one manifest-owned dependency on demand and verifies its hash.
// Intended for optional subprocesses such as cloudflared; it never launches it.
[[nodiscard]] bool MaterializePrivateRuntimeFile(std::wstring_view relativePath,
                                                 std::wstring& error);
}
