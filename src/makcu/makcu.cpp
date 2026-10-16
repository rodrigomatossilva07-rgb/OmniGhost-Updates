#include "makcu.h"
#include "serialport.h"
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
    constexpr uint32_t INITIAL_BAUD_RATE = 115200;
    constexpr uint32_t HIGH_SPEED_BAUD_RATE = 4000000;


    const std::vector<uint8_t> BAUD_CHANGE_COMMAND = {
        0xDE, 0xAD, 0x05, 0x00, 0xA5, 0x00, 0x09, 0x3D, 0x00
    };


    std::atomic<bool> PerformanceProfiler::s_enabled{ false };
    std::mutex PerformanceProfiler::s_mutex;
    std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> PerformanceProfiler::s_stats;


    struct CommandCache {

        std::unordered_map<MouseButton, std::string> press_commands;
        std::unordered_map<MouseButton, std::string> release_commands;
        std::unordered_map<std::string, std::string> lock_commands;
        std::unordered_map<std::string, std::string> unlock_commands;
        std::unordered_map<std::string, std::string> query_commands;

        CommandCache() {

            press_commands[MouseButton::LEFT] = "km.left(1)";
            press_commands[MouseButton::RIGHT] = "km.right(1)";
            press_commands[MouseButton::MIDDLE] = "km.middle(1)";
            press_commands[MouseButton::SIDE1] = "km.ms1(1)";
            press_commands[MouseButton::SIDE2] = "km.ms2(1)";

            release_commands[MouseButton::LEFT] = "km.left(0)";
            release_commands[MouseButton::RIGHT] = "km.right(0)";
            release_commands[MouseButton::MIDDLE] = "km.middle(0)";
            release_commands[MouseButton::SIDE1] = "km.ms1(0)";
            release_commands[MouseButton::SIDE2] = "km.ms2(0)";


            lock_commands["X"] = "km.lock_mx(1)";
            lock_commands["Y"] = "km.lock_my(1)";
            lock_commands["LEFT"] = "km.lock_ml(1)";
            lock_commands["RIGHT"] = "km.lock_mr(1)";
            lock_commands["MIDDLE"] = "km.lock_mm(1)";
            lock_commands["SIDE1"] = "km.lock_ms1(1)";
            lock_commands["SIDE2"] = "km.lock_ms2(1)";

            unlock_commands["X"] = "km.lock_mx(0)";
            unlock_commands["Y"] = "km.lock_my(0)";
            unlock_commands["LEFT"] = "km.lock_ml(0)";
            unlock_commands["RIGHT"] = "km.lock_mr(0)";
            unlock_commands["MIDDLE"] = "km.lock_mm(0)";
            unlock_commands["SIDE1"] = "km.lock_ms1(0)";
            unlock_commands["SIDE2"] = "km.lock_ms2(0)";

            query_commands["X"] = "km.lock_mx()";
            query_commands["Y"] = "km.lock_my()";
            query_commands["LEFT"] = "km.lock_ml()";
            query_commands["RIGHT"] = "km.lock_mr()";
            query_commands["MIDDLE"] = "km.lock_mm()";
            query_commands["SIDE1"] = "km.lock_ms1()";
            query_commands["SIDE2"] = "km.lock_ms2()";
        }
    };


    class Device::Impl {
    public:
        std::unique_ptr<SerialPort> serialPort;
        DeviceInfo deviceInfo;
        ConnectionStatus status;
        std::atomic<bool> connected;
        std::atomic<bool> monitoring;
        std::atomic<bool> highPerformanceMode;
        mutable std::mutex mutex;


        CommandCache commandCache;


        std::atomic<uint16_t> lockStateCache{ 0 };
        std::atomic<bool> lockStateCacheValid{ false };


        std::atomic<uint8_t> currentButtonMask{ 0 };


        Device::MouseButtonCallback mouseButtonCallback;
        Device::ConnectionCallback connectionCallback;


        mutable std::string moveCommandBuffer;
        mutable std::mutex moveBufferMutex;

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

        bool switchToHighSpeedMode() {
            if (!serialPort->isOpen()) {
                return false;
            }


            if (!serialPort->write(BAUD_CHANGE_COMMAND)) {
                return false;
            }

            if (!serialPort->flush()) {
                return false;
            }


            std::string portName = serialPort->getPortName();
            serialPort->close();

            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            if (!serialPort->open(portName, HIGH_SPEED_BAUD_RATE)) {
                return false;
            }

            return true;
        }

        bool initializeDevice() {
            if (!serialPort->isOpen()) {
                return false;
            }


            std::this_thread::sleep_for(std::chrono::milliseconds(100));


            return serialPort->sendCommand("km.buttons(1,10)");
        }

        void handleButtonEvent(uint8_t button, bool pressed) {

            (void)pressed;
            if (mouseButtonCallback && button < 5) {
                MouseButton mouseBtn = static_cast<MouseButton>(button);
                try {
                    mouseButtonCallback(mouseBtn, pressed);
                }
                catch (...) {
                    std::cerr << "[MAKCU] Exceção no callback de botão; evento ignorado." << std::endl;
                }
            }
        }

        void notifyConnectionChange(bool isConnected) {
            if (connectionCallback) {
                try {
                    connectionCallback(isConnected);
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

            bool result;
            if (highPerformanceMode.load()) {

                result = serialPort->sendCommand(command);
            }
            else {

                result = serialPort->sendCommand(command);
            }


            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            makcu::PerformanceProfiler::logCommandTiming(command, duration);

            return result;
        }


        bool executeMoveCommand(int32_t x, int32_t y) {
            std::lock_guard<std::mutex> lock(moveBufferMutex);
            moveCommandBuffer.clear();
            moveCommandBuffer.reserve(32);

            moveCommandBuffer = "km.move(";
            moveCommandBuffer += std::to_string(x);
            moveCommandBuffer += ",";
            moveCommandBuffer += std::to_string(y);
            moveCommandBuffer += ")";

            return executeCommand(moveCommandBuffer);
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
        std::lock_guard<std::mutex> lock(m_impl->mutex);

        if (m_impl->connected.load()) {
            return true;
        }

        std::string targetPort = port.empty() ? findFirstDevice() : port;
        if (targetPort.empty()) {
            m_impl->status = ConnectionStatus::CONNECTION_ERROR;
            return false;
        }

        m_impl->status = ConnectionStatus::CONNECTING;


        if (!m_impl->serialPort->open(targetPort, INITIAL_BAUD_RATE)) {
            m_impl->status = ConnectionStatus::CONNECTION_ERROR;
            return false;
        }


        if (!m_impl->switchToHighSpeedMode()) {
            m_impl->serialPort->close();
            m_impl->status = ConnectionStatus::CONNECTION_ERROR;
            return false;
        }


        if (!m_impl->initializeDevice()) {
            m_impl->serialPort->close();
            m_impl->status = ConnectionStatus::CONNECTION_ERROR;
            return false;
        }


        m_impl->deviceInfo.port = targetPort;
        m_impl->deviceInfo.description = TARGET_DESC;
        m_impl->deviceInfo.vid = MAKCU_VID;
        m_impl->deviceInfo.pid = MAKCU_PID;
        m_impl->deviceInfo.isConnected = true;

        m_impl->connected.store(true);
        m_impl->status = ConnectionStatus::CONNECTED;
        m_impl->notifyConnectionChange(true);

        return true;
    }

    std::future<bool> Device::connectAsync(const std::string& port) {
        return std::async(std::launch::async, [this, port]() {
            return connect(port);
            });
    }

    void Device::disconnect() {
        std::lock_guard<std::mutex> lock(m_impl->mutex);

        if (!m_impl->connected.load()) {
            return;
        }

        m_impl->serialPort->close();
        m_impl->connected.store(false);
        m_impl->status = ConnectionStatus::DISCONNECTED;
        m_impl->deviceInfo.isConnected = false;
        m_impl->currentButtonMask.store(0);
        m_impl->lockStateCacheValid.store(false);
        m_impl->notifyConnectionChange(false);
    }

    std::future<void> Device::disconnectAsync() {
        return std::async(std::launch::async, [this]() {
            disconnect();
            });
    }

    bool Device::isConnected() const {
        return m_impl->connected.load();
    }

    ConnectionStatus Device::getStatus() const {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        return m_impl->status;
    }

    DeviceInfo Device::getDeviceInfo() const {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        return m_impl->deviceInfo;
    }

    std::string Device::getVersion() const {
        if (!m_impl->connected.load()) {
            return "";
        }

        auto future = m_impl->serialPort->sendTrackedCommand("km.version()", true,
            std::chrono::milliseconds(100));
        try {
            return future.get();
        }
        catch (const std::exception& ex) {
            std::cerr << "[MAKCU] getVersion failed: " << ex.what() << "\n";
            return "";
        }
        catch (...) {
            std::cerr << "[MAKCU] getVersion failed with unknown exception\n";
            return "";
        }
    }

    std::future<std::string> Device::getVersionAsync() const {
        return std::async(std::launch::async, [this]() {
            return getVersion();
            });
    }


    bool Device::mouseDown(MouseButton button) {
        if (!m_impl->connected.load()) {
            return false;
        }

        auto it = m_impl->commandCache.press_commands.find(button);
        if (it != m_impl->commandCache.press_commands.end()) {
            return m_impl->executeCommand(it->second);
        }
        return false;
    }

    bool Device::mouseUp(MouseButton button) {
        if (!m_impl->connected.load()) {
            return false;
        }

        auto it = m_impl->commandCache.release_commands.find(button);
        if (it != m_impl->commandCache.release_commands.end()) {
            return m_impl->executeCommand(it->second);
        }
        return false;
    }

    bool Device::click(MouseButton button) {
        if (!m_impl->connected.load()) {
            return false;
        }


        auto pressIt = m_impl->commandCache.press_commands.find(button);
        auto releaseIt = m_impl->commandCache.release_commands.find(button);

        if (pressIt != m_impl->commandCache.press_commands.end() &&
            releaseIt != m_impl->commandCache.release_commands.end()) {

            bool result1 = m_impl->executeCommand(pressIt->second);
            bool result2 = m_impl->executeCommand(releaseIt->second);
            return result1 && result2;
        }
        return false;
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
        if (!m_impl->connected.load()) {
            return false;
        }

        return m_impl->executeMoveCommand(x, y);
    }

    bool Device::mouseMoveSmooth(int32_t x, int32_t y, uint32_t segments) {
        if (!m_impl->connected.load()) {
            return false;
        }

        std::string command = "km.move(" + std::to_string(x) + "," +
            std::to_string(y) + "," + std::to_string(segments) + ")";
        return m_impl->executeCommand(command);
    }

    bool Device::mouseMoveBezier(int32_t x, int32_t y, uint32_t segments,
        int32_t ctrl_x, int32_t ctrl_y) {
        if (!m_impl->connected.load()) {
            return false;
        }

        std::string command = "km.move(" + std::to_string(x) + "," + std::to_string(y) + "," +
            std::to_string(segments) + "," + std::to_string(ctrl_x) + "," +
            std::to_string(ctrl_y) + ")";
        return m_impl->executeCommand(command);
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
        if (!m_impl->connected.load()) {
            return false;
        }

        std::string command = "km.wheel(" + std::to_string(delta) + ")";
        return m_impl->executeCommand(command);
    }

    std::future<bool> Device::mouseWheelAsync(int32_t delta) {
        return std::async(std::launch::async, [this, delta]() {
            return mouseWheel(delta);
            });
    }


    bool Device::lockMouseX(bool lock) {
        if (!m_impl->connected.load()) return false;

        const std::string& command = lock ?
            m_impl->commandCache.lock_commands.at("X") :
            m_impl->commandCache.unlock_commands.at("X");

        bool result = m_impl->executeCommand(command);
        if (result) {
            m_impl->updateLockStateCache("X", lock);
        }
        return result;
    }

    bool Device::lockMouseY(bool lock) {
        if (!m_impl->connected.load()) return false;

        const std::string& command = lock ?
            m_impl->commandCache.lock_commands.at("Y") :
            m_impl->commandCache.unlock_commands.at("Y");

        bool result = m_impl->executeCommand(command);
        if (result) {
            m_impl->updateLockStateCache("Y", lock);
        }
        return result;
    }

    bool Device::lockMouseLeft(bool lock) {
        if (!m_impl->connected.load()) return false;

        const std::string& command = lock ?
            m_impl->commandCache.lock_commands.at("LEFT") :
            m_impl->commandCache.unlock_commands.at("LEFT");

        bool result = m_impl->executeCommand(command);
        if (result) {
            m_impl->updateLockStateCache("LEFT", lock);
        }
        return result;
    }

    bool Device::lockMouseMiddle(bool lock) {
        if (!m_impl->connected.load()) return false;

        const std::string& command = lock ?
            m_impl->commandCache.lock_commands.at("MIDDLE") :
            m_impl->commandCache.unlock_commands.at("MIDDLE");

        bool result = m_impl->executeCommand(command);
        if (result) {
            m_impl->updateLockStateCache("MIDDLE", lock);
        }
        return result;
    }

    bool Device::lockMouseRight(bool lock) {
        if (!m_impl->connected.load()) return false;

        const std::string& command = lock ?
            m_impl->commandCache.lock_commands.at("RIGHT") :
            m_impl->commandCache.unlock_commands.at("RIGHT");

        bool result = m_impl->executeCommand(command);
        if (result) {
            m_impl->updateLockStateCache("RIGHT", lock);
        }
        return result;
    }

    bool Device::lockMouseSide1(bool lock) {
        if (!m_impl->connected.load()) return false;

        const std::string& command = lock ?
            m_impl->commandCache.lock_commands.at("SIDE1") :
            m_impl->commandCache.unlock_commands.at("SIDE1");

        bool result = m_impl->executeCommand(command);
        if (result) {
            m_impl->updateLockStateCache("SIDE1", lock);
        }
        return result;
    }

    bool Device::lockMouseSide2(bool lock) {
        if (!m_impl->connected.load()) return false;

        const std::string& command = lock ?
            m_impl->commandCache.lock_commands.at("SIDE2") :
            m_impl->commandCache.unlock_commands.at("SIDE2");

        bool result = m_impl->executeCommand(command);
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
        if (!m_impl->connected.load()) return 0;

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
        if (!m_impl->connected.load()) return 0;

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
        if (!m_impl->connected.load()) return 0;

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
        if (!m_impl->connected.load()) return 0;

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
        if (!m_impl->connected.load()) return 0;

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
        if (!m_impl->connected.load()) {
            return false;
        }




        if (!enable) {
            const bool ok = m_impl->executeCommand("km.buttons(0,0)");
            m_impl->monitoring.store(false);
            (void)m_impl->executeCommand("km.buttons(0)");
            return ok;
        }

        bool ok = m_impl->executeCommand("km.buttons(1,10)");
        if (!ok)
            ok = m_impl->executeCommand("km.buttons(1)");
        else
            (void)m_impl->executeCommand("km.buttons(1)");

        return ok;
    }

    bool Device::isButtonMonitoringEnabled() const {
        return m_impl->monitoring.load();
    }

    uint8_t Device::getButtonMask() const {
        return m_impl->currentButtonMask.load();
    }


    std::string Device::getMouseSerial() {
        if (!m_impl->connected.load()) return "";

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
        if (!m_impl->connected.load()) return false;



        for (char c : serial) {
            if (c == '\'' || c == '\\' || c == '(' || c == ')' || c == '\r' || c == '\n'
                || static_cast<unsigned char>(c) < 0x20)
                return false;
        }

        std::string command = "km.serial('" + serial + "')";
        return m_impl->executeCommand(command);
    }

    bool Device::resetMouseSerial() {
        if (!m_impl->connected.load()) return false;
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
        if (!m_impl->connected.load()) {
            return false;
        }

        return m_impl->serialPort->setBaudRate(baudRate);
    }

    void Device::setMouseButtonCallback(MouseButtonCallback callback) {
        m_impl->mouseButtonCallback = callback;
    }

    void Device::setConnectionCallback(ConnectionCallback callback) {
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
        m_commands.push_back("km.move(" + std::to_string(x) + "," + std::to_string(y) + ")");
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::click(MouseButton button) {
        auto& cache = m_device->m_impl->commandCache;
        auto pressIt = cache.press_commands.find(button);
        auto releaseIt = cache.release_commands.find(button);

        if (pressIt != cache.press_commands.end() && releaseIt != cache.release_commands.end()) {
            m_commands.push_back(pressIt->second);
            m_commands.push_back(releaseIt->second);
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::press(MouseButton button) {
        auto& cache = m_device->m_impl->commandCache;
        auto it = cache.press_commands.find(button);
        if (it != cache.press_commands.end()) {
            m_commands.push_back(it->second);
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::release(MouseButton button) {
        auto& cache = m_device->m_impl->commandCache;
        auto it = cache.release_commands.find(button);
        if (it != cache.release_commands.end()) {
            m_commands.push_back(it->second);
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::scroll(int32_t delta) {
        m_commands.push_back("km.wheel(" + std::to_string(delta) + ")");
        return *this;
    }

    bool Device::BatchCommandBuilder::execute() {
        if (!m_device->m_impl->connected.load()) {
            return false;
        }

        for (const auto& command : m_commands) {
            if (!m_device->m_impl->executeCommand(command)) {
                return false;
            }
        }
        return true;
    }


    bool Device::sendRawCommand(const std::string& command) const {
        if (!m_impl->connected.load()) {
            return false;
        }

        return m_impl->serialPort->sendCommand(command);
    }

    std::string Device::receiveRawResponse() const {


        return "";
    }

    std::future<std::string> Device::sendRawCommandAsync(const std::string& command) const {
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
