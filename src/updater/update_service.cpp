#include "update_service.h"
#include "../app_version.h"
#include "../platform/app_paths.h"
#include "../platform/scope_exit.h"
#include "../platform/session_log.h"
#include "../platform/thread_utils.h"
#include "../platform/text_encoding.h"
#include "../platform/unique_handle.h"
#include "crypto.h"
#include "github_release_resolver.h"
#include "installation_integrity.h"
#include "update_log.h"
#include <Windows.h>
#include <Shellapi.h>
#include <wininet.h>
#include <chrono>
#include <exception>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;
namespace OmniGhost::Update {
namespace {
struct StartupNotice {
    bool updated{};
    bool rolledBack{};
    std::string fromVersion;
    std::string toVersion;
    std::string failedVersion;
    std::string rollbackStage;
    std::string releaseNotesUrl;
};
StartupNotice g_startupNotice;
fs::path LogFile() {
    const fs::path unified = OmniGhost::SessionLog::CurrentLogPath();
    return unified.empty() ? Paths::Logs() / L"logs.txt" : unified;
}
fs::path HealthFile() { return Paths::Updates() / L"update-health.json"; }
std::string Architecture() {
#if defined(_M_X64)
    return "x64";
#elif defined(_M_ARM64)
    return "arm64";
#else
    return "x86";
#endif
}
bool RecentCheck(const Configuration& config) {
    std::error_code ec; const fs::path marker = Paths::Updates() / L"last-check.txt";
    if (!fs::exists(marker, ec)) return false;
    return fs::file_time_type::clock::now() - fs::last_write_time(marker, ec) < config.minimumCheckInterval;
}
void MarkChecked() { std::ofstream(Paths::Updates() / L"last-check.txt", std::ios::trunc) << "checked\n"; }

void RecordHealth(const std::string& stage,
                  const std::string& targetVersion = {},
                  const std::string& packageSha256 = {},
                  const std::string& signerStatus = {},
                  const std::string& rollbackResult = {},
                  const std::string& detail = {}) {
    (void)WriteUpdateHealth(HealthFile(), UpdateHealthRecord{
        stage,
        OmniGhost::Version,
        targetVersion,
        packageSha256,
        signerStatus,
        rollbackResult,
        detail
    });
}
std::wstring Quote(const fs::path& path) { return L"\"" + path.wstring() + L"\""; }

bool DescendantPath(const fs::path& child, const fs::path& parent) {
    std::error_code ec;
    const fs::path canonicalChild = fs::weakly_canonical(child, ec);
    if (ec) return false;
    ec.clear();
    const fs::path canonicalParent = fs::weakly_canonical(parent, ec);
    if (ec) return false;

    auto childPart = canonicalChild.begin();
    for (auto parentPart = canonicalParent.begin(); parentPart != canonicalParent.end(); ++parentPart, ++childPart) {
        if (childPart == canonicalChild.end() || _wcsicmp(childPart->c_str(), parentPart->c_str()) != 0)
            return false;
    }
    return true;
}

bool IsHexToken(const std::wstring& token, std::size_t expectedCharacters) {
    if (token.size() != expectedCharacters)
        return false;
    for (const wchar_t ch : token) {
        const bool decimal = ch >= L'0' && ch <= L'9';
        const bool lower = ch >= L'a' && ch <= L'f';
        const bool upper = ch >= L'A' && ch <= L'F';
        if (!decimal && !lower && !upper)
            return false;
    }
    return true;
}

bool DirectoryWritable(const fs::path& directory) {
    const fs::path probe = directory / (L".omnighost-write-" + std::to_wstring(GetCurrentProcessId()));
    std::ofstream stream(probe, std::ios::binary); const bool ok = static_cast<bool>(stream); stream.close(); std::error_code ec; fs::remove(probe, ec); return ok;
}

bool Contains(const std::string& value, const char* token) {
    return value.find(token) != std::string::npos;
}

bool ContainsNetworkCode(const std::string& detail, int code) {
    const std::string number = std::to_string(code);
    return detail.find("WinHTTP=" + number) != std::string::npos ||
           detail.find("WinINet=" + number) != std::string::npos ||
           detail.find("código " + number) != std::string::npos ||
           detail.find("code " + number) != std::string::npos;
}

bool HasInternetConnection() {
    DWORD flags = 0;
    return InternetGetConnectedState(&flags, 0) == TRUE;
}

bool IsOfflineNetworkFailure(const std::string& detail) {
    const bool networkCode =
        ContainsNetworkCode(detail, 12002) ||
        ContainsNetworkCode(detail, 12007) ||
        ContainsNetworkCode(detail, 12029) ||
        ContainsNetworkCode(detail, 12030);

    if (!networkCode)
        return false;

    // InternetGetConnectedState can report a local network as connected even
    // when that network has no Internet access. When both independent Windows
    // transports fail with the same name/connection error, treat it as an
    // offline condition for the user-facing message while retaining the exact
    // codes under Technical details.
    const bool bothTransportsFailed =
        Contains(detail, "Transporte WinHTTP:") &&
        Contains(detail, "Transporte alternativo WinINet:");

    return !HasInternetConnection() || bothTransportsFailed;
}

std::string ExactErrorTitle(const std::string& stage, const std::string& detail) {
    if (IsOfflineNetworkFailure(detail))
        return "SEM LIGAÇÃO À INTERNET";
    if (ContainsNetworkCode(detail, 12002))
        return "A LIGAÇÃO DEMOROU DEMASIADO";
    if (ContainsNetworkCode(detail, 12007))
        return "SERVIDOR DE ATUALIZAÇÕES NÃO ENCONTRADO";
    if (ContainsNetworkCode(detail, 12029))
        return "NÃO FOI POSSÍVEL LIGAR AO SERVIDOR";
    if (ContainsNetworkCode(detail, 12030))
        return "A LIGAÇÃO FOI INTERROMPIDA";
    if (ContainsNetworkCode(detail, 12175))
        return "NÃO FOI POSSÍVEL CRIAR UMA LIGAÇÃO SEGURA";
    if (ContainsNetworkCode(detail, 12180) || ContainsNetworkCode(detail, 12167))
        return "PROBLEMA NA CONFIGURAÇÃO DE REDE";
    if (Contains(detail, "HTTP 404"))
        return "FICHEIRO DE ATUALIZAÇÃO NÃO ENCONTRADO";
    if (Contains(detail, "HTTP 403"))
        return "PEDIDO DE ATUALIZAÇÃO RECUSADO";
    if (Contains(detail, "HTTP 429"))
        return "DEMASIADOS PEDIDOS AO SERVIDOR";
    if (Contains(detail, "HTTP 5"))
        return "SERVIÇO DE ATUALIZAÇÕES INDISPONÍVEL";
    if (stage == "release-resolution")
        return "NÃO FOI POSSÍVEL CONFIRMAR A VERSÃO MAIS RECENTE";
    if (stage == "manifest")
        return "MANIFESTO DE ATUALIZAÇÃO INVÁLIDO";
    if (stage == "signature")
        return "ASSINATURA DO MANIFESTO INVÁLIDA";
    if (stage == "integrity")
        return "A INSTALAÇÃO TEM FICHEIROS ALTERADOS";
    if (stage == "repair")
        return "NÃO FOI POSSÍVEL PREPARAR A REPARAÇÃO";
    if (stage == "version")
        return "VERSÃO INSTALADA INVÁLIDA";
    if (stage == "download")
        return "NÃO FOI POSSÍVEL DESCARREGAR A ATUALIZAÇÃO";
    if (stage == "sha256")
        return "A VALIDAÇÃO DA ATUALIZAÇÃO FALHOU";
    if (stage == "prepare")
        return "NÃO FOI POSSÍVEL PREPARAR A ATUALIZAÇÃO";
    if (stage == "launch-updater")
        return "NÃO FOI POSSÍVEL INICIAR O ATUALIZADOR";
    if (stage == "authenticode")
        return "ASSINATURA DIGITAL INVÁLIDA";

    return "NÃO FOI POSSÍVEL CONCLUIR A ATUALIZAÇÃO";
}

std::string ExactUserMessage(const std::string& stage, const std::string& detail) {
    if (IsOfflineNetworkFailure(detail))
        return "Sem ligação à Internet. Verifica o Wi-Fi ou o cabo de rede e tenta novamente. A aplicação continuará a funcionar normalmente.";
    if (ContainsNetworkCode(detail, 12002))
        return "O servidor demorou demasiado tempo a responder. Verifica a ligação e tenta novamente.";
    if (ContainsNetworkCode(detail, 12007))
        return "A ligação está ativa, mas não foi possível localizar o servidor de atualizações. Verifica o DNS, VPN, proxy ou firewall.";
    if (ContainsNetworkCode(detail, 12029))
        return "Não foi possível estabelecer ligação ao servidor de atualizações. Verifica a firewall, VPN ou proxy.";
    if (ContainsNetworkCode(detail, 12030))
        return "A ligação ao servidor foi interrompida. Tenta novamente dentro de alguns segundos.";
    if (ContainsNetworkCode(detail, 12175))
        return "O Windows não conseguiu validar a ligação segura. Confirma a data, a hora e os certificados do sistema.";
    if (ContainsNetworkCode(detail, 12180) || ContainsNetworkCode(detail, 12167))
        return "A configuração automática de rede falhou. Verifica as definições de proxy do Windows.";
    if (Contains(detail, "HTTP 404") ||
        Contains(detail, "nenhuma versão estável publicada") ||
        Contains(detail, "não contém um asset update.json"))
        return "Ainda não existe update.json público no GitHub (repo OmniGhost-Updates). "
               "Confirma: (1) gh auth login no PC de build; (2) repositório OmniGhost-Updates público; "
               "(3) Build Publish|x64 até aparecer 'Versão X publicada com sucesso' e a URL do manifesto. "
               "Sem esse passo o launcher não consegue procurar atualizações.";
    if (Contains(detail, "HTTP 403"))
        return "O servidor recusou temporariamente o pedido. Tenta novamente mais tarde.";
    if (Contains(detail, "HTTP 429"))
        return "O limite temporário de pedidos foi atingido. Aguarda alguns minutos e tenta novamente.";
    if (Contains(detail, "HTTP 5"))
        return "O serviço de atualizações está temporariamente indisponível. Tenta novamente mais tarde.";
    if (stage == "release-resolution")
        return "Não foi possível confirmar a versão pública mais recente. Se o repositório OmniGhost-Updates "
               "ainda não tem Releases com update.json, faz Publish|x64 no Visual Studio (com gh autenticado). "
               "Se já publicaste, verifica a ligação e tenta novamente.";
    if (stage == "manifest")
        return "A informação da nova versão foi descarregada, mas não passou na validação de segurança.";
    if (stage == "signature")
        return "A assinatura criptográfica do manifesto não corresponde ao editor confiável e a atualização foi recusada.";
    if (stage == "integrity")
        return "A verificação encontrou ficheiros em falta ou alterados. Podes preparar uma reparação segura.";
    if (stage == "repair")
        return "Não foi possível obter o pacote da versão instalada para reparação.";
    if (stage == "version")
        return "A versão instalada não segue o formato esperado.";
    if (stage == "download")
        return "O pacote da nova versão não foi descarregado completamente.";
    if (stage == "sha256")
        return "O pacote descarregado não corresponde ao ficheiro publicado e foi recusado.";
    if (stage == "prepare")
        return "A atualização foi descarregada, mas não foi possível prepará-la para instalação.";
    if (stage == "launch-updater")
        return "O Windows não conseguiu iniciar o processo de instalação da atualização.";
    if (stage == "authenticode")
        return "A assinatura digital exigida não pôde ser validada.";

    return "Ocorreu um problema durante a atualização. Tenta novamente ou consulta os detalhes técnicos.";
}

}

UpdateService& UpdateService::Instance() { static UpdateService service; return service; }
UpdateService::~UpdateService() { Shutdown(); }

void UpdateService::Initialize(Configuration configuration, std::unique_ptr<IHttpClient> http) {
    if (initialized_.exchange(true)) return;
    config_ = std::move(configuration);
    http_ = http ? std::move(http) : std::make_unique<WinHttpClient>();

    const bool userDirectoriesReady = Paths::EnsureUserDirectories();
    RotateLogs(Paths::Logs());
    WriteLog(
        LogFile(),
        userDirectoriesReady ? LogLevel::Info : LogLevel::Warning,
        OmniGhost::Version,
        {},
        "initialize",
        userDirectoriesReady
            ? "Serviço de atualização inicializado. Log: " + LogFile().string()
            : "Não foi possível preparar todas as pastas do utilizador. Será usado o fallback de log quando necessário.");
    std::error_code cleanupError;
    const auto stale = fs::file_time_type::clock::now() - std::chrono::hours(48);
    for (const auto& entry : fs::directory_iterator(Paths::Updates(), fs::directory_options::skip_permission_denied, cleanupError)) {
        const std::wstring entryName = entry.path().filename().wstring();
        if (entry.path().extension() == L".partial" && entry.last_write_time(cleanupError) < stale) fs::remove(entry.path(), cleanupError);
        if (entry.is_regular_file(cleanupError) && entryName.rfind(L"startup-confirm-", 0) == 0 &&
            entry.path().extension() == L".txt" && entry.last_write_time(cleanupError) < stale)
            fs::remove(entry.path(), cleanupError);
        const std::wstring directoryName = entryName;
        if (entry.is_directory(cleanupError) &&
            (directoryName.rfind(L"staging-", 0) == 0 || directoryName.rfind(L"runner-", 0) == 0) &&
            entry.last_write_time(cleanupError) < stale) fs::remove_all(entry.path(), cleanupError);
        cleanupError.clear();
    }
    SetSnapshot([](Snapshot& value) {
        value.installedVersion = OmniGhost::Version;
        if (g_startupNotice.rolledBack) {
            value.status = Status::RolledBack;
            value.rollbackRestored = true;
            value.availableVersion = g_startupNotice.failedVersion;
            value.errorStage = g_startupNotice.rollbackStage;
            value.userMessage = "A versão anterior foi restaurada com sucesso.";
        } else if (g_startupNotice.updated) {
            value.status = Status::Completed;
            value.recentlyUpdated = true;
            value.updateFromVersion = g_startupNotice.fromVersion;
            value.availableVersion = g_startupNotice.toVersion;
            value.releaseNotesForCompletedUpdate = g_startupNotice.releaseNotesUrl;
            value.releaseNotesUrl = g_startupNotice.releaseNotesUrl;
            value.userMessage = "Atualização concluída com sucesso.";
        } else {
            value.userMessage = "A verificar atualizações em segundo plano…";
        }
    });
}

bool UpdateService::ApplyUserPreferences(Channel channel, bool automaticDownload, bool installOnExit) {
#if defined(OMNIGHOST_PUBLISH_BUILD)
    channel = Channel::Stable;
#elif defined(OMNIGHOST_TESTER_BUILD)
    channel = Channel::Development;
#endif
    if (busy_.load(std::memory_order_acquire))
        return false;
    std::lock_guard lock(mutex_);
    const bool channelChanged = config_.channel != channel;
    config_.channel = channel;
    config_.automaticDownload = automaticDownload;
    config_.automaticInstallOnExit = installOnExit;
    if (channelChanged) {
        manifest_.reset();
        package_.reset();
        downloadedPackage_.clear();
        installOnExit_.store(false, std::memory_order_release);
        snapshot_.status = Status::Idle;
        snapshot_.availableVersion.clear();
        snapshot_.releaseNotesUrl.clear();
        snapshot_.mandatory = false;
        snapshot_.mandatoryReason.clear();
        snapshot_.installOnExit = false;
        snapshot_.userMessage = "Canal alterado. Faz uma nova verificação de atualizações.";
    }
    return true;
}

void UpdateService::SetSnapshot(const std::function<void(Snapshot&)>& change) { std::lock_guard lock(mutex_); change(snapshot_); }
Snapshot UpdateService::GetSnapshot() const { std::lock_guard lock(mutex_); return snapshot_; }

bool UpdateService::ValidateManifestSignature(const std::string& manifestUrl, const std::string& manifestBytes,
                                              std::string& error) {
    if (!config_.requireManifestSignature) return true;
    if (config_.manifestPublicCertificateDerBase64.empty()) {
        error = "A chave pública ou o certificado de publicação não está configurado no cliente.";
        return false;
    }
    const std::string signatureUrl = manifestUrl + ".sig";
    const int timeoutMilliseconds = static_cast<int>(config_.networkTimeout.count() * 1000);
    HttpResult signatureResponse = http_->GetText(signatureUrl, timeoutMilliseconds, cancelled_);
    if (!signatureResponse.error.empty()) {
        error = "Não foi possível obter update.json.sig: " + signatureResponse.error;
        return false;
    }
    if (VerifyDetachedManifestSignature(manifestBytes, signatureResponse.body,
                                        config_.manifestPublicCertificateDerBase64, error)) return true;
    if (!config_.secondaryManifestPublicCertificateDerBase64.empty()) {
        std::string secondaryError;
        if (VerifyDetachedManifestSignature(manifestBytes, signatureResponse.body,
                                            config_.secondaryManifestPublicCertificateDerBase64,
                                            secondaryError)) return true;
        error += "; a âncora de confiança secundária rejeitou a assinatura: " + secondaryError;
    }
    return false;
}

void UpdateService::CheckAsync(bool force) {
    if (!initialized_) Initialize();
    if (busy_.exchange(true)) {
        WriteLog(LogFile(), LogLevel::Warning, OmniGhost::Version, {}, "check",
            "Pedido de verificação ignorado porque já existe uma operação do atualizador em curso.");
        return;
    }

    cancelled_ = false;
    if (worker_.joinable())
        worker_.join();

    worker_ = std::jthread([this, force](std::stop_token stopToken) {
        Platform::SetCurrentThreadName(L"OmniGhost.UpdateCheck");
        std::stop_callback cancelOnStop(stopToken, [this] { cancelled_.store(true, std::memory_order_release); });
        auto busyReset = Platform::MakeScopeExit([this] { busy_ = false; });
        try {
            RunCheck(force);
        } catch (const std::exception& exception) {
            SetError("A verificação terminou de forma inesperada.", exception.what(), "worker");
        } catch (...) {
            SetError("A verificação terminou de forma inesperada.",
                "Exceção não identificada na thread de verificação.", "worker");
        }
    });
}

void UpdateService::RunCheck(bool force) {
    if (!force && RecentCheck(config_)) {
        SetSnapshot([](Snapshot& s) {
            s.status = Status::Idle;
            s.userMessage = "Verificação recente guardada localmente.";
        });
        return;
    }

    SetSnapshot([](Snapshot& s) {
        s.status = Status::Checking;
        s.userMessage = "A verificar atualizações…";
        s.availableVersion.clear();
        s.releaseNotesUrl.clear();
        s.errorTitle.clear();
        s.errorStage.clear();
        s.technicalDetail.clear();
    });

    const auto installed = SemVersion::Parse(OmniGhost::Version);
    if (!installed) {
        SetError({}, "OMNIGHOST_VERSION não segue SemVer.", "version");
        return;
    }

    const int timeoutMilliseconds =
        static_cast<int>(config_.networkTimeout.count() * 1000);

    std::optional<GitHubReleaseSelection> resolvedRelease;
    std::string releaseResolutionWarning;
    bool releaseListResponseReceived = false;

    if (!config_.releasesApiUrl.empty()) {
        WriteLog(
            LogFile(),
            LogLevel::Info,
            OmniGhost::Version,
            {},
            "release-resolution",
            "A consultar a lista pública de versões para encontrar a versão estável mais recente.");

        HttpResult releasesResponse = http_->GetText(
            config_.releasesApiUrl,
            timeoutMilliseconds,
            cancelled_);

        if (cancelled_.load()) {
            SetError("Verificação cancelada.", "O utilizador cancelou a verificação.", "release-resolution");
            return;
        }

        if (releasesResponse.error.empty()) {
            releaseListResponseReceived = true;
            GitHubReleaseResult releaseResult =
                ResolveHighestStableGitHubRelease(releasesResponse.body);

            if (releaseResult.selection) {
                resolvedRelease = std::move(*releaseResult.selection);
                WriteLog(
                    LogFile(),
                    LogLevel::Info,
                    OmniGhost::Version,
                    resolvedRelease->version.original,
                    "release-resolution",
                    "Versão estável mais recente encontrada: " +
                        resolvedRelease->tag + ".");

                if (resolvedRelease->version.Compare(*installed) <= 0) {
                    MarkChecked();
                    SetSnapshot([&](Snapshot& s) {
                        s.status = Status::UpToDate;
                        s.availableVersion = resolvedRelease->version.original;
                        s.releaseNotesUrl = resolvedRelease->releaseUrl;
                        s.userMessage = "O OmniGhost está atualizado.";
                        s.errorTitle.clear();
                        s.errorStage.clear();
                        s.technicalDetail.clear();
                    });
                    WriteLog(
                        LogFile(),
                        LogLevel::Info,
                        OmniGhost::Version,
                        resolvedRelease->version.original,
                        "compare",
                        "A versão estável mais recente não é superior à versão instalada.");
                    RecordHealth("up-to-date", resolvedRelease->version.original, {}, {}, {},
                        "highest stable release is not newer than installed version");
                    return;
                }
            } else {
                releaseResolutionWarning = releaseResult.error;
                WriteLog(
                    LogFile(),
                    LogLevel::Warning,
                    OmniGhost::Version,
                    {},
                    "release-resolution",
                    releaseResolutionWarning + " Será usado o endereço de compatibilidade da versão mais recente.");
            }
        } else {
            releaseResolutionWarning = releasesResponse.error;
            WriteLog(
                LogFile(),
                LogLevel::Warning,
                OmniGhost::Version,
                {},
                "release-resolution",
                releaseResolutionWarning + " Será usado o endereço de compatibilidade da versão mais recente.");
        }
    }

    const std::string manifestUrl = resolvedRelease
        ? resolvedRelease->updateManifestUrl
        : config_.manifestUrl;

    WriteLog(
        LogFile(),
        LogLevel::Info,
        OmniGhost::Version,
        resolvedRelease ? resolvedRelease->version.original : std::string{},
        "check",
        "A consultar o manifesto: " + manifestUrl);

    HttpResult response = http_->GetText(
        manifestUrl,
        timeoutMilliseconds,
        cancelled_);

    if (!response.error.empty()) {
        std::string detail = response.error;
        if (!releaseResolutionWarning.empty()) {
            detail = "Resolução da versão: " + releaseResolutionWarning +
                     " | Manifesto: " + detail;
        }
        SetError({}, detail, "check");
        return;
    }

    std::string signatureError;
    if (!ValidateManifestSignature(manifestUrl, response.body, signatureError)) {
        RecordHealth("manifest-signature-failed", resolvedRelease ? resolvedRelease->version.original : std::string{}, {}, "failed", {}, signatureError);
        SetError({}, signatureError, "signature");
        return;
    }
    if (config_.requireManifestSignature)
        RecordHealth("manifest-signature-verified", resolvedRelease ? resolvedRelease->version.original : std::string{}, {}, "verified");

    ManifestResult parsed =
        ParseAndValidateManifest(response.body, config_, Architecture());
    if (!parsed.manifest) {
        SetError({}, parsed.error, "manifest");
        return;
    }

    Manifest manifest = std::move(*parsed.manifest);

    if (resolvedRelease &&
        manifest.version.Compare(resolvedRelease->version) != 0) {
        SetError(
            {},
            "A versão do update.json ('" + manifest.version.original +
                "') não corresponde à versão selecionada ('" +
                resolvedRelease->version.original + "').",
            "manifest");
        return;
    }

    if (manifest.version.Compare(*installed) <= 0) {
        // When the API failed and the mutable /releases/latest marker points to
        // an older release, reporting "up to date" would be misleading.
        if (releaseListResponseReceived &&
            !releaseResolutionWarning.empty()) {
            SetError(
                {},
                "A API de versões respondeu, mas não foi possível selecionar "
                "com segurança a maior versão estável: " +
                releaseResolutionWarning +
                " O marcador da versão mais recente devolveu " +
                manifest.version.original + ".",
                "release-resolution");
            return;
        }

        if (!releaseResolutionWarning.empty() &&
            manifest.version.Compare(*installed) < 0) {
            SetError(
                {},
                "A lista de versões não pôde ser consultada e o marcador da versão mais recente "
                "devolveu a versão " + manifest.version.original +
                ", que é anterior à versão instalada " +
                installed->original + ".",
                "release-resolution");
            return;
        }

        MarkChecked();
        SetSnapshot([&](Snapshot& s) {
            s.status = Status::UpToDate;
            s.availableVersion = manifest.version.original;
            s.releaseNotesUrl = manifest.releaseNotesUrl;
            s.userMessage = "O OmniGhost está atualizado.";
            s.errorTitle.clear();
            s.errorStage.clear();
            s.technicalDetail.clear();
        });
        WriteLog(
            LogFile(),
            LogLevel::Info,
            OmniGhost::Version,
            manifest.version.original,
            "compare",
            "A versão publicada não é superior à versão instalada.");
        RecordHealth("up-to-date", manifest.version.original, {}, {}, {},
            "published version is not newer than installed version");
        return;
    }

    const Package* selected = SelectPackage(manifest, Architecture());
    if (!selected) {
        SetError({}, "Não existe pacote compatível com esta arquitetura.", "manifest");
        return;
    }

    MarkChecked();
    const bool belowMinimumSupported =
        installed->Compare(manifest.minimumSupportedVersion) < 0;
    // The launcher only permits the newest published client. A newer version
    // is therefore always treated as mandatory, even when the manifest marks
    // it as optional for other distribution channels.
    const bool mandatory = true;
    std::string mandatoryReason;
    if (mandatory) {
        if (!manifest.mandatoryReason.empty()) {
            mandatoryReason = manifest.mandatoryReason;
        } else if (belowMinimumSupported) {
            mandatoryReason = "A versão instalada " + installed->original +
                " está abaixo da versão mínima suportada " +
                manifest.minimumSupportedVersion.original + ".";
        } else {
            mandatoryReason = "É necessário instalar a versão mais recente para continuar.";
        }
    }

    {
        std::lock_guard lock(mutex_);
        manifest_ = manifest;
        package_ = *selected;
        snapshot_.status = Status::Available;
        snapshot_.availableVersion = manifest.version.original;
        snapshot_.releaseNotesUrl = manifest.releaseNotesUrl;
        snapshot_.mandatory = mandatory;
        snapshot_.mandatoryReason = mandatoryReason;
        snapshot_.bytesTotal = selected->size;
        snapshot_.userMessage = mandatory
            ? "É necessário atualizar antes de continuar. " + mandatoryReason
            : "Está disponível uma nova versão.";
        snapshot_.errorTitle.clear();
        snapshot_.errorStage.clear();
        snapshot_.technicalDetail.clear();
    }

    WriteLog(
        LogFile(),
        LogLevel::Info,
        OmniGhost::Version,
        manifest.version.original,
        "available",
        mandatory ? "Atualização obrigatória." : "Atualização opcional.");
    RecordHealth("available", manifest.version.original, selected->sha256, {}, {},
        mandatory ? "mandatory" : "optional");

    if (config_.automaticDownload) {
        if (!mandatory && config_.automaticInstallOnExit) {
            installOnExit_.store(true, std::memory_order_release);
            SetSnapshot([](Snapshot& s) { s.installOnExit = true; });
        }
        RunDownload();
    }
}

void UpdateService::DownloadAndScheduleInstallOnExitAsync() {
    const Snapshot current = GetSnapshot();
    if (current.status != Status::Available || current.mandatory) return;
    if (busy_.exchange(true)) {
        WriteLog(LogFile(), LogLevel::Warning, OmniGhost::Version, {}, "download",
            "Pedido de transferência ignorado porque já existe uma operação do atualizador em curso.");
        return;
    }
    installOnExit_.store(true, std::memory_order_release);
    SetSnapshot([](Snapshot& s) { s.installOnExit = true; });
    StartDownloadWorker();
}

void UpdateService::DownloadAsync() {
    bool automaticInstallOnExit = false;
    {
        std::lock_guard lock(mutex_);
        automaticInstallOnExit = config_.automaticInstallOnExit;
    }
    {
        const Snapshot current = GetSnapshot();
        if (!current.mandatory && automaticInstallOnExit) {
            installOnExit_.store(true, std::memory_order_release);
            SetSnapshot([](Snapshot& s) { s.installOnExit = true; });
        }
    }
    if (busy_.exchange(true)) {
        WriteLog(LogFile(), LogLevel::Warning, OmniGhost::Version, {}, "download",
            "Pedido de transferência ignorado porque já existe uma operação do atualizador em curso.");
        return;
    }
    StartDownloadWorker();
}

void UpdateService::StartDownloadWorker() {
    cancelled_ = false;
    if (worker_.joinable())
        worker_.join();

    worker_ = std::jthread([this](std::stop_token stopToken) {
        Platform::SetCurrentThreadName(L"OmniGhost.UpdateDownload");
        std::stop_callback cancelOnStop(stopToken, [this] { cancelled_.store(true, std::memory_order_release); });
        auto busyReset = Platform::MakeScopeExit([this] { busy_ = false; });
        try {
            RunDownload();
        } catch (const std::exception& exception) {
            SetError("A transferência terminou de forma inesperada.", exception.what(), "worker");
        } catch (...) {
            SetError("A transferência terminou de forma inesperada.",
                "Exceção não identificada na tarefa de transferência.", "worker");
        }
    });
}

void UpdateService::RunDownload() {
    Package package; std::string version;
    bool missingDownloadMetadata = false;
    {
        std::lock_guard lock(mutex_);
        if (!package_ || !manifest_) {
            missingDownloadMetadata = true;
        } else {
            package = *package_;
            version = manifest_->version.original;
        }
    }
    if (missingDownloadMetadata) {
        SetError("A atualização ainda não está preparada para download.",
            "Manifesto ou pacote ausente ao iniciar a transferência.", "download");
        return;
    }
    SetSnapshot([](Snapshot& s) {
        s.status = Status::Downloading;
        s.userMessage = "A descarregar e validar a atualização…";
        s.errorTitle.clear();
        s.errorStage.clear();
        s.technicalDetail.clear();
    });
    const fs::path partial = Paths::Updates() / fs::path(package.fileName + ".partial"); const fs::path final = Paths::Updates() / fs::path(package.fileName);
    std::error_code ec; fs::remove(partial, ec);
    WriteLog(LogFile(), LogLevel::Info, OmniGhost::Version, version, "download",
        "Transferência iniciada; tamanho esperado=" + std::to_string(package.size) + " bytes.");
    RecordHealth("download-started", version, package.sha256, {}, {},
        "download worker started");
    HttpResult result = http_->Download(package.url, partial, package.size, config_.maximumPackageBytes,
        static_cast<int>(config_.networkTimeout.count() * 1000), cancelled_, [this](auto received, auto total, auto speed) {
            SetSnapshot([&](Snapshot& s) { s.bytesReceived = received; s.bytesTotal = total; s.bytesPerSecond = speed; });
        });
    if (!result.error.empty()) { SetError({}, result.error, "download"); return; }
    std::string digest, error;
    if (!Sha256File(partial, digest, error) || !ConstantTimeEquals(digest, package.sha256)) {
        fs::remove(partial, ec); SetError({}, error.empty() ? "SHA-256 diferente do manifesto." : error, "sha256"); return;
    }
    fs::remove(final, ec); fs::rename(partial, final, ec); if (ec) { SetError({}, ec.message(), "prepare"); return; }
    downloadedPackage_ = final;
    SetSnapshot([this](Snapshot& s) {
        s.status = Status::Ready;
        s.installOnExit = installOnExit_.load(std::memory_order_acquire);
        s.userMessage = s.installOnExit
            ? "Atualização validada. Será instalada automaticamente ao sair."
            : "Atualização validada e pronta para instalar.";
        s.errorTitle.clear();
        s.errorStage.clear();
        s.technicalDetail.clear();
    });
    WriteLog(LogFile(), LogLevel::Info, OmniGhost::Version, version, "ready", "Tamanho e SHA-256 validados.");
    RecordHealth("ready", version, package.sha256, {}, {}, "package validated");
}

void UpdateService::VerifyInstallationAsync() {
    if (!initialized_) Initialize();
    if (busy_.exchange(true)) return;
    cancelled_ = false;
    if (worker_.joinable()) worker_.join();
    SetSnapshot([](Snapshot& s) {
        s.integrityStatus = IntegrityStatus::Checking;
        s.integrityMessage = "A verificar ficheiros instalados…";
        s.integrityCheckedFiles = 0;
        s.integrityFailedFiles = 0;
    });
    worker_ = std::jthread([this](std::stop_token stopToken) {
        Platform::SetCurrentThreadName(L"OmniGhost.InstallVerify");
        auto busyReset = Platform::MakeScopeExit([this] { busy_ = false; });
        if (stopToken.stop_requested()) return;
        const IntegrityReport report = VerifyInstallationFiles(Paths::InstallDirectory());
        SetSnapshot([&](Snapshot& s) {
            s.integrityStatus = report.status;
            s.integrityCheckedFiles = report.checkedFiles;
            s.integrityFailedFiles = report.failedFiles;
            s.integrityMessage = report.message;
        });
        if (report.status == IntegrityStatus::Damaged || report.status == IntegrityStatus::Error) {
            std::string detail = report.message;
            for (const auto& issue : report.issues)
                detail += " | " + issue.relativePath + ":" + issue.reason;
            WriteLog(LogFile(), LogLevel::Warning, OmniGhost::Version, {}, "integrity", detail);
        } else {
            WriteLog(LogFile(), LogLevel::Info, OmniGhost::Version, {}, "integrity", report.message);
        }
    });
}

void UpdateService::PrepareRepairAsync() {
    if (!initialized_) Initialize();
    if (busy_.exchange(true)) return;
    cancelled_ = false;
    if (worker_.joinable()) worker_.join();
    worker_ = std::jthread([this](std::stop_token stopToken) {
        Platform::SetCurrentThreadName(L"OmniGhost.UpdateRepair");
        std::stop_callback cancelOnStop(stopToken, [this] { cancelled_.store(true, std::memory_order_release); });
        auto busyReset = Platform::MakeScopeExit([this] { busy_ = false; });
        try { RunPrepareRepair(); }
        catch (const std::exception& exception) { SetError({}, exception.what(), "repair"); }
        catch (...) { SetError({}, "Exceção não identificada ao preparar reparação.", "repair"); }
    });
}

void UpdateService::RunPrepareRepair() {
    const std::string installed = OmniGhost::Version;
    const std::string manifestUrl = config_.allowedPackageUrlPrefix + "v" + installed + "/update.json";
    SetSnapshot([](Snapshot& s) {
        s.status = Status::Checking;
        s.repairMode = true;
        s.userMessage = "A localizar o pacote da versão instalada para reparação…";
        s.errorTitle.clear(); s.errorStage.clear(); s.technicalDetail.clear();
    });
    const int timeoutMilliseconds = static_cast<int>(config_.networkTimeout.count() * 1000);
    HttpResult response = http_->GetText(manifestUrl, timeoutMilliseconds, cancelled_);
    if (!response.error.empty()) { SetError({}, response.error, "repair"); return; }
    std::string signatureError;
    if (!ValidateManifestSignature(manifestUrl, response.body, signatureError)) { SetError({}, signatureError, "signature"); return; }
    ManifestResult parsed = ParseAndValidateManifest(response.body, config_, Architecture());
    if (!parsed.manifest) { SetError({}, parsed.error, "manifest"); return; }
    Manifest manifest = std::move(*parsed.manifest);
    if (manifest.version.original != installed) {
        SetError({}, "O manifesto de reparação não corresponde à versão instalada.", "repair");
        return;
    }
    const Package* selected = SelectPackage(manifest, Architecture());
    if (!selected) { SetError({}, "Não existe pacote x64 para reparar esta versão.", "repair"); return; }
    {
        std::lock_guard lock(mutex_);
        manifest_ = manifest;
        package_ = *selected;
        snapshot_.repairMode = true;
        snapshot_.availableVersion = installed;
        snapshot_.releaseNotesUrl = manifest.releaseNotesUrl;
        snapshot_.bytesTotal = selected->size;
        snapshot_.mandatory = false;
    }
    RunDownload();
    SetSnapshot([](Snapshot& s) {
        if (s.status == Status::Ready) s.userMessage = "Pacote de reparação validado e pronto para reiniciar.";
    });
}

bool UpdateService::ScheduleInstallOnExit() {
    const Snapshot current = GetSnapshot();
    if (current.status != Status::Ready || current.mandatory) return false;
    installOnExit_.store(true, std::memory_order_release);
    SetSnapshot([](Snapshot& s) {
        s.installOnExit = true;
        s.userMessage = s.repairMode ? "A reparação será instalada ao sair." : "A atualização será instalada ao sair.";
    });
    return true;
}

void UpdateService::RetryLastFailure() {
    const Snapshot current = GetSnapshot();
    const std::string stage = current.errorStage;
    if (current.repairMode && (stage == "repair" || stage == "signature" || stage == "manifest" || stage == "check")) {
        PrepareRepairAsync();
        return;
    }
    if (stage == "download" || stage == "sha256" || stage == "prepare") {
        DownloadAsync();
        return;
    }
    if (stage == "launch-updater" || stage == "authenticode") {
        if (fs::exists(downloadedPackage_)) {
            SetSnapshot([](Snapshot& s) { s.status = Status::Ready; });
            if (InstallPreparedUpdate()) return;
        }
        if (current.repairMode) PrepareRepairAsync(); else CheckAsync(true);
        return;
    }
    if (stage == "integrity") { VerifyInstallationAsync(); return; }
    if (stage == "repair") { PrepareRepairAsync(); return; }
    CheckAsync(true);
}

void UpdateService::DismissStartupNotice() {
    SetSnapshot([](Snapshot& s) {
        if (s.status == Status::Completed || s.status == Status::RolledBack) {
            s.status = Status::Idle;
            s.userMessage.clear();
            s.recentlyUpdated = false;
            s.rollbackRestored = false;
        }
    });
}

bool UpdateService::InstallPreparedUpdate() {
    std::string version, expectedHash; std::uint64_t expectedSize{};
    { std::lock_guard lock(mutex_); if (!manifest_ || !package_ ||
          (snapshot_.status != Status::Ready && snapshot_.status != Status::Installing)) return false;
      version = manifest_->version.original; expectedHash = package_->sha256; expectedSize = package_->size; }
    const fs::path installedExecutable = Paths::Executable();
    if (!fs::exists(installedExecutable) || !fs::exists(downloadedPackage_)) { SetError({}, "Executável ou pacote em falta.", "launch-updater"); return false; }
    std::wstring runnerToken;
    std::string randomError;
    if (!RandomTokenHex(16, runnerToken, randomError)) {
        SetError({}, randomError, "launch-updater");
        return false;
    }
    const fs::path runnerDirectory = Paths::Updates() / (L"runner-" + runnerToken);
    const fs::path runner = runnerDirectory / OmniGhost::MainExecutable; std::error_code ec;
    fs::create_directories(runnerDirectory, ec);
    fs::copy_file(installedExecutable, runner, fs::copy_options::overwrite_existing, ec);
    // The updater worker is now the same self-contained EXE. Its bootstrap
    // detects --apply-update and does not load DMA or copy any private runtime file,
    // so the runner directory intentionally contains only OmniGhost.exe.

    std::string sourceRunnerHash, copiedRunnerHash, runnerHashError;
    if (!Sha256File(installedExecutable, sourceRunnerHash, runnerHashError) ||
        !Sha256File(runner, copiedRunnerHash, runnerHashError) ||
        !ConstantTimeEquals(sourceRunnerHash, copiedRunnerHash)) {
        SetError({}, runnerHashError.empty() ? "A cópia temporária do atualizador não corresponde ao executável instalado." : runnerHashError,
            "launch-updater");
        return false;
    }

    if (config_.requireAuthenticode) {
        std::string error;
        bool trustedRunner = VerifyAuthenticode(runner, config_.expectedPublisherSubject,
                                                config_.expectedCertificateThumbprint, error);
        if (!trustedRunner && !config_.secondaryCertificateThumbprint.empty()) {
            std::string secondaryError;
            trustedRunner = VerifyAuthenticode(runner, config_.secondaryPublisherSubject,
                                               config_.secondaryCertificateThumbprint, secondaryError);
            if (!trustedRunner) error += "; o editor secundário rejeitou o executável: " + secondaryError;
        }
        if (!trustedRunner) {
            RecordHealth("authenticode-failed", version, expectedHash, "failed", {}, error);
            SetError({}, error, "authenticode");
            return false;
        }
        RecordHealth("authenticode-verified", version, expectedHash, "verified");
    }
    const fs::path backup = Paths::Backups() / fs::path(version + "-" + std::to_string(GetTickCount64()));
    std::wostringstream arguments; arguments << L"--apply-update --pid " << GetCurrentProcessId() << L" --package " << Quote(downloadedPackage_)
        << L" --target " << Quote(Paths::InstallDirectory()) << L" --executable " << Quote(OmniGhost::MainExecutable)
        << L" --version \"" << Platform::Utf8ToWide(version) << L"\" --backup " << Quote(backup)
        << L" --sha256 \"" << Platform::Utf8ToWide(expectedHash) << L"\" --size " << expectedSize
        << L" --timeout " << config_.updaterExitTimeoutSeconds
        << L" --from-version \"" << Platform::Utf8ToWide(OmniGhost::Version) << L"\"";
    const Snapshot installSnapshot = GetSnapshot();
    if (!installSnapshot.releaseNotesUrl.empty())
        arguments << L" --notes-url \"" << Platform::Utf8ToWide(installSnapshot.releaseNotesUrl) << L"\"";
    const wchar_t* verb = DirectoryWritable(Paths::InstallDirectory()) ? L"open" : L"runas";
    const std::wstring argumentText = arguments.str();
    const std::wstring runnerDirectoryText = runnerDirectory.wstring();
    SHELLEXECUTEINFOW launch{};
    launch.cbSize = sizeof(launch);
    launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    launch.hwnd = nullptr;
    launch.lpVerb = verb;
    launch.lpFile = runner.c_str();
    launch.lpParameters = argumentText.c_str();
    launch.lpDirectory = runnerDirectoryText.c_str();
    launch.nShow = SW_HIDE;
    if (!ShellExecuteExW(&launch) || !launch.hProcess) {
        SetError({}, WindowsError(GetLastError()), "launch-updater");
        return false;
    }
    Platform::UniqueHandle updaterProcess(launch.hProcess);

    // ShellExecute success only means Windows accepted the request. Previously
    // the launcher exited even when the updater rejected its arguments and died
    // immediately, leaving the user with no update and no visible explanation.
    const DWORD earlyExit = WaitForSingleObject(updaterProcess.get(), 750);
    if (earlyExit == WAIT_OBJECT_0) {
        DWORD exitCode = 0;
        GetExitCodeProcess(updaterProcess.get(), &exitCode);
        const std::string detail = "O processo temporário do atualizador terminou imediatamente (código " +
            std::to_string(exitCode) + ").";
        SetError("Não foi possível iniciar a instalação da atualização.", detail, "launch-updater");
        return false;
    }
    if (earlyExit == WAIT_FAILED) {
        SetError("Não foi possível acompanhar o processo de atualização.",
            WindowsError(GetLastError()), "launch-updater");
        return false;
    }
    WriteLog(LogFile(), LogLevel::Info, OmniGhost::Version, version, "launch-updater",
        "Atualizador temporário iniciado e a aguardar o encerramento do launcher.");
    SetSnapshot([](Snapshot& s) {
        s.status = Status::Installing;
        s.installOnExit = false;
        s.userMessage = s.repairMode ? "A reiniciar para concluir a reparação…" : "A reiniciar para concluir a atualização…";
        s.errorTitle.clear();
        s.errorStage.clear();
        s.technicalDetail.clear();
    });
    RecordHealth("install-launched", version, expectedHash,
        config_.requireAuthenticode ? "verified" : "not-required");
    exitRequested_ = true; return true;
}

void UpdateService::InstallPreparedUpdateAsync() {
    const Snapshot current = GetSnapshot();
    if (current.status != Status::Ready || busy_.exchange(true, std::memory_order_acq_rel))
        return;

    SetSnapshot([](Snapshot& s) {
        s.status = Status::Installing;
        s.installOnExit = false;
        s.userMessage = s.repairMode
            ? "A preparar a reparação…"
            : "A preparar a atualização…";
        s.errorTitle.clear();
        s.errorStage.clear();
        s.technicalDetail.clear();
    });

    if (worker_.joinable())
        worker_.join();
    worker_ = std::jthread([this](std::stop_token) {
        Platform::SetCurrentThreadName(L"OmniGhost.UpdateInstallLaunch");
        auto busyReset = Platform::MakeScopeExit([this] {
            busy_.store(false, std::memory_order_release);
        });
        try {
            (void)InstallPreparedUpdate();
        } catch (const std::exception& exception) {
            SetError("Não foi possível preparar a instalação da atualização.",
                exception.what(), "launch-updater");
        } catch (...) {
            SetError("Não foi possível preparar a instalação da atualização.",
                "Falha inesperada no arranque do atualizador.", "launch-updater");
        }
    });
}

void UpdateService::DeferForSession() {
    installOnExit_.store(false, std::memory_order_release);
    std::lock_guard lock(mutex_); deferred_ = true; snapshot_.installOnExit = false; snapshot_.status = Status::Deferred; snapshot_.userMessage = "Atualização adiada nesta sessão.";
}
void UpdateService::Cancel() {
    cancelled_ = true;
    if (worker_.joinable()) worker_.request_stop();
}
bool UpdateService::BlocksGameLaunch() const {
    const Snapshot value = GetSnapshot();
    switch (value.status) {
    case Status::Checking:
    case Status::Available:
    case Status::Downloading:
    case Status::Ready:
    case Status::Installing:
        return true;
    default:
        return false;
    }
}
void UpdateService::SetError(const std::string& userMessage, const std::string& detail, const std::string& stage) {
    const std::string title = ExactErrorTitle(stage, detail);
    const std::string resolvedMessage =
        userMessage.empty() ? ExactUserMessage(stage, detail) : userMessage;

    SetSnapshot([&](Snapshot& s) {
        s.status = Status::Error;
        s.userMessage = resolvedMessage;
        s.errorTitle = title;
        s.errorStage = stage;
        s.technicalDetail = detail.empty()
            ? "Nenhum detalhe técnico foi devolvido pela operação."
            : detail;
    });
    WriteLog(LogFile(), LogLevel::Error, OmniGhost::Version, GetSnapshot().availableVersion, stage, detail);
    RecordHealth("error", GetSnapshot().availableVersion, {}, {}, {}, stage + ": " + detail);
}
void UpdateService::Shutdown() {
    cancelled_ = true;
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    busy_ = false;
    if (installOnExit_.exchange(false, std::memory_order_acq_rel)) {
        const Snapshot current = GetSnapshot();
        if (current.status == Status::Ready) (void)InstallPreparedUpdate();
    }
}

void CaptureUpdaterStartupNoticeFromCommandLine(int argc, wchar_t** argv) {
    g_startupNotice = {};
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i] ? argv[i] : L"";
        if (arg == L"--updated-from" && i + 1 < argc) {
            g_startupNotice.updated = true;
            g_startupNotice.fromVersion = Platform::WideToUtf8(argv[++i]);
        } else if (arg == L"--updated-to" && i + 1 < argc) {
            g_startupNotice.updated = true;
            g_startupNotice.toVersion = Platform::WideToUtf8(argv[++i]);
        } else if (arg == L"--update-notes-url" && i + 1 < argc) {
            g_startupNotice.releaseNotesUrl = Platform::WideToUtf8(argv[++i]);
        } else if (arg == L"--rollback-restored") {
            g_startupNotice.rolledBack = true;
            g_startupNotice.updated = false;
        } else if (arg == L"--failed-version" && i + 1 < argc) {
            g_startupNotice.failedVersion = Platform::WideToUtf8(argv[++i]);
        } else if (arg == L"--rollback-stage" && i + 1 < argc) {
            g_startupNotice.rollbackStage = Platform::WideToUtf8(argv[++i]);
        }
    }
}

bool ConfirmUpdaterStartupFromCommandLine(int argc, wchar_t** argv) {
    std::wstring token, file;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::wstring(argv[i]) == L"--update-confirm") token = argv[++i];
        else if (std::wstring(argv[i]) == L"--update-confirm-file") file = argv[++i];
    }
    const fs::path confirmationPath(file);
    if (!IsHexToken(token, 64) || confirmationPath.empty() || !confirmationPath.is_absolute() ||
        !DescendantPath(confirmationPath.parent_path(), Paths::Updates())) {
        return false;
    }

    std::wofstream stream(confirmationPath, std::ios::trunc);
    if (!stream) return false;
    stream << token;
    stream.flush();
    return static_cast<bool>(stream);
}
}
