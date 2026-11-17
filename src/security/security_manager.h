#pragma once

#include "isecurity_manager.h"

namespace OmniGhost::Security {

// Keep existing types for backward compatibility
using Severity = ISecurityManager::Severity;
using Enforcement = ISecurityManager::Enforcement;
using EventId = ISecurityManager::SecurityEventId;
using Assessment = ISecurityManager::Assessment;

class SecurityManager final : public ISecurityManager {
public:
    static SecurityManager& Instance() noexcept;
    [[nodiscard]] Expected<bool> VerifyHostExecutablePresent() noexcept override;
    [[nodiscard]] Expected<bool> VerifyStartup() noexcept override;
    [[nodiscard]] Expected<bool> VerifyProtectedFiles() noexcept override;
    [[nodiscard]] bool CanUseProtectedFeatures() const noexcept override;
    void Record(EventId id) noexcept override;

    // Static methods (unchanged)
    [[nodiscard]] static Assessment Classify(EventId id) noexcept;
    [[nodiscard]] static std::string_view Code(EventId id) noexcept;

    // Backward compatibility - returns bool instead of Expected<bool>
    [[nodiscard]] bool VerifyHostExecutablePresentLegacy() noexcept;
    [[nodiscard]] bool VerifyStartupLegacy() noexcept;
    [[nodiscard]] bool VerifyProtectedFilesLegacy() noexcept;

private:
    SecurityManager() = default;
};

} // namespace OmniGhost::Security
