#include "discord_ipc.h"
#include <Windows.h>
#include <string>
#include <optional>
#include <array>
#include <charconv>
#include <mutex>
#include <chrono>
#include <thread>

namespace OmniGhost::Auth::DiscordIPC {

namespace {

// Discord IPC opcodes
constexpr uint32_t OP_HANDSHAKE = 0;
constexpr uint32_t OP_FRAME = 1;
constexpr uint32_t OP_CLOSE = 2;
constexpr uint32_t OP_PING = 3;
constexpr uint32_t OP_PONG = 4;
constexpr uint32_t MAX_FRAME_SIZE = 64 * 1024;

// Cliente ID do OmniGhost (precisa ser registado no Discord Dev Portal para OAuth fallback)
// Para IPC local, qualquer string funciona, mas usamos um ID consistente
constexpr std::string_view CLIENT_ID = "1189059358221160550"; // OmniGhost placeholder

// Cache
std::optional<Profile> g_cached_profile;
std::mutex g_cache_mutex;
ProfileCallback g_callback = nullptr;
bool g_initialized = false;
std::chrono::steady_clock::time_point g_last_attempt{};
constexpr auto CACHE_TTL = std::chrono::minutes(5);

// ---- Helpers de leitura/escrita exata ----
bool ReadExact(HANDLE pipe, void* dst, size_t size) {
    uint8_t* out = static_cast<uint8_t*>(dst);
    while (size > 0) {
        DWORD read = 0;
        if (!ReadFile(pipe, out, static_cast<DWORD>(size), &read, nullptr) || read == 0)
            return false;
        out += read;
        size -= read;
    }
    return true;
}

bool WriteExact(HANDLE pipe, const void* src, size_t size) {
    const uint8_t* in = static_cast<const uint8_t*>(src);
    while (size > 0) {
        DWORD written = 0;
        if (!WriteFile(pipe, in, static_cast<DWORD>(size), &written, nullptr) || written == 0)
            return false;
        in += written;
        size -= written;
    }
    return true;
}

bool WriteFrame(HANDLE pipe, uint32_t opcode, std::string_view payload) {
    uint32_t header[2] = { opcode, static_cast<uint32_t>(payload.size()) };
    return WriteExact(pipe, header, sizeof(header)) &&
           WriteExact(pipe, payload.data(), payload.size());
}

bool ReadFrame(HANDLE pipe, uint32_t& opcode, std::string& payload) {
    uint32_t header[2] = {};
    if (!ReadExact(pipe, header, sizeof(header)))
        return false;
    if (header[1] > MAX_FRAME_SIZE)
        return false;

    opcode = header[0];
    payload.resize(header[1]);
    return header[1] == 0 || ReadExact(pipe, payload.data(), payload.size());
}

// Extrai string JSON simples (sem dependência nlohmann para manter leve)
std::optional<std::string> JsonString(std::string_view json, std::string_view key) {
    std::string needle = "\"" + std::string(key) + "\"";
    size_t key_pos = json.find(needle);
    if (key_pos == std::string_view::npos)
        return std::nullopt;

    size_t pos = json.find(':', key_pos + needle.size());
    if (pos == std::string_view::npos)
        return std::nullopt;

    while (++pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {}
    if (pos >= json.size() || json[pos] != '"')
        return std::nullopt;

    std::string value;
    for (++pos; pos < json.size(); ++pos) {
        char ch = json[pos];
        if (ch == '"')
            return value;
        if (ch != '\\' || ++pos == json.size())
            value.push_back(ch);
        else {
            switch (json[pos]) {
                case '"': value.push_back('"'); break;
                case '\\': value.push_back('\\'); break;
                case '/': value.push_back('/'); break;
                case 'b': value.push_back('\b'); break;
                case 'f': value.push_back('\f'); break;
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                default: return std::nullopt;
            }
        }
    }
    return std::nullopt;
}

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                    value.data(), static_cast<int>(value.size()),
                                    nullptr, 0);
    if (!size) return {};
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                         value.data(), static_cast<int>(value.size()),
                         out.data(), size);
    return out;
}

// Tenta conectar a um pipe específico
std::optional<HANDLE> TryConnectPipe(uint32_t index) {
    std::wstring pipe_name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(index);
    HANDLE pipe = CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE,
                              0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (pipe != INVALID_HANDLE_VALUE)
        return pipe;
    return std::nullopt;
}

// Faz handshake e lê evento READY
std::optional<Profile> DoHandshakeAndReadProfile(HANDLE pipe) {
    std::string handshake = "{\"v\":1,\"client_id\":\"" + std::string(CLIENT_ID) + "\"}";
    if (!WriteFrame(pipe, OP_HANDSHAKE, handshake))
        return std::nullopt;

    // Lê até 8 frames procurando READY
    for (uint32_t frame = 0; frame < 8; ++frame) {
        uint32_t opcode = 0;
        std::string payload;
        if (!ReadFrame(pipe, opcode, payload))
            break;

        if (opcode == OP_PING) {
            // Responde pong com mesmo payload
            WriteFrame(pipe, OP_PONG, payload);
            continue;
        }
        if (opcode == OP_CLOSE)
            break;

        if (opcode == OP_FRAME) {
            auto evt = JsonString(payload, "evt");
            if (evt && *evt == "READY") {
                // Extrai dados do utilizador
                auto id = JsonString(payload, "id");
                auto username = JsonString(payload, "username");
                auto global_name = JsonString(payload, "global_name");
                auto avatar = JsonString(payload, "avatar");

                if (!id || !username)
                    break;

                // Valida ID numérico
                uint64_t numeric_id = 0;
                auto [end, err] = std::from_chars(id->data(), id->data() + id->size(), numeric_id);
                if (err != std::errc{} || end != id->data() + id->size())
                    break;

                Profile profile;
                profile.id = *id;
                profile.username = *username;
                profile.display_name = (global_name && !global_name->empty()) ? *global_name : *username;

                if (avatar && !avatar->empty()) {
                    profile.has_avatar = true;
                    profile.animated_avatar = avatar->rfind("a_", 0) == 0;
                    profile.avatar_url = MakeAvatarURL(*id, *avatar);
                } else {
                    // Avatar padrão baseado no ID
                    profile.avatar_url = "https://cdn.discordapp.com/embed/avatars/"
                                       + std::to_string((numeric_id >> 22) % 6) + ".png";
                }

                return profile;
            }
        }
    }
    return std::nullopt;
}

} // anonymous namespace

std::optional<Profile> TryGetCurrentProfile() {
    // Verifica cache
    {
        std::lock_guard lock(g_cache_mutex);
        if (g_cached_profile && std::chrono::steady_clock::now() - g_last_attempt < CACHE_TTL)
            return g_cached_profile;
    }

    // Tenta pipes 0-9
    for (uint32_t i = 0; i < 10; ++i) {
        auto pipe = TryConnectPipe(i);
        if (!pipe) continue;

        auto profile = DoHandshakeAndReadProfile(*pipe);
        CloseHandle(*pipe);

        if (profile) {
            std::lock_guard lock(g_cache_mutex);
            g_cached_profile = profile;
            g_last_attempt = std::chrono::steady_clock::now();
            if (g_callback) g_callback(*profile);
            return profile;
        }
    }

    // Atualiza timestamp mesmo se falhou (evita spam)
    g_last_attempt = std::chrono::steady_clock::now();
    return std::nullopt;
}

bool IsAvailable() {
    for (uint32_t i = 0; i < 10; ++i) {
        std::wstring pipe_name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
        HANDLE pipe = CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE,
                                  0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe);
            return true;
        }
    }
    return false;
}

std::string MakeAvatarURL(std::string_view user_id, std::string_view avatar_hash) {
    std::string url = "https://cdn.discordapp.com/avatars/";
    url += user_id;
    url += "/";
    url += avatar_hash;
    url += (avatar_hash.rfind("a_", 0) == 0) ? ".gif" : ".png";
    return url;
}

void Initialize() {
    if (g_initialized) return;
    g_initialized = true;
    // Warm-up attempt
    TryGetCurrentProfile();
}

void Shutdown() {
    std::lock_guard lock(g_cache_mutex);
    g_cached_profile.reset();
    g_callback = nullptr;
    g_initialized = false;
}

void SetProfileChangedCallback(ProfileCallback cb) {
    g_callback = cb;
}

} // namespace OmniGhost::Auth::DiscordIPC