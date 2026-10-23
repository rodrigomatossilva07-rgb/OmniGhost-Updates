#include "makcu.h"
#include "serialport.h"
#include "makcu_connection.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <atomic>
#include <mutex>
#include <unordered_map>


extern void MakcuNotifyMaskFromSerial(uint8_t m);

namespace makcu {


    constexpr uint16_t MAKCU_VID = 0x1A86;
    constexpr uint16_t MAKCU_PID = 0x55D3;
    constexpr const char* TARGET_DESC = "USB-Enhanced-SERIAL CH343";
    constexpr const char* DEFAULT_NAME = "USB-SERIAL CH340";
    std::atomic<bool> PerformanceProfiler::s_enabled{ false };
    std::mutex PerformanceProfiler::s_mutex;
    std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> PerformanceProfiler::s_stats;


    class Device::Impl {
    public:
        std::unique_ptr<SerialPort> serialPort;
        DeviceInfo deviceInfo;




        std::atomic<ConnectionStatus> status{ConnectionStatus::DISCONNECTED};
        std::atomic<bool> connected;
        std::atomic<bool> monitoring;
        std::atomic<bool> highPerformanceMode;
        mutable std::mutex mutex;
        FirmwareDescriptor firmware;
        CapabilitySet capabilities;
        std::unique_ptr<IMakcuProtocol> protocol;
        MakcuConnectionManager connectionManager;
        std::string lastError;




        mutable std::mutex statusMutex;


        std::atomic<uint16_t> lockStateCache{ 0 };
        std::atomic<bool> lockStateCacheValid{ false };


        std::atomic<uint8_t> currentButtonMask{ 0 };


        Device::MouseButtonCallback mouseButtonCallback;
        Device::ConnectionCallback connectionCallback;
        std::mutex callbackMutex;


        mutable std::string moveCommandBuffer;
        mutable std::mutex moveBufferMutex;




        void markConnectionStatus(ConnectionStatus value) noexcept {
            status.store(value, std::memory_order_release);
        }
        void markConnectionError(std::string error) {
            status.store(ConnectionStatus::CONNECTION_ERROR, std::memory_order_release);
            std::lock_guard<std::mutex> lock(statusMutex);
            lastError = std::move(error);
        }
        void clearConnectionError() {
            std::lock_guard<std::mutex> lock(statusMutex);
            lastError.clear();
        }

        Impl() : serialPort(std::make_unique<SerialPort>())
            , status(ConnectionStatus::DISCONNECTED)
            , connected(false)
            , monitoring(false)
            , highPerformanceMode(false) {
            deviceInfo.isConnected = false;





            serialPort->setMaskCallback([this](uint8_t mask) {
                currentButtonMask.store(mask);
                monitoring.store(true);
                MakcuNotifyMaskFromSerial(mask);
            });
            serialPort->setButtonCallback([this](uint8_t button, bool pressed) {
                handleButtonEvent(button, pressed);
            });
        }

        ~Impl() = default;







        std::atomic<uint32_t> reconnectAttempts{ 0 };
        static constexpr uint32_t kMaxReconnectAttempts = 5;
        std::chrono::steady_clock::time_point lastReconnectAttempt{};
        std::chrono::milliseconds reconnectCooldown{ 2000 };

        bool executeWire(const WireCommand& wire) {
            if (!connected.load() || !serialPort->isOpen()) return false;
            bool ok;
            if (wire.encoding == WireEncoding::Text) {
                const std::string command(wire.bytes.begin(), wire.bytes.end());
                ok = serialPort->sendCommand(command);
            } else {
                ok = serialPort->writeRaw(wire.bytes);
            }



            if (!ok) {
                const bool wasConnected = connected.exchange(false);
                if (wasConnected) {
                    markConnectionError("Write failed; device disconnected mid-session.");
                    currentButtonMask.store(0);
                    monitoring.store(false);
                }
            }
            return ok;
        }

        bool executeWireUngated(const WireCommand& wire) {
            if (!serialPort->isOpen()) return false;
            if (wire.encoding == WireEncoding::Text) {
                const std::string command(wire.bytes.begin(), wire.bytes.end());
                return serialPort->sendCommand(command);
            }
            return serialPort->writeRaw(wire.bytes);
        }

        void handleButtonEvent(uint8_t button, bool pressed) {

            (void)pressed;
            Device::MouseButtonCallback callback;
            {
                std::lock_guard<std::mutex> lock(callbackMutex);
                callback = mouseButtonCallback;
            }
            if (callback && button < 5) {
                MouseButton mouseBtn = static_cast<MouseButton>(button);
                try {
                    callback(mouseBtn, pressed);
                }
                catch (...) {
                    std::cerr << "[MAKCU] Exceção no callback de botão; evento ignorado." << std::endl;
                }
            }
        }

        void notifyConnectionChange(bool isConnected) {
            Device::ConnectionCallback callback;
            {
                std::lock_guard<std::mutex> lock(callbackMutex);
                callback = connectionCallback;
            }
            if (callback) {
                try {
                    callback(isConnected);
                }
                catch (...) {
                    std::cerr << "[MAKCU] Exceção no callback de ligação; estado preservado." << std::endl;
                }
            }
        }


        bool executeCommand(const std::string& command) {
            if (!connected.load()) {
                return false;
            }

            auto start = std::chrono::high_resolution_clock::now();
            const bool result = serialPort->sendCommand(command);
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            makcu::PerformanceProfiler::logCommandTiming(command, duration);

            return result;
        }


        bool executeMoveCommand(int32_t x, int32_t y) {
            if (!protocol) return false;
            const auto command = protocol->Move(x, y);
            return command && executeWire(*command);
        }


        void updateLockStateCache(const std::string& target, bool locked) {
            static const std::unordered_map<std::string, int> lockBitMap = {
                {"X", 0}, {"Y", 1}, {"LEFT", 2}, {"RIGHT", 3},
                {"MIDDLE", 4}, {"SIDE1", 5}, {"SIDE2", 6}
            };

            auto it = lockBitMap.find(target);
            if (it != lockBitMap.end()) {
                uint16_t cache = lockStateCache.load();
                if (locked) {
                    cache |= (1 << it->second);
                }
                else {
                    cache &= ~(1 << it->second);
                }
                lockStateCache.store(cache);
                lockStateCacheValid.store(true);
            }
        }

        bool getLockStateFromCache(const std::string& target) const {
            static const std::unordered_map<std::string, int> lockBitMap = {
                {"X", 0}, {"Y", 1}, {"LEFT", 2}, {"RIGHT", 3},
                {"MIDDLE", 4}, {"SIDE1", 5}, {"SIDE2", 6}
            };

            if (!lockStateCacheValid.load()) {
                return false;
            }

            auto it = lockBitMap.find(target);
            if (it != lockBitMap.end()) {
                return (lockStateCache.load() & (1 << it->second)) != 0;
            }
            return false;
        }
    };


    Device::Device() : m_impl(std::make_unique<Impl>()) {}

    Device::~Device() {
        disconnect();
    }

    std::vector<DeviceInfo> Device::findDevices() {
        std::vector<DeviceInfo> devices;
        auto ports = SerialPort::findMakcuPorts();

        for (const auto& port : ports) {
            DeviceInfo info;
            info.port = port;
            info.description = TARGET_DESC;
            info.vid = MAKCU_VID;
            info.pid = MAKCU_PID;
            info.isConnected = false;
            devices.push_back(info);
        }

        return devices;
    }

    std::string Device::findFirstDevice() {
        auto devices = findDevices();
        return devices.empty() ? "" : devices[0].port;
    }

    bool Device::connect(const std::string& port) {
        std::unique_lock<std::mutex> lock(m_impl->mutex);

        if (m_impl->connected.load() && m_impl->serialPort->isOpen()) {
            return true;
        }
        if (m_impl->connected.exchange(false)) {
            m_impl->currentButtonMask.store(0);
            m_impl->monitoring.store(false);
            m_impl->protocol.reset();
            m_impl->serialPort->close();
        }

        std::string targetPort = port.empty() ? findFirstDevice() : port;
        if (targetPort.empty()) {
            m_impl->markConnectionError("No candidate MAKCU serial port was found.");
            return false;
        }

        m_impl->markConnectionStatus(ConnectionStatus::CONNECTING);


        const ConnectionAttempt attempt = m_impl->connectionManager.Connect(*m_impl->serialPort, targetPort);
        if (!attempt.connected || !attempt.firmware.compatible) {
            m_impl->markConnectionError(attempt.error);
            return false;
        }

        m_impl->firmware = attempt.firmware;
        m_impl->capabilities = MakcuCapabilityRegistry::Resolve(m_impl->firmware);
        m_impl->protocol = MakcuProtocolRegistry::Create(m_impl->firmware);
        if (!m_impl->protocol) {
            m_impl->serialPort->close();
            m_impl->markConnectionError("No protocol adapter accepted the detected firmware.");
            return false;
        }


        m_impl->deviceInfo.port = targetPort;
        m_impl->deviceInfo.description = TARGET_DESC;
        m_impl->deviceInfo.vid = MAKCU_VID;
        m_impl->deviceInfo.pid = MAKCU_PID;
        m_impl->deviceInfo.isConnected = true;

        m_impl->connected.store(true);
        m_impl->markConnectionStatus(ConnectionStatus::CONNECTED);
        m_impl->clearConnectionError();
        m_impl->reconnectAttempts.store(0);
        m_impl->lastReconnectAttempt = {};
        for (uint8_t button = 0; button < 5; ++button) {
            if (const auto release = m_impl->protocol->Button(button, false))
                (void)m_impl->executeWire(*release);
        }
        if (const auto monitoring = m_impl->protocol->ButtonMonitoring(true))
            m_impl->executeWire(*monitoring);
        lock.unlock();
        m_impl->notifyConnectionChange(true);

        return true;
    }

    std::future<bool> Device::connectAsync(const std::string& port) {
        return std::async(std::launch::async, [this, port]() {
            return connect(port);
            });
    }

    void Device::disconnect() {
        std::unique_lock<std::mutex> lock(m_impl->mutex);

        if (m_impl->connected.load() && m_impl->protocol && m_impl->serialPort->isOpen()) {
            for (uint8_t button = 0; button < 5; ++button) {
                if (const auto release = m_impl->protocol->Button(button, false))
                    (void)m_impl->executeWire(*release);
            }
            if (const auto monitoring = m_impl->protocol->ButtonMonitoring(false))
                (void)m_impl->executeWire(*monitoring);
        }
        const bool wasConnected = m_impl->connected.exchange(false);
        if (m_impl->serialPort->isOpen()) m_impl->serialPort->close();
        m_impl->markConnectionStatus(ConnectionStatus::DISCONNECTED);
        m_impl->deviceInfo.isConnected = false;
        m_impl->currentButtonMask.store(0);
        m_impl->lockStateCacheValid.store(false);
        m_impl->protocol.reset();
        m_impl->firmware = {};
        m_impl->capabilities = {};
        lock.unlock();
        if (wasConnected) m_impl->notifyConnectionChange(false);
    }

    std::future<void> Device::disconnectAsync() {
        return std::async(std::launch::async, [this]() {
            disconnect();
            });
    }

    bool Device::isConnected() const {
        return m_impl->connected.load() && m_impl->serialPort->isOpen();
    }

    ConnectionStatus Device::getStatus() const {




        const ConnectionStatus status = m_impl->status.load(std::memory_order_acquire);
        if (status == ConnectionStatus::CONNECTED && !m_impl->serialPort->isOpen())
            return ConnectionStatus::CONNECTION_ERROR;
        return status;
    }

    DeviceInfo Device::getDeviceInfo() const {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        DeviceInfo info = m_impl->deviceInfo;
        info.isConnected = m_impl->connected.load() && m_impl->serialPort->isOpen();
        return info;
    }

    std::string Device::getVersion() const {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        return m_impl->connected.load() ? FirmwareVersionText(m_impl->firmware) : std::string{};
    }

    FirmwareDescriptor Device::getFirmwareDescriptor() const {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        return m_impl->firmware;
    }

    CapabilityState Device::getCapability(Capability capability) const {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        return m_impl->capabilities.Get(capability);
    }

    std::string Device::getLastError() const {


        std::lock_guard<std::mutex> lock(m_impl->statusMutex);
        return m_impl->lastError;
    }

    std::future<std::string> Device::getVersionAsync() const {
        return std::async(std::launch::async, [this]() {
            return getVersion();
            });
    }


    bool Device::mouseDown(MouseButton button) {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        if (!m_impl->connected.load() || !m_impl->protocol) return false;
        const auto command = m_impl->protocol->Button(static_cast<uint8_t>(button), true);
        return command && m_impl->executeWire(*command);
    }

    bool Device::mouseUp(MouseButton button) {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        if (!m_impl->connected.load() || !m_impl->protocol) return false;
        const auto command = m_impl->protocol->Button(static_cast<uint8_t>(button), false);
        return command && m_impl->executeWire(*command);
    }

    bool Device::click(MouseButton button) {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        if (!m_impl->connected.load() || !m_impl->protocol) return false;
        const auto press = m_impl->protocol->Button(static_cast<uint8_t>(button), true);
        const auto release = m_impl->protocol->Button(static_cast<uint8_t>(button), false);
        if (!press || !release) return false;
        const bool pressed = m_impl->executeWire(*press);
        const bool released = m_impl->executeWire(*release);
        return pressed && released;
    }

    bool Device::forceReleaseAll() {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        if (!m_impl->protocol || !m_impl->serialPort->isOpen()) return false;
        bool any = false;
        for (uint8_t button = 0; button < 5; ++button) {
            const auto release = m_impl->protocol->Button(button, false);
            if (release && m_impl->executeWireUngated(*release))
                any = true;
        }
        m_impl->currentButtonMask.store(0);
        return any;
    }

    bool Device::tryReconnect() {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        if (m_impl->connected.load() && m_impl->serialPort->isOpen()) return true;
        if (!m_impl->protocol || !m_impl->serialPort->isOpen()) return false;
        if (m_impl->reconnectAttempts.load() >= Impl::kMaxReconnectAttempts) return false;
        const auto now = std::chrono::steady_clock::now();
        if (m_impl->lastReconnectAttempt.time_since_epoch().count() != 0 &&
            now - m_impl->lastReconnectAttempt < m_impl->reconnectCooldown)
            return false;
        m_impl->lastReconnectAttempt = now;
        m_impl->reconnectAttempts.fetch_add(1);
        const auto probe = m_impl->protocol->ButtonMonitoring(true);
        if (!probe || !m_impl->executeWireUngated(*probe)) return false;
        m_impl->connected.store(true);
        m_impl->markConnectionStatus(ConnectionStatus::CONNECTED);
        m_impl->clearConnectionError();
        m_impl->reconnectAttempts.store(0);
        m_impl->monitoring.store(true);
        for (uint8_t button = 0; button < 5; ++button) {
            const auto release = m_impl->protocol->Button(button, false);
            if (release) (void)m_impl->executeWireUngated(*release);
        }
        return true;
    }

    std::future<bool> Device::mouseDownAsync(MouseButton button) {
        return std::async(std::launch::async, [this, button]() {
            return mouseDown(button);
            });
    }

    std::future<bool> Device::mouseUpAsync(MouseButton button) {
        return std::async(std::launch::async, [this, button]() {
            return mouseUp(button);
            });
    }

    std::future<bool> Device::clickAsync(MouseButton button) {
        return std::async(std::launch::async, [this, button]() {
            return click(button);
            });
    }

    bool Device::mouseButtonState(MouseButton button) {
        if (!m_impl->connected.load()) {
            return false;
        }


        uint8_t mask = m_impl->currentButtonMask.load();
        return (mask & (1 << static_cast<uint8_t>(button))) != 0;
    }

    std::future<bool> Device::mouseButtonStateAsync(MouseButton button) {
        return std::async(std::launch::async, [this, button]() {
            return mouseButtonState(button);
            });
    }


    bool Device::mouseMove(int32_t x, int32_t y) {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        if (!m_impl->connected.load()) {
            return false;
        }

        return m_impl->executeMoveCommand(x, y);
    }

    bool Device::mouseMoveSmooth(int32_t x, int32_t y, uint32_t segments) {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        if (!m_impl->connected.load() || !m_impl->protocol) return false;
        const auto command = m_impl->protocol->MoveSmooth(x, y, segments);
        return command && m_impl->executeWire(*command);
    }

    bool Device::mouseMoveBezier(int32_t x, int32_t y, uint32_t segments,
        int32_t ctrl_x, int32_t ctrl_y) {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        if (!m_impl->connected.load() || !m_impl->protocol) return false;
        const auto command = m_impl->protocol->MoveBezier(x, y, segments, ctrl_x, ctrl_y);
        return command && m_impl->executeWire(*command);
    }

    std::future<bool> Device::mouseMoveAsync(int32_t x, int32_t y) {
        return std::async(std::launch::async, [this, x, y]() {
            return mouseMove(x, y);
            });
    }

    std::future<bool> Device::mouseMoveSmoothAsync(int32_t x, int32_t y, uint32_t segments) {
        return std::async(std::launch::async, [this, x, y, segments]() {
            return mouseMoveSmooth(x, y, segments);
            });
    }

    std::future<bool> Device::mouseMoveBezierAsync(int32_t x, int32_t y, uint32_t segments,
        int32_t ctrl_x, int32_t ctrl_y) {
        return std::async(std::launch::async, [this, x, y, segments, ctrl_x, ctrl_y]() {
            return mouseMoveBezier(x, y, segments, ctrl_x, ctrl_y);
            });
    }

    bool Device::mouseWheel(int32_t delta) {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        if (!m_impl->connected.load() || !m_impl->protocol) return false;
        const auto command = m_impl->protocol->Wheel(delta);
        return command && m_impl->executeWire(*command);
    }

    std::future<bool> Device::mouseWheelAsync(int32_t delta) {
        return std::async(std::launch::async, [this, delta]() {
            return mouseWheel(delta);
            });
    }


    bool Device::lockMouseX(bool lock) {
        std::lock_guard<std::mutex> guard(m_impl->mutex);
        if (!m_impl->connected.load() || !m_impl->protocol) return false;
        const auto command = m_impl->protocol->Lock("mx", lock);
        const bool result = command && m_impl->executeWire(*command);
        if (result) {
            m_impl->updateLockStateCache("X", lock);
        }
        return result;
    }

    bool Device::lockMouseY(bool lock) {
        std::lock_guard<std::mutex> guard(m_impl->mutex);
        if (!m_impl->connected.load() || !m_impl->protocol) return false;
        const auto command = m_impl->protocol->Lock("my", lock);
        const bool result = command && m_impl->executeWire(*command);
        if (result) {
            m_impl->updateLockStateCache("Y", lock);
        }
        return result;
    }

    bool Device::lockMouseLeft(bool lock) {
        std::lock_guard<std::mutex> guard(m_impl->mutex);
        if (!m_impl->connected.load() || !m_impl->protocol) return false;
        const auto command = m_impl->protocol->Lock("ml", lock);
        const bool result = command && m_impl->executeWire(*command);
        if (result) {
            m_impl->updateLockStateCache("LEFT", lock);
        }
        return result;
    }

    bool Device::lockMouseMiddle(bool lock) {
        std::lock_guard<std::mutex> guard(m_impl->mutex);
        if (!m_impl->connected.load() || !m_impl->protocol) return false;
        const auto command = m_impl->protocol->Lock("mm", lock);
        const bool result = command && m_impl->executeWire(*command);
        if (result) {
            m_impl->updateLockStateCache("MIDDLE", lock);
        }
        return result;
    }

    bool Device::lockMouseRight(bool lock) {
        std::lock_guard<std::mutex> guard(m_impl->mutex);
        if (!m_impl->connected.load() || !m_impl->protocol) return false;
        const auto command = m_impl->protocol->Lock("mr", lock);
        const bool result = command && m_impl->executeWire(*command);
        if (result) {
            m_impl->updateLockStateCache("RIGHT", lock);
        }
        return result;
    }

    bool Device::lockMouseSide1(bool lock) {
        std::lock_guard<std::mutex> guard(m_impl->mutex);
        if (!m_impl->connected.load() || !m_impl->protocol) return false;
        const auto command = m_impl->protocol->Lock("ms1", lock);
        const bool result = command && m_impl->executeWire(*command);
        if (result) {
            m_impl->updateLockStateCache("SIDE1", lock);
        }
        return result;
    }

    bool Device::lockMouseSide2(bool lock) {
        std::lock_guard<std::mutex> guard(m_impl->mutex);
        if (!m_impl->connected.load() || !m_impl->protocol) return false;
        const auto command = m_impl->protocol->Lock("ms2", lock);
        const bool result = command && m_impl->executeWire(*command);
        if (result) {
            m_impl->updateLockStateCache("SIDE2", lock);
        }
        return result;
    }


    bool Device::isMouseXLocked() const {
        return m_impl->getLockStateFromCache("X");
    }

    bool Device::isMouseYLocked() const {
        return m_impl->getLockStateFromCache("Y");
    }

    bool Device::isMouseLeftLocked() const {
        return m_impl->getLockStateFromCache("LEFT");
    }

    bool Device::isMouseMiddleLocked() const {
        return m_impl->getLockStateFromCache("MIDDLE");
    }

    bool Device::isMouseRightLocked() const {
        return m_impl->getLockStateFromCache("RIGHT");
    }

    bool Device::isMouseSide1Locked() const {
        return m_impl->getLockStateFromCache("SIDE1");
    }

    bool Device::isMouseSide2Locked() const {
        return m_impl->getLockStateFromCache("SIDE2");
    }

    std::unordered_map<std::string, bool> Device::getAllLockStates() const {
        return {
            {"X", isMouseXLocked()},
            {"Y", isMouseYLocked()},
            {"LEFT", isMouseLeftLocked()},
            {"RIGHT", isMouseRightLocked()},
            {"MIDDLE", isMouseMiddleLocked()},
            {"SIDE1", isMouseSide1Locked()},
            {"SIDE2", isMouseSide2Locked()}
        };
    }


    uint8_t Device::catchMouseLeft() {


        std::lock_guard<std::mutex> guard(m_impl->mutex);
        if (!m_impl->connected.load() || m_impl->firmware.family != FirmwareFamily::LegacyText) return 0;

        auto future = m_impl->serialPort->sendTrackedCommand("km.catch_ml()", true,
            std::chrono::milliseconds(50));
        try {
            std::string response = future.get();
            return static_cast<uint8_t>(std::stoi(response));
        }
        catch (const std::exception& ex) {
            std::cerr << "[MAKCU] catchMouseLeft failed: " << ex.what() << "\n";
            return 0;
        }
        catch (...) {
            std::cerr << "[MAKCU] catchMouseLeft failed with unknown exception\n";
            return 0;
        }
    }

    uint8_t Device::catchMouseMiddle() {


        std::lock_guard<std::mutex> guard(m_impl->mutex);
        if (!m_impl->connected.load() || m_impl->firmware.family != FirmwareFamily::LegacyText) return 0;

        auto future = m_impl->serialPort->sendTrackedCommand("km.catch_mm()", true,
            std::chrono::milliseconds(50));
        try {
            std::string response = future.get();
            return static_cast<uint8_t>(std::stoi(response));
        }
        catch (const std::exception& ex) {
            std::cerr << "[MAKCU] catchMouseMiddle failed: " << ex.what() << "\n";
            return 0;
        }
        catch (...) {
            std::cerr << "[MAKCU] catchMouseMiddle failed with unknown exception\n";
            return 0;
        }
    }

    uint8_t Device::catchMouseRight() {


        std::lock_guard<std::mutex> guard(m_impl->mutex);
        if (!m_impl->connected.load() || m_impl->firmware.family != FirmwareFamily::LegacyText) return 0;

        auto future = m_impl->serialPort->sendTrackedCommand("km.catch_mr()", true,
            std::chrono::milliseconds(50));
        try {
            std::string response = future.get();
            return static_cast<uint8_t>(std::stoi(response));
        }
        catch (const std::exception& ex) {
            std::cerr << "[MAKCU] catchMouseRight failed: " << ex.what() << "\n";
            return 0;
        }
        catch (...) {
            std::cerr << "[MAKCU] catchMouseRight failed with unknown exception\n";
            return 0;
        }
    }

    uint8_t Device::catchMouseSide1() {


        std::lock_guard<std::mutex> guard(m_impl->mutex);
        if (!m_impl->connected.load() || m_impl->firmware.family != FirmwareFamily::LegacyText) return 0;

        auto future = m_impl->serialPort->sendTrackedCommand("km.catch_ms1()", true,
            std::chrono::milliseconds(50));
        try {
            std::string response = future.get();
            return static_cast<uint8_t>(std::stoi(response));
        }
        catch (const std::exception& ex) {
            std::cerr << "[MAKCU] catchMouseSide1 failed: " << ex.what() << "\n";
            return 0;
        }
        catch (...) {
            std::cerr << "[MAKCU] catchMouseSide1 failed with unknown exception\n";
            return 0;
        }
    }

    uint8_t Device::catchMouseSide2() {


        std::lock_guard<std::mutex> guard(m_impl->mutex);
        if (!m_impl->connected.load() || m_impl->firmware.family != FirmwareFamily::LegacyText) return 0;

        auto future = m_impl->serialPort->sendTrackedCommand("km.catch_ms2()", true,
            std::chrono::milliseconds(50));
        try {
            std::string response = future.get();
            return static_cast<uint8_t>(std::stoi(response));
        }
        catch (const std::exception& ex) {
            std::cerr << "[MAKCU] catchMouseSide2 failed: " << ex.what() << "\n";
            return 0;
        }
        catch (...) {
            std::cerr << "[MAKCU] catchMouseSide2 failed with unknown exception\n";
            return 0;
        }
    }


    bool Device::enableButtonMonitoring(bool enable) {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        if (!m_impl->connected.load() || !m_impl->protocol) return false;
        const auto command = m_impl->protocol->ButtonMonitoring(enable);
        if (!command) return false;
        const bool ok = m_impl->executeWire(*command);
        if (ok) m_impl->monitoring.store(enable);
        return ok;
    }

    bool Device::isButtonMonitoringEnabled() const {
        return m_impl->monitoring.load();
    }

    uint8_t Device::getButtonMask() const {
        return m_impl->currentButtonMask.load();
    }


    std::string Device::getMouseSerial() {


        std::lock_guard<std::mutex> guard(m_impl->mutex);
        if (!m_impl->connected.load() ||
            m_impl->capabilities.Get(Capability::MouseSerial) != CapabilityState::Supported) return "";

        auto future = m_impl->serialPort->sendTrackedCommand("km.serial()", true,
            std::chrono::milliseconds(100));
        try {
            return future.get();
        }
        catch (...) {
            return "";
        }
    }

    bool Device::setMouseSerial(const std::string& serial) {

        std::lock_guard<std::mutex> guard(m_impl->mutex);
        if (!m_impl->connected.load() ||
            m_impl->capabilities.Get(Capability::MouseSerial) != CapabilityState::Supported) return false;



        for (char c : serial) {
            if (c == '\'' || c == '\\' || c == '(' || c == ')' || c == '\r' || c == '\n'
                || static_cast<unsigned char>(c) < 0x20)
                return false;
        }

        std::string command = "km.serial('" + serial + "')";
        return m_impl->executeCommand(command);
    }

    bool Device::resetMouseSerial() {

        std::lock_guard<std::mutex> guard(m_impl->mutex);
        if (!m_impl->connected.load() ||
            m_impl->capabilities.Get(Capability::MouseSerial) != CapabilityState::Supported) return false;
        return m_impl->executeCommand("km.serial(0)");
    }

    std::future<std::string> Device::getMouseSerialAsync() {
        return std::async(std::launch::async, [this]() {
            return getMouseSerial();
            });
    }

    std::future<bool> Device::setMouseSerialAsync(const std::string& serial) {
        return std::async(std::launch::async, [this, serial]() {
            return setMouseSerial(serial);
            });
    }

    bool Device::setBaudRate(uint32_t baudRate) {
        std::unique_lock<std::mutex> lock(m_impl->mutex);
        if (!m_impl->connected.load() ||
            m_impl->capabilities.Get(Capability::BaudQuery) != CapabilityState::Supported ||
            (baudRate != 115200 && baudRate != 4000000)) return false;
        const std::string port = m_impl->serialPort->getPortName();
        if (!m_impl->serialPort->sendCommand("km.baud(" + std::to_string(baudRate) + ")")) return false;
        (void)m_impl->serialPort->flush();
        lock.unlock();
        disconnect();
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        return connect(port);
    }

    void Device::setMouseButtonCallback(MouseButtonCallback callback) {
        std::lock_guard<std::mutex> lock(m_impl->callbackMutex);
        m_impl->mouseButtonCallback = callback;
    }

    void Device::setConnectionCallback(ConnectionCallback callback) {
        std::lock_guard<std::mutex> lock(m_impl->callbackMutex);
        m_impl->connectionCallback = callback;
    }


    bool Device::clickSequence(const std::vector<MouseButton>& buttons,
        std::chrono::milliseconds delay) {
        if (!m_impl->connected.load()) {
            return false;
        }

        for (const auto& button : buttons) {
            if (!click(button)) {
                return false;
            }
            if (delay.count() > 0) {
                std::this_thread::sleep_for(delay);
            }
        }
        return true;
    }

    bool Device::movePattern(const std::vector<std::pair<int32_t, int32_t>>& points,
        bool smooth, uint32_t segments) {
        if (!m_impl->connected.load()) {
            return false;
        }

        for (const auto& [x, y] : points) {
            if (smooth) {
                if (!mouseMoveSmooth(x, y, segments)) {
                    return false;
                }
            }
            else {
                if (!mouseMove(x, y)) {
                    return false;
                }
            }
        }
        return true;
    }

    void Device::enableHighPerformanceMode(bool enable) {
        m_impl->highPerformanceMode.store(enable);
    }

    bool Device::isHighPerformanceModeEnabled() const {
        return m_impl->highPerformanceMode.load();
    }


    Device::BatchCommandBuilder Device::createBatch() {
        return BatchCommandBuilder(this);
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::move(int32_t x, int32_t y) {
        if (m_device->m_impl->protocol) {
            if (auto command = m_device->m_impl->protocol->Move(x, y))
                m_commands.push_back(std::move(*command));
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::click(MouseButton button) {
        if (m_device->m_impl->protocol) {
            if (auto command = m_device->m_impl->protocol->Button(static_cast<uint8_t>(button), true))
                m_commands.push_back(std::move(*command));
            if (auto command = m_device->m_impl->protocol->Button(static_cast<uint8_t>(button), false))
                m_commands.push_back(std::move(*command));
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::press(MouseButton button) {
        if (m_device->m_impl->protocol)
            if (auto command = m_device->m_impl->protocol->Button(static_cast<uint8_t>(button), true))
                m_commands.push_back(std::move(*command));
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::release(MouseButton button) {
        if (m_device->m_impl->protocol)
            if (auto command = m_device->m_impl->protocol->Button(static_cast<uint8_t>(button), false))
                m_commands.push_back(std::move(*command));
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::scroll(int32_t delta) {
        if (m_device->m_impl->protocol)
            if (auto command = m_device->m_impl->protocol->Wheel(delta))
                m_commands.push_back(std::move(*command));
        return *this;
    }

    bool Device::BatchCommandBuilder::execute() {



        std::lock_guard<std::mutex> guard(m_device->m_impl->mutex);
        if (!m_device->m_impl->connected.load()) {
            return false;
        }

        for (const auto& command : m_commands) {
            if (!m_device->m_impl->executeWire(command)) {
                return false;
            }
        }
        return true;
    }


    bool Device::sendRawCommand(const std::string& command) const {


        std::lock_guard<std::mutex> guard(m_impl->mutex);
        if (!m_impl->connected.load()) {
            return false;
        }

        return m_impl->serialPort->sendCommand(command);
    }

    std::string Device::receiveRawResponse() const {


        return "";
    }

    std::future<std::string> Device::sendRawCommandAsync(const std::string& command) const {


        std::lock_guard<std::mutex> guard(m_impl->mutex);
        if (!m_impl->connected.load()) {
            std::promise<std::string> promise;
            promise.set_exception(std::make_exception_ptr(
                std::runtime_error("Not connected")));
            return promise.get_future();
        }

        return m_impl->serialPort->sendTrackedCommand(command, true,
            std::chrono::milliseconds(100));
    }


    std::string mouseButtonToString(MouseButton button) {
        switch (button) {
        case MouseButton::LEFT: return "LEFT";
        case MouseButton::RIGHT: return "RIGHT";
        case MouseButton::MIDDLE: return "MIDDLE";
        case MouseButton::SIDE1: return "SIDE1";
        case MouseButton::SIDE2: return "SIDE2";
        }
        return "UNKNOWN";
    }

    MouseButton stringToMouseButton(const std::string& buttonName) {
        std::string upper = buttonName;
        std::transform(upper.begin(), upper.end(), upper.begin(),
            [](unsigned char c) { return std::toupper(c); });

        if (upper == "LEFT") return MouseButton::LEFT;
        if (upper == "RIGHT") return MouseButton::RIGHT;
        if (upper == "MIDDLE") return MouseButton::MIDDLE;
        if (upper == "SIDE1") return MouseButton::SIDE1;
        if (upper == "SIDE2") return MouseButton::SIDE2;

        return MouseButton::LEFT;
    }

}
