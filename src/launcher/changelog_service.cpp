#include "changelog_service.h"

#include "../platform/app_paths.h"
#include "../platform/scope_exit.h"
#include "../platform/thread_utils.h"
#include "../app_version.h"
#include "../updater/http_client.h"
#include "../updater/json.h"
#include "../updater/github_release_resolver.h"
#include "../updater/update_log.h"
#include "../updater/update_types.h"
#include "../platform/session_log.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <variant>
#include "platform/strprotect/strprotect.h"

namespace fs = std::filesystem;

namespace Launcher {
namespace {

const std::string kReleasesApiUrl =
    OGS_LIT("https://api.github.com/repos/rodrigomatossilva07-rgb/"
    "OmniGhost-Updates/releases?per_page=30");
const std::string kChangelogFallbackUrl =
    OGS_LIT("https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/"
    "releases/latest/download/changelog.json");
const std::string kReleasePagePrefix =
    OGS_LIT("https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/releases/tag/");
constexpr std::size_t kMaximumReleases = 200;
constexpr std::size_t kMaximumModulesPerRelease = 32;
constexpr std::size_t kMaximumChangesPerModule = 500;
constexpr std::size_t kMaximumTextLength = 4096;

namespace SharedJson = OmniGhost::Update::Json;
using Json = SharedJson::Value;
using Object = SharedJson::Object;
using Array = SharedJson::Array;

const Json* Field(const Object& object, const char* name) {
    return SharedJson::Field(object, name);
}

bool Text(const Object& object, const char* name, std::string& output,
          bool allowEmpty = false) {
    const Json* field = Field(object, name);
    const std::string* value = field ? SharedJson::AsString(*field) : nullptr;
    if (!value || value->size() > kMaximumTextLength || (!allowEmpty && value->empty()))
        return false;
    output = *value;
    return true;
}

bool OptionalText(const Object& object, const char* name, std::string& output) {
    const Json* field = Field(object, name);
    if (!field) {
        output.clear();
        return true;
    }
    if (std::holds_alternative<std::nullptr_t>(field->value)) {
        output.clear();
        return true;
    }
    const std::string* value = SharedJson::AsString(*field);
    if (!value || value->size() > kMaximumTextLength) return false;
    output = *value;
    return true;
}

bool Number(const Object& object, const char* name, int& output) {
    const Json* field = Field(object, name);
    std::uint64_t value = 0;
    if (!field || !SharedJson::AsUInt64(*field, value) || value > 100000u)
        return false;
    output = static_cast<int>(value);
    return true;
}

bool SafeIdentifier(const std::string& value) {
    if (value.empty() || value.size() > 128) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isalnum(character) || character == '-' || character == '_' ||
               character == '.' || character == ':';
    });
}

bool SafeDate(const std::string& value) {
    static const std::regex pattern(
        R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?Z$)");
    return value.size() <= 40 && std::regex_match(value, pattern);
}

bool SafeReleaseUrl(const std::string& value, const std::string& tag) {
    if (value.empty()) return true;
    const std::string expected = std::string(kReleasePagePrefix) + tag;
    return value == expected &&
           value.find('\r') == std::string::npos &&
           value.find('\n') == std::string::npos &&
           value.find('\\') == std::string::npos;
}

bool HasDemoText(const std::string& value) {
    std::string upper = value;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    static const char* blocked[] = {
        OGS_CSTR("EXEMPLO"), OGS_CSTR("DADOS DE EXEMPLO"), OGS_CSTR("DADOS NÃO OFICIAIS"),
        OGS_CSTR("CONTEÚDO DE DEMONSTRAÇÃO"), OGS_CSTR("DESCREVE AQUI"), OGS_CSTR("SUBSTITUI ESTE TEXTO")
    };
    for (const char* text : blocked)
        if (upper.find(text) != std::string::npos) return true;
    return false;
}

std::optional<ChangeType> ParseChangeType(const std::string& type) {



    std::string normalized;
    normalized.reserve(type.size());
    for (const unsigned char character : type)
        normalized.push_back(static_cast<char>(std::tolower(character)));

    if (normalized == OGS_LIT("added")) return ChangeType::Added;
    if (normalized == OGS_LIT("improved")) return ChangeType::Improved;
    if (normalized == OGS_LIT("fixed")) return ChangeType::Fixed;
    if (normalized == OGS_LIT("performance")) return ChangeType::Performance;
    if (normalized == OGS_LIT("compatibility")) return ChangeType::Compatibility;
    if (normalized == OGS_LIT("security")) return ChangeType::Security;
    if (normalized == OGS_LIT("removed")) return ChangeType::Removed;
    if (normalized == OGS_LIT("breaking")) return ChangeType::Breaking;
    if (normalized == OGS_LIT("maintenance")) return ChangeType::Maintenance;
    return std::nullopt;
}

struct ParseResult {
    std::optional<ChangelogSnapshot> snapshot;
    std::string error;
};

ParseResult ParseChangelog(const std::string& json) {
    auto fail = [](std::string message) {
        return ParseResult{std::nullopt, std::move(message)};
    };
    if (json.empty() || json.size() > 1024 * 1024)
        return fail(OGS_LIT("O changelog está vazio ou excede 1 MB."));

    Json root;
    SharedJson::Limits limits;
    limits.maximumDepth = 48;
    limits.maximumStringBytes = kMaximumTextLength;
    limits.maximumNodes = 50000;
    const auto parsedJson = SharedJson::Parse(json, root, limits);
    if (!parsedJson.ok)
        return fail(OGS_LIT("JSON do changelog inválido (offset ") +
                    std::to_string(parsedJson.errorOffset) + "): " + parsedJson.error + ".");
    const Object* object = SharedJson::AsObject(root);
    if (!object) return fail(OGS_LIT("A raiz de changelog.json não é um objeto."));

    int schemaVersion = 0;
    if (!Number(*object, OGS_LIT("schemaVersion"), schemaVersion) || schemaVersion != 1)
        return fail(OGS_LIT("schemaVersion do changelog não é suportado."));

    ChangelogSnapshot snapshot;
    snapshot.status = ChangelogStatus::Ready;
    if (!Text(*object, OGS_LIT("generatedAt"), snapshot.generatedAt) ||
        !SafeDate(snapshot.generatedAt))
        return fail(OGS_LIT("generatedAt do changelog é inválido."));
    if (!Text(*object, OGS_LIT("channel"), snapshot.channel) ||
        (snapshot.channel != OGS_LIT("stable") && snapshot.channel != OGS_LIT("beta")))
        return fail(OGS_LIT("Canal do changelog é inválido."));

    const Json* releasesField = Field(*object, OGS_LIT("releases"));
    const Array* releases = releasesField
        ? SharedJson::AsArray(*releasesField) : nullptr;
    if (!releases || releases->size() > kMaximumReleases)
        return fail(OGS_LIT("Lista de releases do changelog é inválida."));

    std::set<std::string> versions;
    std::set<std::string> releaseIds;
    for (const Json& releaseValue : *releases) {
        const Object* releaseObject = SharedJson::AsObject(releaseValue);
        if (!releaseObject) return fail(OGS_LIT("Uma release do changelog não é um objeto."));

        ChangelogRelease release;
        if (!Text(*releaseObject, "id", release.id) || !SafeIdentifier(release.id))
            return fail(OGS_LIT("ID de release inválido."));
        if (!Text(*releaseObject, OGS_LIT("version"), release.version) ||
            !OmniGhost::Update::SemVersion::Parse(release.version))
            return fail(OGS_LIT("Versão de release inválida."));
        if (!Text(*releaseObject, "tag", release.tag) ||
            release.tag != "v" + release.version)
            return fail(OGS_LIT("A tag do changelog não corresponde à versão."));
        if (!Text(*releaseObject, OGS_LIT("publishedAt"), release.publishedAt) ||
            !SafeDate(release.publishedAt))
            return fail(OGS_LIT("publishedAt de uma release é inválido."));
        if (!Text(*releaseObject, OGS_LIT("title"), release.title) || HasDemoText(release.title))
            return fail(OGS_LIT("Título de release inválido."));
        if (!Text(*releaseObject, OGS_LIT("summary"), release.summary, true) ||
            HasDemoText(release.summary))
            return fail(OGS_LIT("Resumo de release inválido."));
        if (!OptionalText(*releaseObject, OGS_LIT("author"), release.author) ||
            !OptionalText(*releaseObject, OGS_LIT("commit"), release.commit) ||
            !OptionalText(*releaseObject, OGS_LIT("releaseUrl"), release.releaseUrl))
            return fail(OGS_LIT("Metadados públicos da Release são inválidos."));
        if (release.author.empty())
            release.author = OGS_LIT("rodrigomatossilva07-rgb");
        if (release.releaseUrl.empty())
            release.releaseUrl = std::string(kReleasePagePrefix) + release.tag;
        if (!SafeReleaseUrl(release.releaseUrl, release.tag))
            return fail(OGS_LIT("URL pública da Release é inválida."));
        if (!release.commit.empty() && !SafeIdentifier(release.commit))
            return fail(OGS_LIT("Identificador de commit da Release é inválido."));
        if (!versions.insert(release.version).second ||
            !releaseIds.insert(release.id).second)
            return fail(OGS_LIT("Versão ou ID de release duplicado no changelog."));

        const Json* modulesField = Field(*releaseObject, OGS_LIT("modules"));
        std::vector<const Json*> moduleValues;
        if (modulesField) {
            if (const Array* modules = SharedJson::AsArray(*modulesField)) {
                if (modules->size() > kMaximumModulesPerRelease)
                    return fail(OGS_LIT("Uma release contém demasiados módulos."));
                for (const Json& module : *modules)
                    moduleValues.push_back(&module);
            } else if (SharedJson::AsObject(*modulesField) != nullptr) {


                moduleValues.push_back(modulesField);
            }
        }
        if (moduleValues.empty())
            return fail(OGS_LIT("Uma release não contém módulos válidos."));

        std::set<std::string> moduleIds;
        for (const Json* moduleValue : moduleValues) {
            const Object* moduleObject = moduleValue
                ? SharedJson::AsObject(*moduleValue)
                : nullptr;
            if (!moduleObject) return fail(OGS_LIT("Um módulo do changelog não é um objeto."));

            ChangelogModule module;
            if (!Text(*moduleObject, "id", module.id) || !SafeIdentifier(module.id) ||
                !moduleIds.insert(module.id).second)
                return fail(OGS_LIT("ID de módulo inválido ou duplicado."));
            if (!Text(*moduleObject, OGS_LIT("name"), module.name) || HasDemoText(module.name))
                return fail(OGS_LIT("Nome de módulo inválido."));
            if (!OptionalText(*moduleObject, OGS_LIT("version"), module.version))
                return fail(OGS_LIT("Versão de módulo inválida."));

            const Json* changesField = Field(*moduleObject, OGS_LIT("changes"));
            std::vector<const Json*> changeValues;
            if (changesField) {
                if (const Array* changes = SharedJson::AsArray(*changesField)) {
                    if (changes->size() > kMaximumChangesPerModule)
                        return fail(OGS_LIT("Um módulo contém demasiadas alterações."));
                    for (const Json& change : *changes)
                        changeValues.push_back(&change);
                } else if (SharedJson::AsObject(*changesField) != nullptr) {

                    changeValues.push_back(changesField);
                }
            }
            if (changeValues.empty())
                return fail(OGS_LIT("Um módulo não contém alterações válidas."));

            std::set<std::string> uniqueChanges;
            for (const Json* changeValue : changeValues) {
                const Object* changeObject = changeValue
                    ? SharedJson::AsObject(*changeValue)
                    : nullptr;
                if (!changeObject) return fail(OGS_LIT("Uma alteração não é um objeto."));
                std::string typeText;
                ChangelogChange change;
                if (!Text(*changeObject, OGS_LIT("type"), typeText))
                    return fail(OGS_LIT("Categoria de alteração em falta."));
                const auto parsedType = ParseChangeType(typeText);
                if (!parsedType)
                    return fail(OGS_LIT("Categoria de alteração desconhecida."));
                change.type = *parsedType;
                if (!Text(*changeObject, OGS_LIT("text"), change.text) ||
                    HasDemoText(change.text))
                    return fail(OGS_LIT("Texto de alteração inválido."));
                std::string uniqueKey = typeText + "\n" + change.text;
                if (uniqueChanges.insert(uniqueKey).second)
                    module.changes.push_back(std::move(change));
            }
            if (module.changes.empty())
                return fail(OGS_LIT("Um módulo ficou sem alterações depois da validação."));
            release.modules.push_back(std::move(module));
        }
        snapshot.releases.push_back(std::move(release));
    }

    std::sort(snapshot.releases.begin(), snapshot.releases.end(),
        [](const ChangelogRelease& left, const ChangelogRelease& right) {
            const auto leftVersion = OmniGhost::Update::SemVersion::Parse(left.version);
            const auto rightVersion = OmniGhost::Update::SemVersion::Parse(right.version);
            if (leftVersion && rightVersion)
                return leftVersion->Compare(*rightVersion) > 0;
            return left.publishedAt > right.publishedAt;
        });
    snapshot.status = snapshot.releases.empty()
        ? ChangelogStatus::Empty : ChangelogStatus::Ready;
    snapshot.message = snapshot.releases.empty()
        ? OGS_LIT("Ainda não existem atualizações publicadas.") : std::string{};
    return {std::move(snapshot), {}};
}

fs::path CacheDirectory() {
    return OmniGhost::Paths::Cache();
}

fs::path CacheFile() {
    return CacheDirectory() / L"changelog.json";
}

fs::path LogFile() {
    const fs::path unified = OmniGhost::SessionLog::CurrentLogPath();
    return unified.empty() ? OmniGhost::Paths::Logs() / L"logs.txt" : unified;
}

void Log(OmniGhost::Update::LogLevel level, const std::string& stage,
         const std::string& message) {
    OmniGhost::Update::WriteLog(LogFile(), level, OmniGhost::Version, {}, stage, message);
}

bool WriteAtomically(const fs::path& target, const std::string& content) {
    std::error_code error;
    fs::create_directories(target.parent_path(), error);
    if (error) return false;
    const fs::path temporary = target.wstring() + OGS_WIDE(".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.flush();
        if (!output) return false;
    }
    fs::remove(target, error);
    error.clear();
    fs::rename(temporary, target, error);
    if (error) {
        fs::remove(temporary, error);
        return false;
    }
    return true;
}

std::string ReadFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

}

ChangelogService& ChangelogService::Instance() {
    static ChangelogService service;
    return service;
}

ChangelogService::~ChangelogService() {
    Shutdown();
}

void ChangelogService::Initialize(
    std::unique_ptr<OmniGhost::Update::IHttpClient> http) {
    if (initialized_.exchange(true)) return;
    OmniGhost::Paths::EnsureUserDirectories();
    http_ = http ? std::move(http)
                 : std::make_unique<OmniGhost::Update::WinHttpClient>();
    LoadCache();
}

void ChangelogService::LoadCache() {
    const std::string content = ReadFile(CacheFile());
    if (content.empty()) {
        ChangelogSnapshot empty;
        empty.status = ChangelogStatus::Idle;
        SetSnapshot(std::move(empty));
        return;
    }
    ParseResult parsed = ParseChangelog(content);
    if (!parsed.snapshot) {
        ChangelogSnapshot empty;
        empty.status = ChangelogStatus::Idle;
        empty.message = OGS_LIT("O cache local do histórico foi ignorado por ser inválido.");
        SetSnapshot(std::move(empty));
        return;
    }
    parsed.snapshot->fromCache = true;
    SetSnapshot(std::move(*parsed.snapshot));
}

void ChangelogService::RefreshAsync(bool force) {
    (void)force;
    if (!initialized_) Initialize();
    if (busy_.exchange(true)) {
        Log(OmniGhost::Update::LogLevel::Warning, OGS_LIT("worker"),
            OGS_LIT("Pedido de refresh ignorado porque o changelog já está ocupado."));
        return;
    }
    cancelled_ = false;
    if (worker_.joinable())
        worker_.join();

    worker_ = std::jthread([this, force](std::stop_token stopToken) {
        OmniGhost::Platform::SetCurrentThreadName(L"OmniGhost.Changelog");
        std::stop_callback cancelOnStop(stopToken, [this] { cancelled_.store(true, std::memory_order_release); });
        auto busyReset = OmniGhost::Platform::MakeScopeExit([this] { busy_ = false; });
        try {
            RunRefresh(force);
        } catch (const std::exception& exception) {
            ChangelogSnapshot failed = GetSnapshot();
            failed.status = ChangelogStatus::Error;
            failed.message = OGS_LIT("O histórico terminou de forma inesperada.");
            SetSnapshot(std::move(failed));
            Log(OmniGhost::Update::LogLevel::Error, OGS_LIT("worker"), exception.what());
        } catch (...) {
            ChangelogSnapshot failed = GetSnapshot();
            failed.status = ChangelogStatus::Error;
            failed.message = OGS_LIT("O histórico terminou de forma inesperada.");
            SetSnapshot(std::move(failed));
            Log(OmniGhost::Update::LogLevel::Error, OGS_LIT("worker"),
                OGS_LIT("Exceção não identificada na thread do changelog."));
        }
    });
}

void ChangelogService::RunRefresh(bool force) {
    (void)force;
    Log(OmniGhost::Update::LogLevel::Info, OGS_LIT("check"), OGS_LIT("A iniciar a atualização do changelog."));
    ChangelogSnapshot current = GetSnapshot();
    current.status = ChangelogStatus::Loading;
    current.message = OGS_LIT("A carregar atualizações…");
    SetSnapshot(std::move(current));

    std::string changelogUrl = kChangelogFallbackUrl;
    const auto releasesResponse = http_->GetText(
        kReleasesApiUrl,
        15000,
        cancelled_);

    if (releasesResponse.error.empty()) {
        const auto releaseResult =
            OmniGhost::Update::ResolveHighestStableGitHubRelease(
                releasesResponse.body);
        if (releaseResult.selection &&
            !releaseResult.selection->changelogUrl.empty()) {
            changelogUrl = releaseResult.selection->changelogUrl;
            Log(
                OmniGhost::Update::LogLevel::Info,
                OGS_LIT("release-resolution"),
                OGS_LIT("Changelog associado à maior Release estável: ") +
                    releaseResult.selection->tag + ".");
        } else if (!releaseResult.error.empty()) {
            Log(
                OmniGhost::Update::LogLevel::Warning,
                OGS_LIT("release-resolution"),
                releaseResult.error +
                    OGS_LIT(" Será usado o marcador Latest como compatibilidade."));
        }
    } else {
        Log(
            OmniGhost::Update::LogLevel::Warning,
            OGS_LIT("release-resolution"),
            releasesResponse.error +
                OGS_LIT(" Será usado o marcador Latest como compatibilidade."));
    }

    const auto result = http_->GetText(changelogUrl, 15000, cancelled_);
    if (!result.error.empty()) {
        ChangelogSnapshot fallback = GetSnapshot();
        const bool networkUnavailable =
            result.errorClass == OmniGhost::Update::HttpPolicy::ErrorClass::Dns ||
            result.errorClass == OmniGhost::Update::HttpPolicy::ErrorClass::Connection ||
            result.errorClass == OmniGhost::Update::HttpPolicy::ErrorClass::Timeout;

        if (!fallback.releases.empty()) {
            fallback.status = ChangelogStatus::OfflineCache;
            fallback.fromCache = true;
            fallback.message = networkUnavailable
                ? OGS_LIT("Sem ligação à Internet. A mostrar o histórico guardado.")
                : OGS_LIT("Não foi possível atualizar o histórico. A mostrar os dados guardados.");
        } else if (result.statusCode == 404) {
            fallback.status = ChangelogStatus::Empty;
            fallback.message = OGS_LIT("Ainda não existem atualizações publicadas.");
        } else {
            fallback.status = ChangelogStatus::Error;
            fallback.message = networkUnavailable
                ? OGS_LIT("Sem ligação à Internet. Liga-te a uma rede e tenta novamente.")
                : OGS_LIT("Não foi possível carregar o histórico de atualizações.");
        }
        Log(OmniGhost::Update::LogLevel::Warning, OGS_LIT("download"), result.error);
        SetSnapshot(std::move(fallback));
        return;
    }

    ParseResult parsed = ParseChangelog(result.body);
    if (!parsed.snapshot) {
        ChangelogSnapshot fallback = GetSnapshot();
        if (!fallback.releases.empty()) {
            fallback.status = ChangelogStatus::OfflineCache;
            fallback.fromCache = true;
            fallback.message =
                OGS_LIT("O histórico remoto é inválido. A mostrar os dados guardados.");
        } else {
            fallback.status = ChangelogStatus::Error;
            fallback.message = OGS_LIT("Não foi possível carregar o histórico de atualizações.");
        }
        Log(OmniGhost::Update::LogLevel::Error, OGS_LIT("validation"), parsed.error);
        SetSnapshot(std::move(fallback));
        return;
    }

    if (!WriteAtomically(CacheFile(), result.body)) {
        Log(OmniGhost::Update::LogLevel::Warning, OGS_LIT("cache"), OGS_LIT("Não foi possível atualizar a cache local."));
    }
    parsed.snapshot->fromCache = false;
    Log(OmniGhost::Update::LogLevel::Info, OGS_LIT("complete"), OGS_LIT("Changelog remoto validado e carregado."));
    SetSnapshot(std::move(*parsed.snapshot));
}

void ChangelogService::SetSnapshot(ChangelogSnapshot value) {
    std::lock_guard lock(mutex_);
    snapshot_ = std::move(value);
}

ChangelogSnapshot ChangelogService::GetSnapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}

std::string ChangelogService::CardId(const ChangelogRelease& release,
                                     const ChangelogModule& module) {
    return release.id + ":" + module.id;
}

int ChangelogService::UnreadCount() const {
    const ChangelogSnapshot snapshot = GetSnapshot();
    int count = 0;
    for (const ChangelogRelease& release : snapshot.releases) {
        for (const ChangelogModule& module : release.modules) {
            if (!module.changes.empty() &&
                !Launcher::IsUpdateRead(CardId(release, module).c_str()))
                ++count;
        }
    }
    return count;
}

void ChangelogService::Shutdown() {
    cancelled_ = true;
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    busy_ = false;
}

}
