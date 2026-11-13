#pragma once
#include <filesystem>
#include <string>

namespace OmniGhost::Paths {
std::filesystem::path Executable();
std::filesystem::path InstallDirectory();
std::wstring InstallIdentity();
std::filesystem::path LocalData();


std::filesystem::path NativeRuntime();
std::filesystem::path RuntimeRoot();
std::filesystem::path Configs();
std::filesystem::path Cs2Configs();
std::filesystem::path Logs();
std::filesystem::path Cache();
std::filesystem::path Updates();
std::filesystem::path Backups();
bool EnsureUserDirectories();
void MigrateLegacyUserData();

}
