#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace makcu {

enum class FirmwareFamily : uint8_t {
    Unknown,
    LegacyText,
    LegacyV2Binary,
    V4
};

enum class CapabilityState : uint8_t {
    Unsupported,
    Supported,
    Unknown
};

enum class Capability : uint8_t {
    RelativeMove,
    MouseButtons,
    Wheel,
    ButtonMonitoring,
    MouseLocks,
    SmoothMove,
    BezierMove,
    MouseSerial,
    BaudQuery,
    DeviceKinds,
    Count
};

struct FirmwareDescriptor {
    FirmwareFamily family = FirmwareFamily::Unknown;
    uint32_t numericVersion = 0;
    uint32_t baudRate = 0;
    uint8_t deviceKinds = 0;
    bool numericVersionKnown = false;
    bool deviceKindsKnown = false;
    bool compatible = false;
    bool commandEchoObserved = false;
    std::string identity;
    std::string rawResponse;
};

class CapabilitySet final {
public:
    CapabilitySet();
    [[nodiscard]] CapabilityState Get(Capability capability) const noexcept;
    void Set(Capability capability, CapabilityState state) noexcept;

private:
    std::array<CapabilityState, static_cast<size_t>(Capability::Count)> states_{};
};

enum class WireEncoding : uint8_t {
    Text,
    Binary
};

struct WireCommand {
    WireEncoding encoding = WireEncoding::Text;
    std::vector<uint8_t> bytes;
    bool expectsReply = false;
};

class IMakcuProtocol {
public:
    virtual ~IMakcuProtocol() = default;
    [[nodiscard]] virtual FirmwareFamily Family() const noexcept = 0;
    [[nodiscard]] virtual const CapabilitySet& Capabilities() const noexcept = 0;
    [[nodiscard]] virtual std::optional<WireCommand> Move(int32_t x, int32_t y) const = 0;
    [[nodiscard]] virtual std::optional<WireCommand> MoveSmooth(int32_t x, int32_t y,
                                                                uint32_t segments) const = 0;
    [[nodiscard]] virtual std::optional<WireCommand> MoveBezier(int32_t x, int32_t y,
                                                                uint32_t segments,
                                                                int32_t controlX,
                                                                int32_t controlY) const = 0;
    [[nodiscard]] virtual std::optional<WireCommand> Button(uint8_t button, bool down) const = 0;
    [[nodiscard]] virtual std::optional<WireCommand> Wheel(int32_t delta) const = 0;
    [[nodiscard]] virtual std::optional<WireCommand> ButtonMonitoring(bool enable) const = 0;
    [[nodiscard]] virtual std::optional<WireCommand> Lock(std::string_view target, bool enabled) const = 0;
};

class MakcuFirmwareDetector final {
public:
    [[nodiscard]] static FirmwareDescriptor ParseTextHandshake(std::string_view response,
                                                               uint32_t baudRate);
    [[nodiscard]] static std::optional<uint32_t> ParseV4FirmwareVersion(std::span<const uint8_t> response);
    [[nodiscard]] static std::optional<uint8_t> ParseV4DeviceKinds(std::span<const uint8_t> response);
    [[nodiscard]] static std::optional<std::string> ParseLegacyV2Version(std::span<const uint8_t> response);
    [[nodiscard]] static std::vector<uint8_t> V4Query(uint8_t command);
    [[nodiscard]] static std::vector<uint8_t> LegacyV2Query(uint8_t command);
};

class MakcuCapabilityRegistry final {
public:
    [[nodiscard]] static CapabilitySet Resolve(const FirmwareDescriptor& descriptor);
};

class MakcuProtocolRegistry final {
public:
    [[nodiscard]] static std::unique_ptr<IMakcuProtocol> Create(const FirmwareDescriptor& descriptor);
};

[[nodiscard]] const char* FirmwareFamilyName(FirmwareFamily family) noexcept;
[[nodiscard]] const char* CapabilityStateName(CapabilityState state) noexcept;
[[nodiscard]] std::string FirmwareVersionText(const FirmwareDescriptor& descriptor);

}
