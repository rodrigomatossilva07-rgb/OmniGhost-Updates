#pragma once
#include "ui/ui_models.h"
#include <string>
#include <cstdint>

namespace aim_type {

    extern Config config;

    void Initialize();
    void Shutdown();
    bool Connect(DeviceType type);
    void Disconnect(DeviceType type);
    bool Test(DeviceType type);

    // Hardware input only (no game memory writes)
    void Move(int x, int y);
    void LeftClick();
    void LeftClickRelease();
    std::uint8_t ButtonMask();
    bool IsDown(int vk);
    bool IsConnected();
    const char* StatusText();
    std::string LastError(DeviceType type);

} // namespace aim_type
