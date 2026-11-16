#include "update_service.h"
#include "../app_version.h"
#include "../platform/app_paths.h"
#include "../platform/scope_exit.h"
#include "../platform/session_log.h"
#include "../platform/thread_utils.h"
#include "../platform/text_encoding.h"
#include "../platform/unique_handle.h"
#include "../launcher/remote_usage_service.h"
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
#include "platform/strprotect/strprotect.h"

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
fs::path HealthFile() { return Paths::Updates() / OGS_WIDE("update-health.json"); }
std::string Architecture() {
#if defined(_M_X64)
    return "x64";
#elif defined(_M_ARM64)
    return OGS_CSTR("arm64");
#else
    return "x86";
#endif
}
bool RecentCheck(const Configuration& config) {
    std::error_code ec; const fs::path marker = Paths::Updates() / OGS_WIDE("last-check.txt");
    if (!fs::exists(marker, ec)) return false;
    return fs::file_time_type::clock::now() - fs::last_write_time(marker, ec) < config.minimumCheckInterval;
}
void MarkChecked() { std::ofstream(Paths::Updates() / OGS_WIDE("last-check.txt"), std::ios::trunc) << OGS_CSTR("checked\n"); }

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
std::wstring Quote(const std::wstring& text) {

    std::wstring safe;
    safe.reserve(text.size() + 2);
    for (wchar_t c : text) {
        if (c != L'"')
            safe.push_back(c);
    }
    return L"\"" + safe + L"\"";
}

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
    const fs::path probe = directory / (OGS_WIDE(".omnighost-write-") + std::to_wstring(GetCurrentProcessId()));
    std::ofstream stream(probe, std::ios::binary); const bool ok = static_cast<bool>(stream); stream.close(); std::error_code ec; fs::remove(probe, ec); return ok;
}

bool Contains(const std::string& value, const char* token) {
    return value.find(token) != std::string::npos;
}

bool ContainsNetworkCode(const std::string& detail, int code) {
    const std::string number = std::to_string(code);
    return detail.find(OGS_CSTR("WinHTTP=") + number) != std::string::npos ||
           detail.find(OGS_CSTR("WinINet=") + number) != std::string::npos ||
           detail.find(OGS_CSTR("cÃ³digo ") + number) != std::string::npos ||
           detail.find(OGS_CSTR("code ") + number) != std::string::npos;
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






    const bool bothTransportsFailed =
        Contains(detail, OGS_CSTR("Transporte WinHTTP:")) &&
        Contains(detail, OGS_CSTR("Transporte alternativo WinINet:"));

    return !HasInternetConnection() || bothTransportsFailed;
}

std::string ExactErrorTitle(const std::string& stage, const std::string& detail) {
    if (IsOfflineNetworkFailure(detail))
        return OGS_CSTR("SEM LIGAÃ‡ÃƒO Ã€ INTERNET");
    if (ContainsNetworkCode(detail, 12002))
        return OGS_CSTR("A LIGAÃ‡ÃƒO DEMOROU DEMASIADO");
    if (ContainsNetworkCode(detail, 12007))
        return OGS_CSTR("SERVIDOR DE ATUALIZAÃ‡Ã•ES NÃƒO ENCONTRADO");
    if (ContainsNetworkCode(detail, 12029))
        return OGS_CSTR("NÃƒO FOI POSSÃVEL LIGAR AO SERVIDOR");
    if (ContainsNetworkCode(detail, 12030))
        return OGS_CSTR("A LIGAÃ‡ÃƒO FOI INTERROMPIDA");
    if (ContainsNetworkCode(detail, 12175))
        return OGS_CSTR("NÃƒO FOI POSSÃVEL CRIAR UMA LIGAÃ‡ÃƒO SEGURA");
    if (ContainsNetworkCode(detail, 12180) || ContainsNetworkCode(detail, 12167))
        return OGS_CSTR("PROBLEMA NA CONFIGURAÃ‡ÃƒO DE REDE");
    if (Contains(detail, OGS_CSTR("HTTP 404")))
        return OGS_CSTR("FICHEIRO DE ATUALIZAÃ‡ÃƒO NÃƒO ENCONTRADO");
    if (Contains(detail, OGS_CSTR("HTTP 403")))
        return OGS_CSTR("PEDIDO DE ATUALIZAÃ‡ÃƒO RECUSADO");
    if (Contains(detail, OGS_CSTR("HTTP 429")))
        return OGS_CSTR("DEMASIADOS PEDIDOS AO SERVIDOR");
    if (Contains(detail, OGS_CSTR("HTTP 5")))
        return OGS_CSTR("SERVIÃ‡O DE ATUALIZAÃ‡Ã•ES INDISPONÃVEL");
    if (stage == OGS_CSTR("release-resolution"))
        return OGS_CSTR("NÃƒO FOI POSSÃVEL CONFIRMAR A VERSÃƒO MAIS RECENTE");
    if (stage == OGS_CSTR("manifest"))
        return OGS_CSTR("MANIFESTO DE ATUALIZAÃ‡ÃƒO INVÃLIDO");
    if (stage == OGS_LIT("integrity"))
        return OGS_CSTR("A INSTALAÃ‡ÃƒO TEM FICHEIROS ALTERADOS");
    if (stage == OGS_CSTR("repair"))
        return OGS_CSTR("NÃƒO FOI POSSÃVEL PREPARAR A REPARAÃ‡ÃƒO");
    if (stage == OGS_CSTR("version"))
        return OGS_CSTR("VERSÃƒO INSTALADA INVÃLIDA");
    if (stage == OGS_CSTR("download"))
        return OGS_CSTR("NÃƒO FOI POSSÃVEL DESCARREGAR A ATUALIZAÃ‡ÃƒO");
    if (stage == OGS_CSTR("sha256"))
        return OGS_CSTR("A VALIDAÃ‡ÃƒO DA ATUALIZAÃ‡ÃƒO FALHOU");
    if (stage == OGS_CSTR("prepare"))
        return OGS_CSTR("NÃƒO FOI POSSÃVEL PREPARAR A ATUALIZAÃ‡ÃƒO");
    if (stage == OGS_CSTR("launch-updater"))
        return OGS_CSTR("NÃƒO FOI POSSÃVEL INICIAR O ATUALIZADOR");

    return OGS_CSTR("NÃƒO FOI POSSÃVEL CONCLUIR A ATUALIZAÃ‡ÃƒO");
}

std::string ExactUserMessage(const std::string& stage, const std::string& detail) {
    if (IsOfflineNetworkFailure(detail))
        return OGS_CSTR("Sem ligaÃ§Ã£o Ã  Internet. Verifica o Wi-Fi ou o cabo de rede e tenta novamente. A aplicaÃ§Ã£o continuarÃ¡ a funcionar normalmente.");
    if (ContainsNetworkCode(detail, 12002))
        return OGS_CSTR("O servidor demorou demasiado tempo a responder. Verifica a ligaÃ§Ã£o e tenta novamente.");
    if (ContainsNetworkCode(detail, 12007))
        return OGS_CSTR("A ligaÃ§Ã£o estÃ¡ ativa, mas nÃ£o foi possÃ­vel localizar o servidor de atualizaÃ§Ãµes. Verifica o DNS, VPN, proxy ou firewall.");
    if (ContainsNetworkCode(detail, 12029))
        return OGS_CSTR("NÃ£o foi possÃ­vel estabelecer ligaÃ§Ã£o ao servidor de atualizaÃ§Ãµes. Verifica a firewall, VPN ou proxy.");
    if (ContainsNetworkCode(detail, 12030))
        return OGS_CSTR("A ligaÃ§Ã£o ao servidor foi interrompida. Tenta novamente dentro de alguns segundos.");
    if (ContainsNetworkCode(detail, 12175))
        return OGS_CSTR("O Windows nÃ£o conseguiu validar a ligaÃ§Ã£o segura. Confirma a data, a hora e os certificados do sistema.");
    if (ContainsNetworkCode(detail, 12180) || ContainsNetworkCode(detail, 12167))
        return OGS_CSTR("A configuraÃ§Ã£o automÃ¡tica de rede falhou. Verifica as definiÃ§Ãµes de proxy do Windows.");
    if (Contains(detail, OGS_CSTR("HTTP 404")) ||
        Contains(detail, OGS_CSTR("nenhuma versÃ£o estÃ¡vel publicada")) ||
        Contains(detail, OGS_CSTR("nÃ£o contÃ©m um asset update.json")))
        return OGS_CSTR("Ainda nÃ£o existe update.json pÃºblico no GitHub (repo OmniGhost-Updates). "
               "Confirma: (1) gh auth login no PC de build; (2) repositÃ³rio OmniGhost-Updates pÃºblico; "
               "(3) Build Publish|x64 atÃ© aparecer 'VersÃ£o X publicada com sucesso' e a URL do manifesto. "
               "Sem esse passo o launcher nÃ£o consegue procurar atualizaÃ§Ãµes.");
    if (Contains(detail, OGS_CSTR("HTTP 403")))
        return OGS_CSTR("O servidor recusou temporariamente o pedido. Tenta novamente mais tarde.");
    if (Contains(detail, OGS_CSTR("HTTP 429")))
        return OGS_CSTR("O limite temporÃ¡rio de pedidos foi atingido. Aguarda alguns minutos e tenta novamente.");
    if (Contains(detail, OGS_CSTR("HTTP 5")))
        return OGS_CSTR("O serviÃ§o de atualizaÃ§Ãµes estÃ¡ temporariamente indisponÃ­vel. Tenta novamente mais tarde.");
    if (stage == OGS_CSTR("release-resolution"))
        return OGS_CSTR("NÃ£o foi possÃ­vel confirmar a versÃ£o pÃºblica mais recente. Se o repositÃ³rio OmniGhost-Updates "
               "ainda nÃ£o tem Releases com update.json, faz Publish|x64 no Visual Studio (com gh autenticado). "
               "Se jÃ¡ publicaste, verifica a ligaÃ§Ã£o e tenta novamente.");
    if (stage == OGS_CSTR("manifest"))
        return OGS_CSTR("A informaÃ§Ã£o da nova versÃ£o foi descarregada, mas nÃ£o passou na validaÃ§Ã£o de seguranÃ§a.");
    if (stage == OGS_CSTR("integrity"))
        return OGS_CSTR("A verificaÃ§Ã£o encontrou ficheiros em falta ou alterados. Podes preparar uma reparaÃ§Ã£o segura.");
    if (stage == OGS_CSTR("repair"))
        return OGS_CSTR("NÃ£o foi possÃ­vel obter o pacote da versÃ£o instalada para reparaÃ§Ã£o.");
    if (stage == OGS_CSTR("version"))
        return OGS_CSTR("A versÃ£o instalada nÃ£o segue o formato esperado.");
    if (stage == OGS_CSTR("download"))
        return OGS_CSTR("O pacote da nova versÃ£o nÃ£o foi descarregado completamente.");
    if (stage == OGS_CSTR("sha256"))
        return OGS_CSTR("O pacote descarregado nÃ£o corresponde ao ficheiro publicado e foi recusado.");
    if (stage == OGS_CSTR("prepare"))
        return OGS_CSTR("A atualizaÃ§Ã£o foi descarregada, mas nÃ£o foi possÃ­vel preparÃ¡-la para instalaÃ§Ã£o.");
    if (stage == OGS_CSTR("launch-updater"))
        return OGS_CSTR("O Windows nÃ£o conseguiu iniciar o processo de instalaÃ§Ã£o da atualizaÃ§Ã£o.");

    return OGS_CSTR("Ocorreu um problema durante a atualizaÃ§Ã£o. Tenta novamente ou consulta os detalhes tÃ©cnicos.");
}

}

UpdateService& UpdateService::Instance() { static UpdateService service; return service; }
UpdateService::~UpdateService() { Shutdown(); }

void UpdateService::Initialize(Configuration configuration, std::unique_ptr<IHttpClient> http) {
    if (initialized_.exchange(true)) return;
    config_ = std::move(configuration);



    HttpPolicy::SetConfiguration(config_);
    http_ = http ? std::move(http) : std::make_unique<WinHttpClient>();

    const bool userDirectoriesReady = Paths::EnsureUserDirectories();
    RotateLogs(Paths::Logs());
    WriteLog(
        LogFile(),
        userDirectoriesReady ? LogLevel::Info : LogLevel::Warning,
        OmniGhost::Version,
        {},
        OGS_CSTR("initialize"),
        userDirectoriesReady
            ? OGS_CSTR("ServiÃ§o de atualizaÃ§Ã£o inicializado. Log: ") + LogFile().string()
            : OGS_CSTR("NÃ£o foi possÃ­vel preparar todas as pastas do utilizador. SerÃ¡ usado o fallback de log quando necessÃ¡rio."));





    if (config_.githubTlsCertificateThumbprints.empty()) {
        WriteLog(
            LogFile(),
            LogLevel::Warning,
            OmniGhost::Version,
            {},
            OGS_CSTR("tls-pinning"),
            OGS_CSTR("Pin TLS do GitHub INATIVO (githubTlsCertificateThumbprints vazio): a cadeia de "
            "atualizaÃ§Ã£o depende apenas da validaÃ§Ã£o CA do Windows. Consulta o comentÃ¡rio em "
            "update_types.h para ativar com pins SPKI SHA-256 reais."));
    }
    std::error_code cleanupError;
    const auto stale = fs::file_time_type::clock::now() - std::chrono::hours(48);
    for (const auto& entry : fs::directory_iterator(Paths::Updates(), fs::directory_options::skip_permission_denied, cleanupError)) {
        const std::wstring entryName = entry.path().filename().wstring();





        if (entry.path().extension().wstring() == OGS_WIDE(".partial")) {
            std::error_code timeError;
            const auto modified = entry.last_write_time(timeError);
            if (!timeError && modified < stale) fs::remove(entry.path(), cleanupError);
        }
        std::error_code typeError;
        const bool isRegular = entry.is_regular_file(typeError) && !typeError;
        if (isRegular && entryName.rfind(OGS_WIDE("startup-confirm-"), 0) == 0 &&
            entry.path().extension().wstring() == OGS_WIDE(".txt")) {
            std::error_code timeError;
            const auto modified = entry.last_write_time(timeError);
            if (!timeError && modified < stale) fs::remove(entry.path(), cleanupError);
        }
        const std::wstring directoryName = entryName;
        std::error_code dirError;
        const bool isDirectory = entry.is_directory(dirError) && !dirError;
        if (isDirectory &&
            (directoryName.rfind(OGS_WIDE("staging-"), 0) == 0 || directoryName.rfind(OGS_WIDE("runner-"), 0) == 0)) {
            std::error_code timeError;
            const auto modified = entry.last_write_time(timeError);
            if (!timeError && modified < stale) fs::remove_all(entry.path(), cleanupError);
        }
        cleanupError.clear();
    }
    SetSnapshot([](Snapshot& value) {
        value.installedVersion = OmniGhost::Version;
        if (g_startupNotice.rolledBack) {
            value.status = Status::RolledBack;
            value.rollbackRestored = true;
            value.availableVersion = g_startupNotice.failedVersion;
            value.errorStage = g_startupNotice.rollbackStage;
            value.userMessage = OGS_CSTR("A versÃ£o anterior foi restaurada com sucesso.");
        } else if (g_startupNotice.updated) {
            value.status = Status::Completed;
            value.recentlyUpdated = true;
            value.updateFromVersion = g_startupNotice.fromVersion;
            value.availableVersion = g_startupNotice.toVersion;
            value.releaseNotesForCompletedUpdate = g_startupNotice.releaseNotesUrl;
            value.releaseNotesUrl = g_startupNotice.releaseNotesUrl;
            value.userMessage = OGS_CSTR("AtualizaÃ§Ã£o concluÃ­da com sucesso.");
        } else {
            value.userMessage = OGS_CSTR("A verificar atualizaÃ§Ãµes em segundo planoâ€¦");
        }
    });
}

bool UpdateService::ApplyUserPreferences(Channel channel, bool automaticDownload, bool installOnExit) {
#if defined(OMNIGHOST_PUBLISH_BUILD)
    channel = Channel::Stable;
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
        snapshot_.userMessage = OGS_CSTR("Canal alterado. Faz uma nova verificaÃ§Ã£o de atualizaÃ§Ãµes.");
    }
    return true;
}

void UpdateService::SetSnapshot(const std::function<void(Snapshot&)>& change) { std::lock_guard lock(mutex_); change(snapshot_); }
Snapshot UpdateService::GetSnapshot() const { std::lock_guard lock(mutex_); return snapshot_; }

void UpdateService::CheckAsync(bool force) {
    if (!initialized_) Initialize();
    if (busy_.exchange(true)) {
        WriteLog(LogFile(), LogLevel::Warning, OmniGhost::Version, {}, OGS_CSTR("check"),
            OGS_CSTR("Pedido de verificaÃ§Ã£o ignorado porque jÃ¡ existe uma operaÃ§Ã£o do atualizador em curso."));
        return;
    }

    cancelled_ = false;
    {

        std::lock_guard workerLock(workerMutex_);
        if (worker_.joinable())
            worker_.join();

        worker_ = std::jthread([this, force](std::stop_token stopToken) {
            Platform::SetCurrentThreadName(L"OmniGhost.UpdateCheck");
            std::stop_callback cancelOnStop(stopToken, [this] { cancelled_.store(true, std::memory_order_release); });
            auto busyReset = Platform::MakeScopeExit([this] { busy_ = false; });
            try {
                RunCheck(force);
            } catch (const std::exception& exception) {
                SetError(OGS_CSTR("A verificaÃ§Ã£o terminou de forma inesperada."), exception.what(), OGS_CSTR("worker"));
            } catch (...) {
                SetError(OGS_CSTR("A verificaÃ§Ã£o terminou de forma inesperada."),
                    OGS_CSTR("ExceÃ§Ã£o nÃ£o identificada na thread de verificaÃ§Ã£o."), OGS_CSTR("worker"));
            }
        });
    }
}

void UpdateService::RunCheck(bool force) {
    if (!force && RecentCheck(config_)) {
        SetSnapshot([](Snapshot& s) {
            s.status = Status::Idle;
            s.userMessage = OGS_CSTR("VerificaÃ§Ã£o recente guardada localmente.");
        });
        return;
    }

    SetSnapshot([](Snapshot& s) {
        s.status = Status::Checking;
        s.userMessage = OGS_CSTR("A verificar atualizaÃ§Ãµesâ€¦");
        s.availableVersion.clear();
        s.releaseNotesUrl.clear();
        s.errorTitle.clear();
        s.errorStage.clear();
        s.technicalDetail.clear();
    });

    const auto installed = SemVersion::Parse(OmniGhost::Version);
    if (!installed) {
        SetError({}, OGS_CSTR("OMNIGHOST_VERSION nÃ£o segue SemVer."), OGS_CSTR("version"));
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
            OGS_CSTR("release-resolution"),
            OGS_CSTR("A consultar a lista pÃºblica de versÃµes para encontrar a versÃ£o estÃ¡vel mais recente."));

        HttpResult releasesResponse = http_->GetText(
            config_.releasesApiUrl,
            timeoutMilliseconds,
            cancelled_);

        if (cancelled_.load()) {
            SetError(OGS_CSTR("VerificaÃ§Ã£o cancelada."), OGS_CSTR("O utilizador cancelou a verificaÃ§Ã£o."), OGS_CSTR("release-resolution"));
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
                    OGS_CSTR("release-resolution"),
                    OGS_CSTR("VersÃ£o estÃ¡vel mais recente encontrada: ") +
                        resolvedRelease->tag + ".");

                if (resolvedRelease->version.Compare(*installed) <= 0) {
                    MarkChecked();
                    SetSnapshot([&](Snapshot& s) {
                        s.status = Status::UpToDate;
                        s.availableVersion = resolvedRelease->version.original;
                        s.releaseNotesUrl = resolvedRelease->releaseUrl;
                        s.userMessage = OGS_CSTR("O OmniGhost estÃ¡ atualizado.");
                        s.errorTitle.clear();
                        s.errorStage.clear();
                        s.technicalDetail.clear();
                    });
                    WriteLog(
                        LogFile(),
                        LogLevel::Info,
                        OmniGhost::Version,
                        resolvedRelease->version.original,
                        OGS_CSTR("compare"),
                        OGS_CSTR("A versÃ£o estÃ¡vel mais recente nÃ£o Ã© superior Ã  versÃ£o instalada."));
                    RecordHealth(OGS_CSTR("up-to-date"), resolvedRelease->version.original, {}, {}, {},
                        OGS_CSTR("highest stable release is not newer than installed version"));
                    return;
                }
            } else {
                releaseResolutionWarning = releaseResult.error;
                WriteLog(
                    LogFile(),
                    LogLevel::Warning,
                    OmniGhost::Version,
                    {},
                    OGS_CSTR("release-resolution"),
                    releaseResolutionWarning + OGS_CSTR(" SerÃ¡ usado o endereÃ§o de compatibilidade da versÃ£o mais recente."));
            }
        } else {
            releaseResolutionWarning = releasesResponse.error;
            WriteLog(
                LogFile(),
                LogLevel::Warning,
                OmniGhost::Version,
                {},
                OGS_CSTR("release-resolution"),
                releaseResolutionWarning + OGS_CSTR(" SerÃ¡ usado o endereÃ§o de compatibilidade da versÃ£o mais recente."));
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
        OGS_CSTR("check"),
        OGS_CSTR("A consultar o manifesto: ") + manifestUrl);

    HttpResult response = http_->GetText(
        manifestUrl,
        timeoutMilliseconds,
        cancelled_);

    if (!response.error.empty()) {
        std::string detail = response.error;
        if (!releaseResolutionWarning.empty()) {
            detail = OGS_CSTR("ResoluÃ§Ã£o da versÃ£o: ") + releaseResolutionWarning +
                     OGS_CSTR(" | Manifesto: ") + detail;
        }
        SetError({}, detail, OGS_CSTR("check"));
        return;
    }

    ManifestResult parsed =
        ParseAndValidateManifest(response.body, config_, Architecture());
    if (!parsed.manifest) {
        SetError({}, parsed.error, OGS_CSTR("manifest"));
        return;
    }

    Manifest manifest = std::move(*parsed.manifest);

    if (resolvedRelease &&
        manifest.version.Compare(resolvedRelease->version) != 0) {
        SetError(
            {},
            OGS_CSTR("A versÃ£o do update.json ('") + manifest.version.original +
                OGS_CSTR("') nÃ£o corresponde Ã  versÃ£o selecionada ('") +
                resolvedRelease->version.original + "').",
            OGS_CSTR("manifest"));
        return;
    }

    if (manifest.version.Compare(*installed) <= 0) {


        if (releaseListResponseReceived &&
            !releaseResolutionWarning.empty()) {
            SetError(
                {},
                OGS_CSTR("A API de versÃµes respondeu, mas nÃ£o foi possÃ­vel selecionar "
                "com seguranÃ§a a maior versÃ£o estÃ¡vel: ") +
                releaseResolutionWarning +
                OGS_CSTR(" O marcador da versÃ£o mais recente devolveu ") +
                manifest.version.original + ".",
                OGS_CSTR("release-resolution"));
            return;
        }

        if (!releaseResolutionWarning.empty() &&
            manifest.version.Compare(*installed) < 0) {
            SetError(
                {},
                OGS_CSTR("A lista de versÃµes nÃ£o pÃ´de ser consultada e o marcador da versÃ£o mais recente "
                "devolveu a versÃ£o ") + manifest.version.original +
                OGS_CSTR(", que Ã© anterior Ã  versÃ£o instalada ") +
                installed->original + ".",
                OGS_CSTR("release-resolution"));
            return;
        }

        MarkChecked();
        SetSnapshot([&](Snapshot& s) {
            s.status = Status::UpToDate;
            s.availableVersion = manifest.version.original;
            s.releaseNotesUrl = manifest.releaseNotesUrl;
            s.userMessage = OGS_CSTR("O OmniGhost estÃ¡ atualizado.");
            s.errorTitle.clear();
            s.errorStage.clear();
            s.technicalDetail.clear();
        });
        WriteLog(
            LogFile(),
            LogLevel::Info,
            OmniGhost::Version,
            manifest.version.original,
            OGS_CSTR("compare"),
            OGS_CSTR("A versÃ£o publicada nÃ£o Ã© superior Ã  versÃ£o instalada."));
        RecordHealth(OGS_CSTR("up-to-date"), manifest.version.original, {}, {}, {},
            OGS_CSTR("published version is not newer than installed version"));
        return;
    }

    const Package* selected = SelectPackage(manifest, Architecture());
    if (!selected) {
        SetError({}, OGS_CSTR("NÃ£o existe pacote compatÃ­vel com esta arquitetura."), OGS_CSTR("manifest"));
        return;
    }

    MarkChecked();
    const bool belowMinimumSupported =
        installed->Compare(manifest.minimumSupportedVersion) < 0;





    const bool mandatory = manifest.mandatory || belowMinimumSupported;
    std::string mandatoryReason;
    if (mandatory) {
        if (!manifest.mandatoryReason.empty()) {
            mandatoryReason = manifest.mandatoryReason;
        } else if (belowMinimumSupported) {
            mandatoryReason = OGS_CSTR("A versÃ£o instalada ") + installed->original +
                OGS_CSTR(" estÃ¡ abaixo da versÃ£o mÃ­nima suportada ") +
                manifest.minimumSupportedVersion.original + ".";
        } else {
            mandatoryReason = OGS_CSTR("Ã‰ necessÃ¡rio instalar a versÃ£o mais recente para continuar.");
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
            ? OGS_CSTR("Ã‰ necessÃ¡rio atualizar antes de continuar. ") + mandatoryReason
            : OGS_CSTR("EstÃ¡ disponÃ­vel uma nova versÃ£o.");
        snapshot_.errorTitle.clear();
        snapshot_.errorStage.clear();
        snapshot_.technicalDetail.clear();
    }

    WriteLog(
        LogFile(),
        LogLevel::Info,
        OmniGhost::Version,
        manifest.version.original,
        OGS_CSTR("available"),
        mandatory ? OGS_CSTR("AtualizaÃ§Ã£o obrigatÃ³ria.") : OGS_CSTR("AtualizaÃ§Ã£o opcional."));
    RecordHealth(OGS_CSTR("available"), manifest.version.original, selected->sha256, {}, {},
        mandatory ? OGS_CSTR("mandatory") : OGS_CSTR("optional"));

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
        WriteLog(LogFile(), LogLevel::Warning, OmniGhost::Version, {}, OGS_CSTR("download"),
            OGS_CSTR("Pedido de transferÃªncia ignorado porque jÃ¡ existe uma operaÃ§Ã£o do atualizador em curso."));
        return;
    }
    installOnExit_.store(true, std::memory_order_release);
    SetSnapshot([](Snapshot& s) { s.installOnExit = true; });
    StartDownloadWorker();
}

void UpdateService::DownloadAsync() {



    if (busy_.exchange(true)) {
        WriteLog(LogFile(), LogLevel::Warning, OmniGhost::Version, {}, OGS_CSTR("download"),
            OGS_CSTR("Another update operation is already in progress."));
        return;
    }
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
    StartDownloadWorker();
}

void UpdateService::StartDownloadWorker() {
    cancelled_ = false;
    {

        std::lock_guard workerLock(workerMutex_);
        if (worker_.joinable())
            worker_.join();

        worker_ = std::jthread([this](std::stop_token stopToken) {
            Platform::SetCurrentThreadName(L"OmniGhost.UpdateDownload");
            std::stop_callback cancelOnStop(stopToken, [this] { cancelled_.store(true, std::memory_order_release); });
            auto busyReset = Platform::MakeScopeExit([this] { busy_ = false; });
            try {
                RunDownload();
            } catch (const std::exception& exception) {
                SetError(OGS_CSTR("A transferÃªncia terminou de forma inesperada."), exception.what(), OGS_CSTR("worker"));
            } catch (...) {
                SetError(OGS_CSTR("A transferÃªncia terminou de forma inesperada."),
                    OGS_CSTR("ExceÃ§Ã£o nÃ£o identificada na tarefa de transferÃªncia."), OGS_CSTR("worker"));
            }
        });
    }
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
        SetError(OGS_CSTR("A atualizaÃ§Ã£o ainda nÃ£o estÃ¡ preparada para download."),
            OGS_CSTR("Manifesto ou pacote ausente ao iniciar a transferÃªncia."), OGS_CSTR("download"));
        return;
    }
    SetSnapshot([](Snapshot& s) {
        s.status = Status::Downloading;
        s.userMessage = OGS_CSTR("A descarregar e validar a atualizaÃ§Ã£oâ€¦");
        s.errorTitle.clear();
        s.errorStage.clear();
        s.technicalDetail.clear();
    });
    const fs::path partial = Paths::Updates() / fs::path(package.fileName + OGS_CSTR(".partial")); const fs::path final = Paths::Updates() / fs::path(package.fileName);
    std::error_code ec; fs::remove(partial, ec);
    WriteLog(LogFile(), LogLevel::Info, OmniGhost::Version, version, OGS_CSTR("download"),
        OGS_CSTR("TransferÃªncia iniciada; tamanho esperado=") + std::to_string(package.size) + OGS_CSTR(" bytes."));
    RecordHealth(OGS_CSTR("download-started"), version, package.sha256, {}, {},
        OGS_CSTR("download worker started"));
    HttpResult result = http_->Download(package.url, partial, package.size, config_.maximumPackageBytes,
        static_cast<int>(config_.networkTimeout.count() * 1000), cancelled_, [this](auto received, auto total, auto speed) {
            SetSnapshot([&](Snapshot& s) { s.bytesReceived = received; s.bytesTotal = total; s.bytesPerSecond = speed; });
        });
    if (!result.error.empty()) { SetError({}, result.error, OGS_CSTR("download")); return; }
    std::string digest, error;
    if (!Sha256File(partial, digest, error) || !ConstantTimeEquals(digest, package.sha256)) {
        fs::remove(partial, ec); SetError({}, error.empty() ? OGS_CSTR("SHA-256 diferente do manifesto.") : error, OGS_CSTR("sha256")); return;
    }
    fs::remove(final, ec); fs::rename(partial, final, ec); if (ec) { SetError({}, ec.message(), OGS_CSTR("prepare")); return; }
    {

        std::lock_guard lock(mutex_);
        downloadedPackage_ = final;
    }
    SetSnapshot([this](Snapshot& s) {
        s.status = Status::Ready;
        s.installOnExit = installOnExit_.load(std::memory_order_acquire);
        s.userMessage = s.installOnExit
            ? OGS_CSTR("AtualizaÃ§Ã£o validada. SerÃ¡ instalada automaticamente ao sair.")
            : OGS_CSTR("AtualizaÃ§Ã£o validada e pronta para instalar.");
        s.errorTitle.clear();
        s.errorStage.clear();
        s.technicalDetail.clear();
    });
    WriteLog(LogFile(), LogLevel::Info, OmniGhost::Version, version, OGS_CSTR("ready"), OGS_CSTR("Tamanho e SHA-256 validados."));
    RecordHealth(OGS_CSTR("ready"), version, package.sha256, {}, {}, OGS_CSTR("package validated"));




    if (installImmediately_.exchange(false, std::memory_order_acq_rel)) {
        SetSnapshot([](Snapshot& s) {
            s.status = Status::Installing;
            s.installOnExit = false;
            s.userMessage = OGS_CSTR("A concluir a instalaÃ§Ã£o e a reiniciar o OmniGhostâ€¦");
        });
        (void)InstallPreparedUpdate();
    }
}

void UpdateService::VerifyInstallationAsync() {
    if (!initialized_) Initialize();
    if (busy_.exchange(true)) return;
    cancelled_ = false;
    {

        std::lock_guard workerLock(workerMutex_);
        if (worker_.joinable()) worker_.join();
        SetSnapshot([](Snapshot& s) {
            s.integrityStatus = IntegrityStatus::Checking;
            s.integrityMessage = OGS_CSTR("A verificar ficheiros instaladosâ€¦");
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
            WriteLog(LogFile(), LogLevel::Warning, OmniGhost::Version, {}, OGS_CSTR("integrity"), detail);
        } else {
            WriteLog(LogFile(), LogLevel::Info, OmniGhost::Version, {}, OGS_CSTR("integrity"), report.message);
        }
        });
    }
}

void UpdateService::PrepareRepairAsync() {
    if (!initialized_) Initialize();
    if (busy_.exchange(true)) return;
    cancelled_ = false;
    {

        std::lock_guard workerLock(workerMutex_);
        if (worker_.joinable()) worker_.join();
        worker_ = std::jthread([this](std::stop_token stopToken) {
        Platform::SetCurrentThreadName(L"OmniGhost.UpdateRepair");
        std::stop_callback cancelOnStop(stopToken, [this] { cancelled_.store(true, std::memory_order_release); });
        auto busyReset = Platform::MakeScopeExit([this] { busy_ = false; });
        try { RunPrepareRepair(); }
        catch (const std::exception& exception) { SetError({}, exception.what(), OGS_CSTR("repair")); }
        catch (...) { SetError({}, OGS_CSTR("ExceÃ§Ã£o nÃ£o identificada ao preparar reparaÃ§Ã£o."), OGS_CSTR("repair")); }
    });
    }
}

void UpdateService::RunPrepareRepair() {
    const std::string installed = OmniGhost::Version;
    const std::string manifestUrl = config_.allowedPackageUrlPrefix + "v" + installed + OGS_CSTR("/update.json");
    SetSnapshot([](Snapshot& s) {
        s.status = Status::Checking;
        s.repairMode = true;
        s.userMessage = OGS_CSTR("A localizar o pacote da versÃ£o instalada para reparaÃ§Ã£oâ€¦");
        s.errorTitle.clear(); s.errorStage.clear(); s.technicalDetail.clear();
    });
    const int timeoutMilliseconds = static_cast<int>(config_.networkTimeout.count() * 1000);
    HttpResult response = http_->GetText(manifestUrl, timeoutMilliseconds, cancelled_);
    if (!response.error.empty()) { SetError({}, response.error, OGS_CSTR("repair")); return; }
    ManifestResult parsed = ParseAndValidateManifest(response.body, config_, Architecture());
    if (!parsed.manifest) { SetError({}, parsed.error, OGS_CSTR("manifest")); return; }
    Manifest manifest = std::move(*parsed.manifest);
    if (manifest.version.original != installed) {
        SetError({}, OGS_CSTR("O manifesto de reparaÃ§Ã£o nÃ£o corresponde Ã  versÃ£o instalada."), OGS_CSTR("repair"));
        return;
    }
    const Package* selected = SelectPackage(manifest, Architecture());
    if (!selected) { SetError({}, OGS_CSTR("NÃ£o existe pacote x64 para reparar esta versÃ£o."), OGS_CSTR("repair")); return; }
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
        if (s.status == Status::Ready) s.userMessage = OGS_CSTR("Pacote de reparaÃ§Ã£o validado e pronto para reiniciar.");
    });
}

bool UpdateService::ScheduleInstallOnExit() {
    const Snapshot current = GetSnapshot();
    if (current.status != Status::Ready || current.mandatory) return false;
    installOnExit_.store(true, std::memory_order_release);
    SetSnapshot([](Snapshot& s) {
        s.installOnExit = true;
        s.userMessage = s.repairMode ? OGS_CSTR("A reparaÃ§Ã£o serÃ¡ instalada ao sair.") : OGS_CSTR("A atualizaÃ§Ã£o serÃ¡ instalada ao sair.");
    });
    return true;
}

void UpdateService::RetryLastFailure() {
    const Snapshot current = GetSnapshot();
    const std::string stage = current.errorStage;
    if (current.repairMode && (stage == OGS_CSTR("repair") || stage == OGS_CSTR("manifest") || stage == OGS_CSTR("check"))) {
        PrepareRepairAsync();
        return;
    }
    if (stage == OGS_CSTR("download") || stage == OGS_CSTR("sha256") || stage == OGS_CSTR("prepare")) {
        DownloadAsync();
        return;
    }
    if (stage == OGS_CSTR("launch-updater")) {
        if (fs::exists(downloadedPackage_)) {
            SetSnapshot([](Snapshot& s) { s.status = Status::Ready; });
            if (InstallPreparedUpdate()) return;
        }
        if (current.repairMode) PrepareRepairAsync(); else CheckAsync(true);
        return;
    }
    if (stage == OGS_LIT("integrity")) { VerifyInstallationAsync(); return; }
    if (stage == OGS_CSTR("repair")) { PrepareRepairAsync(); return; }
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
    std::string version, expectedHash, packageUrl; std::uint64_t expectedSize{};
    fs::path downloadedPackage;
    { std::lock_guard lock(mutex_); if (!manifest_ || !package_ ||
          (snapshot_.status != Status::Available && snapshot_.status != Status::Ready &&
           snapshot_.status != Status::Installing)) return false;
      version = manifest_->version.original; expectedHash = package_->sha256;
      expectedSize = package_->size; packageUrl = package_->url;

      downloadedPackage = downloadedPackage_.empty()
          ? Paths::Updates() / fs::path(package_->fileName)
          : downloadedPackage_; }
    const fs::path installedExecutable = Paths::Executable();
    if (!fs::exists(installedExecutable)) { SetError({}, OGS_CSTR("ExecutÃ¡vel instalado em falta."), OGS_CSTR("launch-updater")); return false; }
    std::wstring runnerToken;
    std::string randomError;
    if (!RandomTokenHex(16, runnerToken, randomError)) {
        SetError({}, randomError, OGS_CSTR("launch-updater"));
        return false;
    }
    const fs::path runnerDirectory = Paths::Updates() / (OGS_WIDE("runner-") + runnerToken);
    const fs::path runner = runnerDirectory / OmniGhost::MainExecutable; std::error_code ec;
    fs::create_directories(runnerDirectory, ec);
    fs::copy_file(installedExecutable, runner, fs::copy_options::overwrite_existing, ec);




    std::string sourceRunnerHash, copiedRunnerHash, runnerHashError;
    if (!Sha256File(installedExecutable, sourceRunnerHash, runnerHashError) ||
        !Sha256File(runner, copiedRunnerHash, runnerHashError) ||
        !ConstantTimeEquals(sourceRunnerHash, copiedRunnerHash)) {
        SetError({}, runnerHashError.empty() ? OGS_CSTR("A cÃ³pia temporÃ¡ria do atualizador nÃ£o corresponde ao executÃ¡vel instalado.") : runnerHashError,
            OGS_CSTR("launch-updater"));
        return false;
    }

    std::wostringstream arguments; arguments << OGS_WIDE("--apply-update --pid ") << GetCurrentProcessId() << OGS_WIDE(" --package ") << Quote(downloadedPackage)
        << OGS_WIDE(" --updates-root ") << Quote(Paths::Updates())
        << OGS_WIDE(" --target ") << Quote(Paths::InstallDirectory()) << OGS_WIDE(" --executable ") << Quote(fs::path(OmniGhost::MainExecutable))
		<< OGS_WIDE(" --version \"") << Platform::Utf8ToWide(version) << L"\""
        << OGS_WIDE(" --package-url ") << Quote(Platform::Utf8ToWide(packageUrl))
        << OGS_WIDE(" --sha256 \"") << Platform::Utf8ToWide(expectedHash) << OGS_WIDE("\" --size ") << expectedSize
        << OGS_WIDE(" --timeout ") << config_.updaterExitTimeoutSeconds
        << OGS_WIDE(" --from-version \"") << Platform::Utf8ToWide(OmniGhost::Version) << L"\"";
    const Snapshot installSnapshot = GetSnapshot();
    if (!installSnapshot.releaseNotesUrl.empty())
        arguments << OGS_WIDE(" --notes-url ") << Quote(Platform::Utf8ToWide(installSnapshot.releaseNotesUrl));
    const bool needElevation = !DirectoryWritable(Paths::InstallDirectory());
    const wchar_t* verb = needElevation ? OGS_WCSTR("runas") : OGS_WCSTR("open");
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
        const DWORD code = GetLastError();
        if (needElevation && code == ERROR_CANCELLED) {
            SetError(OGS_CSTR("A atualizaÃ§Ã£o requer privilÃ©gios de administrador."),
                OGS_CSTR("O controlo de conta de utilizador (UAC) foi negado ou cancelado. Execute o OmniGhost como administrador para instalar a atualizaÃ§Ã£o."), OGS_CSTR("launch-updater"));
        } else {
            SetError({}, WindowsError(GetLastError()), OGS_CSTR("launch-updater"));
        }
        return false;
    }
    Platform::UniqueHandle updaterProcess(launch.hProcess);




    const DWORD earlyExit = WaitForSingleObject(updaterProcess.get(), 750);
    if (earlyExit == WAIT_OBJECT_0) {
        DWORD exitCode = 0;
        GetExitCodeProcess(updaterProcess.get(), &exitCode);
        const std::string detail = OGS_CSTR("O processo temporÃ¡rio do atualizador terminou imediatamente (cÃ³digo ") +
            std::to_string(exitCode) + ").";
        SetError(OGS_CSTR("NÃ£o foi possÃ­vel iniciar a instalaÃ§Ã£o da atualizaÃ§Ã£o."), detail, OGS_CSTR("launch-updater"));
        return false;
    }
    if (earlyExit == WAIT_FAILED) {
        SetError(OGS_CSTR("NÃ£o foi possÃ­vel acompanhar o processo de atualizaÃ§Ã£o."),
            WindowsError(GetLastError()), OGS_CSTR("launch-updater"));
        return false;
    }
    WriteLog(LogFile(), LogLevel::Info, OmniGhost::Version, version, OGS_CSTR("launch-updater"),
        OGS_CSTR("Atualizador temporÃ¡rio iniciado e a aguardar o encerramento do launcher."));
    SetSnapshot([](Snapshot& s) {
        s.status = Status::Installing;
        s.installOnExit = false;
        s.userMessage = s.repairMode ? OGS_CSTR("A reiniciar para concluir a reparaÃ§Ã£oâ€¦") : OGS_CSTR("A reiniciar para concluir a atualizaÃ§Ã£oâ€¦");
        s.errorTitle.clear();
        s.errorStage.clear();
        s.technicalDetail.clear();
    });
    RecordHealth(OGS_CSTR("install-launched"), version, expectedHash, OGS_CSTR("verification-delegated-to-independent-updater"));


    OmniGhost::RemoteUsage::SuppressFinalFlush();
    exitRequested_ = true; return true;
}

void UpdateService::InstallUpdateAsync() {
    const Snapshot current = GetSnapshot();
    if ((current.status != Status::Available && current.status != Status::Ready) ||
        busy_.exchange(true, std::memory_order_acq_rel))
        return;
    installImmediately_.store(current.status == Status::Available, std::memory_order_release);
    SetSnapshot([&](Snapshot& s) {
        s.status = current.status == Status::Available ? Status::Downloading : Status::Installing;
        s.installOnExit = false;
        s.userMessage = current.status == Status::Available
            ? OGS_CSTR("A descarregar e validar a atualizaÃ§Ã£oâ€¦")
            : OGS_CSTR("A concluir a instalaÃ§Ã£o e a reiniciar o OmniGhostâ€¦");
        s.errorTitle.clear(); s.errorStage.clear(); s.technicalDetail.clear();
    });
    {
        std::lock_guard workerLock(workerMutex_);
        if (worker_.joinable()) worker_.join();
        worker_ = std::jthread([this](std::stop_token) {
            Platform::SetCurrentThreadName(L"OmniGhost.UpdateAndInstall");
            auto reset = Platform::MakeScopeExit([this] { busy_.store(false, std::memory_order_release); });
            try {
                if (installImmediately_.load(std::memory_order_acquire))
                    RunDownload();
                else
                    (void)InstallPreparedUpdate();
            }
            catch (const std::exception& exception) {
                installImmediately_.store(false, std::memory_order_release);
                SetError(OGS_CSTR("NÃ£o foi possÃ­vel iniciar o atualizador independente."), exception.what(), OGS_CSTR("launch-updater"));
            } catch (...) {
                installImmediately_.store(false, std::memory_order_release);
                SetError(OGS_CSTR("NÃ£o foi possÃ­vel iniciar o atualizador independente."),
                         OGS_CSTR("Falha inesperada ao preparar o processo."), OGS_CSTR("launch-updater"));
            }
        });
    }
}

void UpdateService::InstallPreparedUpdateAsync() {
    const Snapshot current = GetSnapshot();
    if (current.status != Status::Ready || busy_.exchange(true, std::memory_order_acq_rel))
        return;

    SetSnapshot([](Snapshot& s) {
        s.status = Status::Installing;
        s.installOnExit = false;
        s.userMessage = s.repairMode
            ? OGS_CSTR("A preparar a reparaÃ§Ã£oâ€¦")
            : OGS_CSTR("A preparar a atualizaÃ§Ã£oâ€¦");
        s.errorTitle.clear();
        s.errorStage.clear();
        s.technicalDetail.clear();
    });

    {
        std::lock_guard workerLock(workerMutex_);
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
                SetError(OGS_CSTR("NÃ£o foi possÃ­vel preparar a instalaÃ§Ã£o da atualizaÃ§Ã£o."),
                    exception.what(), OGS_CSTR("launch-updater"));
            } catch (...) {
                SetError(OGS_CSTR("NÃ£o foi possÃ­vel preparar a instalaÃ§Ã£o da atualizaÃ§Ã£o."),
                    OGS_CSTR("Falha inesperada no arranque do atualizador."), OGS_CSTR("launch-updater"));
            }
        });
    }
}

void UpdateService::DeferForSession() {
    installOnExit_.store(false, std::memory_order_release);
    std::lock_guard lock(mutex_);
    snapshot_.installOnExit = false;
    snapshot_.status = Status::Deferred;
    snapshot_.userMessage = OGS_CSTR("AtualizaÃ§Ã£o adiada nesta sessÃ£o.");
}
void UpdateService::Cancel() {
    cancelled_ = true;
    installImmediately_.store(false, std::memory_order_release);


    std::lock_guard workerLock(workerMutex_);
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
    installImmediately_.store(false, std::memory_order_release);
    const std::string title = ExactErrorTitle(stage, detail);
    const std::string resolvedMessage =
        userMessage.empty() ? ExactUserMessage(stage, detail) : userMessage;

    SetSnapshot([&](Snapshot& s) {
        s.status = Status::Error;
        s.userMessage = resolvedMessage;
        s.errorTitle = title;
        s.errorStage = stage;
        s.technicalDetail = detail.empty()
            ? OGS_CSTR("Nenhum detalhe tÃ©cnico foi devolvido pela operaÃ§Ã£o.")
            : detail;
    });
    WriteLog(LogFile(), LogLevel::Error, OmniGhost::Version, GetSnapshot().availableVersion, stage, detail);
    RecordHealth(OGS_CSTR("error"), GetSnapshot().availableVersion, {}, {}, {}, stage + ": " + detail);
}
void UpdateService::Shutdown() {
    cancelled_ = true;
    {

        std::lock_guard workerLock(workerMutex_);
        if (worker_.joinable()) {
            worker_.request_stop();
            worker_.join();
        }
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
        if (arg == OGS_WIDE("--updated-from") && i + 1 < argc) {
            g_startupNotice.updated = true;
            g_startupNotice.fromVersion = Platform::WideToUtf8(argv[++i]);
        } else if (arg == OGS_WIDE("--updated-to") && i + 1 < argc) {
            g_startupNotice.updated = true;
            g_startupNotice.toVersion = Platform::WideToUtf8(argv[++i]);
        } else if (arg == OGS_WIDE("--update-notes-url") && i + 1 < argc) {
            g_startupNotice.releaseNotesUrl = Platform::WideToUtf8(argv[++i]);
        } else if (arg == OGS_WIDE("--rollback-restored")) {
            g_startupNotice.rolledBack = true;
            g_startupNotice.updated = false;
        } else if (arg == OGS_WIDE("--failed-version") && i + 1 < argc) {
            g_startupNotice.failedVersion = Platform::WideToUtf8(argv[++i]);
        } else if (arg == OGS_WIDE("--rollback-stage") && i + 1 < argc) {
            g_startupNotice.rollbackStage = Platform::WideToUtf8(argv[++i]);
        }
    }
}

bool ConfirmUpdaterStartupFromCommandLine(int argc, wchar_t** argv) {
    std::wstring token, file;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::wstring(argv[i]) == OGS_WIDE("--update-confirm")) token = argv[++i];
        else if (std::wstring(argv[i]) == OGS_WIDE("--update-confirm-file")) file = argv[++i];
    }
    const fs::path confirmationPath(file);
    const fs::path confirmationDirectory = confirmationPath.parent_path();
    const std::wstring confirmationName = confirmationPath.filename().wstring();
    const bool validConfirmationName =
        confirmationName.rfind(OGS_WIDE("startup-confirm-"), 0) == 0 &&
        confirmationPath.extension() == fs::path(std::wstring(OGS_WIDE(".txt")));




    const bool validConfirmationDirectory =
        _wcsicmp(confirmationDirectory.filename().c_str(), OGS_WCSTR("updates-staging")) == 0 &&
        DescendantPath(confirmationDirectory, Paths::RuntimeRoot());
    if (!IsHexToken(token, 64) || confirmationPath.empty() || !confirmationPath.is_absolute() ||
        !validConfirmationName || !validConfirmationDirectory) {
        return false;
    }

    std::wofstream stream(confirmationPath, std::ios::trunc);
    if (!stream) return false;
    stream << token;
    stream.flush();
    return static_cast<bool>(stream);
}
}
