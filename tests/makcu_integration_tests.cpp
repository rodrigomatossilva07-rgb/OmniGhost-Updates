#include "makcu/makcu_connection.h"
#include "makcu/makcu_protocol.h"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::vector<uint8_t> Bytes(std::string_view value) {
    return {value.begin(), value.end()};
}

class MockTransport final : public makcu::IMakcuTransport {
public:
    bool open(const std::string& port, uint32_t baudRate, bool startReader) override {
        opened = allowedBauds.contains(baudRate) && allowedBauds[baudRate];
        currentPort = port;
        currentBaud = baudRate;
        reader = opened && startReader;
        opens.push_back(baudRate);
        return opened;
    }

    void close() override {
        opened = false;
        reader = false;
    }

    bool isOpen() const override { return opened; }

    bool startListener() override {
        if (!opened || failReader) return false;
        reader = true;
        return true;
    }

    void setReaderMode(makcu::TransportReaderMode mode) noexcept override { readerMode = mode; }

    bool writeRaw(std::span<const uint8_t> data) override {
        if (!opened || failWrite) return false;
        writes.emplace_back(data.begin(), data.end());
        return true;
    }

    std::vector<uint8_t> readFor(std::chrono::milliseconds,
                                 std::chrono::milliseconds) override {
        if (replies.empty()) return {};
        auto reply = std::move(replies.front());
        replies.pop_front();
        return reply;
    }

    bool purge() override { return opened && !reader; }
    uint32_t getBaudRate() const override { return currentBaud; }
    std::string getPortName() const override { return currentPort; }

    std::map<uint32_t, bool> allowedBauds{{4000000, true}, {115200, true}};
    std::deque<std::vector<uint8_t>> replies;
    std::vector<std::vector<uint8_t>> writes;
    std::vector<uint32_t> opens;
    makcu::TransportReaderMode readerMode = makcu::TransportReaderMode::Text;
    std::string currentPort;
    uint32_t currentBaud = 0;
    bool opened = false;
    bool reader = false;
    bool failWrite = false;
    bool failReader = false;
};

void TestIdentification() {
    const auto v27 = makcu::MakcuFirmwareDetector::ParseTextHandshake(
        "km.version()\r\nV27\r\n>>> ", 115200);
    Require(v27.family == makcu::FirmwareFamily::LegacyText, "V27 family");
    Require(v27.numericVersion == 27 && v27.commandEchoObserved, "V27 identity and echo");

    const auto v29 = makcu::MakcuFirmwareDetector::ParseTextHandshake(
        ">>> km.version()\r\n>>> V29\r\n>>> ", 115200);
    Require(v29.family == makcu::FirmwareFamily::LegacyText, "V29 family");
    Require(v29.numericVersion == 29, "V29 version");

    const auto v4 = makcu::MakcuFirmwareDetector::ParseTextHandshake(
        "km.MAKCU\r\n>>> ", 4000000);
    Require(v4.family == makcu::FirmwareFamily::V4 && !v4.numericVersionKnown,
            "V4 identity is not numeric firmware version");

    const auto v30 = makcu::MakcuFirmwareDetector::ParseTextHandshake("V30\r\n>>> ", 4000000);
    Require(v30.family == makcu::FirmwareFamily::V4 && v30.numericVersion == 30, "V30 matrix family");

    const auto unknown = makcu::MakcuFirmwareDetector::ParseTextHandshake(
        "km.FutureThing\r\n>>> ", 4000000);
    Require(!unknown.compatible && unknown.family == makcu::FirmwareFamily::Unknown,
            "unknown firmware remains incompatible");
}

void TestBinaryParsing() {
    const std::vector<uint8_t> v4Version{0x99, 0xDE, 0xAD, 0x04, 0x00, 0x04, 0x06, 0x10, 0x00, 0x00};
    const auto version = makcu::MakcuFirmwareDetector::ParseV4FirmwareVersion(v4Version);
    Require(version && *version == 4102, "V4 numeric version frame");
    Require(!makcu::MakcuFirmwareDetector::ParseV4FirmwareVersion(
        std::span<const uint8_t>(v4Version.data(), 7)), "partial V4 frame rejected");

    const std::vector<uint8_t> kinds{0xDE, 0xAD, 0x01, 0x00, 0x02, 0x03};
    Require(makcu::MakcuFirmwareDetector::ParseV4DeviceKinds(kinds).value_or(0) == 3,
            "V4 device kinds");

    const std::vector<uint8_t> v2{0x50, 0xBF, 0x03, 0x00, 'V', '2', '9'};
    Require(makcu::MakcuFirmwareDetector::ParseLegacyV2Version(v2).value_or("") == "V29",
            "V2 version frame");
    const std::vector<uint8_t> falsePositive{0x50, 0xBF, 0x03, 0x00, 'A', 'B', 'C'};
    Require(!makcu::MakcuFirmwareDetector::ParseLegacyV2Version(falsePositive),
            "unknown V2 payload rejected");
}

void TestCapabilitiesAndCommands() {
    makcu::FirmwareDescriptor v4;
    v4.family = makcu::FirmwareFamily::V4;
    v4.compatible = true;
    v4.numericVersionKnown = true;
    v4.numericVersion = 4102;
    auto protocol = makcu::MakcuProtocolRegistry::Create(v4);
    Require(protocol != nullptr, "V4 adapter exists");
    Require(protocol->Capabilities().Get(makcu::Capability::RelativeMove) ==
            makcu::CapabilityState::Supported, "V4 move capability");
    Require(protocol->Capabilities().Get(makcu::Capability::MouseSerial) ==
            makcu::CapabilityState::Unsupported, "V4 serial is explicitly unsupported");
    const auto move = protocol->Move(-2, 300);
    Require(move && move->bytes == std::vector<uint8_t>({0xDE,0xAD,0x04,0x00,0x18,0xFE,0xFF,0x2C,0x01}),
            "V4 relative move encoding");
    Require(!protocol->Move(40000, 0), "V4 move range validation");
    const auto down = protocol->Button(0, true);
    Require(down && down->bytes == std::vector<uint8_t>({0xDE,0xAD,0x01,0x00,0x11,0x01}),
            "V4 button encoding");
    Require(!protocol->Button(5, true), "invalid button rejected");

    makcu::FirmwareDescriptor legacy;
    legacy.family = makcu::FirmwareFamily::LegacyText;
    legacy.numericVersionKnown = true;
    legacy.numericVersion = 29;
    legacy.compatible = true;
    auto legacyProtocol = makcu::MakcuProtocolRegistry::Create(legacy);
    const auto legacyMove = legacyProtocol->Move(12, -4);
    Require(legacyMove && std::string(legacyMove->bytes.begin(), legacyMove->bytes.end()) == "km.move(12,-4)",
            "legacy text move");

    makcu::FirmwareDescriptor v2;
    v2.family = makcu::FirmwareFamily::LegacyV2Binary;
    v2.compatible = true;
    auto v2Protocol = makcu::MakcuProtocolRegistry::Create(v2);
    const auto v2Move = v2Protocol->Move(1, -1);
    Require(v2Move && v2Move->bytes == std::vector<uint8_t>({0x50,0x0D,0x07,0x00,0x01,0x00,0xFF,0xFF,0x01,0x00,0x00}),
            "V2 move encoding");
    const auto v2Buttons = v2Protocol->ButtonMonitoring(true);
    Require(v2Buttons && v2Buttons->bytes == std::vector<uint8_t>({0x50,0x02,0x01,0x00,0x01}),
            "V2 button stream enable encoding");
    Require(v2Protocol->Capabilities().Get(makcu::Capability::ButtonMonitoring) ==
            makcu::CapabilityState::Supported,
            "V2 button stream documented capability");
}

void TestConnectionSelection() {
    makcu::MakcuConnectionManager manager;
    MockTransport v4;
    v4.replies.push_back(Bytes("km.MAKCU\r\n>>> "));
    v4.replies.push_back({0xDE,0xAD,0x04,0x00,0x04,0x06,0x10,0x00,0x00});
    v4.replies.push_back({0xDE,0xAD,0x01,0x00,0x02,0x01});
    const auto v4Attempt = manager.Connect(v4, "COM7");
    Require(v4Attempt.connected && v4Attempt.firmware.numericVersion == 4102, "V4 connection");
    Require(v4.opens == std::vector<uint32_t>({4000000}), "V4 preferred baud");
    Require(v4.writes.size() == 3 && v4.reader, "V4 handshake order and reader");

    MockTransport legacy;
    legacy.allowedBauds[4000000] = false;
    legacy.replies.push_back(Bytes("km.version()\r\nV29\r\n>>> "));
    const auto legacyAttempt = manager.Connect(legacy, "COM8");
    Require(legacyAttempt.connected && legacyAttempt.firmware.family == makcu::FirmwareFamily::LegacyText,
            "legacy fallback connection");
    Require(legacy.opens == std::vector<uint32_t>({4000000,115200}), "baud fallback order");

    MockTransport v2;
    v2.allowedBauds[4000000] = false;
    v2.replies.push_back({});
    v2.replies.push_back({0x50,0xBF,0x03,0x00,'V','2','9'});
    const auto v2Attempt = manager.Connect(v2, "COM9");
    Require(v2Attempt.connected && v2Attempt.firmware.family == makcu::FirmwareFamily::LegacyV2Binary,
            "V2 binary fallback");
    Require(v2.readerMode == makcu::TransportReaderMode::LegacyV2Binary, "V2 reader mode");

    MockTransport timeout;
    timeout.replies.push_back({});
    timeout.replies.push_back({});
    timeout.replies.push_back({});
    timeout.replies.push_back({});
    const auto timeoutAttempt = manager.Connect(timeout, "COM10");
    Require(!timeoutAttempt.connected && !timeout.isOpen(), "timeout closes transport");

    MockTransport disconnected;
    disconnected.failWrite = true;
    const auto disconnectedAttempt = manager.Connect(disconnected, "COM11");
    Require(!disconnectedAttempt.connected, "write failure does not connect");
}

void TestReconnectDoesNotReplay() {
    makcu::MakcuConnectionManager manager;
    MockTransport transport;
    for (int pass = 0; pass < 2; ++pass) {
        transport.replies.push_back(Bytes("km.MAKCU\r\n>>> "));
        transport.replies.push_back({0xDE,0xAD,0x04,0x00,0x04,0x06,0x10,0x00,0x00});
        transport.replies.push_back({0xDE,0xAD,0x01,0x00,0x02,0x01});
        const auto attempt = manager.Connect(transport, "COM12");
        Require(attempt.connected, "reconnect succeeds");
        transport.close();
    }
    Require(transport.writes.size() == 6, "reconnect only sends discovery queries");
    for (size_t i = 0; i < transport.writes.size(); i += 3) {
        Require(transport.writes[i] == Bytes("km.version()\r"), "reconnect starts with identity query");
        Require(transport.writes[i + 1] == makcu::MakcuFirmwareDetector::V4Query(0x04),
                "version query order");
        Require(transport.writes[i + 2] == makcu::MakcuFirmwareDetector::V4Query(0x02),
                "device query order");
    }
}

}

int main() {
    TestIdentification();
    TestBinaryParsing();
    TestCapabilitiesAndCommands();
    TestConnectionSelection();
    TestReconnectDoesNotReplay();
    std::cout << "MAKCU integration tests: PASS\n";
    return 0;
}
