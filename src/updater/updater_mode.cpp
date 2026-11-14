#include "updater_mode.h"
#include "../app_version.h"
#include "../platform/app_paths.h"
#include "../platform/scope_exit.h"
#include "../platform/unique_handle.h"
#include "../platform/text_encoding.h"
#include "crypto.h"
#include "http_client.h"
#include "install_engine.h"
#include "installation_integrity.h"
#include "update_log.h"
#include "update_types.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>
#include "platform/strprotect/strprotect.h"

namespace fs = std::filesystem;

namespace OmniGhost::Update {
namespace {
fs::path HealthFile() { return Paths::Updates() / OGS_WIDE("update-health.json"); }

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



std::string ReadTrimmedTextFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return {};
    std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    while (!contents.empty() && (contents.back() == '\n' || contents.back() == '\r' ||
                                 contents.back() == ' ' || contents.back() == '\t'))
        contents.pop_back();
    return contents;
}



fs::path ResolveRollbackBackup(const fs::path& target, const fs::path& backupsDirectory,
                               const std::wstring& backupFileName) {
    std::error_code ec;
    const fs::path preferred = backupsDirectory / backupFileName;
    if (fs::is_regular_file(preferred, ec) && !ec) return preferred;
    ec.clear();
    const fs::path legacy = target / backupFileName;
    if (fs::is_regular_file(legacy, ec) && !ec) return legacy;
    return preferred;
}

void RemoveRollbackBackupFiles(const fs::path& target, const fs::path& backupsDirectory,
                               const std::wstring& executableName) {
    const std::wstring backupFileName = executableName + std::wstring(OGS_WIDE(".previous"));
    std::error_code ignored;
    fs::remove(backupsDirectory / backupFileName, ignored);
    fs::remove(backupsDirectory / (backupFileName + OGS_WIDE(".sha256")), ignored);
    fs::remove(target / backupFileName, ignored);
    fs::remove(target / (backupFileName + OGS_WIDE(".sha256")), ignored);
    ignored.clear();
}

bool WaitForExit(DWORD pid, int seconds, std::string& error) {
    Platform::UniqueHandle process(OpenProcess(SYNCHRONIZE, FALSE, pid));
    if (!process) {
        if (GetLastError() == ERROR_INVALID_PARAMETER) return true;
        error = OGS_LIT("Não foi possível aguardar pelo processo principal.");
        return false;
    }
    const DWORD result = WaitForSingleObject(process.get(), static_cast<DWORD>(seconds * 1000));
    if (result != WAIT_OBJECT_0) { error = OGS_LIT("A aplicação principal não terminou dentro do tempo limite."); return false; }
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
    std::wstring command = Quote(executable.wstring()) + OGS_WIDE(" --update-confirm ") + Quote(token) +
        OGS_WIDE(" --update-confirm-file ") + Quote(confirmFile.wstring());
    if (!extraArguments.empty()) command += L" " + extraArguments;
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    Platform::UniqueProcessInformation process;
    std::vector<wchar_t> mutableCommand(command.begin(), command.end()); mutableCommand.push_back(L'\0');
    if (!CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr,
        executable.parent_path().c_str(), &startup, process.put())) {
        error = OGS_LIT("A nova versão não conseguiu iniciar: ") + WindowsError(GetLastError()); return false;
    }
    pid = process.process_id();
    return true;
}

std::string ExitCodeDescription(DWORD exitCode) {
    switch (exitCode) {
    case 0xC0000135u:
        return OGS_LIT("STATUS_DLL_NOT_FOUND (dependencia DLL em falta no PC de destino)");
    case 0xC000007Bu:
        return OGS_LIT("STATUS_INVALID_IMAGE_FORMAT (DLL/EXE de arquitetura ou formato incompatível)");
    case 0xC0000142u:
        return OGS_LIT("STATUS_DLL_INIT_FAILED (uma DLL falhou durante a inicializacao)");
    default:
        return OGS_LIT("exit_code=0x") + [] (DWORD value) {
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
            fs::remove(file, cleanupError);
            return true;
        }
        Platform::UniqueHandle process(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
        if (!process) {
            error = OGS_LIT("Nao foi possivel acompanhar o processo da nova versao: ") + WindowsError(GetLastError());
            return false;
        }
        const bool exited = WaitForSingleObject(process.get(), 0) == WAIT_OBJECT_0;
        if (exited) {
            DWORD exitCode = 0;
            if (GetExitCodeProcess(process.get(), &exitCode))
                error = OGS_LIT("A nova versao terminou antes de confirmar o arranque: ") + ExitCodeDescription(exitCode);
            else
                error = OGS_LIT("A nova versao terminou antes de confirmar o arranque.");
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    error = OGS_LIT("A nova versao permaneceu em execucao mas nao confirmou o arranque dentro de ") +
        std::to_string(seconds) + OGS_LIT(" segundos.");
    return false;
}

}

std::optional<int> RunUpdaterModeIfRequested(int argc, wchar_t** argv) {
    if (argc < 2 || std::wstring(argv[1]) != OGS_WIDE("--apply-update")) return std::nullopt;



    const fs::path log = Paths::Logs() / L"logs.txt";
    WriteLog(log, LogLevel::Info, Version, {}, OGS_LIT("entry"),
        OGS_LIT("Pedido para iniciar o modo atualizador recebido."));
    const auto args = Arguments(argc, argv);

    Platform::UniqueHandle mutex(CreateMutexW(nullptr, TRUE, OGS_WCSTR("Local\\OmniGhostUpdater-6A670FC7")));
    auto reject = [&](int code, const std::string& stage, const std::string& detail) -> std::optional<int> {
        WriteLog(log, LogLevel::Error, Version, {}, stage,
            detail + OGS_LIT(" Código interno=") + std::to_string(code) + ".");
        RecordHealth(stage, Version, {}, {}, {}, {}, detail);
        const std::wstring message = Platform::Utf8ToWide(detail +
            OGS_LIT("\n\nEtapa: ") + stage + OGS_LIT(" · Código interno: ") + std::to_string(code));
        MessageBoxW(nullptr, message.c_str(), OGS_WCSTR("OmniGhost \u2014 Atualiza\u00e7\u00e3o"), MB_OK | MB_ICONERROR);
        mutex.reset();
        return code;
    };
    if (!mutex)
        return reject(20, OGS_LIT("mutex"), OGS_LIT("Não foi possível criar o bloqueio do atualizador."));
    if (GetLastError() == ERROR_ALREADY_EXISTS)
        return reject(20, OGS_LIT("mutex"), OGS_LIT("Já existe outro processo de atualização em execução."));
    const wchar_t* required[] = { OGS_WCSTR("--pid"), OGS_WCSTR("--package"), OGS_WCSTR("--package-url"), OGS_WCSTR("--updates-root"),
        OGS_WCSTR("--target"), OGS_WCSTR("--executable"), OGS_WCSTR("--version"), OGS_WCSTR("--sha256"), OGS_WCSTR("--size") };
    for (const wchar_t* name : required) {
        if (!args.contains(name) || args.at(name).empty())
            return reject(21, OGS_LIT("arguments"), OGS_LIT("Falta um argumento obrigatório no pedido de atualização."));
    }

    DWORD pid{}; std::uint64_t expectedSize{};
    try { pid = static_cast<DWORD>(std::stoul(args.at(OGS_WIDE("--pid")))); expectedSize = std::stoull(args.at(OGS_WIDE("--size"))); }
    catch (const std::exception& ex) {
        std::cerr << OGS_LIT("[Updater] Invalid PID or size argument: ") << ex.what() << "\n";
        return reject(21, OGS_LIT("arguments"), OGS_LIT("PID ou tamanho do pacote inválido."));
    }
    catch (...) {
        std::cerr << OGS_LIT("[Updater] Invalid PID or size argument: unknown exception\n");
        return reject(21, OGS_LIT("arguments"), OGS_LIT("PID ou tamanho do pacote inválido."));
    }
	const fs::path package = args.at(OGS_WIDE("--package")), updatesRoot = args.at(OGS_WIDE("--updates-root")), target = args.at(OGS_WIDE("--target"));
    const fs::path executableName = args.at(OGS_WIDE("--executable"));
    const auto targetVersionValue = NarrowAscii(args.at(OGS_WIDE("--version")));
    const auto expectedHashValue = NarrowAscii(args.at(OGS_WIDE("--sha256")));
    const auto packageUrlValue = NarrowAscii(args.at(OGS_WIDE("--package-url")));
    if (!targetVersionValue || !expectedHashValue || !packageUrlValue)
        return reject(21, OGS_LIT("arguments"), OGS_LIT("Versão ou SHA-256 contém caracteres inválidos."));
    const std::string& targetVersion = *targetVersionValue;
    const std::string& expectedHash = *expectedHashValue;
    const std::string& packageUrl = *packageUrlValue;
    std::string fromVersion = Version;
    std::string notesUrl;
    if (args.contains(OGS_WIDE("--from-version"))) {
        const auto value = NarrowAscii(args.at(OGS_WIDE("--from-version")));
        if (!value || !SemVersion::Parse(*value))
            return reject(21, OGS_LIT("arguments"), OGS_LIT("A versão de origem é inválida."));
        fromVersion = *value;
    }
    if (args.contains(OGS_WIDE("--notes-url"))) {
        const auto value = NarrowAscii(args.at(OGS_WIDE("--notes-url")));
        if (!value || (value->rfind(OGS_LIT("https://"), 0) != 0 && !value->empty()))
            return reject(21, OGS_LIT("arguments"), OGS_LIT("O endereço das notas da atualização é inválido."));
        notesUrl = *value;
    }
	if (!AbsoluteSafe(package) || !AbsoluteSafe(updatesRoot) || !AbsoluteSafe(target) || executableName.is_absolute() ||
        executableName.has_parent_path() || _wcsicmp(executableName.c_str(), MainExecutable) != 0 ||
		_wcsicmp(package.extension().c_str(), OGS_WCSTR(".zip")) != 0 || !fs::exists(target / executableName) ||
		!Descendant(updatesRoot, Paths::RuntimeRoot()) || !Descendant(package, updatesRoot) ||
        !SemVersion::Parse(targetVersion) || expectedSize == 0 || expectedHash.size() != 64 ||
        packageUrl.rfind(Configuration{}.allowedPackageUrlPrefix, 0) != 0)
        return reject(22, OGS_LIT("arguments"), OGS_LIT("Os caminhos ou metadados do pedido de atualização não são seguros/válidos."));

    WriteLog(log, LogLevel::Info, Version, targetVersion, OGS_LIT("start"), OGS_LIT("Modo atualizador do OmniGhost iniciado."));
    std::string error, actualHash;








    if (!fs::exists(package)) {
        const fs::path partial = package.wstring() + OGS_WIDE(".partial");
        std::error_code downloadCleanup;
        fs::remove(partial, downloadCleanup);
        std::atomic_bool cancelled{false};
        WinHttpClient downloadClient;
        const auto transferStarted = std::chrono::steady_clock::now();
        HttpResult download = downloadClient.Download(packageUrl, partial, expectedSize,
            Configuration{}.maximumPackageBytes, 30000, cancelled, {});
        if (!download.error.empty()) {
            fs::remove(partial, downloadCleanup);
            return reject(34, OGS_LIT("download"), download.error);
        }
        fs::rename(partial, package, downloadCleanup);
        if (downloadCleanup) {
            fs::remove(partial, downloadCleanup);
            return reject(34, OGS_LIT("download"), OGS_LIT("Não foi possível preparar o pacote transferido."));
        }
        const auto transferDuration = std::chrono::steady_clock::now() - transferStarted;
        (void)transferDuration;
    }

    Platform::UniqueHandle packageLock(CreateFileW(package.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!packageLock) {
        WriteLog(log, LogLevel::Error, Version, targetVersion, OGS_LIT("revalidate"),
            OGS_LIT("Não foi possível abrir o pacote em modo exclusivo de leitura (partilha de escrita negada)."));
        RecordHealth(OGS_LIT("revalidate-failed"), Version, targetVersion, expectedHash, {}, {},
            OGS_LIT("package could not be locked for verification"));
        return reject(29, OGS_LIT("revalidate"), OGS_LIT("Não foi possível bloquear o pacote para verificação."));
    }
    LARGE_INTEGER pinnedSize{};
    if (!GetFileSizeEx(packageLock.get(), &pinnedSize) ||
        static_cast<std::uint64_t>(pinnedSize.QuadPart) != expectedSize ||
        !Sha256File(package, actualHash, error) ||
        !ConstantTimeEquals(actualHash, expectedHash)) {
        WriteLog(log, LogLevel::Error, Version, targetVersion, OGS_LIT("revalidate"),
            error.empty() ? OGS_LIT("Tamanho ou SHA-256 alterado antes da instalação.") : error);
        RecordHealth(OGS_LIT("revalidate-failed"), Version, targetVersion, expectedHash, {}, {},
            error.empty() ? OGS_LIT("size or sha256 changed before install") : error);
        return reject(29, OGS_LIT("revalidate"), error.empty()
            ? OGS_LIT("O tamanho ou SHA-256 do pacote foi alterado antes da instalação.") : error);
    }
    RecordHealth(OGS_LIT("revalidated"), Version, targetVersion, expectedHash, {}, {},
        OGS_LIT("package size and sha256 verified (write-locked during install)"));

    {
        std::atomic_bool cancelled{false};
        WinHttpClient verifyClient;
        const int verifyTimeoutMs = 20000;
        const std::string verifyManifestUrl = Configuration{}.allowedPackageUrlPrefix +
            "v" + targetVersion + OGS_LIT("/update.json");
        HttpResult manifestResponse = verifyClient.GetText(
            verifyManifestUrl, verifyTimeoutMs, cancelled);
        if (!manifestResponse.error.empty())
            return reject(30, OGS_LIT("official-verify"),
                OGS_LIT("Não foi possível obter o manifesto oficial: ") + manifestResponse.error);
        ManifestResult parsed = ParseAndValidateManifest(
            manifestResponse.body, Configuration{}, "x64");
        if (!parsed.manifest)
            return reject(30, OGS_LIT("official-verify"),
                OGS_LIT("O manifesto oficial é inválido: ") + parsed.error);
        const Package* official = SelectPackage(*parsed.manifest, "x64");
        if (!official)
            return reject(30, OGS_LIT("official-verify"),
                OGS_LIT("O manifesto oficial não contém um pacote x64 utilizável."));
        if (parsed.manifest->version.original != targetVersion ||
            !ConstantTimeEquals(official->sha256, expectedHash) ||
            official->size != expectedSize ||
            official->fileName != package.filename().string()) {
            return reject(30, OGS_LIT("official-verify"),
                OGS_LIT("O pacote pedido não corresponde à versão oficial publicada."));
        }
        RecordHealth(OGS_LIT("official-verified"), Version, targetVersion, expectedHash, {}, {},
            OGS_LIT("package matches the officially published manifest"));
    }

    int timeout = 30;
    if (args.contains(OGS_WIDE("--timeout"))) {
        try {
            timeout = (std::clamp)(std::stoi(args.at(OGS_WIDE("--timeout"))), 5, 120);
        } catch (const std::exception& exception) {
            WriteLog(log, LogLevel::Warning, Version, targetVersion, OGS_LIT("arguments"),
                     std::string(OGS_LIT("Timeout inválido; usado valor seguro: ")) + exception.what());
        }
    }
    if (!WaitForExit(pid, timeout, error)) {
        WriteLog(log, LogLevel::Error, Version, targetVersion, OGS_LIT("wait"), error);
        return reject(23, OGS_LIT("wait"), error);
    }

    Configuration configuration{};
    std::wstring stagingToken;
    if (!RandomTokenHex(16, stagingToken, error)) {
        WriteLog(log, LogLevel::Error, Version, targetVersion, OGS_LIT("staging-token"), error);
        return reject(31, OGS_LIT("staging-token"), error);
    }
    const fs::path staging = package.parent_path() / (OGS_WIDE("staging-") + stagingToken); ArchiveInfo archive{};
    if (!ExtractZipSecurely(package, staging, configuration, archive, error)) {
        WriteLog(log, LogLevel::Error, Version, targetVersion, OGS_LIT("extract"), error);
        return reject(24, OGS_LIT("extract"), error);
    }





    packageLock.reset();

    const IntegrityReport stagedIntegrity = VerifyInstallationFiles(staging);
    const bool singleExecutablePackage = archive.files.size() == 1 &&
        archive.files.front() == executableName;



    if (stagedIntegrity.status != IntegrityStatus::Healthy &&
        !(singleExecutablePackage && stagedIntegrity.status == IntegrityStatus::ManifestMissing)) {
        WriteLog(log, LogLevel::Error, Version, targetVersion, OGS_LIT("integrity"), stagedIntegrity.message);
        RecordHealth(OGS_LIT("staged-integrity-failed"), Version, targetVersion, expectedHash, {}, {}, stagedIntegrity.message);
        return reject(33, OGS_LIT("integrity"), stagedIntegrity.message);
    }




    const fs::path backupsDirectory = Paths::Backups();
    std::error_code backupsDirectoryError;
    fs::create_directories(backupsDirectory, backupsDirectoryError);
    backupsDirectoryError.clear();
    std::string stagedExecutableHash, stagedExecutableHashError;
    if (!Sha256File(staging / executableName, stagedExecutableHash, stagedExecutableHashError)) {
        WriteLog(log, LogLevel::Error, Version, targetVersion, OGS_LIT("install"),
            stagedExecutableHashError.empty()
                ? OGS_LIT("Não foi possível calcular o SHA-256 do executável extraído.")
                : stagedExecutableHashError);
        RecordHealth(OGS_LIT("install-failed"), Version, targetVersion, expectedHash, {}, OGS_LIT("disabled-single-exe"),
            OGS_LIT("staged executable hash unavailable"));
        return reject(26, OGS_LIT("install"), stagedExecutableHashError.empty()
            ? OGS_LIT("Não foi possível validar o executável extraído.") : stagedExecutableHashError);
    }
	InstallOptions install;
	install.staging = staging;
	install.target = target;
	install.executableName = executableName.wstring();
	install.backupDirectory = backupsDirectory;
	install.expectedExecutableSha256 = stagedExecutableHash;
	const InstallResult installed = InstallSingleExecutable(install);
    if (!installed.success) {
        WriteLog(log, LogLevel::Error, Version, targetVersion, OGS_LIT("install"), installed.error);
        RecordHealth(OGS_LIT("install-failed"), Version, targetVersion, expectedHash, {},
			OGS_LIT("disabled-single-exe"), installed.error);
		return reject(26, OGS_LIT("install"), installed.error);
    }
    RecordHealth(OGS_LIT("installed-awaiting-restart"), Version, targetVersion, expectedHash, {}, {},
        OGS_LIT("files installed; awaiting startup confirmation"));

    std::wstring token;
    if (!RandomTokenHex(32, token, error)) {
        WriteLog(log, LogLevel::Error, Version, targetVersion, OGS_LIT("restart-token"), error);
        return reject(30, OGS_LIT("restart-token"), error);
    }
    const fs::path confirmation = package.parent_path() / (OGS_WIDE("startup-confirm-") + stagingToken + OGS_WIDE(".txt"));
    std::error_code cleanupError; fs::remove(confirmation, cleanupError);
    {
        std::ofstream confirmationStream(confirmation, std::ios::binary | std::ios::trunc);
        if (!confirmationStream) {
            WriteLog(log, LogLevel::Error, Version, targetVersion, OGS_LIT("confirmation-acl"),
                OGS_LIT("Não foi possível criar o ficheiro de confirmação protegido."));
            return reject(32, OGS_LIT("confirmation-acl"),
                OGS_LIT("Não foi possível criar o ficheiro protegido de confirmação de arranque."));
        }
    }
    if (!RestrictPathToCurrentUser(confirmation, false, error)) {
        WriteLog(log, LogLevel::Error, Version, targetVersion, OGS_LIT("confirmation-acl"), error);
        fs::remove(confirmation, cleanupError);
        return reject(32, OGS_LIT("confirmation-acl"), error);
    }
    const auto confirmationCleanup = Platform::MakeScopeExit([&confirmation] {
        std::error_code cleanup;
        fs::remove(confirmation, cleanup);
    });
    (void)confirmationCleanup;
    DWORD newPid{};
    std::wstring successArguments = OGS_WIDE("--updated-from ") + Quote(Platform::Utf8ToWide(fromVersion)) +
        OGS_WIDE(" --updated-to ") + Quote(Platform::Utf8ToWide(targetVersion));
    if (!notesUrl.empty())
        successArguments += OGS_WIDE(" --update-notes-url ") + Quote(Platform::Utf8ToWide(notesUrl));
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
        const std::string restartDetail = error.empty()
            ? OGS_LIT("A nova versao nao confirmou o arranque.")
            : error;


        const fs::path backupExe = ResolveRollbackBackup(target, backupsDirectory,
            executableName.wstring() + std::wstring(OGS_WIDE(".previous")));
        std::error_code rollbackError;
        bool rolledBack = false;
        std::string rollbackDetail;
        if (fs::exists(backupExe, rollbackError)) {






            const fs::path backupHashSidecar = backupExe.parent_path() /
                (executableName.wstring() + std::wstring(OGS_WIDE(".previous.sha256")));
            const std::string expectedBackupHash = ReadTrimmedTextFile(backupHashSidecar);




            bool backupVerified = false;
            if (expectedBackupHash.size() != 64) {
                rollbackDetail = OGS_LIT("rollback blocked: backup hash unavailable");
            } else {
                std::string actualBackupHash, hashError;
                backupVerified = Sha256File(backupExe, actualBackupHash, hashError) &&
                                ConstantTimeEquals(actualBackupHash, expectedBackupHash);
                if (!backupVerified)
                    rollbackDetail = OGS_LIT("rollback blocked: OmniGhost.exe.previous SHA-256 mismatch");
            }
            if (backupVerified) {
                fs::copy_file(backupExe, target / executableName,
                    fs::copy_options::overwrite_existing, rollbackError);
                rolledBack = !rollbackError;
                if (rolledBack) {

                    RemoveRollbackBackupFiles(target, backupsDirectory, executableName.wstring());
                }
            }
        }
        WriteLog(log, LogLevel::Error, Version, targetVersion, OGS_LIT("restart"),
            restartDetail + (rolledBack
                ? OGS_LIT("; rollback executado — versao anterior restaurada.")
                : (rollbackDetail.empty()
                    ? OGS_LIT("; rollback indisponivel — a versao instalada e mantida.")
                    : "; " + rollbackDetail + OGS_LIT(" — a versao instalada e mantida."))));
        RecordHealth(OGS_LIT("startup-confirmation-failed"), Version, targetVersion, expectedHash, {},
            rolledBack ? OGS_LIT("restored-previous") : OGS_LIT("skipped-keep-latest"),
            restartDetail + (rolledBack
                ? OGS_LIT("; previous executable restored from backup")
                : (rollbackDetail.empty()
                    ? OGS_LIT("; kept latest version (no backup available)")
                    : "; " + rollbackDetail)));
        return reject(28, OGS_LIT("restart"), restartDetail + (rolledBack
            ? OGS_LIT("; a versão anterior foi restaurada com segurança.")
            : OGS_LIT("; a versão instalada foi mantida.")));
    }

    fs::remove(package, cleanupError); fs::remove_all(staging, cleanupError); fs::remove(confirmation, cleanupError);


    RemoveRollbackBackupFiles(target, backupsDirectory, executableName.wstring());
    WriteLog(log, LogLevel::Info, targetVersion, targetVersion, std::string(OGS_LIT("complete")), std::string(OGS_LIT("Atualização concluída e arranque confirmado.")));
    RecordHealth(std::string(OGS_LIT("complete")), targetVersion, targetVersion, expectedHash, {}, std::string(OGS_LIT("not-required")),
        std::string(OGS_LIT("update completed and startup confirmed")));

    MoveFileExW(Paths::Executable().c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    return 0;
}
}
