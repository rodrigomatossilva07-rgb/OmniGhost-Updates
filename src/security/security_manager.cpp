#include "security_manager.h"

#include "../platform/app_paths.h"
#include "../platform/file_integrity.h"
#include "../platform/session_log.h"
#include "../platform/text_encoding.h"
#include "../platform/runtime_bootstrap.h"
#include "../updater/installation_integrity.h"

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
    std::error_code error;
    if (!std::filesystem::is_regular_file(exePath, error) || error) return {};
    return exePath;
}
}

SecurityManager& SecurityManager::Instance() noexcept {
    static SecurityManager manager;
    return manager;
}

Assessment SecurityManager::Classify(EventId id) noexcept {
    switch (id) {
    case EventId::InstallManifestInvalid:
    case EventId::RuntimeFileInvalid:
        return {id, Severity::Critical, Enforcement::DenyFeatures};
    case EventId::SessionInvalid:
        return {id, Severity::High, Enforcement::DenyFeatures};
    case EventId::SessionUnavailable:
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
    case EventId::InstallManifestInvalid: return "SEC-003";
    case EventId::InstallManifestMissing: return "SEC-011";
    case EventId::RuntimeFileInvalid: return "SEC-004";
    case EventId::SessionInvalid: return "SEC-005";
    case EventId::SessionUnavailable: return "SEC-006";
    case EventId::SessionRestored: return "SEC-007";
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

    if (verifiedExe.empty()) {
        Record(EventId::RuntimeFileInvalid);
        return false;
    }
    return true;
}

bool SecurityManager::VerifyHostIdentity() noexcept {




    const auto verifiedExe = VerifiedExecutablePath();

    if (verifiedExe.empty()) {
        Record(EventId::RuntimeFileInvalid);
        return false;
    }
    return true;
}

bool SecurityManager::VerifyStartup() noexcept {





    return VerifyProtectedFiles();
}

}
