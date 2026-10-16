#pragma once

#include "makcu_protocol.h"
#include "makcu_transport.h"

#include <array>
#include <chrono>
#include <string>

namespace makcu {

struct ConnectionPolicy {
    std::array<uint32_t, 2> baudRates{4000000, 115200};
    std::chrono::milliseconds handshakeTimeout{350};
    std::chrono::milliseconds responseQuietPeriod{25};
};

struct ConnectionAttempt {
    bool connected = false;
    FirmwareDescriptor firmware;
    std::string error;
};

class MakcuConnectionManager final {
public:
    explicit MakcuConnectionManager(ConnectionPolicy policy = {});
    [[nodiscard]] ConnectionAttempt Connect(IMakcuTransport& transport,
                                            const std::string& port) const;

private:
    [[nodiscard]] std::vector<uint8_t> Exchange(IMakcuTransport& transport,
                                                std::span<const uint8_t> request) const;
    ConnectionPolicy policy_;
};

}
