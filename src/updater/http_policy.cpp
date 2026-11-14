#include "http_policy.h"

#include <algorithm>
#include "platform/strprotect/strprotect.h"

namespace OmniGhost::Update::HttpPolicy {

int ClampTimeoutMilliseconds(int requested) noexcept {
    if (requested <= 0) return DefaultTimeoutMilliseconds;
    return (std::clamp)(requested, MinimumTimeoutMilliseconds, MaximumTimeoutMilliseconds);
}

bool IsTlsOnlyUrl(std::string_view url) noexcept {
    if (url.size() < 9 || url.size() > 2048 || url.substr(0, 8) != OGS_LIT("https://")) return false;
    const std::string_view remainder = url.substr(8);
    if (remainder.empty() || remainder.front() == '/' || remainder.find('@') != std::string_view::npos)
        return false;
    return url.find('\\') == std::string_view::npos &&
           url.find('\r') == std::string_view::npos &&
           url.find('\n') == std::string_view::npos;
}

bool IsRetryableStatus(int statusCode) noexcept {
    return statusCode == 408 || statusCode == 425 || statusCode == 429 ||
           statusCode == 500 || statusCode == 502 || statusCode == 503 || statusCode == 504;
}

unsigned RetryDelayMilliseconds(unsigned attempt, int retryAfterSeconds) noexcept {
    if (retryAfterSeconds > 0) {
        const unsigned seconds = static_cast<unsigned>((std::min)(retryAfterSeconds, 30));
        return seconds * 1000u;
    }
    const unsigned capped = (std::min)(attempt, 5u);
    return 500u * (1u << capped);
}

std::string StatusMessage(int statusCode, std::string_view finalUrl) {
    std::string message;
    if (statusCode == 403 || statusCode == 429) {
        message = OGS_LIT("O serviço de atualizações está temporariamente limitado (HTTP ") +
                  std::to_string(statusCode) + OGS_LIT("). Tenta mais tarde.");
    } else if (statusCode == 404) {
        message = OGS_LIT("O ficheiro de atualização não foi encontrado (HTTP 404).");
    } else if (statusCode >= 500) {
        message = OGS_LIT("O serviço de atualizações está temporariamente indisponível (HTTP ") +
                  std::to_string(statusCode) + ").";
    } else {
        message = OGS_LIT("O servidor respondeu com HTTP ") + std::to_string(statusCode) + ".";
    }
    if (!finalUrl.empty()) message += OGS_LIT(" URL final: ") + std::string(finalUrl);
    return message;
}


ErrorClass ClassifyFailure(int statusCode, std::string_view error) noexcept {
    if (error.empty()) return ErrorClass::None;
    if (statusCode != 0) return ErrorClass::Http;
    auto contains = [&](std::string_view needle) { return error.find(needle) != std::string_view::npos; };
    if (contains(OGS_LIT("cancelad")) || contains(OGS_LIT("Cancelad"))) return ErrorClass::Cancelled;
    if (contains(OGS_LIT("timeout")) || contains(OGS_LIT("tempo limite")) || contains(OGS_LIT("TIMEOUT"))) return ErrorClass::Timeout;
    if (contains("DNS") || contains(OGS_LIT("resolver")) || contains(OGS_LIT("NAME_NOT_RESOLVED"))) return ErrorClass::Dns;
    if (contains("TLS") || contains(OGS_LIT("certificado")) || contains(OGS_LIT("SECURE_FAILURE")) || contains(OGS_LIT("SEC_CERT"))) return ErrorClass::Tls;
    if (contains(OGS_LIT("redirecion")) || contains(OGS_LIT("REDIRECT"))) return ErrorClass::Redirect;
    if (contains(OGS_LIT("tamanho")) || contains(OGS_LIT("limite permitido"))) return ErrorClass::SizeLimit;
    if (contains("URL") && (contains(OGS_LIT("inválida")) || contains(OGS_LIT("HTTPS")))) return ErrorClass::InvalidUrl;
    if (contains(OGS_LIT("escrever")) || contains(OGS_LIT("ficheiro")) || contains(OGS_LIT("pasta temporária"))) return ErrorClass::Io;
    if (contains(OGS_LIT("connect")) || contains(OGS_LIT("ligação")) || contains(OGS_LIT("CONNECTION"))) return ErrorClass::Connection;
    return ErrorClass::Unknown;
}

const char* ErrorClassName(ErrorClass value) noexcept {
    switch (value) {
    case ErrorClass::None: return OGS_CSTR("none");
    case ErrorClass::Cancelled: return OGS_CSTR("cancelled");
    case ErrorClass::Timeout: return OGS_CSTR("timeout");
    case ErrorClass::Dns: return "dns";
    case ErrorClass::Connection: return OGS_CSTR("connection");
    case ErrorClass::Tls: return "tls";
    case ErrorClass::Http: return OGS_CSTR("http");
    case ErrorClass::Redirect: return OGS_CSTR("redirect");
    case ErrorClass::SizeLimit: return OGS_CSTR("size-limit");
    case ErrorClass::Io: return "io";
    case ErrorClass::InvalidUrl: return OGS_CSTR("invalid-url");
    default: return OGS_CSTR("unknown");
    }
}

}

namespace OmniGhost::Update::HttpPolicy {
namespace {


    Configuration s_config;
}
const Configuration& GetConfiguration() noexcept {
    return s_config;
}
void SetConfiguration(const Configuration& config) {
    s_config = config;
}
}
