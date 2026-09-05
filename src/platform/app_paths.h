#pragma once
#include <filesystem>
#include <string>

namespace OmniGhost::Paths {
std::filesystem::path Executable();
std::filesystem::path InstallDirectory();
std::wstring InstallIdentity();
std::filesystem::path LocalData();
// Immutable native components which Windows/vendor APIs require as physical
// files. Kept separate from mutable user state and never contains OmniGhost.exe.
std::filesystem::path NativeRuntime();
std::filesystem::path Configs();
std::filesystem::path Cs2Configs();
std::filesystem::path Logs();
std::filesystem::path Cache();
std::filesystem::path Updates();
std::filesystem::path Backups();
bool EnsureUserDirectories();
void MigrateLegacyUserData();
bool IsProtectedInstallPath(const std::filesystem::path& relativePath);
}
