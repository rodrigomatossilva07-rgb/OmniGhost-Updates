#include "http_policy.h"

#include <algorithm>

namespace OmniGhost::Update::HttpPolicy {

int ClampTimeoutMilliseconds(int requested) noexcept {
    if (requested <= 0) return DefaultTimeoutMilliseconds;
    return (std::clamp)(requested, MinimumTimeoutMilliseconds, MaximumTimeoutMilliseconds);
}

bool IsTlsOnlyUrl(std::string_view url) noexcept {
    if (url.size() < 9 || url.size() > 2048 || url.substr(0, 8) != "https://") return false;
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
        message = "O serviço de atualizações está temporariamente limitado (HTTP " +
                  std::to_string(statusCode) + "). Tenta mais tarde.";
    } else if (statusCode == 404) {
        message = "O ficheiro de atualização não foi encontrado (HTTP 404).";
    } else if (statusCode >= 500) {
        message = "O serviço de atualizações está temporariamente indisponível (HTTP " +
                  std::to_string(statusCode) + ").";
    } else {
        message = "O servidor respondeu com HTTP " + std::to_string(statusCode) + ".";
    }
    if (!finalUrl.empty()) message += " URL final: " + std::string(finalUrl);
    return message;
}


ErrorClass ClassifyFailure(int statusCode, std::string_view error) noexcept {
    if (error.empty()) return ErrorClass::None;
    if (statusCode != 0) return ErrorClass::Http;
    auto contains = [&](std::string_view needle) { return error.find(needle) != std::string_view::npos; };
    if (contains("cancelad") || contains("Cancelad")) return ErrorClass::Cancelled;
    if (contains("timeout") || contains("tempo limite") || contains("TIMEOUT")) return ErrorClass::Timeout;
    if (contains("DNS") || contains("resolver") || contains("NAME_NOT_RESOLVED")) return ErrorClass::Dns;
    if (contains("TLS") || contains("certificado") || contains("SECURE_FAILURE") || contains("SEC_CERT")) return ErrorClass::Tls;
    if (contains("redirecion") || contains("REDIRECT")) return ErrorClass::Redirect;
    if (contains("tamanho") || contains("limite permitido")) return ErrorClass::SizeLimit;
    if (contains("URL") && (contains("inválida") || contains("HTTPS"))) return ErrorClass::InvalidUrl;
    if (contains("escrever") || contains("ficheiro") || contains("pasta temporária")) return ErrorClass::Io;
    if (contains("connect") || contains("ligação") || contains("CONNECTION")) return ErrorClass::Connection;
    return ErrorClass::Unknown;
}

const char* ErrorClassName(ErrorClass value) noexcept {
    switch (value) {
    case ErrorClass::None: return "none";
    case ErrorClass::Cancelled: return "cancelled";
    case ErrorClass::Timeout: return "timeout";
    case ErrorClass::Dns: return "dns";
    case ErrorClass::Connection: return "connection";
    case ErrorClass::Tls: return "tls";
    case ErrorClass::Http: return "http";
    case ErrorClass::Redirect: return "redirect";
    case ErrorClass::SizeLimit: return "size-limit";
    case ErrorClass::Io: return "io";
    case ErrorClass::InvalidUrl: return "invalid-url";
    default: return "unknown";
    }
}

} // namespace OmniGhost::Update::HttpPolicy
