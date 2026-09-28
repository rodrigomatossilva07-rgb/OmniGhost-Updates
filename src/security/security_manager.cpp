#include "security_manager.h"

#include "../platform/app_paths.h"
#include "../platform/file_integrity.h"
#include "../platform/session_log.h"
#include "../platform/runtime_bootstrap.h"
#include "../updater/crypto.h"
#include "../updater/installation_integrity.h"
#include "../updater/release_trust.h"

#include <Windows.h>
#include <filesystem>
#include <mutex>
#include <string>

namespace OmniGhost::Security {
namespace {
std::mutex eventMutex;
bool denied = false;

std::filesystem::path SignedHostPath() {
    const DWORD needed = GetEnvironmentVariableW(L"OMNIGHOST_HOST_EXE", nullptr, 0);
    if (needed < 2 || needed >= 32768) return {};
    std::wstring value(needed, L'\0');
    const DWORD size = GetEnvironmentVariableW(L"OMNIGHOST_HOST_EXE", value.data(), needed);
    if (size == 0 || size >= needed) return {};
    value.resize(size);
    return std::filesystem::path(value);
}

bool CoreMatchesSignedHost(const std::filesystem::path& host) {
    // Build-BootstrapPayload.ps1 reserves RCDATA 3001 for the core payload.
    HMODULE mapped = LoadLibraryExW(host.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (!mapped) return false;
    const HRSRC resource = FindResourceW(mapped, MAKEINTRESOURCEW(3001), RT_RCDATA);
    if (!resource) { FreeLibrary(mapped); return false; }
    const HGLOBAL loaded = LoadResource(mapped, resource);
    const void* bytes = loaded ? LockResource(loaded) : nullptr;
    const DWORD size = SizeofResource(mapped, resource);
    if (!bytes || size == 0) { FreeLibrary(mapped); return false; }
    std::string embeddedHash, ownHash, error;
    const bool valid = OmniGhost::Platform::Sha256Text(
        std::string_view(static_cast<const char*>(bytes), size), embeddedHash, error) &&
        OmniGhost::Platform::Sha256File(OmniGhost::Paths::Executable(), ownHash, error) &&
        OmniGhost::Platform::ConstantTimeEquals(embeddedHash, ownHash);
    FreeLibrary(mapped);
    return valid;
}
}

SecurityManager& SecurityManager::Instance() noexcept {
    static SecurityManager manager;
    return manager;
}

Assessment SecurityManager::Classify(EventId id) noexcept {
    switch (id) {
    case EventId::ReleaseTrustMissing:
    case EventId::ExecutableSignatureInvalid:
    case EventId::InstallManifestInvalid:
    case EventId::RuntimeFileInvalid:
        return {id, Severity::Critical, Enforcement::DenyFeatures};
    case EventId::SessionInvalid:
        return {id, Severity::High, Enforcement::DenyFeatures};
    case EventId::SessionUnavailable:
    case EventId::SignatureVerificationUnavailable:
        return {id, Severity::Medium, Enforcement::Recheck};
    case EventId::SessionRestored:
        return {id, Severity::Info, Enforcement::None};
    case EventId::DebuggerObserved:
    case EventId::UnknownModuleObserved:
        return {id, Severity::Low, Enforcement::Observe};
    }
    return {id, Severity::Info, Enforcement::None};
}

std::string_view SecurityManager::Code(EventId id) noexcept {
    switch (id) {
    case EventId::ReleaseTrustMissing: return "SEC-001";
    case EventId::ExecutableSignatureInvalid: return "SEC-002";
    case EventId::InstallManifestInvalid: return "SEC-003";
    case EventId::RuntimeFileInvalid: return "SEC-004";
    case EventId::SessionInvalid: return "SEC-005";
    case EventId::SessionUnavailable: return "SEC-006";
    case EventId::SessionRestored: return "SEC-007";
    case EventId::SignatureVerificationUnavailable: return "SEC-010";
    case EventId::DebuggerObserved: return "SEC-008";
    case EventId::UnknownModuleObserved: return "SEC-009";
    }
    return "SEC-000";
}

void SecurityManager::Record(EventId id) noexcept {
    const auto assessment = Classify(id);
    if (assessment.enforcement == Enforcement::DenyFeatures &&
        id != EventId::SessionInvalid) {
        std::scoped_lock lock(eventMutex);
        denied = true;
    }
    OmniGhost::SessionLog::Write(
        assessment.severity == Severity::Critical ? OmniGhost::SessionLog::Severity::Critical
            : assessment.severity == Severity::High ? OmniGhost::SessionLog::Severity::Error
            : assessment.severity == Severity::Medium ? OmniGhost::SessionLog::Severity::Warning
            : OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Core, "security event",
        {{"event_id", std::string(Code(id)), false},
         {"severity", std::to_string(static_cast<int>(assessment.severity)), false},
         {"enforcement", std::to_string(static_cast<int>(assessment.enforcement)), false}});
}

bool SecurityManager::CanUseProtectedFeatures() const noexcept {
    std::scoped_lock lock(eventMutex);
    return !denied;
}

bool SecurityManager::VerifyProtectedFiles() noexcept {
#if defined(OMNIGHOST_PUBLISH_BUILD)
    if (!OmniGhost::Update::ReleaseTrust::Configured) {
        return true;
    }
    const auto host = SignedHostPath();
    if (host.empty()) {
        Record(EventId::ReleaseTrustMissing);
        return false;
    }
    const auto report = OmniGhost::Update::VerifyInstallationFiles(host.parent_path());
    if (report.status != OmniGhost::Update::IntegrityStatus::Healthy) {
        Record(report.status == OmniGhost::Update::IntegrityStatus::Damaged
            ? EventId::RuntimeFileInvalid : EventId::InstallManifestInvalid);
        return false;
    }
    // These are the only native modules required for the DMA backend. The
    // bootstrap manifest pins their bytes to hashes embedded in the signed EXE.
    for (const wchar_t* module : {L"libs/vmm.dll", L"libs/leechcore.dll", L"libs/FTD3XXWU.dll"}) {
        std::wstring error;
        if (!OmniGhost::RuntimeBootstrap::ValidatePrivateRuntimeFile(module, error)) {
            Record(EventId::RuntimeFileInvalid);
            return false;
        }
    }
#endif
    return true;
}

bool SecurityManager::VerifyHostIdentity() noexcept {
#if defined(OMNIGHOST_PUBLISH_BUILD)
    if (!OmniGhost::Update::ReleaseTrust::Configured) {
        // Unsigned distribution is an explicit supported release policy
        // (release-publish.json: requireAuthenticode=false). Do not reject the
        // official launcher merely because no certificate anchor was embedded.
        // When a trust anchor is configured, all checks below remain mandatory.
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Info,
            OmniGhost::SessionLog::Subsystem::Core,
            "publisher identity check not required by release policy");
        return true;
    }
    const auto host = SignedHostPath();
    if (host.empty()) {
        Record(EventId::ReleaseTrustMissing);
        return false;
    }
    std::string error;
    bool signedByPublisher = OmniGhost::Update::VerifyAuthenticode(
        host, OmniGhost::Update::ReleaseTrust::PublisherSubject,
        OmniGhost::Update::ReleaseTrust::CertificateThumbprint, error);
    bool revocationUnavailable = error.find("trust.revocation_offline") != std::string::npos;
    if (!signedByPublisher && OmniGhost::Update::ReleaseTrust::SecondaryConfigured) {
        signedByPublisher = OmniGhost::Update::VerifyAuthenticode(
            host, OmniGhost::Update::ReleaseTrust::SecondaryPublisherSubject,
            OmniGhost::Update::ReleaseTrust::SecondaryCertificateThumbprint, error);
        revocationUnavailable = revocationUnavailable ||
            error.find("trust.revocation_offline") != std::string::npos;
    }
    if (!signedByPublisher) {
        Record(revocationUnavailable ? EventId::SignatureVerificationUnavailable
                                     : EventId::ExecutableSignatureInvalid);
        return false;
    }
    if (!CoreMatchesSignedHost(host)) {
        Record(EventId::RuntimeFileInvalid);
        return false;
    }
#endif
    return true;
}

bool SecurityManager::VerifyStartup() noexcept {
    // The signed host/payload check runs before RuntimeBootstrap::Prepare(),
    // so native DLLs are never loaded before their parent EXE is trusted.
    return VerifyProtectedFiles();
}

} // namespace OmniGhost::Security
