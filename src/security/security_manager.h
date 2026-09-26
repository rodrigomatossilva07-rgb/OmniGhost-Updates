#pragma once

#include <cstdint>
#include <string_view>

namespace OmniGhost::Security {

enum class Severity : std::uint8_t { Info, Low, Medium, High, Critical };
enum class Enforcement : std::uint8_t { None, Observe, Recheck, DenyFeatures };
enum class EventId : std::uint8_t {
    ReleaseTrustMissing, ExecutableSignatureInvalid, InstallManifestInvalid,
    RuntimeFileInvalid, SessionInvalid, SessionUnavailable, SessionRestored,
    SignatureVerificationUnavailable,
    DebuggerObserved, UnknownModuleObserved
};

struct Assessment {
    EventId id;
    Severity severity;
    Enforcement enforcement;
};

// No client-side permanent ban or administrative KeyAuth action is exposed.
class SecurityManager final {
public:
    static SecurityManager& Instance() noexcept;
    [[nodiscard]] bool VerifyHostIdentity() noexcept;
    [[nodiscard]] bool VerifyStartup() noexcept;
    [[nodiscard]] bool VerifyProtectedFiles() noexcept;
    [[nodiscard]] bool CanUseProtectedFeatures() const noexcept;
    void Record(EventId id) noexcept;
    [[nodiscard]] static Assessment Classify(EventId id) noexcept;
    [[nodiscard]] static std::string_view Code(EventId id) noexcept;

private:
    SecurityManager() = default;
    bool featuresDenied_{};
};

} // namespace OmniGhost::Security
