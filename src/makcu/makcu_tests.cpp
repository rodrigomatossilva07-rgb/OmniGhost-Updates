




#include "makcu_protocol.h"
#include "makcu_transport.h"
#include "makcu_connection.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <vector>
#include "platform/strprotect/strprotect.h"

namespace makcu_tests {




class MockTransport final : public makcu::IMakcuTransport {
public:
    struct Write {
        std::vector<uint8_t> bytes;
    };

    bool open(const std::string& port, uint32_t baudRate, bool startReader) override {
        opened_ = true;
        port_ = port;
        baudRate_ = baudRate;
        (void)startReader;
        return openResult_;
    }

    void close() override { opened_ = false; }

    bool isOpen() const override { return opened_; }

    bool startListener() override { listenerStarted_ = true; return true; }

    void setReaderMode(makcu::TransportReaderMode mode) noexcept override {
        readerMode_ = mode;
    }

    bool writeRaw(std::span<const uint8_t> data) override {
        if (!opened_) return false;
        writes_.push_back({std::vector<uint8_t>(data.begin(), data.end())});
        return true;
    }

    std::vector<uint8_t> readFor(std::chrono::milliseconds timeout,
                                 std::chrono::milliseconds quietPeriod) override {
        (void)timeout;
        (void)quietPeriod;
        if (responseIndex_ >= responses_.size()) return {};
        return responses_[responseIndex_++];
    }

    bool purge() override { purged_ = true; return true; }

    uint32_t getBaudRate() const override { return baudRate_; }
    std::string getPortName() const override { return port_; }


    void setResponses(std::vector<std::vector<uint8_t>> responses) {
        responses_ = std::move(responses);
        responseIndex_ = 0;
    }
    void setNextResponse(std::vector<uint8_t> response) {
        responses_ = {std::move(response)};
        responseIndex_ = 0;
    }
    const std::vector<Write>& writes() const { return writes_; }
    void resetWrites() { writes_.clear(); }
    makcu::TransportReaderMode readerMode() const { return readerMode_; }
    bool purged() const { return purged_; }
    void setOpenResult(bool result) { openResult_ = result; }

private:
    bool opened_ = false;
    bool openResult_ = true;
    bool listenerStarted_ = false;
    bool purged_ = false;
    std::string port_;
    uint32_t baudRate_ = 0;
    makcu::TransportReaderMode readerMode_ = makcu::TransportReaderMode::Text;
    std::vector<Write> writes_;
    std::vector<std::vector<uint8_t>> responses_;
    size_t responseIndex_ = 0;
};




std::vector<uint8_t> Text(const std::string& value) {
    return std::vector<uint8_t>(value.begin(), value.end());
}

int g_failures = 0;
int g_tests = 0;

void Check(bool condition, const char* label) {
    ++g_tests;
    if (!condition) {
        ++g_failures;
        std::cerr << OGS_LIT("[FAIL] ") << label << "\n";
    }
}




void TestTextHandshakeV27() {
    const auto descriptor = makcu::MakcuFirmwareDetector::ParseTextHandshake(
        OGS_LIT(">>> km.version()\r\n>>> V27\r\n"), 4000000);
    Check(descriptor.family == makcu::FirmwareFamily::LegacyText, OGS_CSTR("V27 → LegacyText"));
    Check(descriptor.numericVersionKnown, OGS_CSTR("V27 version known"));
    Check(descriptor.numericVersion == 27, OGS_CSTR("V27 numeric == 27"));
    Check(descriptor.compatible, OGS_CSTR("V27 compatible"));
}

void TestTextHandshakeV29() {
    const auto descriptor = makcu::MakcuFirmwareDetector::ParseTextHandshake(
        OGS_LIT(">>> km.version()\r\n>>> V29\r\n"), 115200);
    Check(descriptor.family == makcu::FirmwareFamily::LegacyText, OGS_CSTR("V29 → LegacyText"));
    Check(descriptor.numericVersion == 29, OGS_CSTR("V29 numeric == 29"));
    Check(descriptor.compatible, OGS_CSTR("V29 compatible"));
}

void TestTextHandshakeV30() {
    const auto descriptor = makcu::MakcuFirmwareDetector::ParseTextHandshake(
        OGS_LIT(">>> km.version()\r\n>>> V30\r\n"), 4000000);
    Check(descriptor.family == makcu::FirmwareFamily::V4, "V30 → V4");
    Check(descriptor.numericVersion == 30, OGS_CSTR("V30 numeric == 30"));
    Check(descriptor.compatible, OGS_CSTR("V30 compatible"));
}

void TestTextHandshakeMakcuIdentity() {
    const auto descriptor = makcu::MakcuFirmwareDetector::ParseTextHandshake(
        OGS_LIT(">>> km.MAKCU\r\n"), 4000000);
    Check(descriptor.family == makcu::FirmwareFamily::V4, OGS_CSTR("km.MAKCU → V4"));
    Check(!descriptor.numericVersionKnown, OGS_CSTR("km.MAKCU version not yet known"));
    Check(descriptor.compatible, OGS_CSTR("km.MAKCU compatible"));
}

void TestUnknownFirmware() {
    const auto descriptor = makcu::MakcuFirmwareDetector::ParseTextHandshake(
        OGS_LIT(">>> km.version()\r\n>>> SomeRandomDevice v42\r\n"), 4000000);
    Check(descriptor.family == makcu::FirmwareFamily::Unknown, OGS_CSTR("unknown identity → Unknown"));
    Check(!descriptor.compatible, OGS_CSTR("unknown identity not compatible"));
}

void TestEmptyHandshake() {
    const auto descriptor = makcu::MakcuFirmwareDetector::ParseTextHandshake(
        "", 4000000);
    Check(descriptor.family == makcu::FirmwareFamily::Unknown, OGS_CSTR("empty → Unknown"));
    Check(!descriptor.compatible, OGS_CSTR("empty not compatible"));
}

void TestV4BinaryVersionParse() {

    std::vector<uint8_t> response = {0xDE, 0xAD, 0x04, 0x00, 0x04, 0xB9, 0x0F, 0x00, 0x00};
    const auto version = makcu::MakcuFirmwareDetector::ParseV4FirmwareVersion(response);
    Check(version.has_value(), OGS_CSTR("V4 binary version parsed"));
    if (version) Check(*version == 4025, OGS_CSTR("V4 binary version == 4025"));
}

void TestV4BinaryVersionGarbage() {
    std::vector<uint8_t> response = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
    const auto version = makcu::MakcuFirmwareDetector::ParseV4FirmwareVersion(response);
    Check(!version.has_value(), OGS_CSTR("garbage → no version"));
}

void TestV4DeviceKindsParse() {
    std::vector<uint8_t> response = {0xDE, 0xAD, 0x01, 0x00, 0x02, 0x03};
    const auto kinds = makcu::MakcuFirmwareDetector::ParseV4DeviceKinds(response);
    Check(kinds.has_value(), OGS_CSTR("V4 device kinds parsed"));
    if (kinds) Check(*kinds == 0x03, OGS_CSTR("V4 device kinds == 3"));
}

void TestLegacyV2VersionParse() {

    std::vector<uint8_t> response = {0x50, 0xBF, 0x03, 0x00, 'V', '2', '7'};
    const auto version = makcu::MakcuFirmwareDetector::ParseLegacyV2Version(response);
    Check(version.has_value(), OGS_CSTR("Legacy V2 version parsed"));
    if (version) Check(*version == "V27", OGS_CSTR("Legacy V2 version == V27"));
}

void TestLegacyV2AioIdentity() {
    std::vector<uint8_t> response = {0x50, 0xBF, 0x03, 0x00, 'A', 'I', 'O'};
    const auto version = makcu::MakcuFirmwareDetector::ParseLegacyV2Version(response);
    Check(version.has_value(), OGS_CSTR("AIO identity parsed"));
    if (version) Check(*version == "AIO", OGS_CSTR("Legacy V2 identity == AIO"));
}




void TestLegacyTextCapabilities() {
    makcu::FirmwareDescriptor descriptor;
    descriptor.family = makcu::FirmwareFamily::LegacyText;
    descriptor.numericVersion = 27;
    descriptor.numericVersionKnown = true;
    descriptor.compatible = true;

    const auto caps = makcu::MakcuCapabilityRegistry::Resolve(descriptor);
    Check(caps.Get(makcu::Capability::RelativeMove) == makcu::CapabilityState::Supported,
          OGS_CSTR("LegacyText: RelativeMove supported"));
    Check(caps.Get(makcu::Capability::SmoothMove) == makcu::CapabilityState::Supported,
          OGS_CSTR("LegacyText: SmoothMove supported"));
    Check(caps.Get(makcu::Capability::BezierMove) == makcu::CapabilityState::Supported,
          OGS_CSTR("LegacyText: BezierMove supported"));
    Check(caps.Get(makcu::Capability::ButtonMonitoring) == makcu::CapabilityState::Supported,
          OGS_CSTR("LegacyText: ButtonMonitoring supported"));
    Check(caps.Get(makcu::Capability::MouseLocks) == makcu::CapabilityState::Supported,
          OGS_CSTR("LegacyText: MouseLocks supported"));
    Check(caps.Get(makcu::Capability::MouseSerial) == makcu::CapabilityState::Supported,
          OGS_CSTR("LegacyText: MouseSerial supported"));
    Check(caps.Get(makcu::Capability::DeviceKinds) == makcu::CapabilityState::Unsupported,
          OGS_CSTR("LegacyText: DeviceKinds unsupported"));
}

void TestV4Capabilities() {
    makcu::FirmwareDescriptor descriptor;
    descriptor.family = makcu::FirmwareFamily::V4;
    descriptor.compatible = true;

    const auto caps = makcu::MakcuCapabilityRegistry::Resolve(descriptor);
    Check(caps.Get(makcu::Capability::RelativeMove) == makcu::CapabilityState::Supported,
          OGS_CSTR("V4: RelativeMove supported"));
    Check(caps.Get(makcu::Capability::SmoothMove) == makcu::CapabilityState::Unsupported,
          OGS_CSTR("V4: SmoothMove unsupported (binary protocol)"));
    Check(caps.Get(makcu::Capability::BezierMove) == makcu::CapabilityState::Unsupported,
          OGS_CSTR("V4: BezierMove unsupported"));
    Check(caps.Get(makcu::Capability::ButtonMonitoring) == makcu::CapabilityState::Supported,
          OGS_CSTR("V4: ButtonMonitoring supported"));
    Check(caps.Get(makcu::Capability::DeviceKinds) == makcu::CapabilityState::Supported,
          OGS_CSTR("V4: DeviceKinds supported"));
    Check(caps.Get(makcu::Capability::BaudQuery) == makcu::CapabilityState::Unsupported,
          OGS_CSTR("V4 (no version): BaudQuery unsupported"));
}

void TestV4CapabilitiesWithVersion() {
    makcu::FirmwareDescriptor descriptor;
    descriptor.family = makcu::FirmwareFamily::V4;
    descriptor.numericVersion = 4073;
    descriptor.numericVersionKnown = true;
    descriptor.compatible = true;

    const auto caps = makcu::MakcuCapabilityRegistry::Resolve(descriptor);
    Check(caps.Get(makcu::Capability::BaudQuery) == makcu::CapabilityState::Supported,
          OGS_CSTR("V4 >= 4073: BaudQuery supported"));
}

void TestV2BinaryCapabilities() {
    makcu::FirmwareDescriptor descriptor;
    descriptor.family = makcu::FirmwareFamily::LegacyV2Binary;
    descriptor.compatible = true;

    const auto caps = makcu::MakcuCapabilityRegistry::Resolve(descriptor);
    Check(caps.Get(makcu::Capability::RelativeMove) == makcu::CapabilityState::Supported,
          OGS_CSTR("V2: RelativeMove supported"));
    Check(caps.Get(makcu::Capability::SmoothMove) == makcu::CapabilityState::Supported,
          OGS_CSTR("V2: SmoothMove supported"));
    Check(caps.Get(makcu::Capability::BezierMove) == makcu::CapabilityState::Supported,
          OGS_CSTR("V2: BezierMove supported"));
    Check(caps.Get(makcu::Capability::ButtonMonitoring) == makcu::CapabilityState::Supported,
          OGS_CSTR("V2: ButtonMonitoring supported (documented 0x02 button stream)"));
    Check(caps.Get(makcu::Capability::MouseLocks) == makcu::CapabilityState::Unknown,
          OGS_CSTR("V2: MouseLocks unknown"));
}

void TestUnknownCapabilities() {
    makcu::FirmwareDescriptor descriptor;
    descriptor.family = makcu::FirmwareFamily::Unknown;

    const auto caps = makcu::MakcuCapabilityRegistry::Resolve(descriptor);
    for (int i = 0; i < static_cast<int>(makcu::Capability::Count); ++i) {
        Check(caps.Get(static_cast<makcu::Capability>(i)) == makcu::CapabilityState::Unknown,
              OGS_CSTR("Unknown firmware: all capabilities Unknown"));
    }
}




void TestTextProtocolMove() {
    makcu::FirmwareDescriptor descriptor;
    descriptor.family = makcu::FirmwareFamily::LegacyText;
    descriptor.compatible = true;
    auto protocol = makcu::MakcuProtocolRegistry::Create(descriptor);
    Check(protocol != nullptr, OGS_CSTR("LegacyText protocol created"));

    const auto cmd = protocol->Move(100, -50);
    Check(cmd.has_value(), OGS_CSTR("LegacyText Move produced command"));
    if (cmd) {
        Check(cmd->encoding == makcu::WireEncoding::Text, OGS_CSTR("LegacyText Move is Text"));
        const std::string text(cmd->bytes.begin(), cmd->bytes.end());
        Check(text.find(OGS_LIT("km.move(100,-50)")) != std::string::npos,
              OGS_CSTR("LegacyText Move encodes km.move(100,-50)"));
    }
}

void TestTextProtocolMoveRange() {
    makcu::FirmwareDescriptor descriptor;
    descriptor.family = makcu::FirmwareFamily::LegacyText;
    descriptor.compatible = true;
    auto protocol = makcu::MakcuProtocolRegistry::Create(descriptor);


    Check(!protocol->Move(40000, 0).has_value(), OGS_CSTR("LegacyText Move rejects x > int16"));
    Check(!protocol->Move(0, -40000).has_value(), OGS_CSTR("LegacyText Move rejects y < -int16"));
}

void TestV4ProtocolMove() {
    makcu::FirmwareDescriptor descriptor;
    descriptor.family = makcu::FirmwareFamily::V4;
    descriptor.compatible = true;
    auto protocol = makcu::MakcuProtocolRegistry::Create(descriptor);
    Check(protocol != nullptr, OGS_CSTR("V4 protocol created"));

    const auto cmd = protocol->Move(100, -50);
    Check(cmd.has_value(), OGS_CSTR("V4 Move produced command"));
    if (cmd) {
        Check(cmd->encoding == makcu::WireEncoding::Binary, OGS_CSTR("V4 Move is Binary"));
        Check(cmd->bytes.size() >= 9, OGS_CSTR("V4 Move has header + payload"));
        Check(cmd->bytes[0] == 0xDE && cmd->bytes[1] == 0xAD, OGS_CSTR("V4 Move starts with 0xDEAD"));
        Check(cmd->bytes[4] == 0x18, OGS_CSTR("V4 Move opcode == 0x18"));
    }
}

void TestV4ProtocolSmoothUnsupported() {
    makcu::FirmwareDescriptor descriptor;
    descriptor.family = makcu::FirmwareFamily::V4;
    descriptor.compatible = true;
    auto protocol = makcu::MakcuProtocolRegistry::Create(descriptor);
    Check(!protocol->MoveSmooth(100, 100, 10).has_value(), OGS_CSTR("V4 SmoothMove rejected"));
    Check(!protocol->MoveBezier(100, 100, 10, 50, 50).has_value(), OGS_CSTR("V4 BezierMove rejected"));
}

void TestV2BinaryProtocolMove() {
    makcu::FirmwareDescriptor descriptor;
    descriptor.family = makcu::FirmwareFamily::LegacyV2Binary;
    descriptor.compatible = true;
    auto protocol = makcu::MakcuProtocolRegistry::Create(descriptor);
    Check(protocol != nullptr, OGS_CSTR("V2 protocol created"));

    const auto cmd = protocol->Move(100, -50);
    Check(cmd.has_value(), OGS_CSTR("V2 Move produced command"));
    if (cmd) {
        Check(cmd->encoding == makcu::WireEncoding::Binary, OGS_CSTR("V2 Move is Binary"));
        Check(cmd->bytes[0] == 0x50, OGS_CSTR("V2 Move starts with 0x50"));
    }
}

void TestV2BinaryWheelRange() {
    makcu::FirmwareDescriptor descriptor;
    descriptor.family = makcu::FirmwareFamily::LegacyV2Binary;
    descriptor.compatible = true;
    auto protocol = makcu::MakcuProtocolRegistry::Create(descriptor);
    Check(!protocol->Wheel(200).has_value(), OGS_CSTR("V2 Wheel rejects > 127"));
    Check(protocol->Wheel(-100).has_value(), OGS_CSTR("V2 Wheel accepts -100"));
}



void TestV2BinaryButtonMonitoring() {
    makcu::FirmwareDescriptor descriptor;
    descriptor.family = makcu::FirmwareFamily::LegacyV2Binary;
    descriptor.compatible = true;
    auto protocol = makcu::MakcuProtocolRegistry::Create(descriptor);
    Check(protocol != nullptr, OGS_CSTR("V2 protocol created (button monitoring)"));

    const auto enable = protocol->ButtonMonitoring(true);
    Check(enable.has_value(), OGS_CSTR("V2 ButtonMonitoring(true) produced command"));
    if (enable) {
        Check(enable->encoding == makcu::WireEncoding::Binary, OGS_CSTR("V2 ButtonMonitoring is Binary"));
        Check(enable->bytes.size() == 5, OGS_CSTR("V2 ButtonMonitoring frame is header + 1 payload byte"));
        if (enable->bytes.size() == 5) {
            Check(enable->bytes[0] == 0x50, OGS_CSTR("V2 ButtonMonitoring starts with 0x50"));
            Check(enable->bytes[1] == 0x02, OGS_CSTR("V2 ButtonMonitoring command == 0x02"));
            Check(enable->bytes[2] == 0x01 && enable->bytes[3] == 0x00,
                  OGS_CSTR("V2 ButtonMonitoring payload length == 1"));
            Check(enable->bytes[4] == 0x01, OGS_CSTR("V2 ButtonMonitoring enable mode == 1"));
        }
    }

    const auto disable = protocol->ButtonMonitoring(false);
    Check(disable.has_value(), OGS_CSTR("V2 ButtonMonitoring(false) produced command"));
    if (disable && disable->bytes.size() == 5)
        Check(disable->bytes[4] == 0x00, OGS_CSTR("V2 ButtonMonitoring disable mode == 0"));
}

void TestButtonEncoding() {
    makcu::FirmwareDescriptor descriptor;
    descriptor.family = makcu::FirmwareFamily::LegacyText;
    descriptor.compatible = true;
    auto protocol = makcu::MakcuProtocolRegistry::Create(descriptor);

    const auto down = protocol->Button(0, true);
    Check(down.has_value(), OGS_CSTR("Text Button(0,true) produced command"));
    if (down) {
        const std::string text(down->bytes.begin(), down->bytes.end());
        Check(text.find(OGS_CSTR("km.left(1)")) != std::string::npos, OGS_CSTR("Text Button left down"));
    }

    const auto up = protocol->Button(0, false);
    if (up) {
        const std::string text(up->bytes.begin(), up->bytes.end());
        Check(text.find(OGS_CSTR("km.left(0)")) != std::string::npos, OGS_CSTR("Text Button left up"));
    }
}

void TestLockEncoding() {
    makcu::FirmwareDescriptor descriptor;
    descriptor.family = makcu::FirmwareFamily::LegacyText;
    descriptor.compatible = true;
    auto protocol = makcu::MakcuProtocolRegistry::Create(descriptor);

    const auto lock = protocol->Lock("mx", true);
    Check(lock.has_value(), OGS_CSTR("Text Lock(mx,true) produced command"));
    if (lock) {
        const std::string text(lock->bytes.begin(), lock->bytes.end());
        Check(text.find(OGS_CSTR("km.lock_mx(1)")) != std::string::npos, OGS_CSTR("Text Lock mx encoding"));
    }


    Check(!protocol->Lock(OGS_CSTR("invalid"), true).has_value(), OGS_CSTR("Lock rejects invalid target"));
}

void TestUnknownFirmwareNoProtocol() {
    makcu::FirmwareDescriptor descriptor;
    descriptor.family = makcu::FirmwareFamily::Unknown;
    auto protocol = makcu::MakcuProtocolRegistry::Create(descriptor);
    Check(protocol == nullptr, OGS_CSTR("Unknown firmware → no protocol adapter"));
}




void TestConnectLegacyText() {
    MockTransport transport;
    makcu::MakcuConnectionManager manager;
    transport.setResponses({
        Text(OGS_LIT(">>> km.version()\r\n>>> V27\r\n"))
    });
    const auto attempt = manager.Connect(transport, OGS_LIT("COM3"));
    Check(attempt.connected, OGS_CSTR("LegacyText connect succeeded"));
    Check(attempt.firmware.family == makcu::FirmwareFamily::LegacyText, OGS_CSTR("Connected to LegacyText"));
    Check(attempt.firmware.numericVersion == 27, OGS_CSTR("Connected firmware version 27"));
    Check(transport.readerMode() == makcu::TransportReaderMode::Text, OGS_CSTR("Reader mode Text"));
}

void TestConnectV4WithVersion() {
    MockTransport transport;
    makcu::MakcuConnectionManager manager;
    transport.setResponses({
        Text(OGS_LIT(">>> km.MAKCU\r\n")),
        {0xDE, 0xAD, 0x04, 0x00, 0x04, 0xB9, 0x0F, 0x00, 0x00},
        {0xDE, 0xAD, 0x01, 0x00, 0x02, 0x03}
    });
    const auto attempt = manager.Connect(transport, OGS_LIT("COM3"));
    Check(attempt.connected, OGS_CSTR("V4 connect succeeded"));
    Check(attempt.firmware.family == makcu::FirmwareFamily::V4, OGS_CSTR("Connected to V4"));
    Check(attempt.firmware.numericVersionKnown, OGS_CSTR("V4 numeric version known"));
    Check(attempt.firmware.numericVersion == 4025, OGS_CSTR("V4 version == 4025"));
    Check(attempt.firmware.deviceKindsKnown, OGS_CSTR("V4 device kinds known"));
}

void TestConnectV2Binary() {
    MockTransport transport;
    makcu::MakcuConnectionManager manager;
    transport.setResponses({
        {},
        {0x50, 0xBF, 0x03, 0x00, 'V', '2', '7'}
    });
    const auto attempt = manager.Connect(transport, OGS_LIT("COM3"));
    Check(attempt.connected, OGS_CSTR("V2 binary connect succeeded"));
    Check(attempt.firmware.family == makcu::FirmwareFamily::LegacyV2Binary, OGS_CSTR("Connected to V2"));
    Check(transport.readerMode() == makcu::TransportReaderMode::LegacyV2Binary, OGS_CSTR("Reader mode V2 binary"));
}

void TestConnectUnknownFirmware() {
    MockTransport transport;
    makcu::MakcuConnectionManager manager;
    transport.setResponses({
        Text(OGS_LIT(">>> km.version()\r\n>>> UnknownDevice v99\r\n")),
        {}
    });
    const auto attempt = manager.Connect(transport, OGS_LIT("COM3"));
    Check(!attempt.connected, OGS_CSTR("Unknown firmware rejected"));
    Check(!attempt.error.empty(), OGS_CSTR("Error message present for unknown firmware"));
}

void TestConnectNoResponse() {
    MockTransport transport;
    makcu::MakcuConnectionManager manager;
    transport.setResponses({{}, {}, {}, {}});
    const auto attempt = manager.Connect(transport, OGS_LIT("COM3"));
    Check(!attempt.connected, OGS_CSTR("No response → not connected"));
}

void TestConnectPurgesBeforeProbe() {
    MockTransport transport;
    makcu::MakcuConnectionManager manager;
    transport.setResponses({
        Text(OGS_LIT(">>> km.version()\r\n>>> V27\r\n"))
    });
    (void)manager.Connect(transport, OGS_LIT("COM3"));
    Check(transport.purged(), OGS_CSTR("Transport purged before probe"));
}

void TestConnectEmptyPort() {
    MockTransport transport;
    makcu::MakcuConnectionManager manager;
    const auto attempt = manager.Connect(transport, "");
    Check(!attempt.connected, OGS_CSTR("Empty port rejected"));
}

void TestConnectWriteNotDuringDiscovery() {
    MockTransport transport;
    makcu::MakcuConnectionManager manager;
    transport.setResponses({
        Text(OGS_LIT(">>> km.version()\r\n>>> V27\r\n"))
    });
    const auto attempt = manager.Connect(transport, OGS_LIT("COM3"));
    Check(attempt.connected, OGS_CSTR("Connected"));


    for (const auto& write : transport.writes()) {
        const std::string text(write.bytes.begin(), write.bytes.end());
        Check(text.find("km.move") == std::string::npos, OGS_CSTR("Discovery never sends km.move"));
        Check(text.find("km.left") == std::string::npos, OGS_CSTR("Discovery never sends km.left"));
        Check(text.find("km.wheel") == std::string::npos, OGS_CSTR("Discovery never sends km.wheel"));
    }
}




void TestMoveZeroRejected() {
    makcu::FirmwareDescriptor descriptor;
    descriptor.family = makcu::FirmwareFamily::LegacyText;
    descriptor.compatible = true;
    auto protocol = makcu::MakcuProtocolRegistry::Create(descriptor);


    const auto cmd = protocol->Move(0, 0);
    Check(cmd.has_value(), OGS_CSTR("Move(0,0) produces a command (caller should filter)"));
}

void TestMoveMaxInt16() {
    makcu::FirmwareDescriptor descriptor;
    descriptor.family = makcu::FirmwareFamily::LegacyText;
    descriptor.compatible = true;
    auto protocol = makcu::MakcuProtocolRegistry::Create(descriptor);

    Check(protocol->Move(32767, -32768).has_value(), OGS_CSTR("Move(int16 max, int16 min) accepted"));
    Check(!protocol->Move(32768, 0).has_value(), OGS_CSTR("Move(x > int16 max) rejected"));
    Check(!protocol->Move(0, -32769).has_value(), OGS_CSTR("Move(y < int16 min) rejected"));
}

}

int main() {
    using namespace makcu_tests;


    TestTextHandshakeV27();
    TestTextHandshakeV29();
    TestTextHandshakeV30();
    TestTextHandshakeMakcuIdentity();
    TestUnknownFirmware();
    TestEmptyHandshake();
    TestV4BinaryVersionParse();
    TestV4BinaryVersionGarbage();
    TestV4DeviceKindsParse();
    TestLegacyV2VersionParse();
    TestLegacyV2AioIdentity();


    TestLegacyTextCapabilities();
    TestV4Capabilities();
    TestV4CapabilitiesWithVersion();
    TestV2BinaryCapabilities();
    TestUnknownCapabilities();


    TestTextProtocolMove();
    TestTextProtocolMoveRange();
    TestV4ProtocolMove();
    TestV4ProtocolSmoothUnsupported();
    TestV2BinaryProtocolMove();
    TestV2BinaryWheelRange();
    TestV2BinaryButtonMonitoring();
    TestButtonEncoding();
    TestLockEncoding();
    TestUnknownFirmwareNoProtocol();


    TestConnectLegacyText();
    TestConnectV4WithVersion();
    TestConnectV2Binary();
    TestConnectUnknownFirmware();
    TestConnectNoResponse();
    TestConnectPurgesBeforeProbe();
    TestConnectEmptyPort();
    TestConnectWriteNotDuringDiscovery();


    TestMoveZeroRejected();
    TestMoveMaxInt16();

    std::cout << OGS_LIT("[MAKCU tests] ") << g_tests << OGS_LIT(" checks, ") << g_failures << OGS_LIT(" failures\n");
    if (g_failures > 0) {
        std::cerr << OGS_LIT("[MAKCU tests] FAILED\n");
        return 1;
    }
    std::cout << OGS_LIT("[MAKCU tests] ALL PASSED\n");
    return 0;
}
