#include "serialport.h"
#include "../platform/thread_utils.h"
#include "../platform/text_encoding.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <vector>
#include <string>
#include <cstring>
#include <chrono>

#ifdef _WIN32
#include <setupapi.h>
#include <devguid.h>
#include <cfgmgr32.h>
#pragma comment(lib, "setupapi.lib")
#endif

namespace makcu {

    namespace {
        // Streamed button masks are binary levels in the range 0x00..0x1F.
        // Printable bytes after "km." belong to echoes such as "km.move(...)".
        constexpr bool IsBinaryButtonMask(uint8_t value) noexcept {
            return value <= 0x1F;
        }

        static_assert(IsBinaryButtonMask(0x00) && IsBinaryButtonMask(0x1F));
        static_assert(!IsBinaryButtonMask(static_cast<uint8_t>('m')));
        static_assert(!IsBinaryButtonMask(static_cast<uint8_t>('l')));
    }

    SerialPort::SerialPort()
        : m_baudRate(115200)
        , m_timeout(100)  // Reduced from 1000ms
        , m_isOpen(false)
#ifdef _WIN32
        , m_handle()
#else
        , m_fd(-1)
#endif
    {
#ifdef _WIN32
        memset(&m_dcb, 0, sizeof(m_dcb));
        memset(&m_timeouts, 0, sizeof(m_timeouts));
#endif
    }

    SerialPort::~SerialPort() {
        close();
    }

    bool SerialPort::open(const std::string& port, uint32_t baudRate) {
        // close() acquires m_mutex itself. Calling it while holding m_mutex caused
        // a self-deadlock when reopening an already-open port.
        if (m_isOpen.load())
            close();

        std::lock_guard<std::mutex> lock(m_mutex);
        m_portName = port;
        m_baudRate = baudRate;

#ifdef _WIN32
        const std::string fullPortNameUtf8 = "\\\\.\\" + port;
        const std::wstring fullPortName = OmniGhost::Platform::Utf8ToWide(fullPortNameUtf8);
        if (fullPortName.empty())
            return false;

        const HANDLE openedHandle = CreateFileW(
            fullPortName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (openedHandle == INVALID_HANDLE_VALUE) {
            return false;
        }
        m_handle.reset(openedHandle);

        if (!configurePort()) {
            m_handle.reset();
            return false;
        }

        m_isOpen = true;

        // Start high-performance listener thread
        m_stopListener = false;
        m_listenerThread = std::jthread([this](std::stop_token stopToken) { listenerLoop(stopToken); });

        return true;
#else
        return false; // Linux implementation would go here
#endif
    }

    void SerialPort::close() {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_isOpen) {
            return;
        }

        // Stop listener thread
        m_stopListener = true;
        if (m_listenerThread.joinable()) {
            m_listenerThread.request_stop();
            m_listenerThread.join();
        }

        // Cancel all pending commands
        {
            std::lock_guard<std::mutex> cmdLock(m_commandMutex);
            for (auto& [id, cmd] : m_pendingCommands) {
                try {
                    cmd->promise.set_exception(std::make_exception_ptr(
                        std::runtime_error("Connection closed")));
                }
                catch (...) {
                    // Promise already set
                }
            }
            m_pendingCommands.clear();
        }

#ifdef _WIN32
        m_handle.reset();
#else
        if (m_fd >= 0) {
            ::close(m_fd);
            m_fd = -1;
        }
#endif

        m_isOpen = false;
    }

    bool SerialPort::isOpen() const {
        return m_isOpen;
    }

    std::future<std::string> SerialPort::sendTrackedCommand(const std::string& command,
        bool expectResponse,
        std::chrono::milliseconds timeout) {
        if (!m_isOpen) {
            std::promise<std::string> promise;
            promise.set_exception(std::make_exception_ptr(
                std::runtime_error("Port not open")));
            return promise.get_future();
        }

        int cmdId = generateCommandId();
        auto pendingCmd = std::make_unique<PendingCommand>(cmdId, command, expectResponse, timeout);
        auto future = pendingCmd->promise.get_future();

        // Store pending command
        {
            std::lock_guard<std::mutex> lock(m_commandMutex);
            m_pendingCommands[cmdId] = std::move(pendingCmd);
        }

        // Send command with ID tracking
        std::string trackedCommand = expectResponse ?
            command + "#" + std::to_string(cmdId) + "\r\n" :
            command + "\r\n";

#ifdef _WIN32
        DWORD bytesWritten = 0;
        bool success = WriteFile(m_handle.get(), trackedCommand.c_str(),
            static_cast<DWORD>(trackedCommand.length()),
            &bytesWritten, nullptr);

        if (!success || bytesWritten != trackedCommand.length()) {
            std::lock_guard<std::mutex> lock(m_commandMutex);
            auto it = m_pendingCommands.find(cmdId);
            if (it != m_pendingCommands.end()) {
                try {
                    it->second->promise.set_exception(std::make_exception_ptr(
                        std::runtime_error("Write failed")));
                }
                catch (...) {
                    // Promise already set
                }
                m_pendingCommands.erase(it);
            }
        }

        FlushFileBuffers(m_handle.get());
#endif

        return future;
    }

    bool SerialPort::sendCommand(const std::string& command) {
        if (!m_isOpen) {
            return false;
        }

        std::string fullCommand = command + "\r\n";

#ifdef _WIN32
        DWORD bytesWritten = 0;
        bool success = WriteFile(m_handle.get(), fullCommand.c_str(),
            static_cast<DWORD>(fullCommand.length()),
            &bytesWritten, nullptr);

        if (success && bytesWritten == fullCommand.length()) {
            FlushFileBuffers(m_handle.get());
            return true;
        }
#endif

        return false;
    }

    void SerialPort::listenerLoop(std::stop_token stopToken) {
        OmniGhost::Platform::SetCurrentThreadName(L"OmniGhost.MakcuSerial");
        std::vector<uint8_t> readBuffer(BUFFER_SIZE);
        std::vector<uint8_t> lineBuffer(LINE_BUFFER_SIZE);
        size_t linePos = 0;

        // Official: "km.buttons" + <mask_u8>
        // HPM:      "km."       + <mask_u8>
        static const char kFull[] = "km.buttons";
        static const size_t kFullLen = 10;

        m_prefixMatch = 0;
        m_expectMaskByte = false;
        m_prefixMode = 0; // 0 idle, 1 waiting mask after full, 2 waiting mask after short

        auto lastCleanup = std::chrono::steady_clock::now();
        constexpr auto cleanupInterval = std::chrono::milliseconds(50);

        while (!stopToken.stop_requested() && !m_stopListener && m_isOpen.load()) {
            try {
#ifdef _WIN32
                COMSTAT comStat;
                DWORD errors = 0;
                if (!ClearCommError(m_handle.get(), &errors, &comStat)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                if (comStat.cbInQue == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(500));
                    continue;
                }

                DWORD bytesToRead = std::min<DWORD>(comStat.cbInQue, static_cast<DWORD>(BUFFER_SIZE));
                DWORD bytesRead = 0;
                if (!ReadFile(m_handle.get(), readBuffer.data(), bytesToRead, &bytesRead, nullptr)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }

                for (DWORD i = 0; i < bytesRead; ++i) {
                    const uint8_t byte = readBuffer[i];

                    // ── Expecting mask byte after a recognized prefix ──
                    if (m_expectMaskByte) {
                        m_expectMaskByte = false;
                        if (byte == '(') {
                            // Text command echo: km.buttons(…) or km.(…) — rebuild prefix
                            const char* p = (m_prefixMode == 2) ? "km." : kFull;
                            while (*p && linePos < LINE_BUFFER_SIZE - 1)
                                lineBuffer[linePos++] = static_cast<uint8_t>(*p++);
                            if (linePos < LINE_BUFFER_SIZE - 1)
                                lineBuffer[linePos++] = '(';
                            m_prefixMatch = 0;
                            m_prefixMode = 0;
                            continue;
                        }
                        if (IsBinaryButtonMask(byte)) {
                            handleButtonData(byte);
                        } else {
                            // Preserve malformed/text continuation as a normal
                            // response; never manufacture a mouse mask from it.
                            const char* p = (m_prefixMode == 2) ? "km." : kFull;
                            while (*p && linePos < LINE_BUFFER_SIZE - 1)
                                lineBuffer[linePos++] = static_cast<uint8_t>(*p++);
                            if (linePos < LINE_BUFFER_SIZE - 1)
                                lineBuffer[linePos++] = byte;
                        }
                        m_prefixMatch = 0;
                        m_prefixMode = 0;
                        continue;
                    }

                    // ── Prefix matcher for "km.buttons" / short "km." ──
                    // State m_prefixMatch = number of matched chars of kFull so far.
                    if (byte == static_cast<uint8_t>(kFull[m_prefixMatch])) {
                        ++m_prefixMatch;
                        if (m_prefixMatch == kFullLen) {
                            // Full "km.buttons" matched → next byte is mask
                            m_expectMaskByte = true;
                            m_prefixMode = 1;
                            m_prefixMatch = 0;
                        }
                        continue;
                    }

                    // At exactly 3 matched chars we have "km." — if next is not 'b',
                    // accept only a BINARY short-form mask. Printable bytes are
                    // command echoes (km.move, km.left, km.right, ...).
                    if (m_prefixMatch == 3) {
                        // byte != 'b' (otherwise the branch above would have matched)
                        if (IsBinaryButtonMask(byte)) {
                            handleButtonData(byte);
                        } else {
                            static const char kShort[] = "km.";
                            for (const char* p = kShort; *p && linePos < LINE_BUFFER_SIZE - 1; ++p)
                                lineBuffer[linePos++] = static_cast<uint8_t>(*p);
                            if (linePos < LINE_BUFFER_SIZE - 1)
                                lineBuffer[linePos++] = byte;
                        }
                        m_prefixMatch = 0;
                        m_prefixMode = 0;
                        continue;
                    }

                    // Prefix broken — restart if this byte could start a new "k"
                    if (m_prefixMatch > 0) {
                        m_prefixMatch = (byte == 'k') ? 1 : 0;
                        if (m_prefixMatch == 1)
                            continue;
                    }

                    // ── Legacy bare mask (Eventuri-style km.buttons(1) stream) ──
                    // Accept 0x00-0x1F when not mid-text-line. Skip lone CR/LF until
                    // we have already seen at least one real button packet (avoids
                    // treating response newlines as masks).
                    if (linePos == 0 && byte <= 0x1F) {
                        const bool crlf = (byte == 0x0A || byte == 0x0D);
                        if (!crlf) {
                            handleButtonData(byte);
                            continue;
                        }
                        if (m_buttonPackets.load() > 0) {
                            // After stream is alive, CR/LF after a mask are terminators
                            linePos = 0;
                            continue;
                        }
                        // else fall through: treat as text line ending
                    }

                    // ── Text line assembly ──
                    if (byte == 0x0A) {
                        if (linePos > 0) {
                            std::string line(lineBuffer.begin(), lineBuffer.begin() + linePos);
                            linePos = 0;
                            if (!line.empty())
                                processResponse(line);
                        }
                    }
                    else if (byte != 0x0D) {
                        if (linePos < LINE_BUFFER_SIZE - 1)
                            lineBuffer[linePos++] = byte;
                    }
                }
#endif
                auto now = std::chrono::steady_clock::now();
                if (now - lastCleanup > cleanupInterval) {
                    cleanupTimedOutCommands();
                    lastCleanup = now;
                }
            }
            catch (const std::exception& exception) {
                std::cerr << "[MAKCU] listener exception: " << exception.what() << "\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            catch (...) {
                std::cerr << "[MAKCU] listener unknown exception\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }

    void SerialPort::handleButtonData(uint8_t data) {
        // Absolute mask 0-31. Always notify (even mask==0) so stream heartbeat
        // confirms monitoring; only fire per-button edges on change.
        data = static_cast<uint8_t>(data & 0x1F);
        const uint8_t lastMask = m_lastButtonMask.exchange(data);
        m_buttonPackets.fetch_add(1, std::memory_order_relaxed);

        if (m_maskCallback) {
            try { m_maskCallback(data); }
            catch (const std::exception& exception) {
                std::cerr << "[MAKCU] callback de máscara falhou: " << exception.what() << "\n";
            }
            catch (...) {
                std::cerr << "[MAKCU] callback de máscara falhou com uma exceção não identificada.\n";
            }
        }

        if (data != lastMask && m_buttonCallback) {
            const uint8_t changedBits = static_cast<uint8_t>(data ^ lastMask);
            for (int bit = 0; bit < 5; ++bit) {
                if (changedBits & (1 << bit)) {
                    const bool isPressed = (data & (1 << bit)) != 0;
                    try {
                        m_buttonCallback(static_cast<uint8_t>(bit), isPressed);
                    } catch (const std::exception& exception) {
                        std::cerr << "[MAKCU] callback de botão falhou: " << exception.what() << "\n";
                    } catch (...) {
                        std::cerr << "[MAKCU] callback de botão falhou com uma exceção não identificada.\n";
                    }
                }
            }
        }
    }

    void SerialPort::processResponse(const std::string& response) {
        // Remove ">>> " prefix if present
        std::string content = response;
        if (content.substr(0, 4) == ">>> ") {
            content = content.substr(4);
        }

        // Check for command ID correlation
        size_t hashPos = content.find('#');
        if (hashPos != std::string::npos) {
            // Extract command ID
            std::string idStr = content.substr(hashPos + 1);
            size_t colonPos = idStr.find(':');
            if (colonPos != std::string::npos) {
                try {
                    int cmdId = std::stoi(idStr.substr(0, colonPos));
                    std::string result = idStr.substr(colonPos + 1);

                    std::lock_guard<std::mutex> lock(m_commandMutex);
                    auto it = m_pendingCommands.find(cmdId);
                    if (it != m_pendingCommands.end()) {
                        try {
                            it->second->promise.set_value(result);
                        }
                        catch (const std::future_error& exception) {
                            std::cerr << "[MAKCU] resposta duplicada/expirada para comando "
                                      << cmdId << ": " << exception.what() << "\n";
                        }
                        m_pendingCommands.erase(it);
                    }
                    return;
                }
                catch (const std::exception& exception) {
                    std::cerr << "[MAKCU] identificador de resposta inválido: "
                              << exception.what() << "\n";
                }
            }
        }

        // Some Makcu firmwares stream button masks as plain numeric lines "0".."31"
        // instead of raw binary bytes. Accept those as button state updates.
        {
            bool numeric = !content.empty();
            for (char c : content) {
                if (c < '0' || c > '9') { numeric = false; break; }
            }
            if (numeric) {
                try {
                    int value = std::stoi(content);
                    if (value >= 0 && value <= 31) {
                        handleButtonData(static_cast<uint8_t>(value));
                        return;
                    }
                } catch (const std::exception& exception) {
                    std::cerr << "[MAKCU] máscara numérica inválida: "
                              << exception.what() << "\n";
                }
            }
        }

        // Text forms: "km.buttons=2", "buttons: 2", "mask=0x02"
        {
            auto tryMask = [&](int value) {
                if (value >= 0 && value <= 31) {
                    handleButtonData(static_cast<uint8_t>(value));
                    return true;
                }
                return false;
            };
            const auto lower = content; // already raw
            auto digAt = [&](size_t pos) -> int {
                if (pos >= lower.size()) return -1;
                size_t i = pos;
                while (i < lower.size() && (lower[i] == ' ' || lower[i] == '=' || lower[i] == ':')) ++i;
                if (i + 1 < lower.size() && lower[i] == '0' && (lower[i+1] == 'x' || lower[i+1] == 'X')) {
                    i += 2;
                    int v = 0; bool any = false;
                    while (i < lower.size()) {
                        char c = lower[i];
                        int d = -1;
                        if (c >= '0' && c <= '9') d = c - '0';
                        else if (c >= 'a' && c <= 'f') d = 10 + c - 'a';
                        else if (c >= 'A' && c <= 'F') d = 10 + c - 'A';
                        else break;
                        any = true; v = (v << 4) + d; ++i;
                    }
                    return any ? v : -1;
                }
                if (i < lower.size() && lower[i] >= '0' && lower[i] <= '9') {
                    int v = 0;
                    while (i < lower.size() && lower[i] >= '0' && lower[i] <= '9') {
                        v = v * 10 + (lower[i] - '0'); ++i;
                    }
                    return v;
                }
                return -1;
            };
            size_t p;
            if ((p = lower.find("buttons")) != std::string::npos) {
                int v = digAt(p + 7);
                if (tryMask(v)) return;
            }
            if ((p = lower.find("mask")) != std::string::npos) {
                int v = digAt(p + 4);
                if (tryMask(v)) return;
            }
        }

        // Handle untracked response (oldest pending command)
        std::lock_guard<std::mutex> lock(m_commandMutex);
        if (!m_pendingCommands.empty()) {
            auto it = m_pendingCommands.begin();
            try {
                it->second->promise.set_value(content);
            }
            catch (...) {
                // Promise already set
            }
            m_pendingCommands.erase(it);
        }
    }

    void SerialPort::cleanupTimedOutCommands() {
        auto now = std::chrono::steady_clock::now();

        std::lock_guard<std::mutex> lock(m_commandMutex);
        auto it = m_pendingCommands.begin();
        while (it != m_pendingCommands.end()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - it->second->timestamp);

            if (elapsed > it->second->timeout) {
                try {
                    it->second->promise.set_exception(std::make_exception_ptr(
                        std::runtime_error("Command timeout")));
                }
                catch (...) {
                    // Promise already set
                }
                it = m_pendingCommands.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    int SerialPort::generateCommandId() {
        return (m_commandCounter.fetch_add(1) % 10000) + 1;
    }

    bool SerialPort::configurePort() {
#ifdef _WIN32
        m_dcb.DCBlength = sizeof(DCB);

        if (!GetCommState(m_handle.get(), &m_dcb)) {
            return false;
        }

        m_dcb.BaudRate = m_baudRate;
        m_dcb.ByteSize = 8;
        m_dcb.Parity = NOPARITY;
        m_dcb.StopBits = ONESTOPBIT;
        m_dcb.fBinary = TRUE;
        m_dcb.fParity = FALSE;
        m_dcb.fOutxCtsFlow = FALSE;
        m_dcb.fOutxDsrFlow = FALSE;
        m_dcb.fDtrControl = DTR_CONTROL_DISABLE;
        m_dcb.fDsrSensitivity = FALSE;
        m_dcb.fTXContinueOnXoff = FALSE;
        m_dcb.fOutX = FALSE;
        m_dcb.fInX = FALSE;
        m_dcb.fErrorChar = FALSE;
        m_dcb.fNull = FALSE;
        m_dcb.fRtsControl = RTS_CONTROL_DISABLE;
        m_dcb.fAbortOnError = FALSE;

        if (!SetCommState(m_handle.get(), &m_dcb)) {
            return false;
        }

        updateTimeouts();
        return true;
#else
        return false;
#endif
    }

    void SerialPort::updateTimeouts() {
#ifdef _WIN32
        // Gaming-optimized timeouts - much faster than original
        m_timeouts.ReadIntervalTimeout = 1;          // 1ms between bytes
        m_timeouts.ReadTotalTimeoutConstant = 10;    // 10ms total read timeout
        m_timeouts.ReadTotalTimeoutMultiplier = 1;   // 1ms per byte
        m_timeouts.WriteTotalTimeoutConstant = 10;   // 10ms write timeout
        m_timeouts.WriteTotalTimeoutMultiplier = 1;  // 1ms per byte

        SetCommTimeouts(m_handle.get(), &m_timeouts);
#endif
    }

    void SerialPort::setButtonCallback(ButtonCallback callback) {
        m_buttonCallback = callback;
    }

    void SerialPort::setMaskCallback(MaskCallback callback) {
        m_maskCallback = callback;
    }

    // Legacy compatibility methods
    bool SerialPort::setBaudRate(uint32_t baudRate) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_isOpen) {
            m_baudRate = baudRate;
            return true;
        }
        m_baudRate = baudRate;
#ifdef _WIN32
        m_dcb.BaudRate = baudRate;
        return SetCommState(m_handle.get(), &m_dcb) != 0;
#else
        return false;
#endif
    }

    uint32_t SerialPort::getBaudRate() const {
        return m_baudRate;
    }

    std::string SerialPort::getPortName() const {
        return m_portName;
    }

    bool SerialPort::write(const std::vector<uint8_t>& data) {
        return sendCommand(std::string(data.begin(), data.end()));
    }

    bool SerialPort::write(const std::string& data) {
        return sendCommand(data);
    }

    std::vector<uint8_t> SerialPort::read(size_t maxBytes) {
        // This is a legacy method - not recommended for high performance
        std::vector<uint8_t> buffer;
        if (!m_isOpen || maxBytes == 0) {
            return buffer;
        }

#ifdef _WIN32
        buffer.resize(maxBytes);
        DWORD bytesRead = 0;
        bool result = ReadFile(m_handle.get(), buffer.data(),
            static_cast<DWORD>(maxBytes), &bytesRead, nullptr);
        if (result && bytesRead > 0) {
            buffer.resize(bytesRead);
        }
        else {
            buffer.clear();
        }
#endif

        return buffer;
    }

    std::string SerialPort::readString(size_t maxBytes) {
        auto data = read(maxBytes);
        return std::string(data.begin(), data.end());
    }

    size_t SerialPort::available() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_isOpen) {
            return 0;
        }

#ifdef _WIN32
        COMSTAT comStat;
        DWORD errors;
        if (ClearCommError(m_handle.get(), &errors, &comStat)) {
            return comStat.cbInQue;
        }
#endif

        return 0;
    }

    bool SerialPort::flush() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_isOpen) {
            return false;
        }

#ifdef _WIN32
        return FlushFileBuffers(m_handle.get()) != 0;
#else
        return false;
#endif
    }

    void SerialPort::setTimeout(uint32_t timeoutMs) {
        m_timeout = timeoutMs;
        if (m_isOpen) {
            updateTimeouts();
        }
    }

    uint32_t SerialPort::getTimeout() const {
        return m_timeout;
    }

    std::vector<std::string> SerialPort::getAvailablePorts() {
        std::vector<std::string> ports;

#ifdef _WIN32
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char valueName[256];
            char data[256];
            DWORD valueNameSize, dataSize, dataType;
            DWORD index = 0;

            while (true) {
                valueNameSize = sizeof(valueName);
                dataSize = sizeof(data);

                LONG result = RegEnumValueA(hKey, index++, valueName, &valueNameSize,
                    nullptr, &dataType,
                    reinterpret_cast<BYTE*>(data), &dataSize);

                if (result == ERROR_NO_MORE_ITEMS) {
                    break;
                }

                if (result == ERROR_SUCCESS && dataType == REG_SZ) {
                    ports.emplace_back(data);
                }
            }

            RegCloseKey(hKey);
        }
#endif

        std::sort(ports.begin(), ports.end());
        return ports;
    }

    std::vector<std::string> SerialPort::findMakcuPorts() {
        std::vector<std::string> makcuPorts;

#ifdef _WIN32
        auto allPorts = getAvailablePorts();
        HDEVINFO deviceInfoSet = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS,
            nullptr, nullptr, DIGCF_PRESENT);
        if (deviceInfoSet == INVALID_HANDLE_VALUE) {
            return makcuPorts;
        }

        SP_DEVINFO_DATA deviceInfoData;
        deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

        for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &deviceInfoData); i++) {
            char description[256] = { 0 };
            char portName[256] = { 0 };

            if (SetupDiGetDeviceRegistryPropertyA(deviceInfoSet, &deviceInfoData,
                SPDRP_DEVICEDESC, nullptr,
                reinterpret_cast<BYTE*>(description),
                sizeof(description), nullptr)) {
                std::string desc(description);

                if (desc.find("USB-Enhanced-SERIAL CH343") != std::string::npos ||
                    desc.find("USB-SERIAL CH340") != std::string::npos) {

                    HKEY hDeviceKey = SetupDiOpenDevRegKey(deviceInfoSet, &deviceInfoData,
                        DICS_FLAG_GLOBAL, 0,
                        DIREG_DEV, KEY_READ);
                    if (hDeviceKey != INVALID_HANDLE_VALUE) {
                        DWORD portNameSize = sizeof(portName);

                        if (RegQueryValueExA(hDeviceKey, "PortName", nullptr, nullptr,
                            reinterpret_cast<BYTE*>(portName),
                            &portNameSize) == ERROR_SUCCESS) {
                            std::string port(portName);
                            if (std::find(allPorts.begin(), allPorts.end(), port) != allPorts.end()) {
                                makcuPorts.emplace_back(port);
                            }
                        }
                        RegCloseKey(hDeviceKey);
                    }
                }
            }
        }

        SetupDiDestroyDeviceInfoList(deviceInfoSet);
#endif

        std::sort(makcuPorts.begin(), makcuPorts.end());
        makcuPorts.erase(std::unique(makcuPorts.begin(), makcuPorts.end()), makcuPorts.end());
        return makcuPorts;
    }

} // namespace makcu
