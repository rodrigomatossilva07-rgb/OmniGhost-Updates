#pragma once
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <cstdint>
#include "makcu.h"

namespace makcu_wrapper {


    extern std::unique_ptr<makcu::Device> device;
    extern std::atomic<bool> connected;


    enum class InputSource : int {
        None = 0,
        Makcu = 1,
        LocalWindows = 2
    };

    struct ButtonState {
        bool down = false;
        InputSource source = InputSource::None;
        uint8_t makcuMask = 0;
    };


    struct InputDiagnostics {
        bool connected = false;
        bool monitoringRequested = false;
        bool monitoringConfirmed = false;
        bool deviceDetected = false;
        uint8_t buttonMask = 0;
        uint64_t lastPacketMs = 0;
        uint64_t packetsReceived = 0;
        uint64_t parseErrors = 0;
        char port[32]{};
        char deviceInfo[64]{};
        char faultInfo[64]{};
        char buttonsStatus[48]{};
        char lastSource[24]{};
    };


    extern std::atomic<bool> allowLocalInputFallback;


    void MakcuInitialize(const std::string& port = "");
    void MakcuShutdown();


    void move(int x, int y);


    void left_click();
    void left_click_release();


    ButtonState GetButtonState(int virtual_key);
    bool IsDown(int virtual_key);
    bool IsKeyJustPressed(int virtual_key);
    bool IsKeyJustReleased(int virtual_key);




    uint8_t GetButtonMask();




    struct DevicePacket {
        bool hasPacket = false;
        uint8_t buttons = 0;
    };
    DevicePacket PollButtons();


    void NotifyMaskFromDevice(uint8_t mask);

    void EnsureButtonMonitoring();


    void ForceClearButtons();


    InputDiagnostics GetDiagnostics();
    const char* DiagnosticsLine();


    std::vector<std::string> GetAvailableMouseButtons();
    int GetMouseButtonKeyCode(const std::string& button_name);


    bool IsConnected();
    std::string GetConnectionStatus();


    uint64_t PacketsReceived();
}
