#pragma once

#include <cstdint>
#include <string>

namespace kmbox_net {

bool Connect(const std::string& ip, int port, const std::string& uuid);
void Disconnect();

bool IsConnected();
bool Move(int x, int y);
bool SetLeft(bool down);
std::uint8_t ButtonMask();

std::string LastError();

} // namespace kmbox_net
