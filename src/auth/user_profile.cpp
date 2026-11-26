#include "user_profile.h"
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "src/platform/app_paths.h"
#include "src/platform/text_encoding.h"
#include <iostream>

namespace OmniGhost::Auth {

namespace {

constexpr char PROFILE_FILE[] = "user_profile.json";
constexpr char LICENSE_FILE[] = "license.dat";

std::filesystem::path GetProfilePath() {
    return OmniGhost::Paths::LocalData() / PROFILE_FILE;
}

std::filesystem::path GetLicensePath() {
    return OmniGhost::Paths::LocalData() / LICENSE_FILE;
}

} // anonymous namespace

bool ProfileManager::Initialize() {
    std::lock_guard lock(mutex_);
    if (initialized_) return true;

    // Inicializa Discord IPC
    DiscordIPC::Initialize();

    // Carrega perfil local
    if (!LoadFromDisk()) {
        // Perfil padrão
        profile_ = UserProfile{};
        profile_.created_unix = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        profile_.username = "User";
        profile_.role = UserRole::Guest;
    }

    // Tenta obter perfil do Discord
    UpdateDiscordProfile();

    initialized_ = true;
    return true;
}

void ProfileManager::Shutdown() {
    std::lock_guard lock(mutex_);
    if (!initialized_) return;
    Save();
    DiscordIPC::Shutdown();
    initialized_ = false;
}

void ProfileManager::UpdateDiscordProfile() {
    auto discord_opt = DiscordIPC::TryGetCurrentProfile();
    if (discord_opt) {
        profile_.discord_profile = *discord_opt;
        profile_.discord_connected = true;
        // Atualiza username se não definido localmente
        if (profile_.username.empty() || profile_.username == "User") {
            profile_.username = discord_opt->username;
        }
    } else {
        profile_.discord_connected = false;
    }
}

bool ProfileManager::LoginLocal(std::string_view username, std::string_view password) {
    // TODO: Implementar autenticação real com backend
    // Por agora, apenas define username local
    std::lock_guard lock(mutex_);
    profile_.username = username;
    profile_.last_login_unix = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    Save();
    NotifyChange();
    return true;
}

void ProfileManager::Logout() {
    std::lock_guard lock(mutex_);
    profile_.username.clear();
    profile_.email.clear();
    profile_.role = UserRole::Guest;
    profile_.license = LicenseInfo{};
    profile_.discord_connected = false;
    profile_.discord_profile = DiscordIPC::Profile{};
    Save();
    NotifyChange();
}

bool ProfileManager::ApplyLicense(std::string_view license_key) {
    // TODO: Validar licença com backend
    // Por agora, aceita qualquer chave não-vazia e define como Premium
    if (license_key.empty()) return false;

    std::lock_guard lock(mutex_);
    profile_.license.license_key = license_key;
    profile_.license.expires_unix = 0; // Vitalício por padrão
    profile_.role = UserRole::Premium;
    Save();
    NotifyChange();
    return true;
}

bool ProfileManager::HasFeature(std::string_view feature) const {
    std::lock_guard lock(mutex_);
    if (!profile_.HasValidLicense()) return false;
    for (const auto& f : profile_.license.features) {
        if (f == feature) return true;
    }
    // Premium tem todas features por padrão
    return profile_.IsPremiumOrAbove();
}

bool ProfileManager::Save() {
    std::lock_guard lock(mutex_);

    try {
        nlohmann::json j;

        // Discord
        if (profile_.discord_connected) {
            j["discord"]["id"] = profile_.discord_profile.id;
            j["discord"]["username"] = profile_.discord_profile.username;
            j["discord"]["display_name"] = profile_.discord_profile.display_name;
            j["discord"]["avatar_url"] = profile_.discord_profile.avatar_url;
            j["discord"]["has_avatar"] = profile_.discord_profile.has_avatar;
            j["discord"]["animated_avatar"] = profile_.discord_profile.animated_avatar;
        }
        j["discord_connected"] = profile_.discord_connected;

        // Local
        j["username"] = profile_.username;
        j["email"] = profile_.email;
        j["role"] = static_cast<int>(profile_.role);
        j["created_unix"] = profile_.created_unix;
        j["last_login_unix"] = profile_.last_login_unix;
        j["last_ip"] = profile_.last_ip;
        j["hwid"] = profile_.hwid;
        j["cloud_sync_enabled"] = profile_.cloud_sync_enabled;
        j["last_synced_config"] = profile_.last_synced_config;

        // License
        j["license"]["key"] = profile_.license.license_key;
        j["license"]["expires_unix"] = profile_.license.expires_unix;
        j["license"]["hwid"] = profile_.license.hwid;
        j["license"]["hwid_locked"] = profile_.license.hwid_locked;
        j["license"]["features"] = profile_.license.features;
        j["license"]["max_devices"] = profile_.license.max_devices;

        auto path = GetProfilePath();
        std::filesystem::create_directories(path.parent_path());

        std::ofstream file(path);
        file << j.dump(4);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ProfileManager] Save error: " << e.what() << std::endl;
        return false;
    }
}

bool ProfileManager::LoadFromDisk() {
    try {
        auto path = GetProfilePath();
        if (!std::filesystem::exists(path)) return false;

        std::ifstream file(path);
        nlohmann::json j;
        file >> j;

        // Discord
        profile_.discord_connected = j.value("discord_connected", false);
        if (profile_.discord_connected && j.contains("discord")) {
            profile_.discord_profile.id = j["discord"].value("id", "");
            profile_.discord_profile.username = j["discord"].value("username", "");
            profile_.discord_profile.display_name = j["discord"].value("display_name", "");
            profile_.discord_profile.avatar_url = j["discord"].value("avatar_url", "");
            profile_.discord_profile.has_avatar = j["discord"].value("has_avatar", false);
            profile_.discord_profile.animated_avatar = j["discord"].value("animated_avatar", false);
        }

        // Local
        profile_.username = j.value("username", "User");
        profile_.email = j.value("email", "");
        profile_.role = static_cast<UserRole>(j.value("role", 0));
        profile_.created_unix = j.value("created_unix", 0);
        profile_.last_login_unix = j.value("last_login_unix", 0);
        profile_.last_ip = j.value("last_ip", "");
        profile_.hwid = j.value("hwid", "");
        profile_.cloud_sync_enabled = j.value("cloud_sync_enabled", true);
        profile_.last_synced_config = j.value("last_synced_config", "");

        // License
        if (j.contains("license")) {
            profile_.license.license_key = j["license"].value("key", "");
            profile_.license.expires_unix = j["license"].value("expires_unix", 0);
            profile_.license.hwid = j["license"].value("hwid", "");
            profile_.license.hwid_locked = j["license"].value("hwid_locked", false);
            profile_.license.features = j["license"].value("features", std::vector<std::string>{});
            profile_.license.max_devices = j["license"].value("max_devices", 1);
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ProfileManager] Load error: " << e.what() << std::endl;
        return false;
    }
}

void ProfileManager::SetProfileChangeCallback(ProfileChangeCallback cb) {
    change_callback_ = std::move(cb);
}

void ProfileManager::NotifyChange() {
    if (change_callback_) {
        change_callback_(profile_);
    }
}

} // namespace OmniGhost::Auth