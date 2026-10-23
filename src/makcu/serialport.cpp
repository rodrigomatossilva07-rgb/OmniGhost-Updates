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
#include <cctype>
#include <limits>

#ifdef _WIN32
#include <setupapi.h>
#include <devguid.h>
#include <cfgmgr32.h>
#pragma comment(lib, "setupapi.lib")
#endif

namespace makcu {

    namespace {


        constexpr bool IsBinaryButtonMask(uint8_t value) noexcept {
            return value <= 0x1F;
        }

        static_assert(IsBinaryButtonMask(0x00) && IsBinaryButtonMask(0x1F));
        static_assert(!IsBinaryButtonMask(static_cast<uint8_t>('m')));
        static_assert(!IsBinaryButtonMask(static_cast<uint8_t>('l')));

        // HW-005: LegacyV2Binary button stream command. Per the published MAKCU
        // V2 binary API, the button stream frame is
        //   [0x50][0x02][len=2][buttons_lo:u8][buttons_hi:u8]
        // with the same 5-bit mask as text mode (bit0=left, 1=right,
        // 2=middle, 3=side1, 4=side2).
        constexpr uint8_t kV2ButtonStreamCommand = 0x02;
    }

    SerialPort::SerialPort()
        : m_baudRate(115200)
        , m_timeout(100)
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

    bool SerialPort::open(const std::string& port, uint32_t baudRate, bool startReader) {


        if (m_isOpen.load() || m_listenerThread.joinable())
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


        m_stopListener = false;
        if (startReader) {
            try {
                m_listenerStarted = true;
                m_listenerThread = std::jthread([this](std::stop_token stopToken) { listenerLoop(stopToken); });
            } catch (...) {
                m_listenerStarted = false;
                m_isOpen = false;
                m_handle.reset();
                return false;
            }
        }

        return true;
#else
        return false;
#endif
    }

    void SerialPort::close() {
        std::jthread listener;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_isOpen && !m_listenerThread.joinable()) return;
            m_isOpen = false;
            m_stopListener = true;
            if (m_listenerThread.joinable()) {
                m_listenerThread.request_stop();
#ifdef _WIN32
                if (m_handle) CancelIoEx(m_handle.get(), nullptr);
#endif
                listener = std::move(m_listenerThread);
            }
        }

        if (listener.joinable()) listener.join();

        {
            std::lock_guard<std::mutex> cmdLock(m_commandMutex);
            for (auto& [id, cmd] : m_pendingCommands) {
                try {
                    cmd->promise.set_exception(std::make_exception_ptr(
                        std::runtime_error("Connection closed")));
                }
                catch (const std::exception& ex) {
                    std::cerr << "[SerialPort] Cancel promise failed: " << ex.what() << "\n";
                }
                catch (...) {
                    std::cerr << "[SerialPort] Cancel promise failed with unknown exception\n";
                }
            }
            m_pendingCommands.clear();
        }

#ifdef _WIN32
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_handle.reset();
        }
#else
        if (m_fd >= 0) {
            ::close(m_fd);
            m_fd = -1;
        }
#endif
        m_listenerStarted = false;
    }

    bool SerialPort::isOpen() const {
        return m_isOpen;
    }

    bool SerialPort::startListener() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_isOpen) return false;
        if (m_listenerStarted.load()) return true;
        m_stopListener = false;
        try {
            m_listenerStarted = true;
            m_listenerThread = std::jthread([this](std::stop_token stopToken) { listenerLoop(stopToken); });
        } catch (...) {
            m_listenerStarted = false;
            return false;
        }
        return true;
    }

    void SerialPort::setReaderMode(TransportReaderMode mode) noexcept {
        if (!m_listenerStarted.load()) m_readerMode.store(mode);
    }

    bool SerialPort::writeRaw(std::span<const uint8_t> data) {
        if (data.empty()) return true;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_isOpen) return false;
#ifdef _WIN32
        size_t offset = 0;
        while (offset < data.size()) {
            const DWORD remaining = static_cast<DWORD>((std::min)(
                data.size() - offset, static_cast<size_t>(std::numeric_limits<DWORD>::max())));
            DWORD written = 0;
            if (!WriteFile(m_handle.get(), data.data() + offset, remaining, &written, nullptr) || written == 0)
                return false;
            offset += written;
        }
        return true;
#else
        return false;
#endif
    }

    std::vector<uint8_t> SerialPort::readFor(std::chrono::milliseconds timeout,
                                             std::chrono::milliseconds quietPeriod) {
        std::vector<uint8_t> output;
        if (m_listenerStarted.load()) return output;
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        auto quietDeadline = deadline;
        bool received = false;
        while (std::chrono::steady_clock::now() < deadline) {
            const size_t queued = available();
            if (queued != 0) {
                std::vector<uint8_t> chunk = read((std::min)(queued, static_cast<size_t>(4096)));
                if (!chunk.empty()) {
                    output.insert(output.end(), chunk.begin(), chunk.end());
                    received = true;
                    quietDeadline = std::chrono::steady_clock::now() + quietPeriod;
                }
            } else if (received && std::chrono::steady_clock::now() >= quietDeadline) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return output;
    }

    bool SerialPort::purge() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_isOpen || m_listenerStarted.load()) return false;
#ifdef _WIN32
        return PurgeComm(m_handle.get(), PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR) != 0;
#else
        return false;
#endif
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
        {
            std::lock_guard<std::mutex> lock(m_commandMutex);
            if (expectResponse && !m_pendingCommands.empty()) {
                pendingCmd->promise.set_exception(std::make_exception_ptr(
                    std::runtime_error("Another MAKCU query is already in flight")));
                return future;
            }
            m_pendingCommands[cmdId] = std::move(pendingCmd);
        }

        const std::string wireCommand = command + "\r\n";
        const bool success = writeRaw(std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(wireCommand.data()), wireCommand.size()));
        if (!success) {
            std::lock_guard<std::mutex> lock(m_commandMutex);
            auto it = m_pendingCommands.find(cmdId);
            if (it != m_pendingCommands.end()) {
                try {
                    it->second->promise.set_exception(std::make_exception_ptr(
                        std::runtime_error("Write failed")));
                }
                catch (const std::exception& ex) {
                    std::cerr << "[SerialPort] Write failed promise set failed: " << ex.what() << "\n";
                }
                catch (...) {
                    std::cerr << "[SerialPort] Write failed promise set failed with unknown exception\n";
                }
                m_pendingCommands.erase(it);
            }
        } else if (!expectResponse) {
            std::lock_guard<std::mutex> lock(m_commandMutex);
            auto it = m_pendingCommands.find(cmdId);
            if (it != m_pendingCommands.end()) {
                it->second->promise.set_value({});
                m_pendingCommands.erase(it);
            }
        }
        return future;
    }

    bool SerialPort::sendCommand(const std::string& command) {
        if (!m_isOpen) {
            return false;
        }

        std::string fullCommand = command + "\r\n";
        return writeRaw(std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(fullCommand.data()), fullCommand.size()));
    }

    void SerialPort::listenerLoop(std::stop_token stopToken) {
        OmniGhost::Platform::SetCurrentThreadName(L"OmniGhost.MakcuSerial");
        std::vector<uint8_t> readBuffer(BUFFER_SIZE);
        std::vector<uint8_t> lineBuffer(LINE_BUFFER_SIZE);
        std::vector<uint8_t> binaryBuffer;
        size_t linePos = 0;



        static const char kFull[] = "km.buttons";
        static const size_t kFullLen = 10;

        m_prefixMatch = 0;
        m_expectMaskByte = false;
        m_prefixMode = 0;

        auto lastCleanup = std::chrono::steady_clock::now();
        constexpr auto cleanupInterval = std::chrono::milliseconds(50);
        unsigned consecutiveIoFailures = 0;

        while (!stopToken.stop_requested() && !m_stopListener && m_isOpen.load()) {
            try {
#ifdef _WIN32
                COMSTAT comStat;
                DWORD errors = 0;
                if (!ClearCommError(m_handle.get(), &errors, &comStat)) {
                    if (++consecutiveIoFailures >= 3) {
                        m_isOpen = false;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                consecutiveIoFailures = 0;
                if (comStat.cbInQue == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(500));
                    continue;
                }

                DWORD bytesToRead = std::min<DWORD>(comStat.cbInQue, static_cast<DWORD>(BUFFER_SIZE));
                DWORD bytesRead = 0;
                if (!ReadFile(m_handle.get(), readBuffer.data(), bytesToRead, &bytesRead, nullptr)) {
                    if (++consecutiveIoFailures >= 3) {
                        m_isOpen = false;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                consecutiveIoFailures = 0;

                if (m_readerMode.load() == TransportReaderMode::LegacyV2Binary) {
                    binaryBuffer.insert(binaryBuffer.end(), readBuffer.begin(), readBuffer.begin() + bytesRead);
                    while (!binaryBuffer.empty()) {
                        const auto start = std::find(binaryBuffer.begin(), binaryBuffer.end(), static_cast<uint8_t>(0x50));
                        if (start != binaryBuffer.begin()) binaryBuffer.erase(binaryBuffer.begin(), start);
                        if (binaryBuffer.size() < 4) break;
                        const size_t payloadLength = static_cast<size_t>(binaryBuffer[2]) |
                                                     (static_cast<size_t>(binaryBuffer[3]) << 8);
                        if (payloadLength > BUFFER_SIZE) {
                            binaryBuffer.erase(binaryBuffer.begin());
                            continue;
                        }
                        const size_t frameLength = 4 + payloadLength;
                        if (binaryBuffer.size() < frameLength) break;
                        // HW-005: this branch used to consume and silently drop
                        // every 0x50 frame, so V2/AIO button reports never
                        // reached the mask/button callbacks and
                        // makcu_wrapper::GetButtonState always fell back to
                        // LocalWindows (useless in a 2-PC setup). Route button
                        // stream frames (cmd 0x02, buttons_lo = first payload
                        // byte) through the same handler the text mode uses.
                        // NEEDS VALIDATION: wire format taken from the
                        // published V2 binary API; end-to-end behaviour still
                        // requires a real V2/AIO device.
                        if (binaryBuffer[1] == kV2ButtonStreamCommand && payloadLength >= 1)
                            handleButtonData(binaryBuffer[4]);
                        binaryBuffer.erase(binaryBuffer.begin(), binaryBuffer.begin() + static_cast<ptrdiff_t>(frameLength));
                    }
                    continue;
                }

                for (DWORD i = 0; i < bytesRead; ++i) {
                    const uint8_t byte = readBuffer[i];


                    if (m_expectMaskByte) {
                        m_expectMaskByte = false;
                        if (byte == '(') {

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



                    if (byte == static_cast<uint8_t>(kFull[m_prefixMatch])) {
                        ++m_prefixMatch;
                        if (m_prefixMatch == kFullLen) {

                            m_expectMaskByte = true;
                            m_prefixMode = 1;
                            m_prefixMatch = 0;
                        }
                        continue;
                    }




                    if (m_prefixMatch == 3) {

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


                    if (m_prefixMatch > 0) {
                        m_prefixMatch = (byte == 'k') ? 1 : 0;
                        if (m_prefixMatch == 1)
                            continue;
                    }





                    if (linePos == 0 && byte <= 0x1F) {
                        const bool crlf = (byte == 0x0A || byte == 0x0D);
                        if (!crlf) {
                            handleButtonData(byte);
                            continue;
                        }
                        if (m_buttonPackets.load() > 0) {

                            linePos = 0;
                            continue;
                        }

                    }


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


        data = static_cast<uint8_t>(data & 0x1F);
        const uint8_t lastMask = m_lastButtonMask.exchange(data);
        m_buttonPackets.fetch_add(1, std::memory_order_relaxed);

        MaskCallback maskCallback;
        ButtonCallback buttonCallback;
        {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            maskCallback = m_maskCallback;
            buttonCallback = m_buttonCallback;
        }

        if (maskCallback) {
            try { maskCallback(data); }
            catch (const std::exception& exception) {
                std::cerr << "[MAKCU] callback de máscara falhou: " << exception.what() << "\n";
            }
            catch (...) {
                std::cerr << "[MAKCU] callback de máscara falhou com uma exceção não identificada.\n";
            }
        }

        if (data != lastMask && buttonCallback) {
            const uint8_t changedBits = static_cast<uint8_t>(data ^ lastMask);
            for (int bit = 0; bit < 5; ++bit) {
                if (changedBits & (1 << bit)) {
                    const bool isPressed = (data & (1 << bit)) != 0;
                    try {
                        buttonCallback(static_cast<uint8_t>(bit), isPressed);
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

        std::string content = response;
        if (content.rfind(">>> ", 0) == 0) {
            content = content.substr(4);
        }
        if (content.empty() || content == ">>>") return;

        {
            std::lock_guard<std::mutex> lock(m_commandMutex);
            if (m_pendingCommands.size() == 1 &&
                content == m_pendingCommands.begin()->second->command)
                return;
        }


        size_t hashPos = content.find('#');
        if (hashPos != std::string::npos) {

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


        {
            auto tryMask = [&](int value) {
                if (value >= 0 && value <= 31) {
                    handleButtonData(static_cast<uint8_t>(value));
                    return true;
                }
                return false;
            };
            const auto lower = content;
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


                        if (v > 31) return -1;
                        any = true; v = (v << 4) + d; ++i;
                    }
                    return any ? v : -1;
                }
                if (i < lower.size() && lower[i] >= '0' && lower[i] <= '9') {
                    int v = 0;
                    while (i < lower.size() && lower[i] >= '0' && lower[i] <= '9') {
                        if (v > 31) return -1;
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






        std::lock_guard<std::mutex> lock(m_commandMutex);
        if (m_pendingCommands.size() == 1) {
            auto it = m_pendingCommands.begin();
            try {
                it->second->promise.set_value(content);
            }
            catch (const std::exception& ex) {
                std::cerr << "[SerialPort] Set value failed for untracked response: " << ex.what() << "\n";
            }
            catch (...) {
                std::cerr << "[SerialPort] Set value failed for untracked response with unknown exception\n";
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
                catch (const std::exception& ex) {
                    std::cerr << "[SerialPort] Timeout exception for command " << it->first << ": " << ex.what() << "\n";
                }
                catch (...) {
                    std::cerr << "[SerialPort] Timeout exception for command " << it->first << " with unknown exception\n";
                }
                it = m_pendingCommands.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    int SerialPort::generateCommandId() {


        return static_cast<int>(m_commandCounter.fetch_add(1));
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

        m_timeouts.ReadIntervalTimeout = 1;
        m_timeouts.ReadTotalTimeoutConstant = 10;
        m_timeouts.ReadTotalTimeoutMultiplier = 1;
        m_timeouts.WriteTotalTimeoutConstant = 10;
        m_timeouts.WriteTotalTimeoutMultiplier = 1;

        SetCommTimeouts(m_handle.get(), &m_timeouts);
#endif
    }

    void SerialPort::setButtonCallback(ButtonCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_buttonCallback = callback;
    }

    void SerialPort::setMaskCallback(MaskCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_maskCallback = callback;
    }


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
        return writeRaw(data);
    }

    bool SerialPort::write(const std::string& data) {
        return writeRaw(std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(data.data()), data.size()));
    }

    std::vector<uint8_t> SerialPort::read(size_t maxBytes) {

        std::vector<uint8_t> buffer;
        std::lock_guard<std::mutex> handleLock(m_mutex);
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
        std::vector<std::string> weakPorts;

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
            char hardwareIds[512] = { 0 };

            if (SetupDiGetDeviceRegistryPropertyA(deviceInfoSet, &deviceInfoData,
                SPDRP_DEVICEDESC, nullptr,
                reinterpret_cast<BYTE*>(description),
                sizeof(description), nullptr)) {
                std::string desc(description);
                SetupDiGetDeviceRegistryPropertyA(deviceInfoSet, &deviceInfoData,
                    SPDRP_HARDWAREID, nullptr,
                    reinterpret_cast<BYTE*>(hardwareIds), sizeof(hardwareIds), nullptr);
                std::string identity = desc + " " + hardwareIds;
                std::transform(identity.begin(), identity.end(), identity.begin(),
                    [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });





                const bool makcuIdentity =
                    identity.find("VID_1A86&PID_55D3") != std::string::npos;
                const bool genericSerialCandidate =
                    !makcuIdentity &&
                    (identity.find("CH343") != std::string::npos ||
                     identity.find("USB-SERIAL CH340") != std::string::npos ||
                     identity.find("USB-ENHANCED-SERIAL") != std::string::npos);

                if (makcuIdentity || genericSerialCandidate) {

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




                                if (makcuIdentity)
                                    makcuPorts.emplace_back(port);
                                else
                                    weakPorts.emplace_back(port);
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
        std::sort(weakPorts.begin(), weakPorts.end());
        weakPorts.erase(std::unique(weakPorts.begin(), weakPorts.end()), weakPorts.end());


        if (!makcuPorts.empty())
            return makcuPorts;
        return weakPorts;
    }

}
