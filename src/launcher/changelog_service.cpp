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
    OGS_CSTR("https://api.github.com/repos/rodrigomatossilva07-rgb/"
    "OmniGhost-Updates/releases?per_page=30");
const std::string kChangelogFallbackUrl =
    OGS_CSTR("https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/"
    "releases/latest/download/changelog.json");
const std::string kReleasePagePrefix =
    OGS_CSTR("https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/releases/tag/");
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
        OGS_CSTR("EXEMPLO"), OGS_CSTR("DADOS DE EXEMPLO"), OGS_CSTR("DADOS NÃƒO OFICIAIS"),
        OGS_CSTR("CONTEÃšDO DE DEMONSTRAÃ‡ÃƒO"), OGS_CSTR("DESCREVE AQUI"), OGS_CSTR("SUBSTITUI ESTE TEXTO")
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

    if (normalized == OGS_CSTR("added")) return ChangeType::Added;
    if (normalized == OGS_CSTR("improved")) return ChangeType::Improved;
    if (normalized == OGS_CSTR("fixed")) return ChangeType::Fixed;
    if (normalized == OGS_CSTR("performance")) return ChangeType::Performance;
    if (normalized == OGS_CSTR("compatibility")) return ChangeType::Compatibility;
    if (normalized == OGS_CSTR("security")) return ChangeType::Security;
    if (normalized == OGS_CSTR("removed")) return ChangeType::Removed;
    if (normalized == OGS_CSTR("breaking")) return ChangeType::Breaking;
    if (normalized == OGS_CSTR("maintenance")) return ChangeType::Maintenance;
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
        return fail(OGS_CSTR("O changelog estÃ¡ vazio ou excede 1 MB."));

    Json root;
    SharedJson::Limits limits;
    limits.maximumDepth = 48;
    limits.maximumStringBytes = kMaximumTextLength;
    limits.maximumNodes = 50000;
    const auto parsedJson = SharedJson::Parse(json, root, limits);
    if (!parsedJson.ok)
        return fail(OGS_CSTR("JSON do changelog invÃ¡lido (offset ") +
                    std::to_string(parsedJson.errorOffset) + "): " + parsedJson.error + ".");
    const Object* object = SharedJson::AsObject(root);
    if (!object) return fail(OGS_CSTR("A raiz de changelog.json nÃ£o Ã© um objeto."));

    int schemaVersion = 0;
    if (!Number(*object, OGS_CSTR("schemaVersion"), schemaVersion) || schemaVersion != 1)
        return fail(OGS_CSTR("schemaVersion do changelog nÃ£o Ã© suportado."));

    ChangelogSnapshot snapshot;
    snapshot.status = ChangelogStatus::Ready;
    if (!Text(*object, OGS_CSTR("generatedAt"), snapshot.generatedAt) ||
        !SafeDate(snapshot.generatedAt))
        return fail(OGS_CSTR("generatedAt do changelog Ã© invÃ¡lido."));
    if (!Text(*object, OGS_CSTR("channel"), snapshot.channel) ||
        (snapshot.channel != OGS_CSTR("stable") && snapshot.channel != OGS_CSTR("beta")))
        return fail(OGS_CSTR("Canal do changelog Ã© invÃ¡lido."));

    const Json* releasesField = Field(*object, OGS_CSTR("releases"));
    const Array* releases = releasesField
        ? SharedJson::AsArray(*releasesField) : nullptr;
    if (!releases || releases->size() > kMaximumReleases)
        return fail(OGS_CSTR("Lista de releases do changelog Ã© invÃ¡lida."));

    std::set<std::string> versions;
    std::set<std::string> releaseIds;
    for (const Json& releaseValue : *releases) {
        const Object* releaseObject = SharedJson::AsObject(releaseValue);
        if (!releaseObject) return fail(OGS_CSTR("Uma release do changelog nÃ£o Ã© um objeto."));

        ChangelogRelease release;
        if (!Text(*releaseObject, "id", release.id) || !SafeIdentifier(release.id))
            return fail(OGS_CSTR("ID de release invÃ¡lido."));
        if (!Text(*releaseObject, OGS_CSTR("version"), release.version) ||
            !OmniGhost::Update::SemVersion::Parse(release.version))
            return fail(OGS_CSTR("VersÃ£o de release invÃ¡lida."));
        if (!Text(*releaseObject, "tag", release.tag) ||
            release.tag != "v" + release.version)
            return fail(OGS_CSTR("A tag do changelog nÃ£o corresponde Ã  versÃ£o."));
        if (!Text(*releaseObject, OGS_CSTR("publishedAt"), release.publishedAt) ||
            !SafeDate(release.publishedAt))
            return fail(OGS_CSTR("publishedAt de uma release Ã© invÃ¡lido."));
        if (!Text(*releaseObject, OGS_CSTR("title"), release.title) || HasDemoText(release.title))
            return fail(OGS_CSTR("TÃ­tulo de release invÃ¡lido."));
        if (!Text(*releaseObject, OGS_CSTR("summary"), release.summary, true) ||
            HasDemoText(release.summary))
            return fail(OGS_CSTR("Resumo de release invÃ¡lido."));
        if (!OptionalText(*releaseObject, OGS_CSTR("author"), release.author) ||
            !OptionalText(*releaseObject, OGS_CSTR("commit"), release.commit) ||
            !OptionalText(*releaseObject, OGS_CSTR("releaseUrl"), release.releaseUrl))
            return fail(OGS_CSTR("Metadados pÃºblicos da Release sÃ£o invÃ¡lidos."));
        if (release.author.empty())
            release.author = OGS_CSTR("rodrigomatossilva07-rgb");
        if (release.releaseUrl.empty())
            release.releaseUrl = std::string(kReleasePagePrefix) + release.tag;
        if (!SafeReleaseUrl(release.releaseUrl, release.tag))
            return fail(OGS_CSTR("URL pÃºblica da Release Ã© invÃ¡lida."));
        if (!release.commit.empty() && !SafeIdentifier(release.commit))
            return fail(OGS_CSTR("Identificador de commit da Release Ã© invÃ¡lido."));
        if (!versions.insert(release.version).second ||
            !releaseIds.insert(release.id).second)
            return fail(OGS_CSTR("VersÃ£o ou ID de release duplicado no changelog."));

        const Json* modulesField = Field(*releaseObject, OGS_CSTR("modules"));
        std::vector<const Json*> moduleValues;
        if (modulesField) {
            if (const Array* modules = SharedJson::AsArray(*modulesField)) {
                if (modules->size() > kMaximumModulesPerRelease)
                    return fail(OGS_CSTR("Uma release contÃ©m demasiados mÃ³dulos."));
                for (const Json& module : *modules)
                    moduleValues.push_back(&module);
            } else if (SharedJson::AsObject(*modulesField) != nullptr) {


                moduleValues.push_back(modulesField);
            }
        }
        if (moduleValues.empty())
            return fail(OGS_CSTR("Uma release nÃ£o contÃ©m mÃ³dulos vÃ¡lidos."));

        std::set<std::string> moduleIds;
        for (const Json* moduleValue : moduleValues) {
            const Object* moduleObject = moduleValue
                ? SharedJson::AsObject(*moduleValue)
                : nullptr;
            if (!moduleObject) return fail(OGS_CSTR("Um mÃ³dulo do changelog nÃ£o Ã© um objeto."));

            ChangelogModule module;
            if (!Text(*moduleObject, "id", module.id) || !SafeIdentifier(module.id) ||
                !moduleIds.insert(module.id).second)
                return fail(OGS_CSTR("ID de mÃ³dulo invÃ¡lido ou duplicado."));
            if (!Text(*moduleObject, OGS_CSTR("name"), module.name) || HasDemoText(module.name))
                return fail(OGS_CSTR("Nome de mÃ³dulo invÃ¡lido."));
            if (!OptionalText(*moduleObject, OGS_CSTR("version"), module.version))
                return fail(OGS_CSTR("VersÃ£o de mÃ³dulo invÃ¡lida."));

            const Json* changesField = Field(*moduleObject, OGS_CSTR("changes"));
            std::vector<const Json*> changeValues;
            if (changesField) {
                if (const Array* changes = SharedJson::AsArray(*changesField)) {
                    if (changes->size() > kMaximumChangesPerModule)
                        return fail(OGS_CSTR("Um mÃ³dulo contÃ©m demasiadas alteraÃ§Ãµes."));
                    for (const Json& change : *changes)
                        changeValues.push_back(&change);
                } else if (SharedJson::AsObject(*changesField) != nullptr) {

                    changeValues.push_back(changesField);
                }
            }
            if (changeValues.empty())
                return fail(OGS_CSTR("Um mÃ³dulo nÃ£o contÃ©m alteraÃ§Ãµes vÃ¡lidas."));

            std::set<std::string> uniqueChanges;
            for (const Json* changeValue : changeValues) {
                const Object* changeObject = changeValue
                    ? SharedJson::AsObject(*changeValue)
                    : nullptr;
                if (!changeObject) return fail(OGS_CSTR("Uma alteraÃ§Ã£o nÃ£o Ã© um objeto."));
                std::string typeText;
                ChangelogChange change;
                if (!Text(*changeObject, OGS_CSTR("type"), typeText))
                    return fail(OGS_CSTR("Categoria de alteraÃ§Ã£o em falta."));
                const auto parsedType = ParseChangeType(typeText);
                if (!parsedType)
                    return fail(OGS_CSTR("Categoria de alteraÃ§Ã£o desconhecida."));
                change.type = *parsedType;
                if (!Text(*changeObject, OGS_CSTR("text"), change.text) ||
                    HasDemoText(change.text))
                    return fail(OGS_CSTR("Texto de alteraÃ§Ã£o invÃ¡lido."));
                std::string uniqueKey = typeText + "\n" + change.text;
                if (uniqueChanges.insert(uniqueKey).second)
                    module.changes.push_back(std::move(change));
            }
            if (module.changes.empty())
                return fail(OGS_CSTR("Um mÃ³dulo ficou sem alteraÃ§Ãµes depois da validaÃ§Ã£o."));
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
        ? OGS_CSTR("Ainda nÃ£o existem atualizaÃ§Ãµes publicadas.") : std::string{};
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
        empty.message = OGS_CSTR("O cache local do histÃ³rico foi ignorado por ser invÃ¡lido.");
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
        Log(OmniGhost::Update::LogLevel::Warning, OGS_CSTR("worker"),
            OGS_CSTR("Pedido de refresh ignorado porque o changelog jÃ¡ estÃ¡ ocupado."));
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
            failed.message = OGS_CSTR("O histÃ³rico terminou de forma inesperada.");
            SetSnapshot(std::move(failed));
            Log(OmniGhost::Update::LogLevel::Error, OGS_CSTR("worker"), exception.what());
        } catch (...) {
            ChangelogSnapshot failed = GetSnapshot();
            failed.status = ChangelogStatus::Error;
            failed.message = OGS_CSTR("O histÃ³rico terminou de forma inesperada.");
            SetSnapshot(std::move(failed));
            Log(OmniGhost::Update::LogLevel::Error, OGS_CSTR("worker"),
                OGS_CSTR("ExceÃ§Ã£o nÃ£o identificada na thread do changelog."));
        }
    });
}

void ChangelogService::RunRefresh(bool force) {
    (void)force;
    Log(OmniGhost::Update::LogLevel::Info, OGS_CSTR("check"), OGS_CSTR("A iniciar a atualizaÃ§Ã£o do changelog."));
    ChangelogSnapshot current = GetSnapshot();
    current.status = ChangelogStatus::Loading;
    current.message = OGS_CSTR("A carregar atualizaÃ§Ãµesâ€¦");
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
                OGS_CSTR("release-resolution"),
                OGS_CSTR("Changelog associado Ã  maior Release estÃ¡vel: ") +
                    releaseResult.selection->tag + ".");
        } else if (!releaseResult.error.empty()) {
            Log(
                OmniGhost::Update::LogLevel::Warning,
                OGS_CSTR("release-resolution"),
                releaseResult.error +
                    OGS_CSTR(" SerÃ¡ usado o marcador Latest como compatibilidade."));
        }
    } else {
        Log(
            OmniGhost::Update::LogLevel::Warning,
            OGS_CSTR("release-resolution"),
            releasesResponse.error +
                OGS_CSTR(" SerÃ¡ usado o marcador Latest como compatibilidade."));
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
                ? OGS_CSTR("Sem ligaÃ§Ã£o Ã  Internet. A mostrar o histÃ³rico guardado.")
                : OGS_CSTR("NÃ£o foi possÃ­vel atualizar o histÃ³rico. A mostrar os dados guardados.");
        } else if (result.statusCode == 404) {
            fallback.status = ChangelogStatus::Empty;
            fallback.message = OGS_CSTR("Ainda nÃ£o existem atualizaÃ§Ãµes publicadas.");
        } else {
            fallback.status = ChangelogStatus::Error;
            fallback.message = networkUnavailable
                ? OGS_CSTR("Sem ligaÃ§Ã£o Ã  Internet. Liga-te a uma rede e tenta novamente.")
                : OGS_CSTR("NÃ£o foi possÃ­vel carregar o histÃ³rico de atualizaÃ§Ãµes.");
        }
        Log(OmniGhost::Update::LogLevel::Warning, OGS_CSTR("download"), result.error);
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
                OGS_CSTR("O histÃ³rico remoto Ã© invÃ¡lido. A mostrar os dados guardados.");
        } else {
            fallback.status = ChangelogStatus::Error;
            fallback.message = OGS_CSTR("NÃ£o foi possÃ­vel carregar o histÃ³rico de atualizaÃ§Ãµes.");
        }
        Log(OmniGhost::Update::LogLevel::Error, OGS_CSTR("validation"), parsed.error);
        SetSnapshot(std::move(fallback));
        return;
    }

    if (!WriteAtomically(CacheFile(), result.body)) {
        Log(OmniGhost::Update::LogLevel::Warning, OGS_CSTR("cache"), OGS_CSTR("NÃ£o foi possÃ­vel atualizar a cache local."));
    }
    parsed.snapshot->fromCache = false;
    Log(OmniGhost::Update::LogLevel::Info, OGS_CSTR("complete"), OGS_CSTR("Changelog remoto validado e carregado."));
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
