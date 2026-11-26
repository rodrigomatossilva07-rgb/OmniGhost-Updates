#pragma once

#include <string>
#include <optional>
#include <chrono>
#include <vector>
#include <mutex>
#include "discord_ipc.h"

namespace OmniGhost::Auth {

// ===== User Profile System =====
// Integra Discord IPC + licença local + roles

enum class UserRole : uint8_t {
    Guest = 0,        // Não autenticado
    Trial = 1,        // Trial temporário
    User = 2,         // Usuário padrão
    Premium = 3,      // Premium
    Developer = 4,    // Developer
    Admin = 5         // Admin
};

inline const char* RoleToString(UserRole role) {
    switch (role) {
        case UserRole::Guest: return "Guest";
        case UserRole::Trial: return "Trial";
        case UserRole::User: return "User";
        case UserRole::Premium: return "Premium";
        case UserRole::Developer: return "Developer";
        case UserRole::Admin: return "Admin";
        default: return "Unknown";
    }
}

inline ImU32 RoleToColor(UserRole role) {
    switch (role) {
        case UserRole::Guest: return IM_COL32(180, 180, 180, 255);
        case UserRole::Trial: return IM_COL32(100, 200, 255, 255);
        case UserRole::User: return IM_COL32(100, 255, 150, 255);
        case UserRole::Premium: return IM_COL32(255, 215, 0, 255);
        case UserRole::Developer: return IM_COL32(255, 100, 255, 255);
        case UserRole::Admin: return IM_COL32(255, 50, 50, 255);
        default: return IM_COL32(255, 255, 255, 255);
    }
}

struct LicenseInfo {
    std::string license_key;           // Chave de licença (se aplicável)
    int64_t expires_unix = 0;          // Expiração (0 = vitalício)
    std::string hwid;                  // Hardware ID vinculado
    bool hwid_locked = false;          // Se deve travar no HWID
    std::vector<std::string> features; // Features habilitadas
    int max_devices = 1;               // Máximo de dispositivos

    [[nodiscard]] bool IsExpired() const {
        if (expires_unix == 0) return false; // Vitalício
        auto now = std::chrono::system_clock::now();
        auto exp = std::chrono::system_clock::from_time_t(expires_unix);
        return now >= exp;
    }

    [[nodiscard]] int64_t DaysRemaining() const {
        if (expires_unix == 0) return -1; // Vitalício
        auto now = std::chrono::system_clock::now();
        auto exp = std::chrono::system_clock::from_time_t(expires_unix);
        auto diff = std::chrono::duration_cast<std::chrono::days>(exp - now);
        return diff.count();
    }
};

struct UserProfile {
    // Discord info (via IPC)
    DiscordIPC::Profile discord_profile;
    bool discord_connected = false;

    // Local account
    std::string username;              // Username local
    std::string email;                 // Email (se autenticado via launcher)
    UserRole role = UserRole::Guest;
    LicenseInfo license;

    // Metadata
    int64_t created_unix = 0;
    int64_t last_login_unix = 0;
    std::string last_ip;
    std::string hwid;

    // Settings sync
    bool cloud_sync_enabled = true;
    std::string last_synced_config;

    // Computed
    [[nodiscard]] std::string GetDisplayName() const {
        if (discord_connected && !discord_profile.display_name.empty())
            return discord_profile.display_name;
        return username.empty() ? "Guest" : username;
    }

    [[nodiscard]] std::string GetAvatarURL() const {
        if (discord_connected && discord_profile.has_avatar)
            return discord_profile.avatar_url;
        return "";
    }

    [[nodiscard]] bool HasValidLicense() const {
        return role != UserRole::Guest && !license.IsExpired();
    }

    [[nodiscard]] bool IsPremiumOrAbove() const {
        return role >= UserRole::Premium;
    }
};

class ProfileManager {
public:
    static ProfileManager& Instance() {
        static ProfileManager instance;
        return instance;
    }

    // Inicializa (carrega do disco, inicia Discord IPC)
    bool Initialize();

    // Shutdown
    void Shutdown();

    // Perfil atual
    const UserProfile& GetProfile() const { return profile_; }
    UserProfile& GetMutableProfile() { return profile_; }

    // Atualiza perfil do Discord (chamar periodicamente)
    void UpdateDiscordProfile();

    // Login/logout local
    bool LoginLocal(std::string_view username, std::string_view password);
    void Logout();

    // Aplica licença
    bool ApplyLicense(std::string_view license_key);

    // Verifica feature
    bool HasFeature(std::string_view feature) const;

    // Salva perfil no disco
    bool Save();

    // Callbacks
    using ProfileChangeCallback = std::function<void(const UserProfile&)>;
    void SetProfileChangeCallback(ProfileChangeCallback cb);

private:
    ProfileManager() = default;
    ~ProfileManager() { Shutdown(); }
    ProfileManager(const ProfileManager&) = delete;
    ProfileManager& operator=(const ProfileManager&) = delete;

    UserProfile profile_;
    ProfileChangeCallback change_callback_;
    std::mutex mutex_;
    bool initialized_ = false;

    bool LoadFromDisk();
    void NotifyChange();
};

// Convenience
inline ProfileManager& Profile() { return ProfileManager::Instance(); }
inline const UserProfile& CurrentProfile() { return Profile().GetProfile(); }
inline UserRole CurrentRole() { return CurrentProfile().role; }
inline bool IsPremium() { return CurrentProfile().IsPremiumOrAbove(); }

} // namespace OmniGhost::Auth