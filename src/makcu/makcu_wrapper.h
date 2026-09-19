#pragma once
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <cstdint>
#include "makcu.h"

namespace makcu_wrapper {

    // Global state
    extern std::unique_ptr<makcu::Device> device;
    extern std::atomic<bool> connected;

    // Unified input source for binds (GAME PC Makcu vs MENU PC local)
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

    // Diagnostic snapshot (thread-safe copies)
    struct InputDiagnostics {
        bool connected = false;
        bool monitoringRequested = false;
        bool monitoringConfirmed = false;
        bool deviceDetected = false;
        uint8_t buttonMask = 0;
        uint64_t lastPacketMs = 0;      // age of last valid mask packet
        uint64_t packetsReceived = 0;
        uint64_t parseErrors = 0;
        char port[32]{};
        char deviceInfo[64]{};          // km.device() result
        char faultInfo[64]{};           // km.fault() result
        char buttonsStatus[48]{};       // km.buttons() result
        char lastSource[24]{};          // "MAKCU" / "Windows local" / "none"
    };

    // When false, menu-PC mouse cannot activate aim binds (GAME PC only)
    extern std::atomic<bool> allowLocalInputFallback;

    // Initialize MAKCU device
    void MakcuInitialize(const std::string& port = "");
    void MakcuShutdown();

    // Mouse movement
    void move(int x, int y);

    // Mouse clicks
    void left_click();
    void left_click_release();

    // Unified button query — prefers recent Makcu packet, then optional local fallback
    ButtonState GetButtonState(int virtual_key);
    bool IsDown(int virtual_key);
    bool IsKeyJustPressed(int virtual_key);
    bool IsKeyJustReleased(int virtual_key);

    // Raw latched mask from serial button stream (bit0=LMB … bit4=X2).
    // Change-only firmwares: this stays at the last PRESS mask until a real
    // RELEASE packet arrives. Absence of packets does NOT mean button up.
    uint8_t GetButtonMask();

    // Packet-aware poll: separates "no new serial event" from "mask is 0".
    // hasPacket==false → keep previous hold state (change-only silence).
    // hasPacket==true  → buttons is authoritative (0 = explicit release).
    struct DevicePacket {
        bool hasPacket = false;
        uint8_t buttons = 0;
    };
    DevicePacket PollButtons();

    // Called when serial receives a valid mask packet (press OR release)
    void NotifyMaskFromDevice(uint8_t mask);
    // Re-send km.buttons(1,10) only if stream went silent
    void EnsureButtonMonitoring();
    // Force mask=0 and ignore polled non-zero until a real serial packet.
    // Call on map change / match enter to kill stuck LMB after lost release packets.
    void ForceClearButtons();

    // Diagnostics
    InputDiagnostics GetDiagnostics();
    const char* DiagnosticsLine(); // short one-liner for aim status UI

    // Helper functions for mouse button management
    std::vector<std::string> GetAvailableMouseButtons();
    int GetMouseButtonKeyCode(const std::string& button_name);

    // Connection status
    bool IsConnected();
    std::string GetConnectionStatus();

    // Serial button packets seen since connect (0 = stream not confirmed yet)
    uint64_t PacketsReceived();
}
