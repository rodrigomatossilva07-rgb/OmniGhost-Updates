#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "kmbox_net.h"
#include "../platform/thread_utils.h"
#include "../platform/unique_socket.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <exception>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <stop_token>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace kmbox_net {
namespace {

constexpr std::uint32_t kCommandConnect = 0xAF3C2828u;
constexpr std::uint32_t kCommandMouseMove = 0xAEDE7345u;
constexpr std::uint32_t kCommandMouseLeft = 0x9823AE8Du;
constexpr std::uint32_t kCommandMonitor = 0x27388020u;
constexpr int kSocketTimeoutMs = 80;

#pragma pack(push, 1)
struct CommandHeader {
    std::uint32_t mac;
    std::uint32_t random;
    std::uint32_t index;
    std::uint32_t command;
};

struct MousePayload {
    std::int32_t buttons;
    std::int32_t x;
    std::int32_t y;
    std::int32_t wheel;
    std::int32_t points[10];
};

struct MonitorMouseReport {
    std::uint8_t reportId;
    std::uint8_t buttons;
    std::int16_t x;
    std::int16_t y;
    std::int16_t wheel;
};
#pragma pack(pop)

static_assert(sizeof(CommandHeader) == 16, "Unexpected KMBox command header size");
static_assert(sizeof(MousePayload) == 56, "Unexpected KMBox mouse payload size");
static_assert(sizeof(MonitorMouseReport) == 8, "Unexpected KMBox monitor report size");

OmniGhost::Platform::UniqueSocket g_commandSocket;
OmniGhost::Platform::UniqueSocket g_monitorSocket;
sockaddr_in g_deviceAddress{};
std::mutex g_commandMutex;
std::mutex g_stateMutex;
std::jthread g_monitorThread;
std::atomic_bool g_monitorRunning{ false };
std::atomic_bool g_connected{ false };
std::atomic_uint8_t g_buttonMask{ 0 };
bool g_winsockStarted = false;
std::uint32_t g_mac = 0;
std::uint32_t g_index = 0;
std::atomic_int32_t g_buttons{ 0 };
std::string g_lastError;
std::mt19937 g_random{ std::random_device{}() };

void SetError(std::string message) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_lastError = std::move(message);
}

bool ParseUuid(const std::string& input, std::uint32_t& value) {
    std::string hex;
    hex.reserve(8);
    for (char character : input) {
        const unsigned char current = static_cast<unsigned char>(character);
        if (std::isxdigit(current))
            hex.push_back(static_cast<char>(std::toupper(current)));
        else if (character != '-' && character != ':' && !std::isspace(current))
            return false;
    }
    if (hex.size() != 8) return false;
    try {
        const unsigned long parsed = std::stoul(hex, nullptr, 16);
        value = static_cast<std::uint32_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool SameEndpoint(const sockaddr_in& address) {
    return address.sin_family == AF_INET &&
        address.sin_addr.s_addr == g_deviceAddress.sin_addr.s_addr &&
        address.sin_port == g_deviceAddress.sin_port;
}

bool SameDeviceAddress(const sockaddr_in& address) {
    // Monitor reports may originate from another UDP source port. The
    // official protocol only guarantees that they come from the device IP.
    return address.sin_family == AF_INET &&
        address.sin_addr.s_addr == g_deviceAddress.sin_addr.s_addr;
}

bool Request(std::uint32_t command, const void* payload, std::size_t payloadSize,
             std::uint32_t randomValue = 0, bool fixedRandom = false) {
    std::lock_guard<std::mutex> lock(g_commandMutex);
    if (!g_commandSocket) {
        SetError("Socket KMBox-Net fechado.");
        return false;
    }
    if (payloadSize > sizeof(MousePayload)) {
        SetError("Payload KMBox-Net invalido.");
        return false;
    }

    std::array<std::uint8_t, sizeof(CommandHeader) + sizeof(MousePayload)> packet{};
    CommandHeader header{};
    header.mac = g_mac;
    header.random = fixedRandom ? randomValue : g_random();
    header.index = ++g_index;
    header.command = command;
    std::memcpy(packet.data(), &header, sizeof(header));
    if (payload && payloadSize)
        std::memcpy(packet.data() + sizeof(header), payload, payloadSize);

    const int length = static_cast<int>(sizeof(header) + payloadSize);
    const int maxAttempts = (command == kCommandConnect || command == kCommandMonitor) ? 3 : 1;
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        const int sent = sendto(g_commandSocket.get(),
            reinterpret_cast<const char*>(packet.data()), length, 0,
            reinterpret_cast<const sockaddr*>(&g_deviceAddress), sizeof(g_deviceAddress));
        if (sent != length) {
            SetError("Falha ao enviar comando ao KMBox-Net.");
            return false;
        }
        std::array<std::uint8_t, 1024> response{};
        sockaddr_in source{};
        int sourceLength = sizeof(source);
        const int received = recvfrom(g_commandSocket.get(),
            reinterpret_cast<char*>(response.data()), static_cast<int>(response.size()), 0,
            reinterpret_cast<sockaddr*>(&source), &sourceLength);
        if (received < static_cast<int>(sizeof(CommandHeader)))
            continue;
        if (!SameEndpoint(source))
            continue;
        CommandHeader reply{};
        std::memcpy(&reply, response.data(), sizeof(reply));
        if (reply.command == header.command && reply.index == header.index)
            return true;
    }

    SetError("O KMBox-Net nao respondeu ou devolveu uma resposta invalida.");
    return false;
}

void MonitorLoop(std::stop_token stopToken) {
    OmniGhost::Platform::SetCurrentThreadName(L"OmniGhost.KMBoxMonitor");
    std::array<std::uint8_t, 1024> buffer{};
    while (!stopToken.stop_requested() && g_monitorRunning.load(std::memory_order_relaxed)) {
        sockaddr_in source{};
        int sourceLength = sizeof(source);
        const int received = recvfrom(g_monitorSocket.get(),
            reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
            reinterpret_cast<sockaddr*>(&source), &sourceLength);
        if (received < static_cast<int>(sizeof(MonitorMouseReport)))
            continue;
        if (!SameDeviceAddress(source))
            continue;
        MonitorMouseReport report{};
        std::memcpy(&report, buffer.data(), sizeof(report));
        g_buttonMask.store(static_cast<std::uint8_t>(report.buttons & 0x1Fu),
                           std::memory_order_relaxed);
    }
}

bool StartMonitor() {
    g_monitorSocket.reset(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (!g_monitorSocket)
        return false;

    DWORD timeout = 100;
    setsockopt(g_monitorSocket.get(), SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    std::uint16_t selectedPort = 0;
    const std::uint16_t start = static_cast<std::uint16_t>(31200u + (g_random() % 500u));
    for (std::uint16_t offset = 0; offset < 500u; ++offset) {
        const std::uint16_t port = static_cast<std::uint16_t>(31200u + ((start - 31200u + offset) % 500u));
        local.sin_port = htons(port);
        if (bind(g_monitorSocket.get(), reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == 0) {
            selectedPort = port;
            break;
        }
    }
    if (!selectedPort) {
        g_monitorSocket.reset();
        return false;
    }

    const std::uint32_t monitorValue = static_cast<std::uint32_t>(selectedPort) | 0xAA550000u;
    if (!Request(kCommandMonitor, nullptr, 0, monitorValue, true)) {
        g_monitorSocket.reset();
        return false;
    }

    g_monitorRunning = true;
    g_monitorThread = std::jthread([](std::stop_token stopToken) {
        try {
            MonitorLoop(stopToken);
        } catch (const std::exception& exception) {
            SetError(std::string("KMBox monitor terminou com exceção: ") + exception.what());
            g_monitorRunning = false;
        } catch (...) {
            SetError("KMBox monitor terminou com uma exceção não identificada.");
            g_monitorRunning = false;
        }
    });
    return true;
}

void StopMonitor(bool notifyDevice) {
    if (notifyDevice && g_commandSocket)
        Request(kCommandMonitor, nullptr, 0, 0, true);
    g_monitorRunning = false;
    if (g_monitorSocket) {
        g_monitorSocket.reset();
    }
    if (g_monitorThread.joinable()) {
        g_monitorThread.request_stop();
        g_monitorThread.join();
    }
    g_buttonMask = 0;
}

} // namespace

bool Connect(const std::string& ip, int port, const std::string& uuid) {
    Disconnect();
    if (port < 1 || port > 65535) {
        SetError("Porta KMBox-Net invalida.");
        return false;
    }
    if (!ParseUuid(uuid, g_mac)) {
        SetError("UUID KMBox-Net invalido: usa exatamente 8 digitos hexadecimais.");
        return false;
    }

    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        SetError("Nao foi possivel iniciar a rede do Windows.");
        return false;
    }
    g_winsockStarted = true;
    g_commandSocket.reset(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (!g_commandSocket) {
        SetError("Nao foi possivel criar o socket KMBox-Net.");
        Disconnect();
        return false;
    }

    DWORD timeout = kSocketTimeoutMs;
    setsockopt(g_commandSocket.get(), SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(g_commandSocket.get(), SOL_SOCKET, SO_SNDTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    g_deviceAddress = {};
    g_deviceAddress.sin_family = AF_INET;
    g_deviceAddress.sin_port = htons(static_cast<u_short>(port));
    if (inet_pton(AF_INET, ip.c_str(), &g_deviceAddress.sin_addr) != 1) {
        SetError("Endereco IPv4 do KMBox-Net invalido.");
        Disconnect();
        return false;
    }

    g_index = 0;
    g_buttons.store(0);
    if (!Request(kCommandConnect, nullptr, 0)) {
        Disconnect();
        return false;
    }
    g_connected = true;
    if (!StartMonitor())
        SetError("KMBox-Net ligado, mas o monitor de botoes nao ficou disponivel.");
    else
        SetError({});
    return true;
}

void Disconnect() {
    if (g_connected.load())
        SetLeft(false);
    StopMonitor(g_connected.load());
    g_connected = false;
    if (g_commandSocket) {
        g_commandSocket.reset();
    }
    if (g_winsockStarted) {
        WSACleanup();
        g_winsockStarted = false;
    }
    g_index = 0;
    g_buttons.store(0);
}

bool IsConnected() { return g_connected.load(); }

bool Move(int x, int y) {
    if (!IsConnected()) return false;
    MousePayload payload{};
    payload.buttons = g_buttons.load(std::memory_order_relaxed);
    payload.x = std::clamp(x, -32767, 32767);
    payload.y = std::clamp(y, -32767, 32767);
    return Request(kCommandMouseMove, &payload, sizeof(payload));
}

bool SetLeft(bool down) {
    if (!IsConnected()) return false;
    std::int32_t expected = g_buttons.load(std::memory_order_relaxed);
    std::int32_t desired = expected;
    do {
        desired = down ? (expected | 0x01) : (expected & ~0x01);
    } while (!g_buttons.compare_exchange_weak(expected, desired,
        std::memory_order_relaxed, std::memory_order_relaxed));
    MousePayload payload{};
    payload.buttons = desired;
    if (Request(kCommandMouseLeft, &payload, sizeof(payload)))
        return true;
    g_buttons.store(expected, std::memory_order_relaxed);
    return false;
}

std::uint8_t ButtonMask() { return g_buttonMask.load(std::memory_order_relaxed); }

std::string LastError() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    return g_lastError;
}

} // namespace kmbox_net
