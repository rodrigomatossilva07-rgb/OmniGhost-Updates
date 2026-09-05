#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <devguid.h>
#include <setupapi.h>

#include "ferrum_device.h"
#include "../platform/thread_utils.h"
#include "../platform/text_encoding.h"
#include "../platform/unique_handle.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <stop_token>
#include <vector>

#pragma comment(lib, "setupapi.lib")

namespace ferrum_device {
namespace {

OmniGhost::Platform::UniqueHandle g_serial;
std::mutex g_writeMutex;
std::mutex g_stateMutex;
std::jthread g_readerThread;
std::atomic_bool g_readerRunning{ false };
std::atomic_bool g_connected{ false };
std::atomic_uint8_t g_buttonMask{ 0 };
std::string g_port;
std::string g_version;
std::string g_lastError;

void SetError(std::string error) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_lastError = std::move(error);
}

std::string Trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    return value;
}

std::string NormalizePort(std::string port) {
    port = Trim(std::move(port));
    for (char& character : port)
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    if (port.rfind("\\\\.\\", 0) == 0)
        port.erase(0, 4);
    if (port.rfind("COM", 0) != 0)
        return {};
    if (port.size() <= 3 || !std::all_of(port.begin() + 3, port.end(), [](char value) {
            return std::isdigit(static_cast<unsigned char>(value)) != 0;
        }))
        return {};
    return "\\\\.\\" + port;
}

std::vector<std::pair<std::string, std::string>> SerialPorts() {
    std::vector<std::pair<std::string, std::string>> result;
    HDEVINFO info = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (info == INVALID_HANDLE_VALUE) return result;

    SP_DEVINFO_DATA item{};
    item.cbSize = sizeof(item);
    for (DWORD index = 0; SetupDiEnumDeviceInfo(info, index, &item); ++index) {
        char friendly[512]{};
        DWORD required = 0;
        if (!SetupDiGetDeviceRegistryPropertyA(info, &item, SPDRP_FRIENDLYNAME,
                nullptr, reinterpret_cast<PBYTE>(friendly), sizeof(friendly) - 1, &required))
            continue;
        std::string description(friendly);
        std::string upper = description;
        for (char& character : upper)
            character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
        const std::size_t begin = upper.rfind("(COM");
        if (begin == std::string::npos) continue;
        const std::size_t end = upper.find(')', begin);
        if (end == std::string::npos) continue;
        result.emplace_back(upper.substr(begin + 1, end - begin - 1), description);
    }
    SetupDiDestroyDeviceInfoList(info);
    return result;
}

std::string AutoDetectPort() {
    const auto ports = SerialPorts();
    const char* preferred[] = { "FERRUM", "SILICON LABS CP210", "CP210X", "CP210" };
    for (const char* probe : preferred) {
        for (const auto& [port, description] : ports) {
            std::string upper = description;
            for (char& character : upper)
                character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
            if (upper.find(probe) != std::string::npos)
                return port;
        }
    }
    return {};
}

bool WriteCommand(const std::string& command) {
    std::lock_guard<std::mutex> lock(g_writeMutex);
    if (!g_serial) return false;
    DWORD written = 0;
    if (!WriteFile(g_serial.get(), command.data(), static_cast<DWORD>(command.size()), &written, nullptr) ||
        written != static_cast<DWORD>(command.size())) {
        SetError("Falha ao escrever no Ferrum.");
        return false;
    }
    return true;
}

std::string ProbeVersion() {
    PurgeComm(g_serial.get(), PURGE_RXABORT | PURGE_RXCLEAR);
    if (!WriteCommand("km.version()\r\n")) return {};

    std::string response;
    response.reserve(256);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(900);
    while (std::chrono::steady_clock::now() < deadline && response.size() < 1024) {
        char buffer[128]{};
        DWORD read = 0;
        if (!ReadFile(g_serial.get(), buffer, sizeof(buffer), &read, nullptr))
            return {};
        if (read) {
            response.append(buffer, buffer + read);
            std::string lower = response;
            for (char& character : lower)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            if (lower.find("ferrum") != std::string::npos ||
                lower.find("kmbox:") != std::string::npos ||
                lower.find(">>>") != std::string::npos)
                return response;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    return {};
}

void ParseButtonCallbacks(std::vector<std::uint8_t>& pending) {
    for (std::size_t index = 0; index + 3 < pending.size(); ++index) {
        if (pending[index] != 'k' || pending[index + 1] != 'm' || pending[index + 2] != '.')
            continue;
        const std::uint8_t mask = pending[index + 3];
        if (mask <= 0x1Fu)
            g_buttonMask.store(mask, std::memory_order_relaxed);
    }
    if (pending.size() > 256)
        pending.erase(pending.begin(), pending.end() - 16);
}

void ReaderLoop(std::stop_token stopToken) {
    OmniGhost::Platform::SetCurrentThreadName(L"OmniGhost.FerrumReader");
    std::vector<std::uint8_t> pending;
    pending.reserve(256);
    while (!stopToken.stop_requested() && g_readerRunning.load(std::memory_order_relaxed)) {
        std::uint8_t buffer[128]{};
        DWORD read = 0;
        if (!ReadFile(g_serial.get(), buffer, sizeof(buffer), &read, nullptr)) {
            if (g_readerRunning.load()) {
                SetError("Leitura do Ferrum interrompida.");
                g_connected = false;
            }
            break;
        }
        if (!read) continue;
        pending.insert(pending.end(), buffer, buffer + read);
        ParseButtonCallbacks(pending);
    }
    g_readerRunning = false;
}

} // namespace

bool Connect(const std::string& requestedPort, int baudRate) {
    Disconnect();
    std::string port = Trim(requestedPort);
    if (port.empty()) port = AutoDetectPort();
    const std::string path = NormalizePort(port);
    if (path.empty()) {
        SetError("Ferrum nao encontrado. Indica a porta COM do Silicon Labs CP210x.");
        return false;
    }

    const std::wstring pathWide = OmniGhost::Platform::Utf8ToWide(path);
    if (pathWide.empty()) {
        SetError("A porta COM do Ferrum não é UTF-8 válida.");
        return false;
    }
    const HANDLE openedSerial = CreateFileW(pathWide.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (openedSerial == INVALID_HANDLE_VALUE) {
        SetError("Nao foi possivel abrir a porta COM do Ferrum.");
        return false;
    }
    g_serial.reset(openedSerial);

    DCB state{};
    state.DCBlength = sizeof(state);
    if (!GetCommState(g_serial.get(), &state)) {
        SetError("Nao foi possivel ler a configuracao serial do Ferrum.");
        Disconnect();
        return false;
    }
    state.BaudRate = baudRate > 0 ? static_cast<DWORD>(baudRate) : CBR_115200;
    state.ByteSize = 8;
    state.Parity = NOPARITY;
    state.StopBits = ONESTOPBIT;
    state.fBinary = TRUE;
    state.fDtrControl = DTR_CONTROL_ENABLE;
    state.fRtsControl = RTS_CONTROL_ENABLE;
    if (!SetCommState(g_serial.get(), &state)) {
        SetError("Nao foi possivel configurar a porta COM do Ferrum.");
        Disconnect();
        return false;
    }

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = 40;
    timeouts.WriteTotalTimeoutConstant = 250;
    if (!SetCommTimeouts(g_serial.get(), &timeouts)) {
        SetError("Nao foi possivel configurar os timeouts do Ferrum.");
        Disconnect();
        return false;
    }
    SetupComm(g_serial.get(), 4096, 4096);
    PurgeComm(g_serial.get(), PURGE_RXABORT | PURGE_TXABORT | PURGE_RXCLEAR | PURGE_TXCLEAR);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    const std::string version = ProbeVersion();
    std::string lower = version;
    for (char& character : lower)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    const bool softwareApi = lower.find("ferrum") != std::string::npos;
    const bool legacyApi = lower.find("kmbox: 2.0.0 aug 31 2020 21:49:51") != std::string::npos;
    if (!softwareApi && !legacyApi) {
        SetError("A porta respondeu, mas nao se identificou como Ferrum (km.version).");
        Disconnect();
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_port = port;
        g_version = legacyApi ? "Ferrum Legacy API (compatibilidade KMBox 2.0.0)" :
                                "Ferrum Software API";
        g_lastError.clear();
    }
    g_connected = true;
    g_readerRunning = true;
    g_readerThread = std::jthread([](std::stop_token stopToken) {
        try {
            ReaderLoop(stopToken);
        } catch (const std::exception& exception) {
            SetError(std::string("Ferrum reader terminou com exceção: ") + exception.what());
            g_connected = false;
            g_readerRunning = false;
        } catch (...) {
            SetError("Ferrum reader terminou com uma exceção não identificada.");
            g_connected = false;
            g_readerRunning = false;
        }
    });
    if (!WriteCommand("km.buttons(1)\r\n")) {
        Disconnect();
        return false;
    }
    return true;
}

void Disconnect() {
    if (g_serial && g_connected.load()) {
        WriteCommand("km.left(0)\r\n");
        WriteCommand("km.buttons(0)\r\n");
    }
    g_connected = false;
    g_readerRunning = false;
    if (g_serial)
        CancelIoEx(g_serial.get(), nullptr);
    if (g_readerThread.joinable()) {
        g_readerThread.request_stop();
        g_readerThread.join();
    }
    g_serial.reset();
    g_buttonMask = 0;
}

bool IsConnected() { return g_connected.load(); }

bool Move(int x, int y) {
    if (!IsConnected()) return false;
    x = std::clamp(x, -32767, 32767);
    y = std::clamp(y, -32767, 32767);
    char command[64]{};
    std::snprintf(command, sizeof(command), "km.move(%d,%d)\r\n", x, y);
    return WriteCommand(command);
}

bool SetLeft(bool down) {
    if (!IsConnected()) return false;
    return WriteCommand(down ? "km.left(1)\r\n" : "km.left(0)\r\n");
}

std::uint8_t ButtonMask() { return g_buttonMask.load(std::memory_order_relaxed); }

std::string ConnectedPort() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    return g_port;
}

std::string DeviceVersion() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    return g_version;
}

std::string LastError() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    return g_lastError;
}

} // namespace ferrum_device
