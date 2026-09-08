#include "fivem_radar.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <iphlpapi.h>
#include <shellapi.h>

#include "platform/app_paths.h"
#include "platform/embedded_resources.h"
#include "platform/path_security.h"
#include "platform/radar_access.h"
#include "platform/runtime_bootstrap.h"
#include "platform/scope_exit.h"
#include "platform/thread_utils.h"
#include "platform/unique_handle.h"
#include "platform/unique_socket.h"
#include "game/offsets.h"
#include "game/esp_manager.h"
#include "playerInfo/PedData.h"
#include "config/app_settings.h"
#include "../../DMALibrary/Memory/Memory.h"

#include <atomic>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <exception>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <cmath>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <cstdint>
#include <string_view>
#include <thread>
#include <stop_token>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace Fivem_Radar {
namespace {

namespace fs = std::filesystem;

std::atomic<bool> g_requested{ false };
std::atomic<bool> g_online{ false };
std::atomic<bool> g_starting{ false };
std::atomic<int>  g_port{ 8080 };
std::atomic<bool> g_lan{ false };
std::atomic<bool> g_public_access_requested{ false };
std::atomic<bool> g_cf_running{ false };

std::mutex g_data_mutex;
std::string g_json = "{\"players\":[]}";
std::string g_json_etag = "\"0\"";
std::string g_access_token;

std::mutex g_state_mutex;
std::string g_status = "Offline";
std::string g_lan_url;
SOCKET g_socket = INVALID_SOCKET;
std::jthread g_thread;

const char* kHtml = R"HTML(<!DOCTYPE html>
<html lang="pt"><head><meta charset="utf-8"/><title>OmniGhost FiveM Radar</title>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<style>
html,body{margin:0;height:100%;background:#050506;color:#eee;font-family:Segoe UI,system-ui,sans-serif}
#wrap{display:flex;flex-direction:column;height:100%}
header{padding:10px 16px;background:#0d0d10;border-bottom:1px solid #d4af3722;display:flex;justify-content:space-between}
header b{color:#d4af37;letter-spacing:1px}
#c{flex:1;margin:auto;background:#0a0a0c;border:1px solid #d4af3733;border-radius:8px;max-width:min(96vw,720px);max-height:min(96vw,720px)}
footer{padding:8px;color:#888;font-size:12px;text-align:center}
</style></head><body>
<div id="wrap">
<header><b>OMNIGHOST FIVEM RADAR</b><span id="info">A ligar...</span></header>
<canvas id="c" width="600" height="600"></canvas>
<footer>localhost · 100 ms · CT azul · T vermelho</footer>
</div>
<script>
const cv=document.getElementById('c'),ctx=cv.getContext('2d'),info=document.getElementById('info');
const accessToken=decodeURIComponent(location.hash.slice(1));
function draw(data){
  const W=cv.width,H=cv.height,cx=W/2,cy=H/2,scale=2.2;
  ctx.clearRect(0,0,W,H);
  ctx.strokeStyle='#d4af3722';ctx.lineWidth=1;
  for(let r=40;r<W/2;r+=40){ctx.beginPath();ctx.arc(cx,cy,r,0,Math.PI*2);ctx.stroke()}
  ctx.strokeStyle='#d4af3744';ctx.beginPath();ctx.moveTo(cx,0);ctx.lineTo(cx,H);ctx.moveTo(0,cy);ctx.lineTo(W,cy);ctx.stroke();
  ctx.fillStyle='#d4af37';ctx.beginPath();ctx.arc(cx,cy,5,0,Math.PI*2);ctx.fill();
  const players=data.players||[];
  info.textContent=players.length+' jogadores';
  for(const p of players){
    if(p.local) continue;
    const x=cx+p.x*scale,y=cy-p.y*scale;
    ctx.fillStyle=p.team===3?'#4fc3f7':(p.team===2?'#ef5350':'#aaa');
    ctx.beginPath();ctx.arc(x,y,5,0,Math.PI*2);ctx.fill();
    ctx.fillStyle='#ccc';ctx.font='11px sans-serif';
    ctx.fillText((p.name||'')+' '+(p.hp|0),x+7,y+3);
  }
}
async function tick(){
  try{
    const options={cache:'no-store',headers:accessToken?{'Authorization':'Bearer '+accessToken}:{}};
    const r=await fetch('/api/players',options);
    if(!r.ok)throw new Error('HTTP '+r.status);
    draw(await r.json());
  }catch(e){info.textContent='A aguardar dados...'}
  setTimeout(tick,100);
}
tick();
</script></body></html>
)HTML";

void SetStatus(const std::string& value)
{
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_status = value;
}

void SetSocket(SOCKET value)
{
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_socket = value;
}

std::string JsonEscape(const char* value)
{
    std::string result;
    if (!value)
        return result;

    for (const unsigned char c : std::string(value)) {
        switch (c) {
        case '\\': result += "\\\\"; break;
        case '"':  result += "\\\""; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (c < 0x20) {
                char escaped[7]{};
                std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
                result += escaped;
            } else {
                result.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    return result;
}

fs::path WebRoot()
{
    return OmniGhost::Paths::InstallDirectory() / L"data" / L"webradar_fivem";
}

std::optional<std::string> ResolveLanIPv4()
{
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buffer(size);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    ULONG result = GetAdaptersAddresses(AF_INET,
        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
        nullptr, adapters, &size);
    if (result == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(size);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        result = GetAdaptersAddresses(AF_INET,
            GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
            nullptr, adapters, &size);
    }
    if (result != NO_ERROR)
        return std::nullopt;

    std::optional<std::string> fallback;
    for (auto* adapter = adapters; adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        for (auto* address = adapter->FirstUnicastAddress; address; address = address->Next) {
            if (!address->Address.lpSockaddr || address->Address.lpSockaddr->sa_family != AF_INET)
                continue;
            const auto* in = reinterpret_cast<const sockaddr_in*>(address->Address.lpSockaddr);
            const uint32_t host = ntohl(in->sin_addr.s_addr);
            if ((host >> 24) == 127 || host == 0)
                continue;
            char text[INET_ADDRSTRLEN]{};
            if (!inet_ntop(AF_INET, &in->sin_addr, text, sizeof(text)))
                continue;
            const bool privateAddress =
                (host & 0xff000000u) == 0x0a000000u ||
                (host & 0xfff00000u) == 0xac100000u ||
                (host & 0xffff0000u) == 0xc0a80000u;
            if (privateAddress)
                return std::string(text);
            if (!fallback)
                fallback = std::string(text);
        }
    }
    return fallback;
}

const char* MimeType(const fs::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (extension == ".html") return "text/html; charset=utf-8";
    if (extension == ".css") return "text/css; charset=utf-8";
    if (extension == ".js") return "text/javascript; charset=utf-8";
    if (extension == ".json" || extension == ".json5") return "application/json; charset=utf-8";
    if (extension == ".svg") return "image/svg+xml";
    if (extension == ".png") return "image/png";
    if (extension == ".webp") return "image/webp";
    if (extension == ".ico") return "image/x-icon";
    if (extension == ".txt" || extension == ".md" || extension == ".license")
        return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

bool LoadStaticAsset(std::string request_target, std::string& body, const char*& content_type)
{
    const size_t query = request_target.find('?');
    if (query != std::string::npos)
        request_target.erase(query);
    if (request_target == "/")
        request_target = "/index.html";
    if (!OmniGhost::Platform::IsSafeStaticRequestTarget(request_target))
        return false;

    fs::path relative = fs::path(request_target.substr(1)).lexically_normal();
    if (relative.empty() || relative.is_absolute())
        return false;
#if defined(OMNIGHOST_DEV_EXTERNAL_RESOURCES)
    const fs::path root = WebRoot();
    const fs::path file = root / relative;

    std::error_code error;
    if (fs::is_regular_file(file, error) && OmniGhost::Platform::IsPathWithinRoot(root, file)) {
        const uintmax_t size = fs::file_size(file, error);
        if (!error && size <= 16ull * 1024ull * 1024ull) {
            std::ifstream stream(file, std::ios::binary);
            if (stream) {
                body.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
                content_type = MimeType(file);
                if (stream.good() || stream.eof()) return true;
            }
        }
    }
#endif
    const std::string logical = "webradar_fivem/" + relative.generic_string();
    OmniGhost::EmbeddedResourceDiagnostics diagnostics;
    const auto bytes = OmniGhost::LoadEmbeddedResource(logical, &diagnostics);
    if (!bytes || bytes->size() > 16ull * 1024ull * 1024ull) {
        std::clog << "[RESOURCE] id=" << logical << " load=FAIL reason="
                  << diagnostics.error << '\n';
        return false;
    }
    body.assign(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    content_type = MimeType(relative);
    return true;
}

std::string HttpResponse(int code, const char* reason, const char* content_type,
                         const std::string& body, std::string_view extra_headers = {})
{
    std::ostringstream response;
    response << "HTTP/1.1 " << code << ' ' << reason
             << "\r\nContent-Type: " << content_type
             << "\r\nContent-Length: " << body.size()
             << "\r\nCache-Control: no-store"
             << "\r\nX-Content-Type-Options: nosniff"
             << "\r\nX-Frame-Options: DENY"
             << "\r\nReferrer-Policy: no-referrer"
             << "\r\nX-Robots-Tag: noindex, nofollow, noarchive"
             << "\r\nPragma: no-cache"
             << "\r\nExpires: 0"
             << "\r\nCross-Origin-Resource-Policy: same-origin"
             << "\r\nContent-Security-Policy: default-src 'self' 'unsafe-inline' data: blob:; connect-src 'self' ws: wss:; object-src 'none'; frame-ancestors 'none'; base-uri 'self'"
             << "\r\nPermissions-Policy: camera=(), microphone=(), geolocation=()"
             << extra_headers
             << "\r\nConnection: close\r\n\r\n"
             << body;
    return response.str();
}

enum class HttpReadResult { Ok, Closed, Timeout, TooLarge, SocketError };

struct HttpRequestLine {
    std::string method;
    std::string target;
    std::string version;
};

HttpReadResult ReadHttpHeaders(SOCKET socket, std::string& request)
{
    constexpr size_t kMaximumHeaderBytes = 16 * 1024;
    constexpr auto kTotalReadBudget = std::chrono::seconds(5);
    char buffer[2048]{};
    request.clear();
    request.reserve(4096);
    const auto started = std::chrono::steady_clock::now();

    while (request.find("\r\n\r\n") == std::string::npos) {
        const auto now = std::chrono::steady_clock::now();
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            kTotalReadBudget - (now - started));
        if (remaining.count() <= 0)
            return HttpReadResult::Timeout;

        const DWORD receiveTimeout = static_cast<DWORD>((std::clamp)(
            remaining.count(), static_cast<long long>(50), static_cast<long long>(2000)));
        setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char*>(&receiveTimeout), sizeof(receiveTimeout));

        if (request.size() >= kMaximumHeaderBytes)
            return HttpReadResult::TooLarge;
        const size_t available = kMaximumHeaderBytes - request.size();
        const int wanted = static_cast<int>((std::min)(available, sizeof(buffer)));
        const int received = recv(socket, buffer, wanted, 0);
        if (received == 0)
            return HttpReadResult::Closed;
        if (received == SOCKET_ERROR) {
            const int error = WSAGetLastError();
            if (error == WSAETIMEDOUT || error == WSAEWOULDBLOCK)
                return HttpReadResult::Timeout;
            return HttpReadResult::SocketError;
        }
        request.append(buffer, static_cast<size_t>(received));
    }
    return HttpReadResult::Ok;
}

bool ValidateHttpHeaders(const std::string& request)
{
    constexpr size_t kMaximumHeaders = 64;
    constexpr size_t kMaximumHeaderLineBytes = 4096;
    size_t position = request.find("\r\n");
    if (position == std::string::npos)
        return false;
    position += 2;
    size_t count = 0;
    while (position < request.size()) {
        const size_t end = request.find("\r\n", position);
        if (end == std::string::npos)
            return false;
        if (end == position)
            return true;
        if (++count > kMaximumHeaders || end - position > kMaximumHeaderLineBytes)
            return false;
        const std::string_view line(request.data() + position, end - position);
        const size_t colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0)
            return false;
        for (size_t i = 0; i < line.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(line[i]);
            if ((c < 0x20 && c != '\t') || c == 0x7f)
                return false;
            if (i < colon && (c <= 0x20 || c >= 0x7f))
                return false;
        }
        position = end + 2;
    }
    return false;
}

bool ParseHttpRequestLine(const std::string& request, HttpRequestLine& parsed)
{
    const size_t end = request.find("\r\n");
    if (end == std::string::npos || end == 0 || end > 4096)
        return false;
    const std::string_view line(request.data(), end);
    const size_t firstSpace = line.find(' ');
    if (firstSpace == std::string_view::npos)
        return false;
    const size_t secondSpace = line.find(' ', firstSpace + 1);
    if (secondSpace == std::string_view::npos || line.find(' ', secondSpace + 1) != std::string_view::npos)
        return false;

    parsed.method.assign(line.substr(0, firstSpace));
    parsed.target.assign(line.substr(firstSpace + 1, secondSpace - firstSpace - 1));
    parsed.version.assign(line.substr(secondSpace + 1));

    if (parsed.method.empty() || parsed.method.size() > 16 ||
        parsed.target.empty() || parsed.target.size() > 2048)
        return false;
    if (parsed.target.front() != '/')
        return false;
    if (parsed.version != "HTTP/1.1" && parsed.version != "HTTP/1.0")
        return false;
    for (unsigned char c : parsed.target) {
        if (c < 0x20 || c == 0x7f)
            return false;
    }
    return true;
}

bool SendAll(SOCKET socket, const std::string& data)
{
    size_t sent = 0;
    while (sent < data.size()) {
        const int chunk = send(socket, data.data() + sent,
            static_cast<int>(data.size() - sent), 0);
        if (chunk == SOCKET_ERROR || chunk == 0)
            return false;
        sent += static_cast<size_t>(chunk);
    }
    return true;
}

void FailStart(const char* stage, int error)
{
    char message[160]{};
    std::snprintf(message, sizeof(message), "Erro em %s (Winsock %d)", stage, error);
    SetStatus(message);
    g_online = false;
    g_starting = false;
    std::cout << "[FiveM Radar] " << message << std::endl;
}

void ServerThread(std::stop_token stopToken, bool lan, int port)
{
    WSADATA wsa{};
    const int startup = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (startup != 0) {
        FailStart("WSAStartup", startup);
        return;
    }

    auto wsaCleanup = OmniGhost::Platform::MakeScopeExit([] { WSACleanup(); });
    (void)wsaCleanup;

    OmniGhost::Platform::UniqueSocket listenSocket(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!listenSocket) {
        FailStart("socket", WSAGetLastError());
        return;
    }
    const SOCKET listen_socket = listenSocket.get();
    SetSocket(listen_socket);

    BOOL exclusive = TRUE;
    setsockopt(listen_socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
        reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));

    sockaddr_in listenAddress{};
    listenAddress.sin_family = AF_INET;
    listenAddress.sin_addr.s_addr = htonl(lan ? INADDR_ANY : INADDR_LOOPBACK);
    listenAddress.sin_port = htons(static_cast<u_short>(port));

    if (bind(listen_socket, reinterpret_cast<sockaddr*>(&listenAddress), sizeof(listenAddress)) == SOCKET_ERROR) {
        const int error = WSAGetLastError();
        SetSocket(INVALID_SOCKET);
        FailStart("bind", error);
        return;
    }
    if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        const int error = WSAGetLastError();
        SetSocket(INVALID_SOCKET);
        FailStart("listen", error);
        return;
    }

    g_online = true;
    g_starting = false;
    const bool web_assets_ready =
#if defined(OMNIGHOST_DEV_EXTERNAL_RESOURCES)
        fs::is_regular_file(WebRoot() / L"index.html") ||
#endif
        OmniGhost::FindEmbeddedResource("webradar_fivem/index.html") != nullptr;

    std::string lan_ip = "127.0.0.1";
    if (lan) {
        if (const auto resolvedAddress = ResolveLanIPv4())
            lan_ip = *resolvedAddress;
    }

    char online[256]{};
    if (lan) {
        if (web_assets_ready) {
            std::snprintf(online, sizeof(online),
                "Online local http://127.0.0.1:%d/ | LAN http://%s:%d/",
                port, lan_ip.c_str(), port);
        } else {
            std::snprintf(online, sizeof(online),
                "Online local http://127.0.0.1:%d/ (assets em falta) | LAN http://%s:%d/",
                port, lan_ip.c_str(), port);
        }
    } else {
        std::snprintf(online, sizeof(online),
            web_assets_ready ? "Online local http://127.0.0.1:%d/"
                             : "Online local http://127.0.0.1:%d/ (assets em falta)",
            port);
    }
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        if (lan) {
            g_lan_url = "http://" + lan_ip + ":" + std::to_string(port) + "/#" + g_access_token;
        } else {
            g_lan_url.clear();
        }
    }
    SetStatus(online);
    std::cout << "[FiveM Radar] bind=" << (lan ? "LAN" : "loopback")
              << " port=" << port << std::endl;
    if (lan) {
        std::cout << "[FiveM Radar] LAN http://" << lan_ip << ":" << port << "/" << std::endl;
        std::cout << "[FiveM Radar] Firewall: permite TCP " << port << " neste PC." << std::endl;
    }

    std::deque<std::chrono::steady_clock::time_point> recentConnections;
    constexpr size_t kMaximumConnectionsPerWindow = 600;
    constexpr auto kConnectionRateWindow = std::chrono::seconds(10);

    while (g_requested.load() && !stopToken.stop_requested()) {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(listen_socket, &read_set);
        timeval timeout{ 0, 200000 };
        const int selected = select(0, &read_set, nullptr, nullptr, &timeout);
        if (!g_requested.load())
            break;
        if (selected == SOCKET_ERROR) {
            const int error = WSAGetLastError();
            if (error != WSAEINTR && error != WSAENOTSOCK)
                FailStart("select", error);
            break;
        }
        if (selected == 0)
            continue;

        OmniGhost::Platform::UniqueSocket client(accept(listen_socket, nullptr, nullptr));
        if (!client)
            continue;

        const auto acceptedAt = std::chrono::steady_clock::now();
        while (!recentConnections.empty() &&
               acceptedAt - recentConnections.front() > kConnectionRateWindow) {
            recentConnections.pop_front();
        }
        if (recentConnections.size() >= kMaximumConnectionsPerWindow) {
            const std::string throttled = HttpResponse(429, "Too Many Requests",
                "text/plain; charset=utf-8", "Demasiados pedidos");
            (void)SendAll(client.get(), throttled);
            shutdown(client.get(), SD_BOTH);
            continue;
        }
        recentConnections.push_back(acceptedAt);

        const DWORD io_timeout_ms = 2000;
        setsockopt(client.get(), SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char*>(&io_timeout_ms), sizeof(io_timeout_ms));
        setsockopt(client.get(), SOL_SOCKET, SO_SNDTIMEO,
            reinterpret_cast<const char*>(&io_timeout_ms), sizeof(io_timeout_ms));

        std::string requestText;
        const HttpReadResult readResult = ReadHttpHeaders(client.get(), requestText);
        HttpRequestLine request;
        std::string response;
        if (readResult == HttpReadResult::TooLarge) {
            response = HttpResponse(431, "Request Header Fields Too Large",
                "text/plain; charset=utf-8", "Cabeçalhos demasiado grandes");
        } else if (readResult == HttpReadResult::Timeout) {
            response = HttpResponse(408, "Request Timeout",
                "text/plain; charset=utf-8", "Tempo limite do pedido");
        } else if (readResult != HttpReadResult::Ok ||
                   !ParseHttpRequestLine(requestText, request) ||
                   !ValidateHttpHeaders(requestText)) {
            response = HttpResponse(400, "Bad Request",
                "text/plain; charset=utf-8", "Pedido inválido");
        } else if (request.method != "GET") {
            response = HttpResponse(405, "Method Not Allowed",
                "text/plain; charset=utf-8", "Método não permitido");
        } else if ((lan || g_public_access_requested.load()) &&
                   (request.target == "/api/state" || request.target == "/api/players" ||
                    request.target == "/api/ping" || request.target == "/api/health" ||
                    request.target == "/fivem_webradar") &&
                   !OmniGhost::RadarAccess::HasSafeSameOrigin(requestText)) {
            response = HttpResponse(403, "Forbidden",
                "text/plain; charset=utf-8", "Origem recusada");
        } else if ((lan || g_public_access_requested.load()) &&
                   (request.target == "/api/state" || request.target == "/api/players" ||
                    request.target == "/api/ping" || request.target == "/api/health" ||
                    request.target == "/fivem_webradar") &&
                   !(request.target == "/fivem_webradar"
                        ? OmniGhost::RadarAccess::HasValidWebSocketProtocolToken(
                              requestText, g_access_token)
                        : OmniGhost::RadarAccess::HasValidBearerToken(
                              requestText, g_access_token))) {
            response = HttpResponse(401, "Unauthorized",
                "text/plain; charset=utf-8", "Token de acesso inválido",
                "\r\nWWW-Authenticate: Bearer realm=\"OmniGhost Radar\"");
        } else if (request.target == "/fivem_webradar") {
            response = HttpResponse(426, "Upgrade Required",
                "text/plain; charset=utf-8", "Usa o transporte HTTP autenticado");
        } else if (request.target == "/api/state" || request.target == "/api/players") {
            std::string snapshot;
            std::string etag;
            {
                std::lock_guard<std::mutex> lock(g_data_mutex);
                snapshot = g_json;
                etag = g_json_etag;
            }
            std::string ifNone;
            {
                constexpr const char kKey[] = "If-None-Match:";
                const auto pos = requestText.find(kKey);
                if (pos != std::string::npos) {
                    auto start = pos + sizeof(kKey) - 1;
                    while (start < requestText.size() && (requestText[start] == ' ' || requestText[start] == '\t'))
                        ++start;
                    auto end = requestText.find("\r\n", start);
                    if (end == std::string::npos) end = requestText.size();
                    ifNone = requestText.substr(start, end - start);
                }
            }
            if (!ifNone.empty() && !etag.empty() && ifNone == etag) {
                response = HttpResponse(304, "Not Modified", "application/json; charset=utf-8", "",
                    std::string("\r\nETag: ") + etag);
            } else {
                response = HttpResponse(200, "OK", "application/json; charset=utf-8", snapshot,
                    std::string("\r\nETag: ") + etag);
            }
        } else if (request.target == "/api/health") {
            response = HttpResponse(200, "OK", "application/json; charset=utf-8",
                web_assets_ready ? "{\"status\":\"ok\",\"assets\":true}"
                                 : "{\"status\":\"degraded\",\"assets\":false}");
        } else if (request.target == "/api/ping") {
            response = HttpResponse(200, "OK", "application/json; charset=utf-8", "{\"ok\":true}");
        } else {
            std::string asset;
            const char* content_type = nullptr;
            if (LoadStaticAsset(request.target, asset, content_type))
                response = HttpResponse(200, "OK", content_type, asset);
            else if (request.target == "/")
                response = HttpResponse(200, "OK", "text/html; charset=utf-8", kHtml);
            else
                response = HttpResponse(404, "Not Found", "text/plain; charset=utf-8", "Nao encontrado");
        }
        SendAll(client.get(), response);
        shutdown(client.get(), SD_BOTH);
    }

    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        if (g_socket == listen_socket)
            g_socket = INVALID_SOCKET;
    }
    listenSocket.reset();
    g_online = false;
    g_starting = false;
    if (!g_requested.load())
        SetStatus("Offline");
}

void StartServer(int port, bool lan)
{
    if (g_thread.joinable())
        g_thread.join();

    std::string token;
    std::string tokenError;
    if (!OmniGhost::RadarAccess::GenerateSessionToken(token, tokenError)) {
        SetStatus(tokenError);
        std::cerr << "[FiveM Radar] Falha ao gerar credencial efémera do radar." << std::endl;
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        g_access_token = std::move(token);
        g_lan_url.clear();
    }

    g_port = port;
    g_lan = lan;
    g_requested = true;
    g_online = false;
    g_starting = true;
    SetStatus("A iniciar o servidor...");
    g_thread = std::jthread([lan, port](std::stop_token stopToken) {
        OmniGhost::Platform::SetCurrentThreadName(L"OmniGhost.FivemRadarHttp");
        try {
            ServerThread(stopToken, lan, port);
        } catch (const std::exception& exception) {
            SetStatus(std::string("Erro interno do servidor: ") + exception.what());
            g_online = false;
            g_starting = false;
            std::cerr << "[FiveM Radar] exceção na thread HTTP: " << exception.what() << std::endl;
        } catch (...) {
            SetStatus("Erro interno do servidor.");
            g_online = false;
            g_starting = false;
            std::cerr << "[FiveM Radar] exceção não identificada na thread HTTP." << std::endl;
        }
    });
}

void EnsureServer(int port, bool lan)
{
    if (g_requested.load() && (g_port.load() != port || g_lan.load() != lan))
        Shutdown();
    if (g_requested.load() && !g_online.load() && !g_starting.load()) {
        std::cout << "[FiveM Radar] servidor caiu — a reiniciar..." << std::endl;
        if (g_thread.joinable())
            g_thread.join();
        g_requested = false;
    }
    if (!g_requested.load())
        StartServer(port, lan);
}

} // namespace

void EnsureRunning(int port, bool lan)
{
    const int use_port = (port >= 1024 && port <= 65535) ? port : 8080;
    EnsureServer(use_port, lan);
}

void StartCloudflareTunnel(int local_port);
void StopCloudflareTunnel();

void Update()
{
    static bool s_configLoaded = false;
    if (!s_configLoaded) { LoadConfig(); s_configLoaded = true; }
    static ULONGLONG s_lastJsonMs = 0;
    const ULONGLONG nowMs = GetTickCount64();
    const auto& cfg = Fivem_Radar::config;

    if (!cfg.enabled) {
        if (g_requested.load() || g_online.load() || g_starting.load() || g_cf_running.load())
            Shutdown();
        return;
    }

    // Rate from config (5–60 Hz), default ~15 Hz
    const ULONGLONG minInterval = (ULONGLONG)(1000.0 / (std::max)(5.0f, (std::min)(60.0f, cfg.update_rate_hz)));
    if (s_lastJsonMs && (nowMs - s_lastJsonMs) < minInterval)
        return;
    s_lastJsonMs = nowMs;

    const int port = (cfg.port >= 1024 && cfg.port <= 65535)
        ? cfg.port
        : 8080;
    EnsureServer(port, cfg.lan);

    // Cloudflare lifecycle (menu flag → tunnel)
    if (cfg.cloudflare) {
        if (!g_cf_running.load())
            StartCloudflareTunnel(port);
    } else if (g_cf_running.load()) {
        StopCloudflareTunnel();
    }

    const bool dmaOk = ::mem.IsDeviceOpen();

    const auto number = [](float value) {
        return std::isfinite(value) ? value : 0.f;
    };

    // Compass angle: convert GTA yaw (-PI..PI or 0..360) → 0..360 compass
    auto compass = [](float yaw) {
        float angle = 90.0f - yaw * (180.0f / 3.14159265359f);
        angle = std::fmod(angle, 360.0f);
        if (angle < 0.f) angle += 360.0f;
        return angle;
    };

    std::ostringstream json;
    json << std::fixed << std::setprecision(3) << "{\"players\":[";

    bool first = true;
    int playerCount = 0;

    using namespace FiveM;
    using namespace FiveM::ESP;

    if (!validPeds.empty() && !positions.empty()) {
        Vec3 localPos{};
        bool hasLocal = false;
        if (offset::localplayer) {
            localPos = ::mem.Read<Vec3>(offset::localplayer + offset::playerPosition);
            hasLocal = !localPos.IsZero();
        }

        for (size_t i = 0; i < validPeds.size(); ++i) {
            uintptr_t ped = validPeds[i];
            const Vec3& pos = positions[i];

            if (pos.IsZero()) continue;

            bool isLocal = (ped == offset::localplayer);
            if (isLocal && !cfg.show_local)
                continue;

            std::string name = "Ped";
            int netId = 0;
            float health = 100.0f;
            float armor = 0.0f;
            bool inVehicle = false;
            float yaw = 0.0f;
            std::string vehicleName = "";

            // Read playerInfo for name and netId
            if (offset::playerInfo) {
                uintptr_t pi = 0;
                if (::mem.Read(ped + offset::playerInfo, &pi, sizeof(pi)) && pi) {
                    char nameBuf[64]{};
                    if (::mem.Read(pi + offset::playerInfo_name, nameBuf, sizeof(nameBuf) - 1)) {
                        name = nameBuf;
                    }
                    // Net ID
                    ::mem.Read(pi + offset::playerInfo_netId, &netId, sizeof(netId));
                }
            }

            // Skip pure NPCs unless enabled (players usually resolve a non-empty name / netId)
            if (!cfg.show_npcs && !isLocal && name == "Ped" && netId == 0)
                continue;

            // Health
            if (offset::playerHealth) {
                ::mem.Read(ped + offset::playerHealth, &health, sizeof(health));
            }

            // Armor
            ::mem.Read(ped + offset::playerArmor, &armor, sizeof(armor));

            // Vehicle
            uintptr_t veh = 0;
            if (::mem.Read(ped + offset::pedVehicle, &veh, sizeof(veh)) && veh) {
                inVehicle = true;
                // Try to get vehicle model name
                uintptr_t modelInfo = 0;
                if (::mem.Read(veh + offset::vehicleModelInfo, &modelInfo, sizeof(modelInfo)) && modelInfo) {
                    uintptr_t modelNamePtr = 0;
                    if (::mem.Read(modelInfo + 0x0, &modelNamePtr, sizeof(modelNamePtr)) && modelNamePtr) {
                        char modelNameBuf[64]{};
                        if (::mem.Read(modelNamePtr, modelNameBuf, sizeof(modelNameBuf) - 1)) {
                            vehicleName = modelNameBuf;
                        }
                    }
                }
            }

            // Heading: prefer entity forward from transform matrix; fallback velocity.
            // Rage entity matrix is typically at the same base as position (CEntity + 0x60).
            {
                float mat[16]{};
                const uintptr_t matrixBase = ped + offset::playerPosition - 0x30; // position at +0x90 ⇒ matrix ~+0x60
                bool gotMatrix = ::mem.Read(matrixBase, mat, sizeof(mat));
                if (!gotMatrix && offset::boneMatrix)
                    gotMatrix = ::mem.Read(ped + offset::boneMatrix, mat, sizeof(mat));
                if (gotMatrix) {
                    // Row-major forward vector often in m[4], m[5] (second row) or m[1], m[5]
                    const float fx = mat[4], fy = mat[5];
                    if (std::fabs(fx) + std::fabs(fy) > 1e-3f)
                        yaw = std::atan2(fy, fx);
                }
                if (yaw == 0.0f) {
                    Vec3 velocity{};
                    ::mem.Read(ped + offset::pedVelocity, &velocity, sizeof(velocity));
                    if (!velocity.IsZero())
                        yaw = std::atan2(velocity.y, velocity.x);
                }
            }

            if (!first)
                json << ',';
            first = false;
            playerCount++;

            const float relX = hasLocal ? (pos.x - localPos.x) : pos.x;
            const float relY = hasLocal ? (pos.y - localPos.y) : pos.y;
            const float relZ = hasLocal ? (pos.z - localPos.z) : pos.z;
            const float dist = hasLocal ? std::sqrt(relX * relX + relY * relY + relZ * relZ) : 0.0f;

            json << "{\"id\":" << (netId ? netId : (int)ped)
                 << ",\"name\":\"" << JsonEscape(name.c_str()) << "\""
                 << ",\"x\":" << number(pos.x)
                 << ",\"y\":" << number(pos.y)
                 << ",\"z\":" << number(pos.z)
                 << ",\"yaw\":" << number(compass(yaw))
                 << ",\"health\":" << number(health)
                 << ",\"armor\":" << number(armor)
                 << ",\"is_local\":" << (isLocal ? "true" : "false")
                 << ",\"is_player\":" << (name != "Ped" ? "true" : "false")
                 << ",\"in_vehicle\":" << (inVehicle ? "true" : "false")
                 << ",\"vehicle\":\"" << JsonEscape(vehicleName.c_str()) << "\""
                 << ",\"distance\":" << number(dist) << '}';
        }
    }

    json << "],\"objects\":[],\"dma_status\":\"" << (dmaOk ? (playerCount ? "online" : "idle") : "offline") << "\"}";

    std::lock_guard<std::mutex> lock(g_data_mutex);
    g_json = json.str();
    {
        std::uint64_t h = 14695981039346656037ull;
        for (unsigned char c : g_json) { h ^= c; h *= 1099511628211ull; }
        g_json_etag = "\"" + std::to_string(g_json.size()) + "-" + std::to_string(h) + "\"";
    }
    static ULONGLONG s_lastLog = 0;
    if (nowMs - s_lastLog > 3000) {
        s_lastLog = nowMs;
        std::cout << "[FiveM Radar] players=" << playerCount
                  << " dma=" << (dmaOk ? "ok" : "off") << std::endl;
    }
}

// ── Cloudflare quick tunnel (cloudflared) ─────────────────────────────────
HANDLE g_cf_process = nullptr;
HANDLE g_cf_stdout_rd = nullptr;
std::mutex g_cf_mutex;
std::string g_public_url;
std::jthread g_cf_reader;

std::wstring FindCloudflared() {
    std::wstring validationError;
    constexpr std::wstring_view relative = L"libs/cloudflared.exe";
    if (!OmniGhost::RuntimeBootstrap::MaterializePrivateRuntimeFile(
            relative, validationError)) {
        std::wcerr << L"[FiveM Radar] cloudflared não pôde ser materializado: "
                   << validationError << L'\n';
        return {};
    }
    return OmniGhost::RuntimeBootstrap::PrivateRuntimePath(relative).wstring();
}

void StopCloudflareTunnel() {
    g_public_access_requested = false;
    g_cf_running = false;
    if (g_cf_reader.joinable())
        g_cf_reader.request_stop();
    if (g_cf_process) {
        TerminateProcess(g_cf_process, 0);
        CloseHandle(g_cf_process);
        g_cf_process = nullptr;
    }
    if (g_cf_stdout_rd) {
        CloseHandle(g_cf_stdout_rd);
        g_cf_stdout_rd = nullptr;
    }
    if (g_cf_reader.joinable() && g_cf_reader.get_id() != std::this_thread::get_id())
        g_cf_reader.join();
    std::lock_guard<std::mutex> lock(g_cf_mutex);
    g_public_url.clear();
}

void StartCloudflareTunnel(int local_port) {
    StopCloudflareTunnel();
    if (local_port < 1024 || local_port > 65535) local_port = 8080;
    EnsureServer(local_port, g_lan.load());

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE rawRead = nullptr, rawWrite = nullptr;
    if (!CreatePipe(&rawRead, &rawWrite, &sa, 0)) {
        SetStatus("Falha ao preparar o processo Cloudflare.");
        return;
    }
    OmniGhost::Platform::UniqueHandle readPipe(rawRead);
    OmniGhost::Platform::UniqueHandle writePipe(rawWrite);
    SetHandleInformation(readPipe.get(), HANDLE_FLAG_INHERIT, 0);

    const std::wstring exe = FindCloudflared();
    if (exe.empty()) {
        SetStatus("cloudflared privado em falta ou com integridade inválida.");
        return;
    }
    wchar_t cmd[512]{};
    _snwprintf_s(cmd, _TRUNCATE,
        L"\"%s\" tunnel --no-autoupdate --protocol http2 --url http://127.0.0.1:%d",
        exe.c_str(), local_port);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = writePipe.get();
    si.hStdError = writePipe.get();
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    OmniGhost::Platform::UniqueProcessInformation processInfo;

    g_public_access_requested = true;
    if (!CreateProcessW(exe.c_str(), cmd, nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW, nullptr, exe.substr(0, exe.find_last_of(L"\\/")).c_str(),
            &si, processInfo.put())) {
        g_public_access_requested = false;
        SetStatus("Falha ao iniciar o túnel Cloudflare.");
        std::cout << "[FiveM Radar] Cloudflare: CreateProcess falhou erro=" << GetLastError() << std::endl;
        return;
    }
    writePipe.reset();
    g_cf_process = processInfo.release_process();
    g_cf_stdout_rd = readPipe.release();
    g_cf_running = true;
    SetStatus("Radar online; a criar link público seguro...");
    std::cout << "[FiveM Radar] Cloudflare tunnel a iniciar..." << std::endl;

    g_cf_reader = std::jthread([](std::stop_token stopToken) {
        OmniGhost::Platform::SetCurrentThreadName(L"OmniGhost.FivemCloudflared");
        try {
        char buf[512];
        std::string acc;
        DWORD n = 0;
        while (!stopToken.stop_requested() && g_cf_running.load() && g_cf_stdout_rd &&
               ReadFile(g_cf_stdout_rd, buf, sizeof(buf) - 1, &n, nullptr) && n > 0) {
            buf[n] = 0;
            acc += buf;
            const char* key = "https://";
            size_t pos = 0;
            while ((pos = acc.find(key, pos)) != std::string::npos) {
                size_t end = pos;
                while (end < acc.size() &&
                       (std::isalnum(static_cast<unsigned char>(acc[end])) ||
                        acc[end] == ':' || acc[end] == '/' || acc[end] == '.' ||
                        acc[end] == '-' || acc[end] == '_'))
                    ++end;
                std::string url = acc.substr(pos, end - pos);
                constexpr std::string_view prefix = "https://";
                constexpr std::string_view suffix = ".trycloudflare.com";
                const std::string_view candidate(url);
                const std::string_view host = candidate.size() > prefix.size()
                    ? candidate.substr(prefix.size()) : std::string_view{};
                const bool validHost = host.size() > suffix.size() && host.size() <= 253 &&
                    host.ends_with(suffix) &&
                    std::all_of(host.begin(), host.end(), [](unsigned char c) {
                        return std::isalnum(c) || c == '-' || c == '.';
                    });
                if (validHost) {
                    while (!url.empty() && (url.back() == '.' || url.back() == ','))
                        url.pop_back();
                    {
                        std::lock_guard<std::mutex> lock(g_cf_mutex);
                        g_public_url = url;
                    }
                    std::cout << "[FiveM Radar] Link publico: " << url << std::endl;
                    SetStatus("Radar online; link público seguro ativo.");
                    acc.clear();
                    break;
                }
                pos = end;
            }
            if (acc.size() > 8192)
                acc.erase(0, acc.size() - 2048);
        }
        } catch (const std::exception& exception) {
            std::cerr << "[FiveM Radar] Cloudflare reader terminou com exceção: " << exception.what() << std::endl;
            g_cf_running = false;
        } catch (...) {
            std::cerr << "[FiveM Radar] Cloudflare reader terminou com uma exceção não identificada." << std::endl;
            g_cf_running = false;
        }
        if (g_public_access_requested.load() && g_cf_running.exchange(false))
            SetStatus("O túnel público terminou inesperadamente.");
    });
}

bool CloudflareRunning() { return g_cf_running.load() && g_cf_process != nullptr; }

const char* PublicUrl() {
    static thread_local std::string snap;
    std::scoped_lock lock(g_cf_mutex, g_state_mutex);
    snap.clear();
    if (!g_public_url.empty() && !g_access_token.empty()) {
        snap = g_public_url;
        if (snap.back() != '/') snap.push_back('/');
        snap.push_back('#');
        snap += g_access_token;
    }
    return snap.c_str();
}

const char* LocalUrl() {
    static thread_local std::string snap;
    std::lock_guard<std::mutex> lock(g_state_mutex);
    snap = "http://127.0.0.1:" + std::to_string(g_port.load()) + "/";
    if (!g_access_token.empty()) {
        snap.push_back('#');
        snap += g_access_token;
    }
    return snap.c_str();
}

const char* LanUrl() {
    static thread_local std::string snap;
    std::lock_guard<std::mutex> lock(g_state_mutex);
    snap = g_lan_url;
    return snap.c_str();
}

void Shutdown()
{
    StopCloudflareTunnel();
    g_requested = false;
    if (g_thread.joinable())
        g_thread.request_stop();
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        if (g_socket != INVALID_SOCKET)
            shutdown(g_socket, SD_BOTH);
    }
    if (g_thread.joinable() && g_thread.get_id() != std::this_thread::get_id())
        g_thread.join();
    g_online = false;
    g_starting = false;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        g_access_token.clear();
        g_lan_url.clear();
    }
    SetStatus("Offline");
}

bool IsRunning() { return g_online.load(); }
bool IsStarting() { return g_starting.load(); }
int Port() { return g_port.load(); }

const char* Status()
{
    static thread_local std::string snapshot;
    std::lock_guard<std::mutex> lock(g_state_mutex);
    snapshot = g_status;
    if (g_cf_running.load()) {
        std::lock_guard<std::mutex> cf(g_cf_mutex);
        if (!g_public_url.empty()) {
            snapshot += " | Public ";
            snapshot += g_public_url;
        } else {
            snapshot += " | Cloudflare a obter link...";
        }
    }
    return snapshot.c_str();
}

} // namespace Fivem_Radar