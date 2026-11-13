#pragma once

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace makcu {

enum class TransportReaderMode : uint8_t {
    Text,
    LegacyV2Binary
};

class IMakcuTransport {
public:
    virtual ~IMakcuTransport() = default;
    virtual bool open(const std::string& port, uint32_t baudRate, bool startReader = true) = 0;
    virtual void close() = 0;
    [[nodiscard]] virtual bool isOpen() const = 0;
    virtual bool startListener() = 0;
    virtual void setReaderMode(TransportReaderMode mode) noexcept = 0;
    virtual bool writeRaw(std::span<const uint8_t> data) = 0;
    [[nodiscard]] virtual std::vector<uint8_t> readFor(std::chrono::milliseconds timeout,
                                                       std::chrono::milliseconds quietPeriod) = 0;
    virtual bool purge() = 0;
    [[nodiscard]] virtual uint32_t getBaudRate() const = 0;
    [[nodiscard]] virtual std::string getPortName() const = 0;
};

}
