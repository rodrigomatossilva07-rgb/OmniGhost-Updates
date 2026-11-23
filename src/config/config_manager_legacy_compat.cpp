#include "config_manager.h"
#include "config_games.h"
#include "../globals.h"
#include "../platform/app_paths.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace config_manager {
namespace {
constexpr std::string_view kExtension = ".ogcfg";
std::vector<CommunityConfigInfo> g_communityConfigs;

std::string GamePrefix() {
    switch (g_activeGame) {
    case ActiveGame::CS2: return "cs2_";
    case ActiveGame::Rust: return "rust_";
    case ActiveGame::Warzone: return "warzone_";
    case ActiveGame::Apex: return "apex_";
    case ActiveGame::Fortnite: return "fortnite_";
    case ActiveGame::FiveM:
    default: return "fivem_";
    }
}

std::string SafeName(std::string name) {
    name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char value) {
        return !(std::isalnum(value) || value == '_' || value == '-' || value == ' ');
    }), name.end());
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
    return name;
}

std::filesystem::path ConfigPath(const std::string& requestedName) {
    auto name = SafeName(requestedName);
    if (name.empty()) return {};
    if (!name.ends_with(kExtension)) name += kExtension;
    return std::filesystem::path(GetConfigFolder()) / name;
}

bool WriteFile(const std::filesystem::path& path, const std::string& contents) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!output) return false;
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
    }
    return !error;
}

bool ReadFile(const std::filesystem::path& path, std::string& contents) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    contents.assign(std::istreambuf_iterator<char>(input), {});
    return !input.bad();
}

std::string ProfileFileName(std::string_view profile) {
    return GamePrefix() + "profile_" + SafeName(std::string(profile));
}

void SetStatus(std::string value) {
    last_status = std::move(value);
}
}

std::vector<SavedConfigInfo> saved_list;
char new_config_name[64]{};
std::string last_status;
std::string active_config_name;
std::string active_profile_name{"Default"};
std::string active_config_id;

void RefreshList() {
    saved_list.clear();
    std::error_code error;
    const std::filesystem::path directory(GetConfigFolder());
    std::filesystem::create_directories(directory, error);
    if (error) {
        SetStatus("Nao foi possivel abrir a pasta de configuracoes.");
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (error) break;
        if (!entry.is_regular_file() || entry.path().extension() != kExtension) continue;
        SavedConfigInfo info;
        info.name = entry.path().stem().string();
        info.path = entry.path().string();
        const auto timestamp = entry.last_write_time(error);
        if (!error) {
            const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timestamp.time_since_epoch()).count();
            info.date_str = std::to_string(seconds);
        }
        saved_list.push_back(std::move(info));
    }
    std::sort(saved_list.begin(), saved_list.end(), [](const auto& left, const auto& right) {
        return left.name < right.name;
    });
}

void Initialize() {
    OmniGhost::Paths::EnsureUserDirectories();
    RefreshList();
    if (active_config_name.empty() && !saved_list.empty()) active_config_name = saved_list.front().name;
    SetStatus("Configuracoes prontas.");
}

void ActivateCurrentGameConfig() {
    if (active_config_name.empty()) {
        active_config_name = GamePrefix() + "default";
    }
    const auto path = ConfigPath(active_config_name);
    if (std::filesystem::exists(path)) {
        LoadFromFile(active_config_name);
    } else {
        SaveToFile(active_config_name);
    }
}

bool SaveToFile(const std::string& name) {
    const auto cleanName = SafeName(name);
    const auto path = ConfigPath(cleanName);
    if (path.empty()) {
        SetStatus("Nome de configuracao invalido.");
        return false;
    }
    if (!WriteFile(path, SerializeAll())) {
        SetStatus("Nao foi possivel gravar a configuracao.");
        return false;
    }
    active_config_name = cleanName;
    RefreshList();
    SetStatus("Configuracao gravada.");
    return true;
}

bool LoadFromFile(const std::string& name) {
    std::string contents;
    const auto path = ConfigPath(name);
    if (path.empty() || !ReadFile(path, contents)) {
        SetStatus("Configuracao nao encontrada.");
        return false;
    }
    if (!DeserializeAll(contents)) {
        if (last_status.empty()) SetStatus("Configuracao invalida.");
        return false;
    }
    active_config_name = SafeName(name);
    SetStatus("Configuracao carregada.");
    return true;
}

bool DeleteConfigFile(const std::string& name) {
    const auto path = ConfigPath(name);
    std::error_code error;
    const bool removed = !path.empty() && std::filesystem::remove(path, error);
    if (!removed || error) {
        SetStatus("Nao foi possivel apagar a configuracao.");
        return false;
    }
    if (active_config_name == SafeName(name)) active_config_name.clear();
    RefreshList();
    SetStatus("Configuracao apagada.");
    return true;
}

bool DuplicateConfigFile(const std::string& name, const std::string& newName) {
    std::error_code error;
    const auto source = ConfigPath(name);
    const auto destination = ConfigPath(newName);
    if (source.empty() || destination.empty() || !std::filesystem::exists(source) || std::filesystem::exists(destination)) {
        SetStatus("Nao foi possivel duplicar a configuracao.");
        return false;
    }
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, error);
    if (error) { SetStatus("Nao foi possivel duplicar a configuracao."); return false; }
    RefreshList();
    SetStatus("Configuracao duplicada.");
    return true;
}

bool RenameConfigFile(const std::string& name, const std::string& newName) {
    std::error_code error;
    const auto source = ConfigPath(name);
    const auto destination = ConfigPath(newName);
    if (source.empty() || destination.empty() || !std::filesystem::exists(source) || std::filesystem::exists(destination)) {
        SetStatus("Nao foi possivel renomear a configuracao.");
        return false;
    }
    std::filesystem::rename(source, destination, error);
    if (error) { SetStatus("Nao foi possivel renomear a configuracao."); return false; }
    if (active_config_name == SafeName(name)) active_config_name = SafeName(newName);
    RefreshList();
    SetStatus("Configuracao renomeada.");
    return true;
}

bool ExportJson(const std::string& fileName) { return WriteFile(std::filesystem::path(GetConfigFolder()) / fileName, SerializeAll()); }
bool ImportJson(const std::string& fileName) { return LoadFromFile(fileName); }

void FlushActiveConfig() { if (!active_config_name.empty()) SaveToFile(active_config_name); }
void TickAutoSave() { FlushActiveConfig(); }
const char* ActiveConfigName() { return active_config_name.c_str(); }

void ResetActiveGameToDefaults() {
    if (g_activeGame == ActiveGame::CS2 || g_activeGame == ActiveGame::Rust) ResetCS2ToDefaults();
}
void ResetToDefaults() { ResetActiveGameToDefaults(); active_profile_name = "Default"; }
void ResetSettingsOnly() { ResetActiveGameToDefaults(); }

void ApplyMinimalProfileForActiveGame() { if (g_activeGame == ActiveGame::CS2 || g_activeGame == ActiveGame::Rust) ApplyMinimalCS2(); }
void ApplyVisualProfileForActiveGame() { if (g_activeGame == ActiveGame::CS2 || g_activeGame == ActiveGame::Rust) ApplyVisualCS2(); }
void ApplyGameProfile(GameProfile profile) {
    switch (profile) {
    case GameProfile::Minimal: ApplyMinimalProfileForActiveGame(); active_profile_name = "Minimal"; break;
    case GameProfile::Visual: ApplyVisualProfileForActiveGame(); active_profile_name = "Visual"; break;
    case GameProfile::Custom: active_profile_name = "Custom"; break;
    case GameProfile::Default: default: ResetActiveGameToDefaults(); active_profile_name = "Default"; break;
    }
}
bool SaveCustomGameProfile() { SaveNamedProfile("Custom"); return true; }
const char* CurrentGameProfileName() { return active_profile_name.c_str(); }
void SaveNamedProfile(const char* name) { if (name && *name) SaveToFile(ProfileFileName(name)); }
void ApplyNamedProfile(const char* name) {
    if (!name || !*name) return;
    if (LoadFromFile(ProfileFileName(name))) active_profile_name = name;
}

void SaveOrder() {}
void MoveConfig(int from, int to) {
    if (from < 0 || to < 0 || static_cast<size_t>(from) >= saved_list.size() || static_cast<size_t>(to) >= saved_list.size()) return;
    const auto moved = saved_list[static_cast<size_t>(from)];
    saved_list.erase(saved_list.begin() + from);
    saved_list.insert(saved_list.begin() + to, moved);
}
bool ToggleFavoriteConfig(const std::string& name) {
    const auto item = std::find_if(saved_list.begin(), saved_list.end(), [&](const auto& value) { return value.name == SafeName(name); });
    if (item == saved_list.end()) return false;
    item->favorite = !item->favorite;
    return true;
}
bool IsFavoriteConfig(const std::string& name) {
    const auto item = std::find_if(saved_list.begin(), saved_list.end(), [&](const auto& value) { return value.name == SafeName(name); });
    return item != saved_list.end() && item->favorite;
}

bool CopyToClipboard() { SetStatus("Copiar configuracao nao esta disponivel nesta build."); return false; }
bool PasteFromClipboard() { SetStatus("Colar configuracao nao esta disponivel nesta build."); return false; }
void OpenConfigFolder() { ShellExecuteA(nullptr, "open", GetConfigFolder().c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
void MigrateLegacyConfigs() { RefreshList(); }

bool UploadCloudConfig(const std::string&) { SetStatus("Sincronizacao cloud nao configurada."); return false; }
bool DownloadCloudConfig(const std::string&) { SetStatus("Sincronizacao cloud nao configurada."); return false; }
bool DeleteCloudConfig(const std::string&) { SetStatus("Sincronizacao cloud nao configurada."); return false; }

bool DownloadDevConfig(const std::string& name) { return DownloadDevConfigVersioned(name); }
bool UploadDevConfig(const std::string& name) { return UploadDevConfigVersioned(name, name, {}, {}, {}); }

bool RefreshCommunityConfigs(CommunityScope, const std::string&, const std::string&) { g_communityConfigs.clear(); SetStatus("Comunidade ainda nao esta configurada."); return false; }
bool RefreshCommunityConfigsForGame(const std::string&, CommunityScope scope, const std::string& search, const std::string& sort) { return RefreshCommunityConfigs(scope, search, sort); }
const std::vector<CommunityConfigInfo>& CommunityConfigs() { return g_communityConfigs; }
bool PublishCommunityConfig(const std::string&, const std::string&, const std::string&, const std::string&, const std::string&, const std::string&) { SetStatus("Publicacao comunitaria nao configurada."); return false; }
bool ApplyCommunityConfig(const std::string&, bool) { SetStatus("Comunidade ainda nao esta configurada."); return false; }
bool DownloadCommunityToLibrary(const std::string&) { SetStatus("Comunidade ainda nao esta configurada."); return false; }
bool PreviewCommunityConfig(const std::string&) { SetStatus("Comunidade ainda nao esta configurada."); return false; }
bool ToggleCommunityFavorite(const std::string&, bool) { SetStatus("Comunidade ainda nao esta configurada."); return false; }
bool RateCommunityConfig(const std::string&, int) { SetStatus("Comunidade ainda nao esta configurada."); return false; }
bool ReportCommunityConfig(const std::string&, const std::string&) { SetStatus("Comunidade ainda nao esta configurada."); return false; }
bool WithdrawCommunityConfig(const std::string&) { SetStatus("Comunidade ainda nao esta configurada."); return false; }
bool RevertCommunityConfig() { SetStatus("Nao existe configuracao comunitaria para reverter."); return false; }

}
