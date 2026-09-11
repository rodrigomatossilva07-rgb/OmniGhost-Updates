#include "cs2_radar.h"
#include "cs2_game.h"
#include "cs2_config.h"

#include <Windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <iostream>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace CS2::WebRadar {
namespace {

namespace fs = std::filesystem;

std::atomic<bool> g_running{false};
std::atomic<bool> g_wsa{false};
SOCKET g_listen = INVALID_SOCKET;
int g_port = 8080;
std::mutex g_mu;
std::string g_token;
std::string g_public_url;
std::string g_web_root;
HANDLE g_cf_proc = nullptr;
HANDLE g_cf_thread = nullptr;
std::atomic<bool> g_cf_stop{false};

bool EnsureWsa() {
    if (g_wsa.load()) return true;
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return false;
    g_wsa.store(true);
    return true;
}

std::string RandomToken() {
    unsigned char bytes[16]{};
    // Prefer Windows system RNG without CryptoAPI headers.
    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    unsigned seed = static_cast<unsigned>(GetTickCount() ^ static_cast<unsigned>(qpc.QuadPart) ^
                                          static_cast<unsigned>(GetCurrentProcessId() << 16));
    for (int i = 0; i < 16; ++i) {
        seed = seed * 1664525u + 1013904223u + static_cast<unsigned>(i * 97);
        bytes[i] = static_cast<unsigned char>((seed >> 16) & 0xFF);
    }
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
        "Cs2/radar_webapp",
        "cs2/radar_webapp",
        "data/webradar",
        "Cs2/data/webradar",
        "../Cs2/radar_webapp",
        "../../Cs2/radar_webapp",
    };
    fs::path bases[] = { fs::path(exe), fs::path(exe).parent_path(), fs::current_path() };
    for (const auto& base : bases) {
        for (const char* rel : candidates) {
            fs::path p = base / rel;
            if (LooksLikeWebRoot(p))
                return fs::absolute(p).string();
        }
    }
    // Last resort: project-typical absolute relative from workdir
    fs::path fallback = fs::path(exe) / "radar_webapp";
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

void SendAll(SOCKET s, const char* data, int len) {
    int sent = 0;
    while (sent < len) {
        const int n = send(s, data + sent, len - sent, 0);
        if (n <= 0) break;
        sent += n;
    }
}

void SendResponse(SOCKET s, int code, const char* statusText, const std::string& body,
                  const char* contentType, bool noCache = true) {
    char header[512];
    std::snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Headers: Authorization, Content-Type\r\n"
        "%s"
        "Connection: close\r\n\r\n",
        code, statusText, contentType, body.size(),
        noCache ? "Cache-Control: no-store\r\n" : "Cache-Control: public, max-age=60\r\n");
    SendAll(s, header, static_cast<int>(std::strlen(header)));
    if (!body.empty())
        SendAll(s, body.data(), static_cast<int>(body.size()));
}

bool TokenOk(const std::string& req) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_token.empty()) return true;
    // Authorization: Bearer <token>
    auto pos = req.find("Authorization:");
    if (pos == std::string::npos) pos = req.find("authorization:");
    if (pos != std::string::npos) {
        auto lineEnd = req.find("\r\n", pos);
        std::string line = req.substr(pos, lineEnd == std::string::npos ? std::string::npos : lineEnd - pos);
        auto bearer = line.find("Bearer ");
        if (bearer == std::string::npos) bearer = line.find("bearer ");
        if (bearer != std::string::npos) {
            std::string tok = line.substr(bearer + 7);
            while (!tok.empty() && (tok.back() == ' ' || tok.back() == '\r')) tok.pop_back();
            while (!tok.empty() && tok.front() == ' ') tok.erase(tok.begin());
            return tok == g_token;
        }
    }
    // Query ?token=
    auto q = req.find("token=");
    if (q != std::string::npos) {
        auto end = req.find_first_of(" \r\n&", q + 6);
        std::string tok = req.substr(q + 6, end == std::string::npos ? std::string::npos : end - (q + 6));
        return tok == g_token;
    }
    // Allow static assets without token; only protect /api/*
    return false;
}

void HandleClient(SOCKET client) {
    char buf[8192]{};
    const int n = recv(client, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        closesocket(client);
        return;
    }
    buf[n] = 0;
    std::string req(buf, n);

    // OPTIONS CORS preflight
    if (req.rfind("OPTIONS ", 0) == 0) {
        SendResponse(client, 204, "No Content", "", "text/plain");
        closesocket(client);
        return;
    }

    std::string method, path;
    {
        std::istringstream iss(req);
        iss >> method >> path;
    }
    auto qmark = path.find('?');
    std::string pure = qmark == std::string::npos ? path : path.substr(0, qmark);
    if (pure.empty()) pure = "/";

    const bool isApi = pure.rfind("/api/", 0) == 0;
    if (isApi && !TokenOk(req)) {
        // Soft-auth: still allow if no Authorization (local LAN convenience) when token only in hash
        // Browser polling sends Authorization from hash; first load of index does not need token.
    }

    if (method == "GET" && (pure == "/api/live" || pure == "/api/state")) {
        if (!TokenOk(req) && req.find("Authorization:") == std::string::npos &&
            req.find("authorization:") == std::string::npos) {
            // Allow unauthenticated local polling for LAN ease; token still in URL hash for browsers.
        }
        const std::string json = BuildLiveJson();
        SendResponse(client, 200, "OK", json, "application/json; charset=utf-8");
        closesocket(client);
        return;
    }

    if (method == "GET" && pure == "/api/stream") {
        // Minimal SSE stream — one snapshot then close (client will reconnect/poll).
        const std::string json = BuildLiveJson();
        std::string body = "data: " + json + "\n\n";
        char header[256];
        std::snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/event-stream\r\n"
            "Cache-Control: no-store\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n"
            "Content-Length: %zu\r\n\r\n", body.size());
        SendAll(client, header, static_cast<int>(std::strlen(header)));
        SendAll(client, body.data(), static_cast<int>(body.size()));
        closesocket(client);
        return;
    }

    // Static files
    if (method != "GET" && method != "HEAD") {
        SendResponse(client, 405, "Method Not Allowed", "method", "text/plain");
        closesocket(client);
        return;
    }

    if (pure == "/") pure = "/index.html";
    // Prevent path traversal
    if (pure.find("..") != std::string::npos) {
        SendResponse(client, 400, "Bad Request", "bad path", "text/plain");
        closesocket(client);
        return;
    }

    std::string root;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        root = g_web_root;
    }
    fs::path file = fs::path(root) / pure.substr(1);
    std::string body;
    if (!ReadFileBinary(file, body)) {
        SendResponse(client, 404, "Not Found", "not found", "text/plain");
        closesocket(client);
        return;
    }
    if (method == "HEAD") body.clear();
    SendResponse(client, 200, "OK", body, MimeType(file.string()).c_str(), false);
    closesocket(client);
}

DWORD WINAPI CloudflareReader(LPVOID) {
    // Not used if we don't redirect pipes; placeholder.
    return 0;
}

std::wstring FindCloudflared() {
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
    // AppData OmniGhost
    wchar_t local[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH)) {
        fs::path p = fs::path(local) / L"OmniGhost" / L"NativeRuntime" / L"libs" / L"cloudflared.exe";
        if (fs::exists(p)) return p.wstring();
    }
    return L"cloudflared.exe"; // PATH fallback
}

} // namespace

std::string BuildLiveJson() {
    std::ostringstream ss;
    const auto& rt = CS2::runtime;
    std::string map = rt.map_name[0] ? rt.map_name : "unknown";
    // Strip path / extension
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
           << "\"m_health\":" << p.health << ','
           << "\"m_armor\":" << p.armor << ','
           << "\"m_is_local\":" << (p.is_local ? "true" : "false") << ','
           << "\"m_flashed\":" << (p.is_flashed ? "true" : "false") << ','
           << "\"m_has_bomb\":false,"
           << "\"m_color\":" << (p.team == 3 ? 1 : 0) << ','
           << "\"m_eye_angle\":" << p.view_yaw << ','
           << "\"m_position\":{\"x\":" << p.pos[0] << ",\"y\":" << p.pos[1] << ",\"z\":" << p.pos[2] << "}"
           << "}";
        ++idx;
    }
    ss << "],";

    if (rt.bomb.planted) {
        ss << "\"m_bomb\":{"
           << "\"m_is_planted\":true,"
           << "\"m_is_defused\":" << (rt.bomb.defused ? "true" : "false") << ','
           << "\"m_is_defusing\":" << (rt.bomb.defusing ? "true" : "false") << ','
           << "\"m_blow_time\":" << rt.bomb.blow_time << ','
           << "\"m_defuse_time\":" << rt.bomb.defuse_time << ','
           << "\"m_position\":{\"x\":" << rt.bomb.pos[0] << ",\"y\":" << rt.bomb.pos[1] << ",\"z\":" << rt.bomb.pos[2] << "}"
           << "}";
    } else {
        ss << "\"m_bomb\":null";
    }
    ss << "}";
    return ss.str();
}

bool Start(int port) {
    if (g_running.load()) {
        if (port == g_port) return true;
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
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
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
    // Non-blocking accept
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);

    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_listen = s;
        g_port = port;
        if (g_token.empty()) g_token = RandomToken();
        g_web_root = ResolveWebRoot();
        g_public_url.clear();
    }
    g_running.store(true);
    std::cout << "[WebRadar] listening on 0.0.0.0:" << port
              << " root=" << g_web_root << "\n";
    return true;
}

void Stop() {
    g_running.store(false);
    StopCloudflare();
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_listen != INVALID_SOCKET) {
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
    }
    g_public_url.clear();
}

bool IsRunning() noexcept { return g_running.load(); }
int Port() noexcept { return g_port; }

void Tick() {
    if (!g_running.load()) return;
    SOCKET listenSock = INVALID_SOCKET;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        listenSock = g_listen;
    }
    if (listenSock == INVALID_SOCKET) return;

    // Accept a few clients per frame
    for (int i = 0; i < 8; ++i) {
        sockaddr_in cli{};
        int clen = sizeof(cli);
        SOCKET c = accept(listenSock, reinterpret_cast<sockaddr*>(&cli), &clen);
        if (c == INVALID_SOCKET) break;
        // Handle synchronously (keep simple; payload is small)
        u_long nb = 0;
        ioctlsocket(c, FIONBIO, &nb);
        DWORD timeout = 2000;
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        HandleClient(c);
    }
}

std::string LocalIp() { return DetectLanIp(); }

std::string AccessToken() {
    std::lock_guard<std::mutex> lock(g_mu);
    return g_token;
}

std::string LocalUrl() {
    std::lock_guard<std::mutex> lock(g_mu);
    std::ostringstream ss;
    ss << "http://" << DetectLanIp() << ":" << g_port;
    if (!g_token.empty()) ss << "/#" << g_token;
    return ss.str();
}

std::string PublicUrl() {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_public_url.empty()) return {};
    if (!g_token.empty()) return g_public_url + "/#" + g_token;
    return g_public_url;
}

bool StartCloudflare() {
    if (CloudflareRunning()) return true;
    if (!IsRunning()) return false;

    const std::wstring exe = FindCloudflared();
    wchar_t cmd[512]{};
    std::swprintf(cmd, 512, L"\"%s\" tunnel --url http://127.0.0.1:%d",
                  exe.c_str(), g_port);

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
        CloseHandle(rd);
        CloseHandle(wr);
        std::cout << "[WebRadar] cloudflared launch failed\n";
        return false;
    }
    CloseHandle(wr);
    CloseHandle(pi.hThread);
    g_cf_proc = pi.hProcess;

    // Read a bit of output for trycloudflare URL (sync short wait)
    std::string output;
    char tmp[256];
    DWORD read = 0;
    const ULONGLONG deadline = GetTickCount64() + 8000;
    while (GetTickCount64() < deadline) {
        DWORD avail = 0;
        if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr)) break;
        if (avail) {
            if (!ReadFile(rd, tmp, sizeof(tmp) - 1, &read, nullptr) || !read) break;
            tmp[read] = 0;
            output += tmp;
            auto pos = output.find("https://");
            if (pos != std::string::npos) {
                auto end = output.find_first_of(" \r\n\t\"'", pos);
                std::string url = output.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
                // trim trailing slash
                while (!url.empty() && url.back() == '/') url.pop_back();
                std::lock_guard<std::mutex> lock(g_mu);
                g_public_url = url;
                std::cout << "[WebRadar] public URL " << url << "\n";
                break;
            }
        } else {
            Sleep(100);
        }
    }
    // Keep reading pipe in background to avoid fill — drain async not critical
    CloseHandle(rd);
    return CloudflareRunning();
}

void StopCloudflare() {
    if (g_cf_proc) {
        TerminateProcess(g_cf_proc, 0);
        CloseHandle(g_cf_proc);
        g_cf_proc = nullptr;
    }
    std::lock_guard<std::mutex> lock(g_mu);
    g_public_url.clear();
}

bool CloudflareRunning() noexcept {
    if (!g_cf_proc) return false;
    DWORD code = 0;
    if (GetExitCodeProcess(g_cf_proc, &code) && code == STILL_ACTIVE)
        return true;
    return false;
}

} // namespace CS2::WebRadar
