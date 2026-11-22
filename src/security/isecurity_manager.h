#pragma once

#include "platform/expected.h"
#include <string_view>

namespace OmniGhost::Security {

// =============================================================================
// ISecurityManager - Interface for security management
// =============================================================================
// Allows DI and testing of security functionality

class ISecurityManager {
public:
    enum class SecurityEventId : uint8_t {
        InstallManifestInvalid, InstallManifestMissing, RuntimeFileInvalid,
        SessionInvalid, SessionUnavailable, SessionRestored,
        DebuggerObserved, UnknownModuleObserved
    };
    enum class Severity : uint8_t { Info, Low, Medium, High, Critical };
    enum class Enforcement : uint8_t { None, Observe, Recheck, DenyFeatures };
    struct Assessment { SecurityEventId id; Severity severity; Enforcement enforcement; };

    virtual ~ISecurityManager() = default;

    // Verification methods
    [[nodiscard]] virtual Expected<bool> VerifyHostExecutablePresent() noexcept = 0;
    [[nodiscard]] virtual Expected<bool> VerifyStartup() noexcept = 0;
    [[nodiscard]] virtual Expected<bool> VerifyProtectedFiles() noexcept = 0;

    // Feature access
    [[nodiscard]] virtual bool CanUseProtectedFeatures() const noexcept = 0;

    // Event recording
    virtual void Record(SecurityEventId id) noexcept = 0;

    // Static classification
    [[nodiscard]] static Assessment Classify(SecurityEventId id) noexcept;
    [[nodiscard]] static std::string_view Code(SecurityEventId id) noexcept;

};

} // namespace OmniGhost::Security
