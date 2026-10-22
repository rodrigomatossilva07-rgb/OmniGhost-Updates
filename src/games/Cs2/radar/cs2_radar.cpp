#include "cs2_radar.h"
#include "radar_http.h"
#include "cs2_game.h"
#include "../config/cs2_config.h"
#include "../src/platform/runtime_bootstrap.h"
#include "../src/platform/embedded_resources.h"

#include <Windows.h>
#include <bcrypt.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <iostream>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "bcrypt.lib")

namespace CS2::WebRadar {
struct RadarPlayer {
    int ent_index = 0, team = 0, health = 0, armor = 0;
    bool is_local = false, flashed = false;
    float view_yaw = 0.f, pos[3]{};
    char name[64]{};
};
struct RadarSnapshot {
    bool in_match = false;
    char map_name[64]{};
    BombState bomb{};
    std::vector<RadarPlayer> players;
};
std::string BuildLiveJson(const Runtime& rt);
std::string BuildLiveJson(const RadarSnapshot& rt);
namespace {

namespace fs = std::filesystem;

std::atomic<bool> g_running{false};
std::atomic<bool> g_wsa{false};
std::mutex g_wsa_mu;
SOCKET g_listen = INVALID_SOCKET;


SOCKET g_wake_read = INVALID_SOCKET;
SOCKET g_wake_write = INVALID_SOCKET;
std::atomic<int> g_port{8080};
std::mutex g_mu;
std::string g_token;
std::string g_public_url;
std::string g_cloudflare_status;
std::string g_web_root;
std::string g_live_json = "{\"m_map\":\"unknown\",\"m_round_phase\":\"warmup\",\"m_players\":[],\"m_bomb\":null}";
RadarSnapshot g_pending_runtime{};
bool g_pending_runtime_valid = false;
std::atomic<ULONGLONG> g_next_json_refresh{0};
std::thread g_server_thread;
HANDLE g_cf_proc = nullptr;
std::thread g_cf_reader;
std::atomic<bool> g_cf_stop{false};

bool EnsureWsa() {
    if (g_wsa.load()) return true;
    std::lock_guard<std::mutex> lock(g_wsa_mu);
    if (g_wsa.load()) return true;
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return false;
    g_wsa.store(true);
    return true;
}

std::string RandomToken() {
    unsigned char bytes[16]{};
    if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        return {};
    static const char* hex = "0123456789abcdef";
    std::string out(32, '0');
    for (int i = 0; i < 16; ++i) {
        out[i * 2] = hex[bytes[i] >> 4];
        out[i * 2 + 1] = hex[bytes[i] & 0xf];
    }
    return out;
}

std::string DetectLanIp() {
    ULONG size = 0;
    GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                          GAA_FLAG_SKIP_DNS_SERVER, nullptr, nullptr, &size);
    if (!size) return "127.0.0.1";
    std::string buf(size, '\0');
    auto* addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                             GAA_FLAG_SKIP_DNS_SERVER, nullptr, addrs, &size) != NO_ERROR)
        return "127.0.0.1";
    for (auto* a = addrs; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            auto* sa = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
            char ip[64]{};
            inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip));
            if (ip[0] && std::strcmp(ip, "127.0.0.1") != 0)
                return ip;
        }
    }
    return "127.0.0.1";
}

std::string ExeDir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    fs::path p(path);
    return p.parent_path().string();
}

bool LooksLikeWebRoot(const fs::path& p) {
    return fs::exists(p / "index.html") && fs::exists(p / "renderers");
}

std::string ResolveWebRoot() {
    const std::string exe = ExeDir();
    const char* candidates[] = {
        "radar_webapp",
        "src/games/Cs2/radar_webapp",
        "cs2/radar_webapp",
        "data/webradar",
        "src/games/Cs2/data/webradar",
        "../src/games/Cs2/radar_webapp",
        "../../src/games/Cs2/radar_webapp",
        "../../../src/games/Cs2/radar_webapp",
    };
    std::vector<fs::path> bases;
    bases.push_back(fs::path(exe));
    bases.push_back(fs::path(exe).parent_path());
    bases.push_back(fs::current_path());

    {
        fs::path walk = fs::path(exe);
        for (int i = 0; i < 8 && !walk.empty(); ++i) {
            bases.push_back(walk);
            if (fs::exists(walk / "OmiGhost.vcxproj") || fs::exists(walk / "OmniGhost.sln"))
                bases.push_back(walk);
            walk = walk.parent_path();
        }
    }
    for (const auto& base : bases) {
        for (const char* rel : candidates) {
            fs::path p = base / rel;
            std::error_code ec;
            if (LooksLikeWebRoot(p))
                return fs::absolute(p, ec).string();
        }
    }
    fs::path fallback = fs::path(exe) / "radar_webapp";
    std::cout << "[WebRadar] web root not found near EXE; fallback=" << fallback
              << " (embed cs2/webradar/* or copy radar_webapp next to OmniGhost.exe)\n";
    return fallback.string();
}

std::string MimeType(const std::string& path) {
    auto lower = path;
    for (char& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    auto ends = [&](const char* sfx) {
        const size_t n = std::strlen(sfx);
        return lower.size() >= n && lower.compare(lower.size() - n, n, sfx) == 0;
    };
    if (ends(".html") || ends(".htm")) return "text/html; charset=utf-8";
    if (ends(".js")) return "application/javascript; charset=utf-8";
    if (ends(".css")) return "text/css; charset=utf-8";
    if (ends(".json") || ends(".json5")) return "application/json; charset=utf-8";
    if (ends(".png")) return "image/png";
    if (ends(".jpg") || ends(".jpeg")) return "image/jpeg";
    if (ends(".webp")) return "image/webp";
    if (ends(".svg")) return "image/svg+xml";
    if (ends(".ico")) return "image/x-icon";
    if (ends(".woff2")) return "font/woff2";
    if (ends(".map")) return "application/json";
    return "application/octet-stream";
}

bool ReadFileBinary(const fs::path& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

std::string JsonEscape(const char* s) {
    std::string o;
    if (!s) return o;
    for (const char* p = s; *p; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c == '"' || c == '\\') { o.push_back('\\'); o.push_back(static_cast<char>(c)); }
        else if (c < 0x20) { char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); o += buf; }
        else o.push_back(static_cast<char>(c));
    }
    return o;
}

std::string Response(int code, const char* statusText, const std::string& body,
                     const char* contentType, bool noCache = true, bool sendBody = true) {
    char header[512];
    std::snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: http://127.0.0.1\r\n"
        "Access-Control-Allow-Headers: Authorization, Content-Type\r\n"
        "%s"
        "Connection: close\r\n\r\n",
        code, statusText, contentType, body.size(),
        noCache ? "Cache-Control: no-store\r\n" : "Cache-Control: public, max-age=60\r\n");
    std::string response(header);
    if (sendBody) response += body;
    return response;
}

std::string LiveJsonSnapshot() {
    std::lock_guard<std::mutex> lock(g_mu);
    return g_live_json;
}

bool TokenOk(const std::string& req) {
    std::lock_guard<std::mutex> lock(g_mu);
    return Http::TokenOk(req, g_token);
}

std::string HandleRequest(const std::string& req) {

    if (req.rfind("OPTIONS ", 0) == 0) {
        return Response(204, "No Content", "", "text/plain");
    }

    std::string method, path;
    {
        std::istringstream iss(req);
        iss >> method >> path;
    }
    auto qmark = path.find('?');
    std::string pure = qmark == std::string::npos ? path : path.substr(0, qmark);
    if (pure.empty()) pure = "/";
    const bool sendBody = method != "HEAD";
    if (pure.front() != '/' || pure.find("..") != std::string::npos ||
        pure.find('\\') != std::string::npos || pure.find(':') != std::string::npos ||
        pure.rfind("//", 0) == 0)
        return Response(400, "Bad Request", "bad path", "text/plain", true, sendBody);

    const bool isApi = pure.rfind("/api/", 0) == 0;
    if (isApi && !TokenOk(req)) {
        return Response(401, "Unauthorized", "token required", "text/plain", true, sendBody);
    }

    if ((method == "GET" || method == "HEAD") && (pure == "/api/live" || pure == "/api/state")) {
        const std::string json = LiveJsonSnapshot();
        return Response(200, "OK", json, "application/json; charset=utf-8", true, sendBody);
    }

    if ((method == "GET" || method == "HEAD") && pure == "/healthz") {
        return Response(200, "OK", "ok", "text/plain", true, sendBody);
    }

    if ((method == "GET" || method == "HEAD") && pure == "/api/stream") {

        const std::string json = LiveJsonSnapshot();
        std::string body = "data: " + json + "\n\n";
        return Response(200, "OK", body, "text/event-stream", true, sendBody);
    }


    if (method != "GET" && method != "HEAD") {
        return Response(405, "Method Not Allowed", "method", "text/plain");
    }

    if (pure == "/") pure = "/index.html";
    std::string root;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        root = g_web_root;
    }
    fs::path file = fs::path(root) / pure.substr(1);
    std::string body;
    if (!ReadFileBinary(file, body)) {
        std::string logical = "cs2/webradar/" + pure.substr(1);
        for (char& ch : logical) {
            if (ch == '\\') ch = '/';
        }
        if (const auto embedded = OmniGhost::LoadEmbeddedResource(logical)) {
            body.assign(reinterpret_cast<const char*>(embedded->data()), embedded->size());
        } else {
            return Response(404, "Not Found", "not found", "text/plain", true, sendBody);
        }
    }
    return Response(200, "OK", body, MimeType(file.string()).c_str(), false, sendBody);
}

void ServerLoop() {
    struct Client {
        SOCKET socket = INVALID_SOCKET;
        std::string request, response;
        std::size_t sent = 0;
        ULONGLONG deadline = 0;
    };
    std::vector<Client> clients;
    clients.reserve(32);
    while (g_running.load()) {
        const ULONGLONG now = GetTickCount64();
        if (now >= g_next_json_refresh) {
            RadarSnapshot snapshot{};
            bool haveSnapshot = false;
            {
                std::lock_guard<std::mutex> lock(g_mu);
                if (g_pending_runtime_valid) {
                    snapshot = g_pending_runtime;
                    haveSnapshot = true;
                }
            }
            if (haveSnapshot) {
                std::string json = BuildLiveJson(snapshot);
                std::lock_guard<std::mutex> lock(g_mu);
                g_live_json = std::move(json);


                g_next_json_refresh = now + (snapshot.in_match ? 67u : 500u);
            } else {
                g_next_json_refresh = now + 100u;
            }
        }
        SOCKET listenSock = INVALID_SOCKET;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            listenSock = g_listen;
        }
        if (listenSock == INVALID_SOCKET) break;

        fd_set readable{};
        fd_set writable{};
        FD_ZERO(&readable);
        FD_ZERO(&writable);



        FD_SET(listenSock, &readable);


        SOCKET wakeSock = INVALID_SOCKET;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            wakeSock = g_wake_read;
        }
        if (wakeSock != INVALID_SOCKET) FD_SET(wakeSock, &readable);
        for (const auto& client : clients) {
            if (client.response.empty()) FD_SET(client.socket, &readable);
            else FD_SET(client.socket, &writable);
        }
        timeval pollInterval{0, 5000};
        if (select(0, &readable, &writable, nullptr, &pollInterval) == SOCKET_ERROR) {




            g_running.store(false, std::memory_order_release);
            break;
        }

        if (wakeSock != INVALID_SOCKET && FD_ISSET(wakeSock, &readable)) {
            char wakeByte = 0;
            while (recv(wakeSock, &wakeByte, 1, 0) > 0) {}
        }
        if (!g_running.load())
            break;
        for (auto it = clients.begin(); it != clients.end();) {
            bool close = GetTickCount64() >= it->deadline;
            if (!close && it->response.empty() && FD_ISSET(it->socket, &readable)) {
                char buffer[4096];
                const int received = recv(it->socket, buffer, sizeof(buffer), 0);
                if (received > 0) {
                    it->request.append(buffer, static_cast<std::size_t>(received));
                    if (it->request.size() > 8192) close = true;
                    else if (it->request.find("\r\n\r\n") != std::string::npos) {
                        it->response = HandleRequest(it->request);
                        it->request.clear();


                        it->deadline = GetTickCount64() + 2000u;
                    }
                } else if (received == 0 || WSAGetLastError() != WSAEWOULDBLOCK) close = true;
            }
            if (!close && !it->response.empty() && FD_ISSET(it->socket, &writable)) {
                const auto remaining = it->response.size() - it->sent;
                const int chunk = static_cast<int>((std::min)(remaining, std::size_t{64 * 1024}));
                const int sent = send(it->socket, it->response.data() + it->sent, chunk, 0);
                if (sent > 0) {
                    it->sent += static_cast<std::size_t>(sent);
                    close = it->sent == it->response.size();
                } else if (sent == 0 || WSAGetLastError() != WSAEWOULDBLOCK) close = true;
            }
            if (close) {
                closesocket(it->socket);
                it = clients.erase(it);
            } else ++it;
        }
        if (FD_ISSET(listenSock, &readable)) {
            SOCKET accepted = accept(listenSock, nullptr, nullptr);
            if (accepted != INVALID_SOCKET) {
                u_long nonblocking = 1;
                if (ioctlsocket(accepted, FIONBIO, &nonblocking) != 0) {
                    closesocket(accepted);
                    continue;
                }





                constexpr size_t kMaxIncompleteHeaders = 8;
                constexpr size_t kMaxClients = 32;
                size_t incompleteHeaders = 0;
                for (const auto& client : clients)
                    if (client.response.empty()) ++incompleteHeaders;
                bool overloaded = clients.size() >= kMaxClients ||
                    incompleteHeaders >= kMaxIncompleteHeaders;
                if (overloaded) {
                    constexpr char kBusy[] =
                        "HTTP/1.1 429 Too Many Requests\r\n"
                        "Retry-After: 1\r\n"
                        "Connection: close\r\n"
                        "Content-Length: 0\r\n\r\n";
                    (void)send(accepted, kBusy, sizeof(kBusy) - 1, 0);
                    closesocket(accepted);
                    continue;
                }
                clients.push_back({accepted, {}, {}, 0, GetTickCount64() + 3000u});
            }
        }
    }
    for (const auto& client : clients) closesocket(client.socket);
}

std::wstring FindCloudflared() {
    std::wstring materializeError;
    if (OmniGhost::RuntimeBootstrap::MaterializePrivateRuntimeFile(
            L"libs/cloudflared.exe", materializeError)) {
        const fs::path embedded = OmniGhost::RuntimeBootstrap::PrivateRuntimePath(
            L"libs/cloudflared.exe");
        if (fs::is_regular_file(embedded))
            return embedded.wstring();
    }

    const std::string exe = ExeDir();
    const wchar_t* rels[] = {
        L"libs\\cloudflared.exe",
        L"cloudflared.exe",
        L"NativeRuntime\\libs\\cloudflared.exe",
    };
    for (const wchar_t* r : rels) {
        fs::path p = fs::path(exe) / r;
        if (fs::exists(p)) return p.wstring();
    }

    wchar_t local[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH)) {
        fs::path p = fs::path(local) / L"OmniGhost" / L"NativeRuntime" / L"libs" / L"cloudflared.exe";
        if (fs::exists(p)) return p.wstring();
    }
    return L"cloudflared.exe";
}

}




static double JsonSafe(double value) {
    return std::isfinite(value) ? value : 0.0;
}

std::string BuildLiveJson(const RadarSnapshot& rt) {
    std::ostringstream ss;
    std::string map = rt.map_name[0] ? rt.map_name : "unknown";

    {
        auto slash = map.find_last_of("/\\");
        if (slash != std::string::npos) map = map.substr(slash + 1);
        auto dot = map.find('.');
        if (dot != std::string::npos) map = map.substr(0, dot);
    }

    ss << "{\"m_map\":\"" << JsonEscape(map.c_str()) << "\","
       << "\"m_observed_idx\":-1,"
       << "\"m_round_phase\":\"" << (rt.in_match ? "live" : "warmup") << "\","
       << "\"m_players\":[";

    bool first = true;
    int idx = 0;
    for (const auto& p : rt.players) {
        if (!first) ss << ',';
        first = false;
        ss << "{"
           << "\"m_idx\":" << (p.ent_index > 0 ? p.ent_index : idx) << ','
           << "\"m_name\":\"" << JsonEscape(p.name) << "\","
           << "\"m_team\":" << p.team << ','
           << "\"m_health\":" << JsonSafe(p.health) << ','
           << "\"m_armor\":" << JsonSafe(p.armor) << ','
           << "\"m_is_local\":" << (p.is_local ? "true" : "false") << ','
           << "\"m_flashed\":" << (p.flashed ? "true" : "false") << ','
           << "\"m_has_bomb\":false,"
           << "\"m_color\":" << (p.team == 3 ? 1 : 0) << ','
           << "\"m_eye_angle\":" << JsonSafe(p.view_yaw) << ','
           << "\"m_position\":{\"x\":" << JsonSafe(p.pos[0])
           << ",\"y\":" << JsonSafe(p.pos[1]) << ",\"z\":" << JsonSafe(p.pos[2]) << "}"
           << "}";
        ++idx;
    }
    ss << "],";

    if (rt.bomb.planted) {
        ss << "\"m_bomb\":{"
           << "\"m_is_planted\":true,"
           << "\"m_is_defused\":" << (rt.bomb.defused ? "true" : "false") << ','
           << "\"m_is_defusing\":" << (rt.bomb.defusing ? "true" : "false") << ','
           << "\"m_blow_time\":" << JsonSafe(rt.bomb.blow_time) << ','
           << "\"m_defuse_time\":" << JsonSafe(rt.bomb.defuse_time) << ','
           << "\"m_position\":{\"x\":" << JsonSafe(rt.bomb.pos[0])
           << ",\"y\":" << JsonSafe(rt.bomb.pos[1]) << ",\"z\":" << JsonSafe(rt.bomb.pos[2]) << "}"
           << "}";
    } else {
        ss << "\"m_bomb\":null";
    }
    ss << "}";
    return ss.str();
}

std::string BuildLiveJson(const Runtime& rt) {
    RadarSnapshot compact{};
    compact.in_match = rt.in_match;
    std::memcpy(compact.map_name, rt.map_name, sizeof(compact.map_name));
    compact.bomb = rt.bomb;
    compact.players.reserve(rt.players.size());
    for (const auto& player : rt.players) {
        RadarPlayer out{};
        out.ent_index = player.ent_index; out.team = player.team;
        out.health = player.health; out.armor = player.armor;
        out.is_local = player.is_local; out.flashed = player.is_flashed;
        out.view_yaw = player.view_yaw;
        std::memcpy(out.pos, player.pos, sizeof(out.pos));
        std::memcpy(out.name, player.name, sizeof(out.name));
        compact.players.push_back(out);
    }
    return BuildLiveJson(compact);
}

std::string BuildLiveJson() {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_pending_runtime_valid)
        return "{\"m_map\":\"unknown\",\"m_observed_idx\":-1,\"m_round_phase\":\"warmup\",\"m_players\":[],\"m_bomb\":null}";
    return BuildLiveJson(g_pending_runtime);
}

bool Start(int port) {
    if (g_running.load()) {
        if (port == g_port.load()) return true;
        Stop();
    }
    if (!EnsureWsa()) {
        std::cout << "[WebRadar] WSAStartup failed\n";
        return false;
    }
    if (port <= 0 || port > 65535) port = 8080;

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;

    BOOL yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;


    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<u_short>(port));
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cout << "[WebRadar] bind failed port=" << port << " err=" << WSAGetLastError() << "\n";
        closesocket(s);
        return false;
    }
    if (listen(s, 16) != 0) {
        closesocket(s);
        return false;
    }

    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);



    SOCKET wakeRead = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    SOCKET wakeWrite = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (wakeRead != INVALID_SOCKET && wakeWrite != INVALID_SOCKET) {
        sockaddr_in wakeBind{};
        wakeBind.sin_family = AF_INET;
        wakeBind.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        wakeBind.sin_port = 0;
        if (bind(wakeRead, reinterpret_cast<sockaddr*>(&wakeBind), sizeof(wakeBind)) == 0) {
            sockaddr_in wakeAddr{};
            int wakeLen = sizeof(wakeAddr);
            if (getsockname(wakeRead, reinterpret_cast<sockaddr*>(&wakeAddr), &wakeLen) == 0) {
                if (connect(wakeWrite, reinterpret_cast<sockaddr*>(&wakeAddr), wakeLen) != 0) {
                    closesocket(wakeRead);
                    closesocket(wakeWrite);
                    wakeRead = wakeWrite = INVALID_SOCKET;
                }
            }
        } else {
            closesocket(wakeRead);
            closesocket(wakeWrite);
            wakeRead = wakeWrite = INVALID_SOCKET;
        }
    }

    const std::string token = RandomToken();
    if (token.empty()) {
        closesocket(s);
        if (wakeRead != INVALID_SOCKET) closesocket(wakeRead);
        if (wakeWrite != INVALID_SOCKET) closesocket(wakeWrite);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_listen = s;
        g_wake_read = wakeRead;
        g_wake_write = wakeWrite;
        g_port.store(port);
        if (g_token.empty()) g_token = token;
        g_web_root = ResolveWebRoot();
        g_public_url.clear();
        g_cloudflare_status.clear();
        g_pending_runtime_valid = false;
        g_next_json_refresh = 0;
    }
    g_running.store(true);
    g_server_thread = std::thread(ServerLoop);
    std::cout << "[WebRadar] listening on 127.0.0.1:" << port
              << " root=" << g_web_root << "\n";
    {
        std::error_code ec;
        const bool hasIndex = fs::exists(fs::path(g_web_root) / "index.html", ec);
        if (!hasIndex)
            std::cout << "[WebRadar] AVISO: index.html em falta em " << g_web_root
                      << " — UI usa recursos embutidos cs2/webradar/ se existirem.\n";
    }
    return true;
}

void Stop() {
    g_running.store(false);



    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (g_wake_write != INVALID_SOCKET) {
            char wake = 1;
            (void)send(g_wake_write, &wake, 1, 0);
        }
    }
    if (g_server_thread.joinable())
        g_server_thread.join();
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (g_listen != INVALID_SOCKET) {
            closesocket(g_listen);
            g_listen = INVALID_SOCKET;
        }
        if (g_wake_read != INVALID_SOCKET) {
            closesocket(g_wake_read);
            g_wake_read = INVALID_SOCKET;
        }
        if (g_wake_write != INVALID_SOCKET) {
            closesocket(g_wake_write);
            g_wake_write = INVALID_SOCKET;
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_pending_runtime_valid = false;
        g_next_json_refresh = 0;
    }
    StopCloudflare();
    {
        std::lock_guard<std::mutex> lock(g_wsa_mu);
        if (g_wsa.exchange(false))
            WSACleanup();
    }
}

bool IsRunning() noexcept { return g_running.load(); }
int Port() noexcept { return g_port.load(); }

void SubmitSnapshot(const Runtime& snapshot) {
    if (!g_running.load()) return;
    static ULONGLONG nextSnapshot = 0;
    const ULONGLONG now = GetTickCount64();
    if (now < nextSnapshot) return;


    nextSnapshot = now + (snapshot.in_match ? 67 : 500);





    static thread_local RadarSnapshot compact;
    compact.in_match = snapshot.in_match;
    std::memcpy(compact.map_name, snapshot.map_name, sizeof(compact.map_name));
    compact.bomb = snapshot.bomb;
    compact.players.clear();
    compact.players.reserve(snapshot.players.size() + (snapshot.local_pawn ? 1u : 0u));



    if (snapshot.local_pawn) {
        RadarPlayer local{};
        local.team = snapshot.local_team;
        local.health = snapshot.local_health;
        local.is_local = true;
        local.view_yaw = snapshot.local_view_yaw;
        std::memcpy(local.pos, snapshot.local_pos, sizeof(local.pos));
        std::snprintf(local.name, sizeof(local.name), "Local");
        compact.players.push_back(local);
    }
    for (const auto& player : snapshot.players) {
        RadarPlayer out{};
        out.ent_index = player.ent_index;
        out.team = player.team;
        out.health = player.health;
        out.armor = player.armor;
        out.is_local = player.is_local;
        out.flashed = player.is_flashed;
        out.view_yaw = player.view_yaw;
        std::memcpy(out.pos, player.pos, sizeof(out.pos));
        std::memcpy(out.name, player.name, sizeof(out.name));
        compact.players.push_back(out);
    }
    std::lock_guard<std::mutex> lock(g_mu);
    std::swap(g_pending_runtime, compact);
    g_pending_runtime_valid = true;
}

std::string LocalIp() { return DetectLanIp(); }

std::string AccessToken() {
    std::lock_guard<std::mutex> lock(g_mu);
    return g_token;
}

std::string LocalUrl() {
    std::lock_guard<std::mutex> lock(g_mu);
    std::ostringstream ss;
    ss << "http://127.0.0.1:" << g_port.load();
    if (!g_token.empty()) ss << "/#" << g_token;
    return ss.str();
}

std::string PublicUrl() {



    if (!CloudflareRunning()) return {};
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_public_url.empty()) return {};
    if (!g_token.empty()) return g_public_url + "/#" + g_token;
    return g_public_url;
}

std::string CloudflareStatus() {
    std::lock_guard<std::mutex> lock(g_mu);
    return g_cloudflare_status;
}

bool StartCloudflare() {
    if (CloudflareRunning()) return true;
    if (!IsRunning()) return false;




    g_cf_stop.store(true);
    HANDLE staleProcess = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        staleProcess = g_cf_proc;
        g_cf_proc = nullptr;
    }
    if (staleProcess) {
        TerminateProcess(staleProcess, 0);
        WaitForSingleObject(staleProcess, 1000);
        CloseHandle(staleProcess);
    }
    if (g_cf_reader.joinable())
        g_cf_reader.join();
    g_cf_stop.store(false);
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_public_url.clear();
        g_cloudflare_status = "A iniciar túnel...";
    }

    const std::wstring exe = FindCloudflared();
    wchar_t cmd[512]{};


    std::swprintf(cmd, 512, L"\"%s\" tunnel --no-autoupdate --protocol http2 --url http://127.0.0.1:%d",
                  exe.c_str(), g_port.load());

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::wstring mutableCmd(cmd);
    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        const DWORD launchError = GetLastError();
        CloseHandle(rd);
        CloseHandle(wr);
        std::lock_guard<std::mutex> lock(g_mu);
        g_cloudflare_status = launchError == ERROR_FILE_NOT_FOUND
            ? "cloudflared.exe não foi encontrado. Instala-o ou adiciona-o à pasta libs."
            : "Não foi possível iniciar cloudflared (erro " + std::to_string(launchError) + ").";
        std::cout << "[WebRadar] cloudflared launch failed\n";
        return false;
    }
    CloseHandle(wr);
    CloseHandle(pi.hThread);
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_cf_proc = pi.hProcess;
    }



    g_cf_reader = std::thread([rd]() {
        constexpr uint64_t kTunnelStartupTimeoutMs = 15000u;
        const uint64_t startedMs = GetTickCount64();
        std::string output;
        char tmp[512];
        while (!g_cf_stop.load()) {
            if (GetTickCount64() - startedMs >= kTunnelStartupTimeoutMs) {
                bool hasUrl = false;
                {
                    std::lock_guard<std::mutex> lock(g_mu);
                    hasUrl = !g_public_url.empty();
                }
                if (!hasUrl) {
                    HANDLE timedOutProcess = nullptr;
                    {
                        std::lock_guard<std::mutex> lock(g_mu);
                        g_cloudflare_status =
                            "O Cloudflare não respondeu em 15 segundos. Nova tentativa em breve.";
                        timedOutProcess = g_cf_proc;
                        g_cf_proc = nullptr;
                    }
                    if (timedOutProcess) {
                        TerminateProcess(timedOutProcess, ERROR_TIMEOUT);
                        WaitForSingleObject(timedOutProcess, 1000);
                        CloseHandle(timedOutProcess);
                    }
                    break;
                }
            }
            DWORD avail = 0;
            if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr)) break;
            if (!avail) {
                Sleep(50);
                continue;
            }
            DWORD read = 0;
            if (!ReadFile(rd, tmp, sizeof(tmp) - 1, &read, nullptr) || !read) break;
            tmp[read] = 0;
            output.append(tmp, read);
            {
                std::lock_guard<std::mutex> lock(g_mu);
                g_cloudflare_status = output.size() > 220 ? output.substr(output.size() - 220) : output;
            }
            std::size_t pos = 0;
            std::string url;
            while ((pos = output.find("https://", pos)) != std::string::npos) {
                const auto end = output.find_first_of(" \r\n\t\"'", pos);
                const std::string candidate = output.substr(pos,
                    end == std::string::npos ? std::string::npos : end - pos);
                if (candidate.find(".trycloudflare.com") != std::string::npos) {
                    url = candidate;
                    break;
                }
                pos += 8;
            }
            if (!url.empty()) {
                while (!url.empty() && url.back() == '/') url.pop_back();
                {
                    std::lock_guard<std::mutex> lock(g_mu);
                    if (g_public_url.empty()) g_public_url = url;
                    g_cloudflare_status = "Túnel ligado.";
                }
                if (output.size() > 4096) output.erase(0, pos);
            } else if (output.size() > 8192) {
                output.erase(0, output.size() - 1024);
            }
        }
        if (!g_cf_stop.load()) {
            std::lock_guard<std::mutex> lock(g_mu);
            if (g_public_url.empty() && g_cloudflare_status.empty())
                g_cloudflare_status = "cloudflared terminou antes de criar o túnel.";
        }
        CloseHandle(rd);
    });
    return true;
}

void StopCloudflare() {
    g_cf_stop.store(true);
    HANDLE process = nullptr;
    { std::lock_guard<std::mutex> lock(g_mu); process = g_cf_proc; g_cf_proc = nullptr; }
    if (process) {
        TerminateProcess(process, 0);
        WaitForSingleObject(process, 1000);
        CloseHandle(process);
    }
    if (g_cf_reader.joinable())
        g_cf_reader.join();
    std::lock_guard<std::mutex> lock(g_mu);
    g_public_url.clear();
    g_cloudflare_status.clear();
}

bool CloudflareRunning() noexcept {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_cf_proc) return false;
    DWORD code = 0;
    if (GetExitCodeProcess(g_cf_proc, &code) && code == STILL_ACTIVE)
        return true;
    return false;
}

}
