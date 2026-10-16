#include "makcu_connection.h"

#include <charconv>
#include <utility>

namespace makcu {

MakcuConnectionManager::MakcuConnectionManager(ConnectionPolicy policy)
    : policy_(std::move(policy)) {}

std::vector<uint8_t> MakcuConnectionManager::Exchange(IMakcuTransport& transport,
                                                       std::span<const uint8_t> request) const {
    if (!transport.purge() || !transport.writeRaw(request)) return {};
    return transport.readFor(policy_.handshakeTimeout, policy_.responseQuietPeriod);
}

ConnectionAttempt MakcuConnectionManager::Connect(IMakcuTransport& transport,
                                                   const std::string& port) const {
    ConnectionAttempt attempt;
    if (port.empty()) {
        attempt.error = "empty port";
        return attempt;
    }

    static constexpr std::array<uint8_t, 13> textProbe{
        'k','m','.','v','e','r','s','i','o','n','(',')','\r'};

    for (const uint32_t baudRate : policy_.baudRates) {
        transport.close();
        if (!transport.open(port, baudRate, false)) continue;

        const std::vector<uint8_t> textReply = Exchange(transport, textProbe);
        const std::string text(textReply.begin(), textReply.end());
        FirmwareDescriptor descriptor = MakcuFirmwareDetector::ParseTextHandshake(text, baudRate);
        attempt.firmware = descriptor;
        if (!descriptor.identity.empty())
            attempt.error = "unsupported MAKCU identity: " + descriptor.identity;

        if (descriptor.family == FirmwareFamily::V4) {
            const std::vector<uint8_t> versionReply = Exchange(
                transport, MakcuFirmwareDetector::V4Query(0x04));
            if (const auto version = MakcuFirmwareDetector::ParseV4FirmwareVersion(versionReply)) {
                descriptor.numericVersion = *version;
                descriptor.numericVersionKnown = true;
            }
            const std::vector<uint8_t> deviceReply = Exchange(
                transport, MakcuFirmwareDetector::V4Query(0x02));
            if (const auto kinds = MakcuFirmwareDetector::ParseV4DeviceKinds(deviceReply)) {
                descriptor.deviceKinds = *kinds;
                descriptor.deviceKindsKnown = true;
            }
            transport.setReaderMode(TransportReaderMode::Text);
            if (!transport.startListener()) {
                transport.close();
                attempt.error = "reader startup failed";
                continue;
            }
            attempt.connected = true;
            attempt.firmware = std::move(descriptor);
            return attempt;
        }

        if (descriptor.family == FirmwareFamily::LegacyText) {
            transport.setReaderMode(TransportReaderMode::Text);
            if (!transport.startListener()) {
                transport.close();
                attempt.error = "reader startup failed";
                continue;
            }
            attempt.connected = true;
            attempt.firmware = std::move(descriptor);
            return attempt;
        }

        const std::vector<uint8_t> legacyReply = Exchange(
            transport, MakcuFirmwareDetector::LegacyV2Query(0xBF));
        if (const auto version = MakcuFirmwareDetector::ParseLegacyV2Version(legacyReply)) {
            descriptor.family = FirmwareFamily::LegacyV2Binary;
            descriptor.identity = *version;
            descriptor.compatible = true;
            descriptor.rawResponse.assign(reinterpret_cast<const char*>(legacyReply.data()), legacyReply.size());
            uint32_t parsedVersion = 0;
            std::string_view versionView(*version);
            if (!versionView.empty() && (versionView.front() == 'V' || versionView.front() == 'v'))
                versionView.remove_prefix(1);
            const auto parsed = std::from_chars(versionView.data(), versionView.data() + versionView.size(), parsedVersion);
            if (parsed.ec == std::errc{} && parsed.ptr == versionView.data() + versionView.size()) {
                descriptor.numericVersion = parsedVersion;
                descriptor.numericVersionKnown = true;
            }
            transport.setReaderMode(TransportReaderMode::LegacyV2Binary);
            if (!transport.startListener()) {
                transport.close();
                attempt.error = "reader startup failed";
                continue;
            }
            attempt.connected = true;
            attempt.firmware = std::move(descriptor);
            return attempt;
        }
    }

    transport.close();
    if (attempt.error.empty()) attempt.error = "no compatible MAKCU handshake";
    return attempt;
}

}
