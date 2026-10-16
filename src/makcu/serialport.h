#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <future>
#include <unordered_map>
#include <thread>
#include <stop_token>
#include <queue>
#include <chrono>
#include <span>
#include "makcu_transport.h"

#ifdef _WIN32
#include <windows.h>
#include "../platform/unique_handle.h"
#endif

namespace makcu {

    struct PendingCommand {
        int command_id;
        std::string command;
        std::promise<std::string> promise;
        std::chrono::steady_clock::time_point timestamp;
        bool expect_response;
        std::chrono::milliseconds timeout;

        PendingCommand(int id, const std::string& cmd, bool expect_resp, std::chrono::milliseconds to)
            : command_id(id), command(cmd), expect_response(expect_resp), timeout(to) {
            timestamp = std::chrono::steady_clock::now();
        }
    };

    class SerialPort final : public IMakcuTransport {
    public:
        SerialPort();
        ~SerialPort() override;

        bool open(const std::string& port, uint32_t baudRate, bool startReader = true) override;
        void close() override;
        bool isOpen() const override;
        bool startListener() override;
        void setReaderMode(TransportReaderMode mode) noexcept override;
        bool writeRaw(std::span<const uint8_t> data) override;
        std::vector<uint8_t> readFor(std::chrono::milliseconds timeout,
                                     std::chrono::milliseconds quietPeriod) override;
        bool purge() override;

        bool setBaudRate(uint32_t baudRate);
        uint32_t getBaudRate() const override;
        std::string getPortName() const override;


        std::future<std::string> sendTrackedCommand(const std::string& command,
            bool expectResponse = false,
            std::chrono::milliseconds timeout = std::chrono::milliseconds(100));


        bool sendCommand(const std::string& command);


        bool write(const std::vector<uint8_t>& data);
        bool write(const std::string& data);
        std::vector<uint8_t> read(size_t maxBytes = 1024);
        std::string readString(size_t maxBytes = 1024);

        size_t available() const;
        bool flush();


        void setTimeout(uint32_t timeoutMs);
        uint32_t getTimeout() const;


        static std::vector<std::string> getAvailablePorts();
        static std::vector<std::string> findMakcuPorts();



        using ButtonCallback = std::function<void(uint8_t, bool)>;
        void setButtonCallback(ButtonCallback callback);

        using MaskCallback = std::function<void(uint8_t)>;
        void setMaskCallback(MaskCallback callback);

    private:
        std::string m_portName;
        uint32_t m_baudRate;
        uint32_t m_timeout;
        std::atomic<bool> m_isOpen;
        mutable std::mutex m_mutex;

#ifdef _WIN32
        OmniGhost::Platform::UniqueHandle m_handle;
        DCB m_dcb;
        COMMTIMEOUTS m_timeouts;
#else
        int m_fd;
#endif


        std::atomic<int> m_commandCounter{ 0 };
        std::unordered_map<int, std::unique_ptr<PendingCommand>> m_pendingCommands;
        std::mutex m_commandMutex;


        std::jthread m_listenerThread;
        std::atomic<bool> m_stopListener{ false };
        std::atomic<bool> m_listenerStarted{ false };
        std::atomic<TransportReaderMode> m_readerMode{ TransportReaderMode::Text };


        ButtonCallback m_buttonCallback;
        MaskCallback m_maskCallback;
        std::mutex m_callbackMutex;
        std::atomic<uint8_t> m_lastButtonMask{ 0 };
        std::atomic<uint64_t> m_buttonPackets{ 0 };




        size_t m_prefixMatch = 0;
        bool m_expectMaskByte = false;
        int m_prefixMode = 0;


        static constexpr size_t BUFFER_SIZE = 4096;
        static constexpr size_t LINE_BUFFER_SIZE = 256;

        bool configurePort();
        void updateTimeouts();
        void listenerLoop(std::stop_token stopToken);
        void processIncomingData();
        void handleButtonData(uint8_t data);
        void processResponse(const std::string& response);
        void cleanupTimedOutCommands();
        int generateCommandId();


        SerialPort(const SerialPort&) = delete;
        SerialPort& operator=(const SerialPort&) = delete;
    };

}
