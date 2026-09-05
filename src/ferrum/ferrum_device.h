#pragma once

#include <cstdint>
#include <string>

namespace ferrum_device {

bool Connect(const std::string& port, int baudRate = 115200);
void Disconnect();

bool IsConnected();
bool Move(int x, int y);
bool SetLeft(bool down);
std::uint8_t ButtonMask();

std::string ConnectedPort();
std::string DeviceVersion();
std::string LastError();

} // namespace ferrum_device
