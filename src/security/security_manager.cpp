#include "security_manager.h"

#include "../platform/app_paths.h"
#include "../platform/file_integrity.h"
#include "../platform/session_log.h"
#include "../platform/text_encoding.h"
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



std::filesystem::path VerifiedExecutablePath() {
    const auto exePath = OmniGhost::Paths::Executable();

#if defined(OMNIGHOST_PUBLISH_BUILD)

    if (!OmniGhost::Update::ReleaseTrust::Configured) {
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Error,
            OmniGhost::SessionLog::Subsystem::Core,
            "publisher identity unavailable: release trust anchor is not configured");
        return {};
    }
    std::string error;
    bool signedByPublisher = OmniGhost::Update::VerifyAuthenticode(
        exePath, OmniGhost::Update::ReleaseTrust::PublisherSubject,
        OmniGhost::Update::ReleaseTrust::CertificateThumbprint, error);
    bool revocationUnavailable = error.find("trust.revocation_offline") != std::string::npos;
    if (!signedByPublisher && OmniGhost::Update::ReleaseTrust::SecondaryConfigured) {
        signedByPublisher = OmniGhost::Update::VerifyAuthenticode(
            exePath, OmniGhost::Update::ReleaseTrust::SecondaryPublisherSubject,
            OmniGhost::Update::ReleaseTrust::SecondaryCertificateThumbprint, error);
        revocationUnavailable = revocationUnavailable ||
            error.find("trust.revocation_offline") != std::string::npos;
    }
    if (!signedByPublisher) {
        OmniGhost::SessionLog::Write(
            revocationUnavailable ? OmniGhost::SessionLog::Severity::Warning
                                  : OmniGhost::SessionLog::Severity::Error,
            OmniGhost::SessionLog::Subsystem::Core,
            revocationUnavailable
                ? "Authenticode verification unavailable (revocation offline); build requires publisher signature"
                : "Authenticode signature invalid or not from trusted publisher",
            {{"executable", OmniGhost::Platform::WideToUtf8(exePath.wstring())},
             {"error", error}});
        return {};
    }

    return exePath;
#else


    if (OmniGhost::Update::ReleaseTrust::Configured) {
        std::string error;
        bool signedByPublisher = OmniGhost::Update::VerifyAuthenticode(
            exePath, OmniGhost::Update::ReleaseTrust::PublisherSubject,
            OmniGhost::Update::ReleaseTrust::CertificateThumbprint, error);
        if (!signedByPublisher && OmniGhost::Update::ReleaseTrust::SecondaryConfigured) {
            signedByPublisher = OmniGhost::Update::VerifyAuthenticode(
                exePath, OmniGhost::Update::ReleaseTrust::SecondaryPublisherSubject,
                OmniGhost::Update::ReleaseTrust::SecondaryCertificateThumbprint, error);
        }
        if (!signedByPublisher) {
            OmniGhost::SessionLog::Write(
                OmniGhost::SessionLog::Severity::Warning,
                OmniGhost::SessionLog::Subsystem::Core,
                "executable not signed or signature invalid (dev/release build) - verify integrity manually",
                {{"executable", OmniGhost::Platform::WideToUtf8(exePath.wstring())},
                 {"error", error}});
        }
    }



    return exePath;
#endif
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
    case EventId::InstallManifestMissing:
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
    case EventId::InstallManifestMissing: return "SEC-011";
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



    for (const wchar_t* module : {L"libs/vmm.dll", L"libs/leechcore.dll", L"libs/FTD3XXWU.dll"}) {
        std::wstring error;
        if (!OmniGhost::RuntimeBootstrap::ValidatePrivateRuntimeFile(module, error)) {
            Record(EventId::RuntimeFileInvalid);
            return false;
        }
    }



    const auto verifiedExe = VerifiedExecutablePath();
    if (!verifiedExe.empty()) {
        const auto report = OmniGhost::Update::VerifyInstallationFiles(verifiedExe.parent_path());
        // UPD-002 Fix: a manifest-less single-EXE install is a warning (the
        // runtime-DLL SHA-256 gate above stays authoritative); a present-but-
        // mismatched manifest still fails closed.
        if (report.status == OmniGhost::Update::IntegrityStatus::Damaged) {
            Record(EventId::RuntimeFileInvalid);
            return false;
        }
        if (report.status == OmniGhost::Update::IntegrityStatus::Error) {
            Record(EventId::InstallManifestInvalid);
            return false;
        }
        if (report.status == OmniGhost::Update::IntegrityStatus::ManifestMissing)
            Record(EventId::InstallManifestMissing);
    }

#if defined(OMNIGHOST_PUBLISH_BUILD)



    if (verifiedExe.empty()) {
        Record(EventId::ReleaseTrustMissing);
        return false;
    }
#else



#endif
    return true;
}

bool SecurityManager::VerifyHostIdentity() noexcept {




    const auto verifiedExe = VerifiedExecutablePath();

#if defined(OMNIGHOST_PUBLISH_BUILD)

    if (verifiedExe.empty()) {

        Record(EventId::ReleaseTrustMissing);
        return false;
    }

    return true;
#else




    return true;
#endif
}

bool SecurityManager::VerifyStartup() noexcept {





    return VerifyProtectedFiles();
}

}
