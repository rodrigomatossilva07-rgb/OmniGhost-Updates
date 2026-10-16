#include "makcu_protocol.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <sstream>

namespace makcu {
namespace {

std::string Trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.remove_suffix(1);
    return std::string(value);
}

std::vector<std::string> Lines(std::string_view value) {
    std::vector<std::string> result;
    size_t offset = 0;
    while (offset <= value.size()) {
        const size_t end = value.find_first_of("\r\n", offset);
        const size_t count = end == std::string_view::npos ? value.size() - offset : end - offset;
        std::string line = Trim(value.substr(offset, count));
        if (!line.empty()) result.push_back(std::move(line));
        if (end == std::string_view::npos) break;
        offset = end + 1;
        while (offset < value.size() && (value[offset] == '\r' || value[offset] == '\n')) ++offset;
    }
    return result;
}

bool ParseLegacyLabel(std::string_view value, uint32_t& version) {
    if (value.size() < 2 || (value.front() != 'V' && value.front() != 'v')) return false;
    const char* begin = value.data() + 1;
    const char* end = value.data() + value.size();
    const auto parsed = std::from_chars(begin, end, version);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

void PutI16(std::vector<uint8_t>& target, int32_t value) {
    const int16_t narrowed = static_cast<int16_t>(value);
    target.push_back(static_cast<uint8_t>(narrowed & 0xFF));
    target.push_back(static_cast<uint8_t>((static_cast<uint16_t>(narrowed) >> 8) & 0xFF));
}

std::vector<uint8_t> Text(std::string value) {
    return std::vector<uint8_t>(value.begin(), value.end());
}

WireCommand TextCommand(std::string value) {
    return {WireEncoding::Text, Text(std::move(value)), false};
}

WireCommand V4Command(uint8_t command, std::vector<uint8_t> payload) {
    std::vector<uint8_t> bytes{0xDE, 0xAD,
        static_cast<uint8_t>(payload.size() & 0xFF),
        static_cast<uint8_t>((payload.size() >> 8) & 0xFF), command};
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return {WireEncoding::Binary, std::move(bytes), false};
}

WireCommand V2Command(uint8_t command, std::vector<uint8_t> payload) {
    std::vector<uint8_t> bytes{0x50, command,
        static_cast<uint8_t>(payload.size() & 0xFF),
        static_cast<uint8_t>((payload.size() >> 8) & 0xFF)};
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return {WireEncoding::Binary, std::move(bytes), true};
}

class TextProtocol final : public IMakcuProtocol {
public:
    TextProtocol(FirmwareFamily family, CapabilitySet capabilities)
        : family_(family), capabilities_(std::move(capabilities)) {}

    FirmwareFamily Family() const noexcept override { return family_; }
    const CapabilitySet& Capabilities() const noexcept override { return capabilities_; }

    std::optional<WireCommand> Move(int32_t x, int32_t y) const override {
        if (capabilities_.Get(Capability::RelativeMove) != CapabilityState::Supported) return std::nullopt;
        if (x < std::numeric_limits<int16_t>::min() || x > std::numeric_limits<int16_t>::max() ||
            y < std::numeric_limits<int16_t>::min() || y > std::numeric_limits<int16_t>::max()) return std::nullopt;
        return TextCommand("km.move(" + std::to_string(x) + "," + std::to_string(y) + ")");
    }

    std::optional<WireCommand> MoveSmooth(int32_t x, int32_t y, uint32_t segments) const override {
        if (capabilities_.Get(Capability::SmoothMove) != CapabilityState::Supported ||
            x < std::numeric_limits<int16_t>::min() || x > std::numeric_limits<int16_t>::max() ||
            y < std::numeric_limits<int16_t>::min() || y > std::numeric_limits<int16_t>::max() ||
            segments == 0 || segments > 512) return std::nullopt;
        return TextCommand("km.move(" + std::to_string(x) + "," + std::to_string(y) + "," +
                           std::to_string(segments) + ")");
    }

    std::optional<WireCommand> MoveBezier(int32_t x, int32_t y, uint32_t segments,
                                           int32_t controlX, int32_t controlY) const override {
        if (capabilities_.Get(Capability::BezierMove) != CapabilityState::Supported ||
            x < std::numeric_limits<int16_t>::min() || x > std::numeric_limits<int16_t>::max() ||
            y < std::numeric_limits<int16_t>::min() || y > std::numeric_limits<int16_t>::max() ||
            segments == 0 || segments > 512) return std::nullopt;
        return TextCommand("km.move(" + std::to_string(x) + "," + std::to_string(y) + "," +
                           std::to_string(segments) + "," + std::to_string(controlX) + "," +
                           std::to_string(controlY) + ")");
    }

    std::optional<WireCommand> Button(uint8_t button, bool down) const override {
        if (capabilities_.Get(Capability::MouseButtons) != CapabilityState::Supported || button > 4) return std::nullopt;
        static constexpr const char* names[] = {"left", "right", "middle", "ms1", "ms2"};
        return TextCommand(std::string("km.") + names[button] + "(" + (down ? "1)" : "0)"));
    }

    std::optional<WireCommand> Wheel(int32_t delta) const override {
        if (capabilities_.Get(Capability::Wheel) != CapabilityState::Supported ||
            delta < std::numeric_limits<int16_t>::min() || delta > std::numeric_limits<int16_t>::max()) return std::nullopt;
        return TextCommand("km.wheel(" + std::to_string(delta) + ")");
    }

    std::optional<WireCommand> ButtonMonitoring(bool enable) const override {
        if (capabilities_.Get(Capability::ButtonMonitoring) != CapabilityState::Supported) return std::nullopt;
        return TextCommand(enable ? "km.buttons(1)" : "km.buttons(0)");
    }

    std::optional<WireCommand> Lock(std::string_view target, bool enabled) const override {
        if (capabilities_.Get(Capability::MouseLocks) != CapabilityState::Supported) return std::nullopt;
        static constexpr std::array<std::string_view, 7> allowed{"mx", "my", "ml", "mr", "mm", "ms1", "ms2"};
        if (std::find(allowed.begin(), allowed.end(), target) == allowed.end()) return std::nullopt;
        return TextCommand("km.lock_" + std::string(target) + (enabled ? "(1)" : "(0)"));
    }

private:
    FirmwareFamily family_;
    CapabilitySet capabilities_;
};

class V4Protocol final : public IMakcuProtocol {
public:
    explicit V4Protocol(CapabilitySet capabilities) : capabilities_(std::move(capabilities)) {}
    FirmwareFamily Family() const noexcept override { return FirmwareFamily::V4; }
    const CapabilitySet& Capabilities() const noexcept override { return capabilities_; }

    std::optional<WireCommand> Move(int32_t x, int32_t y) const override {
        if (x < std::numeric_limits<int16_t>::min() || x > std::numeric_limits<int16_t>::max() ||
            y < std::numeric_limits<int16_t>::min() || y > std::numeric_limits<int16_t>::max()) return std::nullopt;
        std::vector<uint8_t> payload;
        PutI16(payload, x);
        PutI16(payload, y);
        return V4Command(0x18, std::move(payload));
    }

    std::optional<WireCommand> MoveSmooth(int32_t, int32_t, uint32_t) const override {
        return std::nullopt;
    }

    std::optional<WireCommand> MoveBezier(int32_t, int32_t, uint32_t, int32_t, int32_t) const override {
        return std::nullopt;
    }

    std::optional<WireCommand> Button(uint8_t button, bool down) const override {
        if (button > 4) return std::nullopt;
        return V4Command(static_cast<uint8_t>(0x11 + button), {static_cast<uint8_t>(down ? 1 : 0)});
    }

    std::optional<WireCommand> Wheel(int32_t delta) const override {
        if (delta < std::numeric_limits<int16_t>::min() || delta > std::numeric_limits<int16_t>::max()) return std::nullopt;
        std::vector<uint8_t> payload;
        PutI16(payload, delta);
        return V4Command(0x19, std::move(payload));
    }

    std::optional<WireCommand> ButtonMonitoring(bool enable) const override {
        return TextCommand(enable ? "km.buttons(1)" : "km.buttons(0)");
    }

    std::optional<WireCommand> Lock(std::string_view target, bool enabled) const override {
        return TextProtocol(FirmwareFamily::V4, capabilities_).Lock(target, enabled);
    }

private:
    CapabilitySet capabilities_;
};

class LegacyV2Protocol final : public IMakcuProtocol {
public:
    explicit LegacyV2Protocol(CapabilitySet capabilities) : capabilities_(std::move(capabilities)) {}
    FirmwareFamily Family() const noexcept override { return FirmwareFamily::LegacyV2Binary; }
    const CapabilitySet& Capabilities() const noexcept override { return capabilities_; }

    std::optional<WireCommand> Move(int32_t x, int32_t y) const override {
        if (x < std::numeric_limits<int16_t>::min() || x > std::numeric_limits<int16_t>::max() ||
            y < std::numeric_limits<int16_t>::min() || y > std::numeric_limits<int16_t>::max()) return std::nullopt;
        std::vector<uint8_t> payload;
        PutI16(payload, x);
        PutI16(payload, y);
        payload.insert(payload.end(), {1, 0, 0});
        return V2Command(0x0D, std::move(payload));
    }

    std::optional<WireCommand> MoveSmooth(int32_t x, int32_t y, uint32_t segments) const override {
        return MoveBezier(x, y, segments, 0, 0);
    }

    std::optional<WireCommand> MoveBezier(int32_t x, int32_t y, uint32_t segments,
                                           int32_t controlX, int32_t controlY) const override {
        if (x < std::numeric_limits<int16_t>::min() || x > std::numeric_limits<int16_t>::max() ||
            y < std::numeric_limits<int16_t>::min() || y > std::numeric_limits<int16_t>::max() ||
            segments == 0 || segments > 255 || controlX < -128 || controlX > 127 ||
            controlY < -128 || controlY > 127) return std::nullopt;
        std::vector<uint8_t> payload;
        PutI16(payload, x);
        PutI16(payload, y);
        payload.push_back(static_cast<uint8_t>(segments));
        payload.push_back(static_cast<uint8_t>(static_cast<int8_t>(controlX)));
        payload.push_back(static_cast<uint8_t>(static_cast<int8_t>(controlY)));
        return V2Command(0x0D, std::move(payload));
    }

    std::optional<WireCommand> Button(uint8_t button, bool down) const override {
        static constexpr uint8_t commands[] = {0x08, 0x11, 0x0A, 0x12, 0x13};
        if (button > 4) return std::nullopt;
        return V2Command(commands[button], {static_cast<uint8_t>(down ? 1 : 0)});
    }

    std::optional<WireCommand> Wheel(int32_t delta) const override {
        if (delta < -128 || delta > 127) return std::nullopt;
        return V2Command(0x18, {static_cast<uint8_t>(static_cast<int8_t>(delta))});
    }

    std::optional<WireCommand> ButtonMonitoring(bool) const override { return std::nullopt; }
    std::optional<WireCommand> Lock(std::string_view, bool) const override { return std::nullopt; }

private:
    CapabilitySet capabilities_;
};

}

CapabilitySet::CapabilitySet() {
    states_.fill(CapabilityState::Unknown);
}

CapabilityState CapabilitySet::Get(Capability capability) const noexcept {
    const size_t index = static_cast<size_t>(capability);
    return index < states_.size() ? states_[index] : CapabilityState::Unknown;
}

void CapabilitySet::Set(Capability capability, CapabilityState state) noexcept {
    const size_t index = static_cast<size_t>(capability);
    if (index < states_.size()) states_[index] = state;
}

FirmwareDescriptor MakcuFirmwareDetector::ParseTextHandshake(std::string_view response, uint32_t baudRate) {
    FirmwareDescriptor result;
    result.baudRate = baudRate;
    result.rawResponse = std::string(response);
    for (std::string line : Lines(response)) {
        if (line.rfind(">>>", 0) == 0) line = Trim(std::string_view(line).substr(3));
        if (line == ">>>" || line == ">>> ") continue;
        if (line == "km.version()") {
            result.commandEchoObserved = true;
            continue;
        }
        if (line == "km.MAKCU") {
            result.family = FirmwareFamily::V4;
            result.identity = line;
            result.compatible = true;
            return result;
        }
        uint32_t legacyVersion = 0;
        if (ParseLegacyLabel(line, legacyVersion) && legacyVersion == 30) {
            result.family = FirmwareFamily::V4;
            result.numericVersion = legacyVersion;
            result.numericVersionKnown = true;
            result.identity = line;
            result.compatible = true;
            return result;
        }
        if (ParseLegacyLabel(line, legacyVersion) && (legacyVersion == 27 || legacyVersion == 29)) {
            result.family = FirmwareFamily::LegacyText;
            result.numericVersion = legacyVersion;
            result.numericVersionKnown = true;
            result.identity = line;
            result.compatible = true;
            return result;
        }
        if (result.identity.empty()) result.identity = line;
    }
    return result;
}

std::optional<uint32_t> MakcuFirmwareDetector::ParseV4FirmwareVersion(std::span<const uint8_t> response) {
    for (size_t i = 0; i + 9 <= response.size(); ++i) {
        if (response[i] != 0xDE || response[i + 1] != 0xAD || response[i + 4] != 0x04) continue;
        const uint16_t length = static_cast<uint16_t>(response[i + 2] | (response[i + 3] << 8));
        if (length != 4 || i + 5 + length > response.size()) continue;
        return static_cast<uint32_t>(response[i + 5]) |
               (static_cast<uint32_t>(response[i + 6]) << 8) |
               (static_cast<uint32_t>(response[i + 7]) << 16) |
               (static_cast<uint32_t>(response[i + 8]) << 24);
    }
    return std::nullopt;
}

std::optional<uint8_t> MakcuFirmwareDetector::ParseV4DeviceKinds(std::span<const uint8_t> response) {
    for (size_t i = 0; i + 6 <= response.size(); ++i) {
        if (response[i] == 0xDE && response[i + 1] == 0xAD && response[i + 2] == 1 &&
            response[i + 3] == 0 && response[i + 4] == 0x02)
            return response[i + 5];
    }
    return std::nullopt;
}

std::optional<std::string> MakcuFirmwareDetector::ParseLegacyV2Version(std::span<const uint8_t> response) {
    for (size_t i = 0; i + 4 <= response.size(); ++i) {
        if (response[i] != 0x50 || response[i + 1] != 0xBF) continue;
        const uint16_t length = static_cast<uint16_t>(response[i + 2] | (response[i + 3] << 8));
        if (length == 0 || length > 64 || i + 4 + length > response.size()) continue;
        std::string version(response.begin() + static_cast<ptrdiff_t>(i + 4),
                            response.begin() + static_cast<ptrdiff_t>(i + 4 + length));
        const bool printable = std::all_of(version.begin(), version.end(), [](unsigned char value) {
            return value >= 0x20 && value <= 0x7E;
        });
        uint32_t numeric = 0;
        const bool numbered = printable && ParseLegacyLabel(version, numeric);
        std::string upper = version;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char value) {
            return static_cast<char>(std::toupper(value));
        });
        if (numbered || upper.find("MAKCU") != std::string::npos || upper.find("AIO") != std::string::npos)
            return version;
    }
    return std::nullopt;
}

std::vector<uint8_t> MakcuFirmwareDetector::V4Query(uint8_t command) {
    return {0xDE, 0xAD, 0x00, 0x00, command};
}

std::vector<uint8_t> MakcuFirmwareDetector::LegacyV2Query(uint8_t command) {
    return {0x50, command, 0x00, 0x00};
}

CapabilitySet MakcuCapabilityRegistry::Resolve(const FirmwareDescriptor& descriptor) {
    CapabilitySet result;
    if (descriptor.family == FirmwareFamily::Unknown) return result;

    for (Capability capability : {Capability::RelativeMove, Capability::MouseButtons, Capability::Wheel})
        result.Set(capability, CapabilityState::Supported);

    if (descriptor.family == FirmwareFamily::LegacyV2Binary) {
        result.Set(Capability::ButtonMonitoring, CapabilityState::Unknown);
        result.Set(Capability::MouseLocks, CapabilityState::Unknown);
        result.Set(Capability::SmoothMove, CapabilityState::Supported);
        result.Set(Capability::BezierMove, CapabilityState::Supported);
        result.Set(Capability::MouseSerial, CapabilityState::Unknown);
        result.Set(Capability::BaudQuery, CapabilityState::Unknown);
        result.Set(Capability::DeviceKinds, CapabilityState::Unsupported);
        return result;
    }

    result.Set(Capability::ButtonMonitoring, CapabilityState::Supported);
    result.Set(Capability::DeviceKinds,
               descriptor.family == FirmwareFamily::V4 ? CapabilityState::Supported : CapabilityState::Unsupported);
    if (descriptor.family == FirmwareFamily::LegacyText) {
        result.Set(Capability::MouseLocks, CapabilityState::Supported);
        result.Set(Capability::SmoothMove, CapabilityState::Supported);
        result.Set(Capability::BezierMove, CapabilityState::Supported);
        result.Set(Capability::MouseSerial, CapabilityState::Supported);
        result.Set(Capability::BaudQuery, CapabilityState::Supported);
    } else {
        result.Set(Capability::MouseLocks,
                   !descriptor.numericVersionKnown ? CapabilityState::Unknown :
                   descriptor.numericVersion >= 4026 ? CapabilityState::Supported :
                   CapabilityState::Unsupported);
        result.Set(Capability::SmoothMove, CapabilityState::Unsupported);
        result.Set(Capability::BezierMove, CapabilityState::Unsupported);
        result.Set(Capability::MouseSerial, CapabilityState::Unsupported);
        result.Set(Capability::BaudQuery,
                   descriptor.numericVersionKnown && descriptor.numericVersion >= 4073
                       ? CapabilityState::Supported
                       : CapabilityState::Unsupported);
    }
    return result;
}

std::unique_ptr<IMakcuProtocol> MakcuProtocolRegistry::Create(const FirmwareDescriptor& descriptor) {
    CapabilitySet capabilities = MakcuCapabilityRegistry::Resolve(descriptor);
    switch (descriptor.family) {
    case FirmwareFamily::V4: return std::make_unique<V4Protocol>(std::move(capabilities));
    case FirmwareFamily::LegacyText:
        return std::make_unique<TextProtocol>(FirmwareFamily::LegacyText, std::move(capabilities));
    case FirmwareFamily::LegacyV2Binary: return std::make_unique<LegacyV2Protocol>(std::move(capabilities));
    default: return nullptr;
    }
}

const char* FirmwareFamilyName(FirmwareFamily family) noexcept {
    switch (family) {
    case FirmwareFamily::LegacyText: return "Legacy V27/V29";
    case FirmwareFamily::LegacyV2Binary: return "Legacy V2/AIO";
    case FirmwareFamily::V4: return "V4";
    default: return "Unknown";
    }
}

const char* CapabilityStateName(CapabilityState state) noexcept {
    switch (state) {
    case CapabilityState::Supported: return "Supported";
    case CapabilityState::Unsupported: return "Unsupported";
    default: return "Unknown";
    }
}

std::string FirmwareVersionText(const FirmwareDescriptor& descriptor) {
    if (!descriptor.numericVersionKnown) return descriptor.identity.empty() ? "unknown" : descriptor.identity;
    if (descriptor.family == FirmwareFamily::V4) {
        const uint32_t major = descriptor.numericVersion / 1000;
        const uint32_t build = descriptor.numericVersion % 1000;
        std::ostringstream text;
        text << 'V' << major << '.';
        text.width(3);
        text.fill('0');
        text << build;
        return text.str();
    }
    return 'V' + std::to_string(descriptor.numericVersion);
}

}
