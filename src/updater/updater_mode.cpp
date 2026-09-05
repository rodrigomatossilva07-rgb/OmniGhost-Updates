#include "updater_mode.h"
#include "../app_version.h"
#include "../platform/app_paths.h"
#include "../platform/scope_exit.h"
#include "../platform/unique_handle.h"
#include "../platform/text_encoding.h"
#include "crypto.h"
#include "install_engine.h"
#include "installation_integrity.h"
#include "update_log.h"
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace OmniGhost::Update {
namespace {
fs::path HealthFile() { return Paths::Updates() / L"update-health.json"; }

void RecordHealth(const std::string& stage,
                  const std::string& installedVersion,
                  const std::string& targetVersion,
                  const std::string& packageSha256 = {},
                  const std::string& signerStatus = {},
                  const std::string& rollbackResult = {},
                  const std::string& detail = {}) {
    (void)WriteUpdateHealth(HealthFile(), UpdateHealthRecord{
        stage,
        installedVersion,
        targetVersion,
        packageSha256,
        signerStatus,
        rollbackResult,
        detail
    });
}

std::map<std::wstring, std::wstring> Arguments(int argc, wchar_t** argv) {
    std::map<std::wstring, std::wstring> values;
    for (int i = 2; i + 1 < argc; i += 2)
        if (std::wstring(argv[i]).rfind(L"--", 0) == 0) values[argv[i]] = argv[i + 1];
    return values;
}

std::optional<std::string> NarrowAscii(const std::wstring& value) {
    std::string result;
    result.reserve(value.size());
    for (const wchar_t character : value) {
        if (character < 0 || character > 0x7f)
            return std::nullopt;
        result.push_back(static_cast<char>(character));
    }
    return result;
}

bool AbsoluteSafe(const fs::path& path) {
    return path.is_absolute() && !path.empty() && path.wstring().find(L"..") == std::wstring::npos;
}

bool Descendant(const fs::path& child, const fs::path& parent) {
    std::error_code ec;
    const fs::path canonicalChild = fs::weakly_canonical(child, ec); if (ec) return false;
    const fs::path canonicalParent = fs::weakly_canonical(parent, ec); if (ec) return false;
    auto childPart = canonicalChild.begin();
    for (auto parentPart = canonicalParent.begin(); parentPart != canonicalParent.end(); ++parentPart, ++childPart)
        if (childPart == canonicalChild.end() || _wcsicmp(childPart->c_str(), parentPart->c_str()) != 0) return false;
    return true;
}

bool WaitForExit(DWORD pid, int seconds, std::string& error) {
    Platform::UniqueHandle process(OpenProcess(SYNCHRONIZE, FALSE, pid));
    if (!process) {
        if (GetLastError() == ERROR_INVALID_PARAMETER) return true;
        error = "Não foi possível aguardar pelo processo principal.";
        return false;
    }
    const DWORD result = WaitForSingleObject(process.get(), static_cast<DWORD>(seconds * 1000));
    if (result != WAIT_OBJECT_0) { error = "A aplicação principal não terminou dentro do tempo limite."; return false; }
    return true;
}

std::wstring Quote(const std::wstring& value) {
    std::wstring result = L"\"";
    for (wchar_t character : value) { if (character == L'\"') result += L'\\'; result += character; }
    return result + L"\"";
}

bool StartApplication(const fs::path& executable, const fs::path& confirmFile,
                      const std::wstring& token, const std::wstring& extraArguments,
                      DWORD& pid, std::string& error) {
    std::wstring command = Quote(executable.wstring()) + L" --update-confirm " + Quote(token) +
        L" --update-confirm-file " + Quote(confirmFile.wstring());
    if (!extraArguments.empty()) command += L" " + extraArguments;
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    Platform::UniqueProcessInformation process;
    std::vector<wchar_t> mutableCommand(command.begin(), command.end()); mutableCommand.push_back(L'\0');
    if (!CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr,
        executable.parent_path().c_str(), &startup, process.put())) {
        error = "A nova versão não conseguiu iniciar: " + WindowsError(GetLastError()); return false;
    }
    pid = process.process_id();
    return true;
}

std::string ExitCodeDescription(DWORD exitCode) {
    switch (exitCode) {
    case 0xC0000135u:
        return "STATUS_DLL_NOT_FOUND (dependencia DLL em falta no PC de destino)";
    case 0xC000007Bu:
        return "STATUS_INVALID_IMAGE_FORMAT (DLL/EXE de arquitetura ou formato incompatível)";
    case 0xC0000142u:
        return "STATUS_DLL_INIT_FAILED (uma DLL falhou durante a inicializacao)";
    default:
        return "exit_code=0x" + [] (DWORD value) {
            char buffer[16]{};
            sprintf_s(buffer, "%08lX", value);
            return std::string(buffer);
        }(exitCode);
    }
}

bool ConfirmStarted(DWORD pid, const fs::path& file, const std::wstring& token, int seconds, std::string& error) {
    for (int attempt = 0; attempt < seconds * 10; ++attempt) {
        std::wifstream stream(file);
        std::wstring value;
        std::getline(stream, value);
        if (value == token) {
            stream.close();
            std::error_code cleanupError;
            fs::remove(file, cleanupError); // one-shot confirmation
            return true;
        }
        Platform::UniqueHandle process(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
        if (!process) {
            error = "Nao foi possivel acompanhar o processo da nova versao: " + WindowsError(GetLastError());
            return false;
        }
        const bool exited = WaitForSingleObject(process.get(), 0) == WAIT_OBJECT_0;
        if (exited) {
            DWORD exitCode = 0;
            if (GetExitCodeProcess(process.get(), &exitCode))
                error = "A nova versao terminou antes de confirmar o arranque: " + ExitCodeDescription(exitCode);
            else
                error = "A nova versao terminou antes de confirmar o arranque.";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    error = "A nova versao permaneceu em execucao mas nao confirmou o arranque dentro de " +
        std::to_string(seconds) + " segundos.";
    return false;
}
}

std::optional<int> RunUpdaterModeIfRequested(int argc, wchar_t** argv) {
    if (argc < 2 || std::wstring(argv[1]) != L"--apply-update") return std::nullopt;

    // The updater runs from the same executable and shares the product's single
    // human-readable log. Do not create an updater.log sidecar.
    const fs::path log = Paths::Logs() / L"logs.txt";
    WriteLog(log, LogLevel::Info, Version, {}, "entry",
        "Pedido para iniciar o modo atualizador recebido.");
    auto reject = [&](int code, const char* stage, const std::string& detail) -> std::optional<int> {
        WriteLog(log, LogLevel::Error, Version, {}, stage,
            detail + " Código interno=" + std::to_string(code) + ".");
        RecordHealth(stage, Version, {}, {}, {}, {}, detail);
        return code;
    };

    Platform::UniqueHandle mutex(CreateMutexW(nullptr, TRUE, L"Local\\OmniGhostUpdater-6A670FC7"));
    if (!mutex)
        return reject(20, "mutex", "Não foi possível criar o bloqueio do atualizador.");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
        return reject(20, "mutex", "Já existe outro processo de atualização em execução.");
    const auto args = Arguments(argc, argv);
    const wchar_t* required[] = { L"--pid", L"--package", L"--target", L"--executable", L"--version",
        L"--backup", L"--sha256", L"--size" };
    for (const wchar_t* name : required) {
        if (!args.contains(name) || args.at(name).empty())
            return reject(21, "arguments", "Falta um argumento obrigatório no pedido de atualização.");
    }

    DWORD pid{}; std::uint64_t expectedSize{};
    try { pid = static_cast<DWORD>(std::stoul(args.at(L"--pid"))); expectedSize = std::stoull(args.at(L"--size")); }
    catch (const std::exception& ex) {
        std::cerr << "[Updater] Invalid PID or size argument: " << ex.what() << "\n";
        return reject(21, "arguments", "PID ou tamanho do pacote inválido.");
    }
    catch (...) {
        std::cerr << "[Updater] Invalid PID or size argument: unknown exception\n";
        return reject(21, "arguments", "PID ou tamanho do pacote inválido.");
    }
    const fs::path package = args.at(L"--package"), target = args.at(L"--target"), backup = args.at(L"--backup");
    const fs::path executableName = args.at(L"--executable");
    const auto targetVersionValue = NarrowAscii(args.at(L"--version"));
    const auto expectedHashValue = NarrowAscii(args.at(L"--sha256"));
    if (!targetVersionValue || !expectedHashValue)
        return reject(21, "arguments", "Versão ou SHA-256 contém caracteres inválidos.");
    const std::string& targetVersion = *targetVersionValue;
    const std::string& expectedHash = *expectedHashValue;
    std::string fromVersion = Version;
    std::string notesUrl;
    if (args.contains(L"--from-version")) {
        const auto value = NarrowAscii(args.at(L"--from-version"));
        if (!value || !SemVersion::Parse(*value))
            return reject(21, "arguments", "A versão de origem é inválida.");
        fromVersion = *value;
    }
    if (args.contains(L"--notes-url")) {
        const auto value = NarrowAscii(args.at(L"--notes-url"));
        if (!value || (value->rfind("https://", 0) != 0 && !value->empty()))
            return reject(21, "arguments", "O endereço das notas da atualização é inválido.");
        notesUrl = *value;
    }
    if (!AbsoluteSafe(package) || !AbsoluteSafe(target) || !AbsoluteSafe(backup) || executableName.is_absolute() ||
        executableName.has_parent_path() || _wcsicmp(executableName.c_str(), MainExecutable) != 0 ||
        _wcsicmp(package.extension().c_str(), L".zip") != 0 || target == backup || !fs::exists(target / executableName) ||
        !Descendant(package, Paths::Updates()) || !Descendant(backup.parent_path(), Paths::Backups()) ||
        !SemVersion::Parse(targetVersion) || expectedSize == 0 || expectedHash.size() != 64)
        return reject(22, "arguments", "Os caminhos ou metadados do pedido de atualização não são seguros/válidos.");

    WriteLog(log, LogLevel::Info, Version, targetVersion, "start", "Modo atualizador do OmniGhost iniciado.");
    std::string error, actualHash; std::error_code fileError;
    if (fs::file_size(package, fileError) != expectedSize || !Sha256File(package, actualHash, error) ||
        !ConstantTimeEquals(actualHash, expectedHash)) {
        WriteLog(log, LogLevel::Error, Version, targetVersion, "revalidate",
            error.empty() ? "Tamanho ou SHA-256 alterado antes da instalação." : error);
        RecordHealth("revalidate-failed", Version, targetVersion, expectedHash, {}, {},
            error.empty() ? "size or sha256 changed before install" : error);
        return 29;
    }
    RecordHealth("revalidated", Version, targetVersion, expectedHash, {}, {}, "package size and sha256 verified");
    int timeout = 30;
    if (args.contains(L"--timeout")) {
        try {
            timeout = (std::clamp)(std::stoi(args.at(L"--timeout")), 5, 120);
        } catch (const std::exception& exception) {
            WriteLog(log, LogLevel::Warning, Version, targetVersion, "arguments",
                     std::string("Timeout inválido; usado valor seguro: ") + exception.what());
        }
    }
    if (!WaitForExit(pid, timeout, error)) { WriteLog(log, LogLevel::Error, Version, targetVersion, "wait", error); return 23; }

    Configuration configuration{};
    std::wstring stagingToken;
    if (!RandomTokenHex(16, stagingToken, error)) {
        WriteLog(log, LogLevel::Error, Version, targetVersion, "staging-token", error);
        return 31;
    }
    const fs::path staging = package.parent_path() / (L"staging-" + stagingToken); ArchiveInfo archive{};
    if (!ExtractZipSecurely(package, staging, configuration, archive, error)) {
        WriteLog(log, LogLevel::Error, Version, targetVersion, "extract", error); return 24;
    }

    const IntegrityReport stagedIntegrity = VerifyInstallationFiles(staging);
    const bool singleExecutablePackage = archive.files.size() == 1 &&
        archive.files.front() == executableName;
    // A single-EXE package is already authenticated by the package SHA-256 and
    // the Authenticode check immediately below. It intentionally carries no
    // second file merely to describe the first one.
    if (stagedIntegrity.status != IntegrityStatus::Healthy &&
        !(singleExecutablePackage && stagedIntegrity.status == IntegrityStatus::ManifestMissing)) {
        WriteLog(log, LogLevel::Error, Version, targetVersion, "integrity", stagedIntegrity.message);
        RecordHealth("staged-integrity-failed", Version, targetVersion, expectedHash, {}, {}, stagedIntegrity.message);
        return 33;
    }

    if (configuration.requireAuthenticode) {
        const fs::path stagedExecutable = staging / executableName;
        bool trustedExecutable = VerifyAuthenticode(stagedExecutable, configuration.expectedPublisherSubject,
                                                    configuration.expectedCertificateThumbprint, error);
        if (!trustedExecutable && !configuration.secondaryCertificateThumbprint.empty()) {
            std::string secondaryError;
            trustedExecutable = VerifyAuthenticode(stagedExecutable, configuration.secondaryPublisherSubject,
                                                   configuration.secondaryCertificateThumbprint, secondaryError);
            if (!trustedExecutable) error += "; secondary publisher rejected the executable: " + secondaryError;
        }
        if (!trustedExecutable) {
            WriteLog(log, LogLevel::Error, Version, targetVersion, "authenticode", error);
            RecordHealth("new-executable-authenticode-failed", Version, targetVersion, expectedHash, "failed", {}, error);
            return 34;
        }
        RecordHealth("new-executable-authenticode-verified", Version, targetVersion, expectedHash, "verified");
    }

    InstallOptions install{staging, target, backup, executableName.wstring()};
    const InstallResult installed = InstallWithRollback(install);
    if (!installed.success) {
        WriteLog(log, LogLevel::Error, Version, targetVersion, "install", installed.error);
        RecordHealth("install-failed", Version, targetVersion, expectedHash, {},
            installed.rolledBack ? "success" : "not-completed", installed.error);
        if (installed.rolledBack) {
            std::wstring relaunchToken;
            std::string relaunchError;
            DWORD restoredPid{};
            if (RandomTokenHex(32, relaunchToken, relaunchError)) {
                const std::wstring rollbackArguments = L"--rollback-restored --failed-version " +
                    Quote(Platform::Utf8ToWide(targetVersion)) + L" --rollback-stage installation";
                (void)StartApplication(target / executableName, package.parent_path() / L"rollback-confirm.txt",
                    relaunchToken, rollbackArguments, restoredPid, relaunchError);
            }
        }
        return installed.rolledBack ? 25 : 26;
    }
    RecordHealth("installed-awaiting-restart", Version, targetVersion, expectedHash, {}, {},
        "files installed; awaiting startup confirmation");

    std::wstring token;
    if (!RandomTokenHex(32, token, error)) {
        WriteLog(log, LogLevel::Error, Version, targetVersion, "restart-token", error);
        return 30;
    }
    const fs::path confirmation = package.parent_path() / (L"startup-confirm-" + stagingToken + L".txt");
    std::error_code cleanupError; fs::remove(confirmation, cleanupError);
    {
        std::ofstream confirmationStream(confirmation, std::ios::binary | std::ios::trunc);
        if (!confirmationStream) {
            WriteLog(log, LogLevel::Error, Version, targetVersion, "confirmation-acl",
                "Não foi possível criar o ficheiro de confirmação protegido.");
            return 32;
        }
    }
    if (!RestrictPathToCurrentUser(confirmation, false, error)) {
        WriteLog(log, LogLevel::Error, Version, targetVersion, "confirmation-acl", error);
        fs::remove(confirmation, cleanupError);
        return 32;
    }
    const auto confirmationCleanup = Platform::MakeScopeExit([&confirmation] {
        std::error_code cleanup;
        fs::remove(confirmation, cleanup);
    });
    (void)confirmationCleanup;
    DWORD newPid{};
    std::wstring successArguments = L"--updated-from " + Quote(Platform::Utf8ToWide(fromVersion)) +
        L" --updated-to " + Quote(Platform::Utf8ToWide(targetVersion));
    if (!notesUrl.empty())
        successArguments += L" --update-notes-url " + Quote(Platform::Utf8ToWide(notesUrl));
    const bool applicationStarted = StartApplication(target / executableName, confirmation, token,
        successArguments, newPid, error);
    const bool startupConfirmed = applicationStarted &&
        ConfirmStarted(newPid, confirmation, token, configuration.startupConfirmationSeconds, error);
    if (!startupConfirmed) {
        if (newPid) {
            Platform::UniqueHandle process(OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, newPid));
            if (process) {
                TerminateProcess(process.get(), 1);
                (void)WaitForSingleObject(process.get(), 5000);
            }
        }
        std::string rollbackError; const bool rolledBack = RollbackInstallation(install, rollbackError); DWORD restoredPid{};
        if (rolledBack) {
            std::wstring rollbackToken;
            std::string tokenError;
            if (!RandomTokenHex(32, rollbackToken, tokenError)) rollbackToken = token;
            const std::wstring rollbackArguments = L"--rollback-restored --failed-version " +
                Quote(Platform::Utf8ToWide(targetVersion)) + L" --rollback-stage restart";
            StartApplication(target / executableName, confirmation, rollbackToken, rollbackArguments, restoredPid, rollbackError);
        }
        const std::string restartDetail = error.empty()
            ? "A nova versao nao confirmou o arranque."
            : error;
        WriteLog(log, LogLevel::Error, Version, targetVersion, "restart",
            rolledBack
                ? restartDetail + "; rollback concluido."
                : restartDetail + "; rollback falhou: " + rollbackError);
        RecordHealth("startup-confirmation-failed", Version, targetVersion, expectedHash, {},
            rolledBack ? "success" : "failed",
            rolledBack ? restartDetail + "; rollback completed"
                       : restartDetail + "; rollback failed: " + rollbackError);
        return rolledBack ? 27 : 28;
    }

    fs::remove(package, cleanupError); fs::remove_all(staging, cleanupError); fs::remove(confirmation, cleanupError);
    PruneBackups(backup.parent_path(), configuration.backupsToKeep);
    WriteLog(log, LogLevel::Info, targetVersion, targetVersion, "complete", "Atualização concluída e arranque confirmado.");
    RecordHealth("complete", targetVersion, targetVersion, expectedHash, {}, "not-required",
        "update completed and startup confirmed");
    // This process is the temporary copy of OmniGhost.exe. Windows removes it
    // on reboot if it cannot be cleaned during a later normal startup.
    MoveFileExW(Paths::Executable().c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    return 0;
}
}
