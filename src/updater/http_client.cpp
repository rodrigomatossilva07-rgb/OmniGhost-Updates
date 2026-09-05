#include "http_client.h"
#include "http_policy.h"
#include "update_log.h"
#include "wininet_fallback.h"
#include "../platform/text_encoding.h"
#include "../platform/internet_handle.h"

#include <Windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cwctype>
#include <fstream>
#include <memory>
#include <sstream>
#include <thread>
#include <string_view>
#include <utility>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace OmniGhost::Update {
namespace {

using HttpHandle = OmniGhost::Platform::UniqueWinHttpHandle;

struct RequestHandles {
    HttpHandle session;
    HttpHandle connection;
    HttpHandle request;
    bool noProxy{};
};

std::string WinHttpHint(DWORD code) {
    switch (code) {
    case ERROR_WINHTTP_TIMEOUT:
        return "A ligação excedeu o tempo limite. Verifica a Internet, VPN, proxy e firewall.";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        return "O Windows não conseguiu resolver o nome do servidor. Verifica o DNS e a ligação à Internet.";
    case ERROR_WINHTTP_CANNOT_CONNECT:
        return "Não foi possível estabelecer ligação ao servidor HTTPS. Verifica a firewall, VPN, proxy e acesso ao GitHub.";
    case ERROR_WINHTTP_CONNECTION_ERROR:
        return "A ligação ao servidor foi interrompida ou reposta durante o pedido.";
    case ERROR_WINHTTP_SECURE_FAILURE:
        return "A validação TLS/certificado falhou. Confirma a data e hora do Windows, certificados, antivírus HTTPS e atualizações do sistema.";
#ifdef ERROR_WINHTTP_AUTODETECTION_FAILED
    case ERROR_WINHTTP_AUTODETECTION_FAILED:
        return "A deteção automática de proxy falhou. O atualizador tentará uma ligação direta.";
#endif
#ifdef ERROR_WINHTTP_UNABLE_TO_DOWNLOAD_SCRIPT
    case ERROR_WINHTTP_UNABLE_TO_DOWNLOAD_SCRIPT:
        return "O Windows não conseguiu descarregar o ficheiro de configuração automática de proxy (PAC).";
#endif
#ifdef ERROR_WINHTTP_REDIRECT_FAILED
    case ERROR_WINHTTP_REDIRECT_FAILED:
        return "O redirecionamento HTTPS do GitHub falhou.";
#endif
#ifdef ERROR_WINHTTP_INVALID_URL
    case ERROR_WINHTTP_INVALID_URL:
        return "A URL de atualização é inválida.";
#endif
    default:
        return "Consulta o código WinHTTP apresentado para identificar a causa no Windows.";
    }
}

std::string BuildWinHttpFailure(
    const char* operation,
    DWORD code,
    const std::string& url,
    bool noProxy) {
    std::ostringstream output;
    output << "Etapa HTTP: " << operation
           << " | modo=" << (noProxy ? "ligação direta" : "proxy automático")
           << " | código WinHTTP=" << code
           << " | descrição=" << WindowsError(code)
           << " | diagnóstico=" << WinHttpHint(code);

    if (!url.empty())
        output << " | URL=" << url;

    return output.str();
}

void ConfigureSession(void* session, int timeoutMilliseconds) {
    const int safeTimeout = HttpPolicy::ClampTimeoutMilliseconds(timeoutMilliseconds);
    WinHttpSetTimeouts(
        session,
        safeTimeout,
        safeTimeout,
        safeTimeout,
        safeTimeout);

#ifdef WINHTTP_OPTION_SECURE_PROTOCOLS
    DWORD secureProtocols = 0;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
    secureProtocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#endif
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    secureProtocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    if (secureProtocols != 0) {
        // Ignore failure on older Windows versions. Their system defaults are
        // still used and the actual request will provide a useful error.
        WinHttpSetOption(
            session,
            WINHTTP_OPTION_SECURE_PROTOCOLS,
            &secureProtocols,
            sizeof(secureProtocols));
    }
#endif
}

void ConfigureRequest(void* request) {
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(
        request,
        WINHTTP_OPTION_REDIRECT_POLICY,
        &redirectPolicy,
        sizeof(redirectPolicy));

#ifdef WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS
    DWORD maximumRedirects = 10;
    WinHttpSetOption(
        request,
        WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS,
        &maximumRedirects,
        sizeof(maximumRedirects));
#endif

#ifdef WINHTTP_OPTION_DECOMPRESSION
    DWORD decompression = 0;
#ifdef WINHTTP_DECOMPRESSION_FLAG_GZIP
    decompression |= WINHTTP_DECOMPRESSION_FLAG_GZIP;
#endif
#ifdef WINHTTP_DECOMPRESSION_FLAG_DEFLATE
    decompression |= WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
#endif
    if (decompression != 0) {
        WinHttpSetOption(
            request,
            WINHTTP_OPTION_DECOMPRESSION,
            &decompression,
            sizeof(decompression));
    }
#endif
}

bool CreateAndSendRequest(
    const std::string& url,
    int timeoutMilliseconds,
    const wchar_t* acceptHeader,
    bool noProxy,
    RequestHandles& handles,
    std::string& error) {
    if (!HttpPolicy::IsTlsOnlyUrl(url)) {
        error = "A URL de atualização não é HTTPS ou é inválida.";
        return false;
    }

    const std::wstring wideUrl = OmniGhost::Platform::Utf8ToWide(url);
    if (wideUrl.empty()) {
        error = "A URL contém caracteres inválidos.";
        return false;
    }

    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(
            wideUrl.c_str(),
            static_cast<DWORD>(wideUrl.size()),
            0,
            &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS ||
        parts.dwHostNameLength == 0) {
        error = "A URL de atualização não é HTTPS ou é inválida.";
        return false;
    }

    const DWORD accessType = noProxy
        ? WINHTTP_ACCESS_TYPE_NO_PROXY
        : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;

    handles.noProxy = noProxy;
    handles.session.reset(WinHttpOpen(
        L"OmniGhost-Updater/1.1",
        accessType,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));

    if (!handles.session) {
        const DWORD code = GetLastError();
        error = BuildWinHttpFailure("WinHttpOpen", code, url, noProxy);
        return false;
    }

    ConfigureSession(handles.session.get(), timeoutMilliseconds);

    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    handles.connection.reset(WinHttpConnect(
        handles.session.get(),
        host.c_str(),
        parts.nPort,
        0));

    if (!handles.connection) {
        const DWORD code = GetLastError();
        error = BuildWinHttpFailure("WinHttpConnect", code, url, noProxy) +
                " | host=" + OmniGhost::Platform::WideToUtf8(host);
        return false;
    }

    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength != 0)
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (path.empty())
        path = L"/";

    handles.request.reset(WinHttpOpenRequest(
        handles.connection.get(),
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH));

    if (!handles.request) {
        const DWORD code = GetLastError();
        error = BuildWinHttpFailure("WinHttpOpenRequest", code, url, noProxy);
        return false;
    }

    ConfigureRequest(handles.request.get());

    std::wstring headers = L"Accept: ";
    headers += acceptHeader;
    headers += L"\r\nCache-Control: no-cache\r\nPragma: no-cache\r\n";

    if (!WinHttpAddRequestHeaders(
            handles.request.get(),
            headers.c_str(),
            static_cast<DWORD>(-1),
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
        const DWORD code = GetLastError();
        error = BuildWinHttpFailure("WinHttpAddRequestHeaders", code, url, noProxy);
        return false;
    }

    if (!WinHttpSendRequest(
            handles.request.get(),
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0)) {
        const DWORD code = GetLastError();
        error = BuildWinHttpFailure("WinHttpSendRequest", code, url, noProxy);
        return false;
    }

    if (!WinHttpReceiveResponse(handles.request.get(), nullptr)) {
        const DWORD code = GetLastError();
        error = BuildWinHttpFailure("WinHttpReceiveResponse", code, url, noProxy);
        return false;
    }

    return true;
}

bool OpenRequestWithFallback(
    const std::string& url,
    int timeoutMilliseconds,
    const wchar_t* acceptHeader,
    RequestHandles& handles,
    std::string& error) {
    std::string automaticProxyError;
    if (CreateAndSendRequest(
            url,
            timeoutMilliseconds,
            acceptHeader,
            false,
            handles,
            automaticProxyError)) {
        return true;
    }

    handles = {};

    std::string directError;
    if (CreateAndSendRequest(
            url,
            timeoutMilliseconds,
            acceptHeader,
            true,
            handles,
            directError)) {
        return true;
    }

    error = "Ligação automática: " + automaticProxyError +
            " | Ligação direta: " + directError;
    return false;
}

bool QueryStatus(void* request, int& status, std::string& error) {
    DWORD value = 0;
    DWORD size = sizeof(value);

    if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &value,
            &size,
            WINHTTP_NO_HEADER_INDEX)) {
        error = "Não foi possível ler o estado HTTP: " +
                WindowsError(GetLastError());
        return false;
    }

    status = static_cast<int>(value);
    return true;
}

std::uint64_t ContentLength(void* request) {
    wchar_t value[64]{};
    DWORD size = sizeof(value);

    if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_CONTENT_LENGTH,
            WINHTTP_HEADER_NAME_BY_INDEX,
            value,
            &size,
            WINHTTP_NO_HEADER_INDEX)) {
        return 0;
    }

    try {
        return std::stoull(value);
    } catch (const std::exception& ex) {
        std::cerr << "[Updater] HttpClient: Failed to parse content-length '" << value << "': " << ex.what() << "\n";
        return 0;
    } catch (...) {
        std::cerr << "[Updater] HttpClient: Unknown exception parsing content-length '" << value << "'\n";
        return 0;
    }
}

std::wstring ContentType(void* request) {
    DWORD size = 0;
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_CONTENT_TYPE,
        WINHTTP_HEADER_NAME_BY_INDEX,
        WINHTTP_NO_OUTPUT_BUFFER,
        &size,
        WINHTTP_NO_HEADER_INDEX);

    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size < sizeof(wchar_t))
        return {};

    std::vector<wchar_t> value(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_CONTENT_TYPE,
            WINHTTP_HEADER_NAME_BY_INDEX,
            value.data(),
            &size,
            WINHTTP_NO_HEADER_INDEX)) {
        return {};
    }

    return std::wstring(value.data());
}

std::string FinalUrl(void* request) {
    DWORD size = 0;
    WinHttpQueryOption(request, WINHTTP_OPTION_URL, nullptr, &size);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size < sizeof(wchar_t))
        return {};

    std::vector<wchar_t> value(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryOption(request, WINHTTP_OPTION_URL, value.data(), &size))
        return {};

    return OmniGhost::Platform::WideToUtf8(std::wstring(value.data()));
}

int RetryAfterSeconds(void* request) {
    if (!request) return 0;
    wchar_t value[64]{};
    DWORD size = sizeof(value);
    if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_CUSTOM,
            L"Retry-After",
            value,
            &size,
            WINHTTP_NO_HEADER_INDEX)) {
        return 0;
    }
    wchar_t* end = nullptr;
    const long seconds = std::wcstol(value, &end, 10);
    if (end == value || seconds <= 0) return 0;
    return static_cast<int>((std::min)(seconds, 30L));
}

HttpResult StatusError(int status, const std::string& finalUrl, void* request) {
    HttpResult result{};
    result.statusCode = status;
    result.error = HttpPolicy::StatusMessage(status, finalUrl);
    result.retryAfterSeconds = RetryAfterSeconds(request);
    result.errorClass = HttpPolicy::ErrorClass::Http;
    return result;
}

bool IsHtmlContent(const std::wstring& contentType) {
    std::wstring lowered = contentType;
    for (wchar_t& character : lowered)
        character = static_cast<wchar_t>(towlower(character));
    return lowered.find(L"text/html") != std::wstring::npos;
}

void RemoveUtf8Bom(std::string& body) {
    if (body.size() >= 3 &&
        static_cast<unsigned char>(body[0]) == 0xEF &&
        static_cast<unsigned char>(body[1]) == 0xBB &&
        static_cast<unsigned char>(body[2]) == 0xBF) {
        body.erase(0, 3);
    }
}

} // namespace

HttpResult GetTextWithWinHttp(
    const std::string& url,
    int timeoutMilliseconds,
    std::atomic_bool& cancelled) {
    RequestHandles handles;
    HttpResult result{};

    if (!OpenRequestWithFallback(
            url,
            timeoutMilliseconds,
            L"application/json, text/plain;q=0.9, application/octet-stream;q=0.8, */*;q=0.1",
            handles,
            result.error)) {
        return result;
    }

    if (!QueryStatus(handles.request.get(), result.statusCode, result.error))
        return result;

    const std::string finalUrl = FinalUrl(handles.request.get());
    if (result.statusCode != 200)
        return StatusError(result.statusCode, finalUrl, handles.request.get());

    const std::wstring contentType = ContentType(handles.request.get());
    if (IsHtmlContent(contentType)) {
        result.error = "O servidor devolveu HTML em vez do manifesto JSON.";
        if (!finalUrl.empty())
            result.error += " URL final: " + finalUrl;
        return result;
    }

    const std::uint64_t advertisedLength = ContentLength(handles.request.get());
    if (advertisedLength > HttpPolicy::MaximumManifestBytes) {
        result.error = "O manifesto excede o limite permitido.";
        return result;
    }

    std::array<char, 64 * 1024> buffer{};

    while (!cancelled.load()) {
        DWORD read = 0;
        if (!WinHttpReadData(
                handles.request.get(),
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &read)) {
            const DWORD code = GetLastError();
            result.error = BuildWinHttpFailure(
                "WinHttpReadData",
                code,
                finalUrl.empty() ? url : finalUrl,
                handles.noProxy);
            return result;
        }

        if (read == 0)
            break;

        if (result.body.size() + read > HttpPolicy::MaximumManifestBytes) {
            result.error = "O manifesto excede o limite permitido.";
            return result;
        }

        result.body.append(buffer.data(), read);
    }

    if (cancelled.load()) {
        result.error = "Verificação cancelada.";
        return result;
    }

    RemoveUtf8Bom(result.body);

    if (result.body.empty()) {
        result.error = "O servidor devolveu um manifesto vazio.";
        return result;
    }

    return result;
}

HttpResult DownloadWithWinHttp(
    const std::string& url,
    const std::filesystem::path& partialFile,
    std::uint64_t expectedSize,
    std::uint64_t maximumSize,
    int timeoutMilliseconds,
    std::atomic_bool& cancelled,
    const ProgressCallback& progress) {
    RequestHandles handles;
    HttpResult result{};

    if (!OpenRequestWithFallback(
            url,
            timeoutMilliseconds,
            L"application/octet-stream, application/zip, */*;q=0.1",
            handles,
            result.error)) {
        return result;
    }

    if (!QueryStatus(handles.request.get(), result.statusCode, result.error))
        return result;

    const std::string finalUrl = FinalUrl(handles.request.get());
    if (result.statusCode != 200)
        return StatusError(result.statusCode, finalUrl, handles.request.get());

    const std::uint64_t contentLength = ContentLength(handles.request.get());
    const std::wstring contentType = ContentType(handles.request.get());

    if (IsHtmlContent(contentType)) {
        result.error = "O servidor devolveu HTML em vez do pacote.";
        if (!finalUrl.empty())
            result.error += " URL final: " + finalUrl;
        return result;
    }

    if (contentLength != 0 &&
        (contentLength != expectedSize || contentLength > maximumSize)) {
        result.error = "O tamanho anunciado pelo servidor é inválido. Esperado: " +
                       std::to_string(expectedSize) +
                       "; recebido: " + std::to_string(contentLength) + ".";
        return result;
    }

    std::error_code fileError;
    std::filesystem::create_directories(partialFile.parent_path(), fileError);
    if (fileError) {
        result.error = "Não foi possível criar a pasta temporária: " +
                       fileError.message();
        return result;
    }

    std::ofstream stream(partialFile, std::ios::binary | std::ios::trunc);
    if (!stream) {
        result.error = "Não foi possível criar o download temporário.";
        return result;
    }

    std::array<char, 64 * 1024> buffer{};
    std::uint64_t total = 0;
    const auto start = std::chrono::steady_clock::now();

    while (!cancelled.load()) {
        DWORD read = 0;
        if (!WinHttpReadData(
                handles.request.get(),
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &read)) {
            const DWORD code = GetLastError();
            result.error = BuildWinHttpFailure(
                "WinHttpReadData",
                code,
                finalUrl.empty() ? url : finalUrl,
                handles.noProxy);
            break;
        }

        if (read == 0)
            break;

        total += read;
        if (total > maximumSize || total > expectedSize) {
            result.error = "O download excedeu o tamanho permitido.";
            break;
        }

        stream.write(buffer.data(), read);
        if (!stream) {
            result.error = "Falha ao escrever o download. Verifica o espaço livre.";
            break;
        }

        const double seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start).count();

        if (progress) {
            progress(
                total,
                expectedSize,
                seconds > 0.0 ? total / seconds : 0.0);
        }
    }

    stream.close();

    if (cancelled.load())
        result.error = "Download cancelado.";

    if (result.error.empty() && total != expectedSize) {
        result.error = "O download terminou incompleto. Esperado: " +
                       std::to_string(expectedSize) +
                       "; recebido: " + std::to_string(total) + ".";
    }

    if (!result.error.empty()) {
        std::filesystem::remove(partialFile, fileError);
        return result;
    }

    return result;
}


namespace {

bool ShouldTryWinInetFallback(const HttpResult& result, bool cancelled) {
    if (cancelled || result.error.empty())
        return false;

    return result.statusCode == 0 ||
           result.error.find("WinHttp") != std::string::npos ||
           result.error.find("WinHTTP") != std::string::npos;
}

HttpResult CombineTransportFailures(
    HttpResult primary,
    HttpResult fallback) {
    if (fallback.statusCode == 0)
        fallback.statusCode = primary.statusCode;

    fallback.error =
        "Transporte WinHTTP: " + primary.error +
        " | Transporte alternativo WinINet: " + fallback.error;
    return fallback;
}

} // namespace

bool WaitForRetry(unsigned attempt, const HttpResult& result, std::atomic_bool& cancelled) {
    const unsigned delay = HttpPolicy::RetryDelayMilliseconds(attempt, result.retryAfterSeconds);
    constexpr unsigned slice = 50;
    for (unsigned waited = 0; waited < delay && !cancelled.load(); waited += slice)
        std::this_thread::sleep_for(std::chrono::milliseconds((std::min)(slice, delay - waited)));
    return !cancelled.load();
}

bool Retryable(const HttpResult& result) {
    if (result.error.empty()) return false;
    if (result.statusCode != 0)
        return HttpPolicy::IsRetryableStatus(result.statusCode);

    switch (result.errorClass) {
    case HttpPolicy::ErrorClass::Timeout:
    case HttpPolicy::ErrorClass::Dns:
    case HttpPolicy::ErrorClass::Connection:
    case HttpPolicy::ErrorClass::Tls:
    case HttpPolicy::ErrorClass::Unknown:
        return true;
    case HttpPolicy::ErrorClass::None:
    case HttpPolicy::ErrorClass::Cancelled:
    case HttpPolicy::ErrorClass::Http:
    case HttpPolicy::ErrorClass::Redirect:
    case HttpPolicy::ErrorClass::SizeLimit:
    case HttpPolicy::ErrorClass::Io:
    case HttpPolicy::ErrorClass::InvalidUrl:
    default:
        return false;
    }
}

void FinalizeClassification(HttpResult& result) {
    if (result.errorClass == HttpPolicy::ErrorClass::None && !result.error.empty())
        result.errorClass = HttpPolicy::ClassifyFailure(result.statusCode, result.error);
}

HttpResult GetTextAttempt(const std::string& url, int timeoutMilliseconds, std::atomic_bool& cancelled) {
    HttpResult primary = GetTextWithWinHttp(url, timeoutMilliseconds, cancelled);
    if (!ShouldTryWinInetFallback(primary, cancelled.load())) return primary;
    HttpResult fallback = GetTextWithWinInet(url, timeoutMilliseconds, cancelled);
    if (fallback.error.empty()) return fallback;
    return CombineTransportFailures(std::move(primary), std::move(fallback));
}

HttpResult DownloadAttempt(const std::string& url, const std::filesystem::path& partialFile,
                           std::uint64_t expectedSize, std::uint64_t maximumSize,
                           int timeoutMilliseconds, std::atomic_bool& cancelled,
                           const ProgressCallback& progress) {
    HttpResult primary = DownloadWithWinHttp(url, partialFile, expectedSize, maximumSize,
                                             timeoutMilliseconds, cancelled, progress);
    if (!ShouldTryWinInetFallback(primary, cancelled.load())) return primary;
    std::error_code cleanupError;
    std::filesystem::remove(partialFile, cleanupError);
    HttpResult fallback = DownloadWithWinInet(url, partialFile, expectedSize, maximumSize,
                                              timeoutMilliseconds, cancelled, progress);
    if (fallback.error.empty()) return fallback;
    return CombineTransportFailures(std::move(primary), std::move(fallback));
}

HttpResult WinHttpClient::GetText(
    const std::string& url,
    int timeoutMilliseconds,
    std::atomic_bool& cancelled) {
    HttpResult result{};
    for (unsigned attempt = 0; attempt <= HttpPolicy::MaximumRetries; ++attempt) {
        if (cancelled.load()) {
            result.error = "Verificação cancelada.";
            result.errorClass = HttpPolicy::ErrorClass::Cancelled;
            return result;
        }
        result = GetTextAttempt(url, HttpPolicy::ClampTimeoutMilliseconds(timeoutMilliseconds), cancelled);
        FinalizeClassification(result);
        if (!Retryable(result) || attempt == HttpPolicy::MaximumRetries) return result;
        if (!WaitForRetry(attempt, result, cancelled)) {
            result.error = "Verificação cancelada.";
            result.errorClass = HttpPolicy::ErrorClass::Cancelled;
            return result;
        }
    }
    return result;
}

HttpResult WinHttpClient::Download(
    const std::string& url,
    const std::filesystem::path& partialFile,
    std::uint64_t expectedSize,
    std::uint64_t maximumSize,
    int timeoutMilliseconds,
    std::atomic_bool& cancelled,
    const ProgressCallback& progress) {
    HttpResult result{};
    if (expectedSize == 0 || maximumSize == 0 || expectedSize > maximumSize) {
        result.error = "O tamanho esperado do pacote é inválido ou excede o limite permitido.";
        result.errorClass = HttpPolicy::ErrorClass::SizeLimit;
        return result;
    }
    for (unsigned attempt = 0; attempt <= HttpPolicy::MaximumRetries; ++attempt) {
        if (cancelled.load()) {
            result.error = "Download cancelado.";
            result.errorClass = HttpPolicy::ErrorClass::Cancelled;
            return result;
        }
        if (attempt != 0) {
            std::error_code cleanupError;
            std::filesystem::remove(partialFile, cleanupError);
        }
        result = DownloadAttempt(url, partialFile, expectedSize, maximumSize,
                                 HttpPolicy::ClampTimeoutMilliseconds(timeoutMilliseconds), cancelled, progress);
        FinalizeClassification(result);
        if (!Retryable(result) || attempt == HttpPolicy::MaximumRetries) return result;
        if (!WaitForRetry(attempt, result, cancelled)) {
            result.error = "Download cancelado.";
            result.errorClass = HttpPolicy::ErrorClass::Cancelled;
            return result;
        }
    }
    return result;
}

} // namespace OmniGhost::Update
