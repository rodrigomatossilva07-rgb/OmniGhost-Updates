#include "update_types.h"
#include "json.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <unordered_set>

namespace OmniGhost::Update {
namespace {

constexpr std::size_t kMaximumManifestBytes = 1024u * 1024u;
constexpr std::size_t kMaximumManifestStringBytes = 64u * 1024u;

const Json::Value* Field(const Json::Object& object, const char* name) {
    return Json::Field(object, name);
}

bool Text(const Json::Object& object, const char* name, std::string& output,
          bool allowEmpty = false) {
    const Json::Value* value = Field(object, name);
    const std::string* text = value ? Json::AsString(*value) : nullptr;
    if (!text || (!allowEmpty && text->empty()) || text->size() > kMaximumManifestStringBytes)
        return false;
    output = *text;
    return true;
}

bool Boolean(const Json::Object& object, const char* name, bool& output) {
    const Json::Value* value = Field(object, name);
    const bool* boolean = value ? Json::AsBool(*value) : nullptr;
    if (!boolean) return false;
    output = *boolean;
    return true;
}

bool UInt(const Json::Object& object, const char* name, std::uint64_t& output) {
    const Json::Value* value = Field(object, name);
    return value && Json::AsUInt64(*value, output);
}

bool Https(const std::string& url) {
    if (url.size() < 9 || url.size() > 2048 || url.rfind("https://", 0) != 0) return false;
    return url.find('@', 8) == std::string::npos &&
           url.find('\\') == std::string::npos &&
           url.find_first_of("\r\n") == std::string::npos;
}

bool SafeName(const std::string& value) {
    return !value.empty() && value.size() <= 200 &&
           value.find("..") == std::string::npos &&
           value.find('/') == std::string::npos &&
           value.find('\\') == std::string::npos &&
           value.ends_with(".zip");
}

} // namespace

ManifestResult ParseAndValidateManifest(const std::string& json, const Configuration& config,
                                        const std::string& architecture) {
    auto fail = [](std::string message) {
        return ManifestResult{std::nullopt, std::move(message)};
    };

    if (json.empty() || json.size() > kMaximumManifestBytes)
        return fail("Manifesto vazio ou demasiado grande.");

    Json::Value root;
    Json::Limits limits;
    limits.maximumDepth = 32;
    limits.maximumStringBytes = kMaximumManifestStringBytes;
    limits.maximumNodes = 10000;
    const Json::ParseResult parsed = Json::Parse(json, root, limits);
    if (!parsed.ok)
        return fail("JSON do manifesto inválido (offset " + std::to_string(parsed.errorOffset) + "): " + parsed.error + ".");

    const Json::Object* object = Json::AsObject(root);
    if (!object) return fail("A raiz do manifesto não é um objeto.");

    Manifest manifest{};
    std::uint64_t schema{};
    if (!UInt(*object, "schemaVersion", schema) || schema != 1)
        return fail("Versão de schema não suportada.");
    manifest.schemaVersion = 1;

    std::string channel, version, minimum;
    if (!Text(*object, "appId", manifest.appId) || manifest.appId != config.expectedAppId)
        return fail("appId inválido.");
    if (!Text(*object, "channel", channel)) return fail("Canal em falta.");
    const auto parsedChannel = ParseChannel(channel);
    if (!parsedChannel || *parsedChannel != config.channel)
        return fail("Canal de atualização inválido.");
    manifest.channel = *parsedChannel;

    const auto parsedVersion = [&]() -> std::optional<SemVersion> {
        if (!Text(*object, "version", version)) return std::nullopt;
        return SemVersion::Parse(version);
    }();
    if (!parsedVersion) return fail("Versão inválida.");
    manifest.version = *parsedVersion;

    const auto parsedMinimum = [&]() -> std::optional<SemVersion> {
        if (!Text(*object, "minimumSupportedVersion", minimum)) return std::nullopt;
        return SemVersion::Parse(minimum);
    }();
    if (!parsedMinimum) return fail("Versão mínima inválida.");
    manifest.minimumSupportedVersion = *parsedMinimum;

    if (manifest.channel == Channel::Stable && manifest.version.IsPrerelease())
        return fail("O canal stable rejeita prereleases.");
    if (manifest.minimumSupportedVersion.Compare(manifest.version) > 0)
        return fail("A versão mínima é superior à versão publicada.");

    if (!Boolean(*object, "mandatory", manifest.mandatory))
        return fail("Campo mandatory inválido.");
    if (const Json::Value* reasonValue = Field(*object, "mandatoryReason")) {
        const std::string* reason = Json::AsString(*reasonValue);
        if (!reason || reason->size() > 512)
            return fail("Motivo de atualização obrigatória inválido.");
        manifest.mandatoryReason = *reason;
    }

    if (!Text(*object, "publishedAt", manifest.publishedAt) || manifest.publishedAt.size() < 20)
        return fail("Data de publicação inválida.");
    if (!Text(*object, "releaseNotesUrl", manifest.releaseNotesUrl) || !Https(manifest.releaseNotesUrl))
        return fail("URL das notas inválida.");

    const Json::Value* packagesValue = Field(*object, "packages");
    const Json::Array* packages = packagesValue ? Json::AsArray(*packagesValue) : nullptr;
    if (!packages || packages->empty() || packages->size() > 16)
        return fail("Lista de pacotes inválida.");

    const std::regex hash("^[0-9a-fA-F]{64}$");
    std::unordered_set<std::string> packageTargets;
    std::unordered_set<std::string> packageNames;
    std::unordered_set<std::string> packageUrls;

    for (const Json::Value& item : *packages) {
        const Json::Object* packageObject = Json::AsObject(item);
        if (!packageObject) return fail("Pacote inválido.");

        Package package{};
        if (!Text(*packageObject, "platform", package.platform) || package.platform != "windows" ||
            !Text(*packageObject, "architecture", package.architecture) ||
            !Text(*packageObject, "fileName", package.fileName) || !SafeName(package.fileName) ||
            !Text(*packageObject, "url", package.url) || !Https(package.url) ||
            (!config.allowedPackageUrlPrefix.empty() && package.url.rfind(config.allowedPackageUrlPrefix, 0) != 0) ||
            package.url.find('/' + package.fileName) == std::string::npos ||
            !UInt(*packageObject, "size", package.size) || package.size == 0 ||
            package.size > config.maximumPackageBytes ||
            !Text(*packageObject, "sha256", package.sha256) || !std::regex_match(package.sha256, hash)) {
            return fail("Metadados de pacote inválidos.");
        }

        const std::string targetKey = package.platform + "\n" + package.architecture;
        if (!packageTargets.insert(targetKey).second ||
            !packageNames.insert(package.fileName).second ||
            !packageUrls.insert(package.url).second) {
            return fail("O manifesto contém pacotes duplicados ou ambíguos.");
        }

        std::transform(package.sha256.begin(), package.sha256.end(), package.sha256.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        manifest.packages.push_back(std::move(package));
    }

    if (!SelectPackage(manifest, architecture))
        return fail("Não existe pacote para Windows/" + architecture + ".");
    return {std::move(manifest), {}};
}

const Package* SelectPackage(const Manifest& manifest, const std::string& architecture) {
    for (const auto& package : manifest.packages) {
        if (package.platform == "windows" && package.architecture == architecture)
            return &package;
    }
    return nullptr;
}

} // namespace OmniGhost::Update
