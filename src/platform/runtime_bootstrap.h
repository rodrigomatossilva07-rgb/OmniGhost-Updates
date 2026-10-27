#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace OmniGhost::RuntimeBootstrap {
enum class Result {
    Continue,
    Failed
};



Result Prepare();



[[nodiscard]] bool RetireLegacyPrivateInstall(std::wstring& error);



[[nodiscard]] std::filesystem::path PrivateRuntimePath(std::wstring_view relativePath);
[[nodiscard]] bool ValidatePrivateRuntimeFile(std::wstring_view relativePath,
                                              std::wstring& error);


[[nodiscard]] bool EmbeddedRuntimeFileAvailable(std::wstring_view relativePath) noexcept;
[[nodiscard]] bool ValidateEmbeddedRuntimeBundle(std::wstring& error) noexcept;



[[nodiscard]] bool MaterializePrivateRuntimeFile(std::wstring_view relativePath,
                                                 std::wstring& error);
}
