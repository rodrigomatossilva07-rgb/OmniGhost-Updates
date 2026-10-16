#include "makcu_wrapper.h"
#include "../platform/session_log.h"
#include <Windows.h>
#include <iostream>
#include <map>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace makcu_wrapper {
    std::unique_ptr<makcu::Device> device;
    std::atomic<bool> connected(false);
    std::atomic<bool> allowLocalInputFallback(true);

    // MAK-003 Fix: previous_states is written/read from both the aim lane and
    // the UI/diagnostics lane — guard it with diag_mutex.
    static std::map<int, bool> previous_states;
    static std::mutex diag_mutex;

    struct InternalState {
        std::atomic<bool> monitoringRequested{ false };
        std::atomic<bool> monitoringConfirmed{ false };
        std::atomic<bool> deviceDetected{ false };
        std::atomic<uint8_t> buttonMask{ 0 };
        std::atomic<uint64_t> lastPacketTick{ 0 };
        std::atomic<uint64_t> lastChangeTick{ 0 };
        std::atomic<uint64_t> packetsReceived{ 0 };
        std::atomic<uint64_t> parseErrors{ 0 };

        std::atomic<uint64_t> serialGeneration{ 0 };

        std::atomic<uint64_t> ignorePollUntilPacket{ 0 };
        char port[32]{};
        char deviceInfo[64]{};
        char faultInfo[64]{};
        char buttonsStatus[48]{};
        std::atomic<int> lastSource{ 0 };
    };
    static InternalState g_state;

    static uint64_t s_pollCursor;

    static uint64_t NowMs() { return GetTickCount64(); }

    static void LogDeviceFailure(const char* operation) {
        OmniGhost::SessionLog::Write(OmniGhost::SessionLog::Severity::Warning,
            OmniGhost::SessionLog::Subsystem::Input,
            "MAKCU operation failed and was isolated.",
            {{"operation", operation ? operation : "unknown", false}});
    }

    void NotifyMaskFromDevice(uint8_t mask) {




        const uint8_t m = static_cast<uint8_t>(mask & 0x1F);
        const uint8_t prev = g_state.buttonMask.load();
        g_state.packetsReceived.fetch_add(1);
        g_state.buttonMask.store(m);
        g_state.lastPacketTick.store(NowMs());
        g_state.serialGeneration.fetch_add(1);
        if (m != prev)
            g_state.lastChangeTick.store(NowMs());
        g_state.monitoringConfirmed.store(true);
        g_state.lastSource.store(1);
        g_state.ignorePollUntilPacket.store(0);
    }
}



void MakcuNotifyMaskFromSerial(uint8_t m) {
    makcu_wrapper::NotifyMaskFromDevice(m);
}

namespace makcu_wrapper {


    void MakcuInitialize(const std::string& port) {
        connected = false;
        g_state.monitoringRequested = false;
        g_state.monitoringConfirmed = false;
        g_state.deviceDetected = false;
        g_state.buttonMask = 0;
        g_state.lastPacketTick = 0;
        g_state.lastChangeTick = 0;
        g_state.packetsReceived = 0;
        g_state.serialGeneration = 0;
        g_state.ignorePollUntilPacket = 0;
        s_pollCursor = 0;
        g_state.deviceInfo[0] = 0;
        g_state.faultInfo[0] = 0;
        g_state.buttonsStatus[0] = 0;

        if (!device)
            device = std::make_unique<makcu::Device>();

        if (device->isConnected())
            device->disconnect();

        std::vector<std::string> candidates;
        if (!port.empty()) {
            candidates.push_back(port);
        } else {
            for (const auto& found : makcu::Device::findDevices())
                candidates.push_back(found.port);
        }
        if (candidates.empty()) return;

        std::string target_port;
        for (const auto& candidate : candidates) {
            {
                std::lock_guard<std::mutex> lock(diag_mutex);
                std::snprintf(g_state.port, sizeof(g_state.port), "%s", candidate.c_str());
            }
            if (device->connect(candidate)) {
                target_port = candidate;
                break;
            }
        }
        if (target_port.empty()) return;

        connected = true;
        device->enableHighPerformanceMode(true);

        g_state.monitoringRequested = true;
        device->enableButtonMonitoring(true);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        device->enableButtonMonitoring(true);
        try {
            device->sendRawCommand("km.buttons(1,10)");
        } catch (const std::exception& ex) {
            std::string msg = "enable button stream: " + std::string(ex.what());
            LogDeviceFailure(msg.c_str());
        } catch (...) {
            LogDeviceFailure("enable button stream: unknown exception");
        }
    }

    void MakcuShutdown() {
        connected = false;
        if (device) {
            try { device->enableButtonMonitoring(false); } catch (const std::exception& ex) { std::string msg = "disable monitoring: " + std::string(ex.what()); LogDeviceFailure(msg.c_str()); } catch (...) { LogDeviceFailure("disable monitoring: unknown exception"); }
            try { device->disconnect(); } catch (const std::exception& ex) { std::string msg = "disconnect: " + std::string(ex.what()); LogDeviceFailure(msg.c_str()); } catch (...) { LogDeviceFailure("disconnect: unknown exception"); }




        }
        g_state.monitoringRequested = false;
        g_state.monitoringConfirmed = false;
        g_state.deviceDetected = false;
        g_state.buttonMask = 0;
        g_state.lastPacketTick = 0;
        g_state.lastChangeTick = NowMs();
        g_state.ignorePollUntilPacket = 0;
        previous_states.clear();
    }

    void move(int x, int y) {


        if (!connected || !device || !device->isConnected()) return;
        if (x == 0 && y == 0) return;
        if (x < -127) x = -127; if (x > 127) x = 127;
        if (y < -127) y = -127; if (y > 127) y = 127;
        device->mouseMove(x, y);
    }

    void left_click() {
        if (!connected || !device || !device->isConnected()) return;
        device->mouseDown(makcu::MouseButton::LEFT);
    }

    void left_click_release() {
        if (!connected || !device || !device->isConnected()) return;
        device->mouseUp(makcu::MouseButton::LEFT);
    }

    static constexpr int VkToBit(int virtual_key) {
        switch (virtual_key) {
        case 1: return 0;
        case 2: return 1;
        case 4: return 2;
        case 5: return 3;
        case 6: return 4;
        default: return -1;
        }
    }

    static_assert(VkToBit(VK_LBUTTON) == 0, "left mouse bit mapping changed");
    static_assert(VkToBit(VK_RBUTTON) == 1, "right mouse bit mapping changed");

    static bool LocalKeyDown(int virtual_key) {
        if (virtual_key <= 0) return false;

        if ((GetAsyncKeyState(virtual_key) & 0x8000) != 0) return true;
        if ((GetKeyState(virtual_key) & 0x8000) != 0) return true;
        return false;
    }

    ButtonState GetButtonState(int virtual_key) {
        ButtonState st{};
        st.makcuMask = g_state.buttonMask.load();

        const int bit = VkToBit(virtual_key);
        const bool isMouse = bit >= 0;
        const uint64_t pkts = g_state.packetsReceived.load();
        const bool streamLive = g_state.monitoringConfirmed.load() || pkts > 0;






        if (connected.load() && isMouse && streamLive) {
            bool bitDown = (st.makcuMask & (1u << bit)) != 0;
            st.down = bitDown;
            st.source = InputSource::Makcu;
            if (bitDown)
                g_state.lastSource.store(1);



            if (!bitDown && allowLocalInputFallback.load() && LocalKeyDown(virtual_key)) {
                st.down = true;
                st.source = InputSource::LocalWindows;
                g_state.lastSource.store(2);
            }
            return st;
        }




        if (connected.load() && isMouse) {
            if (allowLocalInputFallback.load() && LocalKeyDown(virtual_key)) {
                st.down = true;
                st.source = InputSource::LocalWindows;
                g_state.lastSource.store(2);
                return st;
            }

            st.down = false;
            st.source = InputSource::None;
            return st;
        }


        if (allowLocalInputFallback.load() && virtual_key > 0) {
            if (LocalKeyDown(virtual_key)) {
                st.down = true;
                st.source = InputSource::LocalWindows;
                g_state.lastSource.store(2);
                return st;
            }
        }

        st.down = false;
        st.source = InputSource::None;
        return st;
    }

    bool IsDown(int virtual_key) {
        return GetButtonState(virtual_key).down;
    }

    uint8_t GetButtonMask() {




        if (g_state.ignorePollUntilPacket.load() != 0)
            return 0;
        return g_state.buttonMask.load();
    }

    DevicePacket PollButtons() {
        DevicePacket pkt{};
        const uint64_t gen = g_state.serialGeneration.load();
        if (gen != s_pollCursor) {
            s_pollCursor = gen;
            pkt.hasPacket = true;
            pkt.buttons = g_state.buttonMask.load();
        } else {
            pkt.hasPacket = false;
            pkt.buttons = g_state.buttonMask.load();
        }
        return pkt;
    }

    void ForceClearButtons() {
        g_state.buttonMask.store(0);
        g_state.ignorePollUntilPacket.store(UINT64_MAX);
        g_state.lastChangeTick.store(NowMs());
        if (device && device->isConnected()) {
            try { device->enableButtonMonitoring(true); } catch (const std::exception& ex) { std::string msg = "resume monitoring: " + std::string(ex.what()); LogDeviceFailure(msg.c_str()); } catch (...) { LogDeviceFailure("resume monitoring: unknown exception"); }
            try { device->sendRawCommand("km.buttons(1,10)"); } catch (const std::exception& ex) { std::string msg = "resume button stream: " + std::string(ex.what()); LogDeviceFailure(msg.c_str()); } catch (...) { LogDeviceFailure("resume button stream: unknown exception"); }
        }
    }

    void EnsureButtonMonitoring() {
        if (!connected || !device || !device->isConnected())
            return;

        (void)GetButtonMask();

        const uint64_t last = g_state.lastPacketTick.load();
        const uint64_t age = last ? (NowMs() - last) : UINT64_MAX;
        const uint8_t mask = g_state.buttonMask.load();
        const uint64_t pkts = g_state.packetsReceived.load();


        const bool need =
            !g_state.monitoringConfirmed.load() ||
            pkts == 0 ||
            (last == 0 && g_state.monitoringRequested.load()) ||
            (last != 0 && age > 2500) ||
            (last != 0 && mask != 0 && age > 1800);

        if (!need) return;

        static auto lastSend = std::chrono::steady_clock::time_point{};
        static int cmdRotate = 0;
        const auto now = std::chrono::steady_clock::now();

        const int minIntervalMs = (pkts == 0) ? 150 :
            (g_state.monitoringConfirmed.load() ? 1200 : 350);
        if (lastSend.time_since_epoch().count() != 0 &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSend).count() < minIntervalMs)
            return;
        lastSend = now;

        g_state.monitoringRequested = true;

        static const char* kEnableCmds[] = {
            "km.buttons(1,10)",
            "km.buttons(1)",
            "km.buttons(1,5)",
            "km.buttons(1,20)",
            "km.buttons(1,0)",
        };
        const char* cmd = kEnableCmds[cmdRotate % 5];
        ++cmdRotate;
        try { device->enableButtonMonitoring(true); } catch (const std::exception& ex) { std::string msg = "diagnostic monitoring: " + std::string(ex.what()); LogDeviceFailure(msg.c_str()); } catch (...) { LogDeviceFailure("diagnostic monitoring: unknown exception"); }
        try { device->sendRawCommand(cmd); } catch (const std::exception& ex) { std::string msg = "diagnostic command: " + std::string(ex.what()); LogDeviceFailure(msg.c_str()); } catch (...) { LogDeviceFailure("diagnostic command: unknown exception"); }

        if (pkts == 0) {
            try { device->sendRawCommand("km.buttons(1,10)"); } catch (const std::exception& ex) { std::string msg = "diagnostic button stream: " + std::string(ex.what()); LogDeviceFailure(msg.c_str()); } catch (...) { LogDeviceFailure("diagnostic button stream: unknown exception"); }
        }
    }

    bool IsKeyJustPressed(int virtual_key) {
        bool current = IsDown(virtual_key);
        std::lock_guard<std::mutex> lock(diag_mutex);
        bool& prev = previous_states[virtual_key];
        bool edge = current && !prev;
        prev = current;
        return edge;
    }

    bool IsKeyJustReleased(int virtual_key) {
        bool current = IsDown(virtual_key);
        std::lock_guard<std::mutex> lock(diag_mutex);
        bool& prev = previous_states[virtual_key];
        bool edge = !current && prev;
        prev = current;
        return edge;
    }

    InputDiagnostics GetDiagnostics() {
        InputDiagnostics d{};
        d.connected = connected.load() && device && device->isConnected();
        d.monitoringRequested = g_state.monitoringRequested.load();
        d.monitoringConfirmed = g_state.monitoringConfirmed.load();
        d.deviceDetected = g_state.deviceDetected.load();
        d.buttonMask = g_state.buttonMask.load();
        const uint64_t last = g_state.lastPacketTick.load();
        d.lastPacketMs = last ? (NowMs() - last) : 0;
        d.packetsReceived = g_state.packetsReceived.load();
        d.parseErrors = g_state.parseErrors.load();
        std::lock_guard<std::mutex> lock(diag_mutex);
        std::memcpy(d.port, g_state.port, sizeof(d.port));
        std::memcpy(d.deviceInfo, g_state.deviceInfo, sizeof(d.deviceInfo));
        std::memcpy(d.faultInfo, g_state.faultInfo, sizeof(d.faultInfo));
        std::memcpy(d.buttonsStatus, g_state.buttonsStatus, sizeof(d.buttonsStatus));
        const int src = g_state.lastSource.load();
        if (src == 1) std::snprintf(d.lastSource, sizeof(d.lastSource), "MAKCU");
        else if (src == 2) std::snprintf(d.lastSource, sizeof(d.lastSource), "Windows local");
        else std::snprintf(d.lastSource, sizeof(d.lastSource), "nenhuma");
        return d;
    }

    const char* DiagnosticsLine() {
        static thread_local char buf[160];
        (void)GetButtonMask();
        auto d = GetDiagnostics();
        if (!d.connected) {
            std::snprintf(buf, sizeof(buf), "Makcu DESLIGADO");
            return buf;
        }
        if (!d.monitoringConfirmed) {
            std::snprintf(buf, sizeof(buf),
                "makcu=ligado fluxo=NÃO máscara=0x%02X (clica no rato dentro do JOGO)",
                d.buttonMask);
            return buf;
        }
        std::snprintf(buf, sizeof(buf),
            "makcu=ligado máscara=0x%02X origem=%s",
            d.buttonMask, d.lastSource);
        return buf;
    }

    std::vector<std::string> GetAvailableMouseButtons() {
        return { "Left Mouse", "Right Mouse", "Middle Mouse", "Mouse 4", "Mouse 5" };
    }

    int GetMouseButtonKeyCode(const std::string& button_name) {
        if (button_name == "Left Mouse") return 1;
        if (button_name == "Right Mouse") return 2;
        if (button_name == "Middle Mouse") return 4;
        if (button_name == "Mouse 4") return 5;
        if (button_name == "Mouse 5") return 6;
        return 1;
    }

    bool IsConnected() {
        return connected && device && device->isConnected();
    }

    uint64_t PacketsReceived() {
        return g_state.packetsReceived.load();
    }

    std::string GetConnectionStatus() {
        if (!device) return "Nenhuma instância do dispositivo";
        switch (device->getStatus()) {
        case makcu::ConnectionStatus::CONNECTED: return "Ligado";
        case makcu::ConnectionStatus::CONNECTING: return "A ligar...";
        case makcu::ConnectionStatus::DISCONNECTED: return "Desligado";
        case makcu::ConnectionStatus::CONNECTION_ERROR: return "Erro de ligação";
        default: return "Desconhecido";
        }
    }
}
