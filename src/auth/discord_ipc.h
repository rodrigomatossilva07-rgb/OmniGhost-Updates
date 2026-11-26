#pragma once

#include <string>
#include <optional>
#include <string_view>

namespace OmniGhost::Auth::DiscordIPC {

struct Profile {
    std::string id;              // Discord user ID (snowflake)
    std::string username;        // username#discriminator (legacy) or username
    std::string display_name;    // global_name or username
    std::string avatar_url;      // full CDN URL (png/gif)
    bool has_avatar = false;
    bool animated_avatar = false;
};

// Tenta ler perfil do Discord via named pipe IPC (Windows only)
// Retorna nullopt se Discord não estiver rodando ou IPC falhar
[[nodiscard]] std::optional<Profile> TryGetCurrentProfile();

// Verifica se IPC está disponível (Discord rodando)
[[nodiscard]] bool IsAvailable();

// Formata URL do avatar dado ID + hash
[[nodiscard]] std::string MakeAvatarURL(std::string_view user_id, std::string_view avatar_hash);

// Inicializa cache interno (chamar no startup)
void Initialize();

// Limpa cache (chamar no shutdown)
void Shutdown();

// Callback opcional para quando perfil muda
using ProfileCallback = void(*)(const Profile&);
void SetProfileChangedCallback(ProfileCallback cb);

} // namespace OmniGhost::Auth::DiscordIPC