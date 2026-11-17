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
#include "platform/strprotect/strprotect.h"

namespace OmniGhost::Security {
namespace {
std::mutex eventMutex;
bool denied = false;
bool sessionDenied = false;

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
    case EventId::InstallManifestInvalid: return OGS_CSTR("SEC-003");
    case EventId::InstallManifestMissing: return OGS_CSTR("SEC-011");
    case EventId::RuntimeFileInvalid: return OGS_CSTR("SEC-004");
    case EventId::SessionInvalid: return OGS_CSTR("SEC-005");
    case EventId::SessionUnavailable: return OGS_CSTR("SEC-006");
    case EventId::SessionRestored: return OGS_CSTR("SEC-007");
    case EventId::DebuggerObserved: return OGS_CSTR("SEC-008");
    case EventId::UnknownModuleObserved: return OGS_CSTR("SEC-009");
    }
    return OGS_CSTR("SEC-000");
}

void SecurityManager::Record(EventId id) noexcept {
    const auto assessment = Classify(id);
    if (assessment.enforcement == Enforcement::DenyFeatures) {
        std::scoped_lock lock(eventMutex);
        if (id == EventId::SessionInvalid)
            sessionDenied = true;
        else
            denied = true;
    } else if (id == EventId::SessionRestored) {
        std::scoped_lock lock(eventMutex);
        sessionDenied = false;
    }
    OmniGhost::SessionLog::Write(
        assessment.severity == Severity::Critical ? OmniGhost::SessionLog::Severity::Critical
            : assessment.severity == Severity::High ? OmniGhost::SessionLog::Severity::Error
            : assessment.severity == Severity::Medium ? OmniGhost::SessionLog::Severity::Warning
            : OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Core, OGS_LIT("security event"),
        {{OGS_LIT("event_id"), std::string(Code(id)), false},
         {OGS_LIT("severity"), std::to_string(static_cast<int>(assessment.severity)), false},
         {OGS_LIT("enforcement"), std::to_string(static_cast<int>(assessment.enforcement)), false}});
}

bool SecurityManager::CanUseProtectedFeatures() const noexcept {
    std::scoped_lock lock(eventMutex);
    return !denied && !sessionDenied;
}

// Expected-based implementations
Expected<bool> SecurityManager::VerifyProtectedFiles() noexcept {
    try {
        for (const wchar_t* module : {OGS_WCSTR("libs/vmm.dll"), OGS_WCSTR("libs/leechcore.dll"), OGS_WCSTR("libs/FTD3XXWU.dll")}) {
            std::wstring error;
            if (!OmniGhost::RuntimeBootstrap::ValidatePrivateRuntimeFile(module, error)) {
                Record(EventId::RuntimeFileInvalid);
                return Failure<bool>(ErrorCode::RuntimeFileInvalid, "Runtime file validation failed");
            }
        }

        const auto verifiedExe = VerifiedExecutablePath();
        if (!verifiedExe.empty()) {
            const auto report = OmniGhost::Update::VerifyInstallationFiles(verifiedExe.parent_path());

            if (report.status == OmniGhost::Update::IntegrityStatus::Damaged) {
                Record(EventId::RuntimeFileInvalid);
                return Failure<bool>(ErrorCode::RuntimeFileInvalid, "Installation files damaged");
            }
            if (report.status == OmniGhost::Update::IntegrityStatus::Error) {
                Record(EventId::InstallManifestInvalid);
                return Failure<bool>(ErrorCode::InstallManifestInvalid, "Manifest validation error");
            }
            if (report.status == OmniGhost::Update::IntegrityStatus::ManifestMissing)
                Record(EventId::InstallManifestMissing);
        }

        if (verifiedExe.empty()) {
            Record(EventId::RuntimeFileInvalid);
            return Failure<bool>(ErrorCode::RuntimeFileInvalid, "Executable not found");
        }
        return Success(true);
    } catch (const std::exception& ex) {
        return Failure<bool>(ErrorCode::InternalError, ex.what());
    }
}

// Legacy compatibility
bool SecurityManager::VerifyProtectedFilesLegacy() noexcept {
    return VerifyProtectedFiles().value_or(false);
}

Expected<bool> SecurityManager::VerifyHostExecutablePresent() noexcept {
    try {
        const auto verifiedExe = VerifiedExecutablePath();

        if (verifiedExe.empty()) {
            Record(EventId::RuntimeFileInvalid);
            return Failure<bool>(ErrorCode::RuntimeFileInvalid, "Executable not found");
        }
        return Success(true);
    } catch (const std::exception& ex) {
        return Failure<bool>(ErrorCode::InternalError, ex.what());
    }
}

bool SecurityManager::VerifyHostExecutablePresentLegacy() noexcept {
    return VerifyHostExecutablePresent().value_or(false);
}

Expected<bool> SecurityManager::VerifyStartup() noexcept {
    try {
        return VerifyProtectedFiles();
    } catch (const std::exception& ex) {
        return Failure<bool>(ErrorCode::InternalError, ex.what());
    }
}

bool SecurityManager::VerifyStartupLegacy() noexcept {
    return VerifyStartup().value_or(false);
}

} // namespace OmniGhost::Security