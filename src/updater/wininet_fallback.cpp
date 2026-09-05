#include "wininet_fallback.h"
#include "http_policy.h"
#include "update_log.h"
#include "../platform/text_encoding.h"
#include "../platform/internet_handle.h"

#include <Windows.h>
#include <wininet.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "wininet.lib")

namespace OmniGhost::Update {
    namespace {

        using InternetHandle = OmniGhost::Platform::UniqueWinInetHandle;

        struct RequestHandles {
            InternetHandle session;
            InternetHandle request;
            bool direct{ false };
        };

        bool IsHttpsUrl(
            const std::wstring& url,
            std::string* diagnostic = nullptr) {
            if (url.empty()) {
                if (diagnostic != nullptr) {
                    *diagnostic = "A URL está vazia.";
                }
                return false;
            }

            /*
             * InternetCrackUrlW only accepts ICU_DECODE/ICU_ESCAPE when the
             * caller supplies output buffers. The previous implementation
             * requested pointer slices and passed ICU_DECODE, so WinINet
             * rejected every valid HTTPS URL before opening the connection.
             *
             * Explicit Unicode buffers also make this independent from the
             * Visual Studio Character Set setting.
             */
            std::array<wchar_t, 16> schemeBuffer{};
            std::array<wchar_t, 256> hostBuffer{};

            URL_COMPONENTSW parts{};
            parts.dwStructSize = sizeof(parts);
            parts.lpszScheme = schemeBuffer.data();
            parts.dwSchemeLength = static_cast<DWORD>(schemeBuffer.size());
            parts.lpszHostName = hostBuffer.data();
            parts.dwHostNameLength = static_cast<DWORD>(hostBuffer.size());

            SetLastError(ERROR_SUCCESS);
            const BOOL parsed = InternetCrackUrlW(
                url.c_str(),
                0,
                0,
                &parts);

            if (parsed != TRUE) {
                if (diagnostic != nullptr) {
                    const DWORD code = GetLastError();
                    *diagnostic =
                        "InternetCrackUrlW falhou com o código " +
                        std::to_string(code) +
                        ": " + WindowsError(code);
                }
                return false;
            }

            if (parts.nScheme != INTERNET_SCHEME_HTTPS) {
                if (diagnostic != nullptr) {
                    *diagnostic = "O esquema da URL não é HTTPS.";
                }
                return false;
            }

            if (parts.dwHostNameLength == 0 || hostBuffer[0] == L'\0') {
                if (diagnostic != nullptr) {
                    *diagnostic = "A URL não contém um nome de servidor.";
                }
                return false;
            }

            return true;
        }

        std::string InternetHint(DWORD code) {
            switch (code) {
            case ERROR_INTERNET_TIMEOUT:
                return
                    "A ligação excedeu o tempo limite. "
                    "Verifica a Internet, VPN, proxy e firewall.";

            case ERROR_INTERNET_NAME_NOT_RESOLVED:
                return
                    "O Windows não conseguiu resolver o nome do servidor "
                    "através das definições de Internet do utilizador.";

            case ERROR_INTERNET_CANNOT_CONNECT:
                return
                    "Não foi possível estabelecer ligação HTTPS ao servidor "
                    "através das definições de Internet do utilizador.";

            case ERROR_INTERNET_CONNECTION_ABORTED:
            case ERROR_INTERNET_CONNECTION_RESET:
                return
                    "A ligação foi interrompida ou reposta durante o pedido.";

            case ERROR_INTERNET_SEC_CERT_DATE_INVALID:
            case ERROR_INTERNET_SEC_CERT_CN_INVALID:
            case ERROR_INTERNET_INVALID_CA:
                return
                    "A validação TLS ou do certificado HTTPS falhou. "
                    "Confirma a data, certificados e inspeção HTTPS do antivírus.";

#ifdef ERROR_INTERNET_REDIRECT_FAILED
            case ERROR_INTERNET_REDIRECT_FAILED:
                return
                    "O redirecionamento HTTPS do GitHub falhou.";
#endif

            default:
                return
                    "Consulta o código WinINet apresentado para identificar "
                    "a causa no Windows.";
            }
        }

        std::string BuildInternetFailure(
            const char* operation,
            DWORD code,
            const std::string& url,
            bool direct) {
            std::ostringstream output;

            output
                << "Etapa WinINet: "
                << (operation != nullptr ? operation : "desconhecida")
                << " | modo="
                << (
                    direct
                    ? "ligação direta"
                    : "definições de Internet do utilizador")
                << " | código WinINet="
                << code
                << " | descrição="
                << WindowsError(code)
                << " | diagnóstico="
                << InternetHint(code);

            if (!url.empty()) {
                output << " | URL=" << url;
            }

            return output.str();
        }

        void ConfigureTimeouts(
            void* handle,
            int timeoutMilliseconds) {
            if (handle == nullptr) {
                return;
            }

            const int effectiveTimeout =
                HttpPolicy::ClampTimeoutMilliseconds(timeoutMilliseconds);

            DWORD timeout =
                static_cast<DWORD>(effectiveTimeout);

            InternetSetOptionW(
                static_cast<HINTERNET>(handle),
                INTERNET_OPTION_CONNECT_TIMEOUT,
                &timeout,
                sizeof(timeout));

            InternetSetOptionW(
                static_cast<HINTERNET>(handle),
                INTERNET_OPTION_SEND_TIMEOUT,
                &timeout,
                sizeof(timeout));

            InternetSetOptionW(
                static_cast<HINTERNET>(handle),
                INTERNET_OPTION_RECEIVE_TIMEOUT,
                &timeout,
                sizeof(timeout));
        }

        std::string FinalUrl(void* request) {
            if (request == nullptr) {
                return {};
            }

            DWORD size = 0;

            SetLastError(ERROR_SUCCESS);

            InternetQueryOptionW(
                static_cast<HINTERNET>(request),
                INTERNET_OPTION_URL,
                nullptr,
                &size);

            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
                size < sizeof(wchar_t)) {
                return {};
            }

            std::vector<wchar_t> value(
                (static_cast<std::size_t>(size) /
                    sizeof(wchar_t)) +
                1,
                L'\0');

            if (!InternetQueryOptionW(
                static_cast<HINTERNET>(request),
                INTERNET_OPTION_URL,
                value.data(),
                &size)) {
                return {};
            }

            value.back() = L'\0';

            return OmniGhost::Platform::WideToUtf8(
                std::wstring(value.data()));
        }

        bool OpenRequest(
            const std::string& url,
            int timeoutMilliseconds,
            const wchar_t* acceptHeader,
            bool direct,
            RequestHandles& handles,
            std::string& error) {
            if (!HttpPolicy::IsTlsOnlyUrl(url)) {
                error = "A URL de atualização não é HTTPS ou é inválida.";
                return false;
            }

            const std::wstring wideUrl = OmniGhost::Platform::Utf8ToWide(url);

            if (wideUrl.empty()) {
                error =
                    "Não foi possível converter a URL de atualização para Unicode.";
                return false;
            }

            std::string urlDiagnostic;
            if (!IsHttpsUrl(wideUrl, &urlDiagnostic)) {
                error =
                    "A URL de atualização não é HTTPS ou é inválida. " +
                    urlDiagnostic +
                    " URL=" + url;
                return false;
            }

            handles.direct = direct;

            handles.session.reset(
                InternetOpenW(
                    L"OmniGhost-Updater/1.2",
                    direct
                    ? INTERNET_OPEN_TYPE_DIRECT
                    : INTERNET_OPEN_TYPE_PRECONFIG,
                    nullptr,
                    nullptr,
                    0));

            if (!handles.session) {
                const DWORD code = GetLastError();

                error = BuildInternetFailure(
                    "InternetOpen",
                    code,
                    url,
                    direct);

                return false;
            }

            ConfigureTimeouts(
                handles.session.get(),
                timeoutMilliseconds);

            std::wstring headers =
                L"Accept: ";

            headers +=
                acceptHeader != nullptr
                ? acceptHeader
                : L"*/*";

            headers +=
                L"\r\n"
                L"Cache-Control: no-cache\r\n"
                L"Pragma: no-cache\r\n";

            constexpr DWORD flags =
                INTERNET_FLAG_RELOAD |
                INTERNET_FLAG_NO_CACHE_WRITE |
                INTERNET_FLAG_PRAGMA_NOCACHE |
                INTERNET_FLAG_NO_UI |
                INTERNET_FLAG_NO_COOKIES |
                INTERNET_FLAG_NO_AUTH |
                INTERNET_FLAG_KEEP_CONNECTION |
                INTERNET_FLAG_SECURE;

            handles.request.reset(
                InternetOpenUrlW(
                    static_cast<HINTERNET>(
                        handles.session.get()),
                    wideUrl.c_str(),
                    headers.c_str(),
                    static_cast<DWORD>(
                        headers.size()),
                    flags,
                    0));

            if (!handles.request) {
                const DWORD code = GetLastError();

                error = BuildInternetFailure(
                    "InternetOpenUrl",
                    code,
                    url,
                    direct);

                return false;
            }

            ConfigureTimeouts(
                handles.request.get(),
                timeoutMilliseconds);

            const std::string finalUrl =
                FinalUrl(handles.request.get());

            if (!finalUrl.empty()) {
                const std::wstring finalWideUrl =
                    OmniGhost::Platform::Utf8ToWide(finalUrl);

                std::string redirectDiagnostic;
                if (finalWideUrl.empty() ||
                    !IsHttpsUrl(finalWideUrl, &redirectDiagnostic)) {
                    error =
                        "O redirecionamento terminou numa URL não segura ou inválida: " +
                        finalUrl;

                    if (!redirectDiagnostic.empty()) {
                        error += " | diagnóstico=" + redirectDiagnostic;
                    }

                    return false;
                }
            }

            return true;
        }

        bool OpenRequestWithFallback(
            const std::string& url,
            int timeoutMilliseconds,
            const wchar_t* acceptHeader,
            RequestHandles& handles,
            std::string& error) {
            std::string userSettingsError;

            if (OpenRequest(
                url,
                timeoutMilliseconds,
                acceptHeader,
                false,
                handles,
                userSettingsError)) {
                return true;
            }

            handles = RequestHandles{};

            std::string directError;

            if (OpenRequest(
                url,
                timeoutMilliseconds,
                acceptHeader,
                true,
                handles,
                directError)) {
                return true;
            }

            error =
                "Definições do utilizador: " +
                userSettingsError +
                " | Ligação direta: " +
                directError;

            return false;
        }

        bool QueryStatus(
            void* request,
            bool direct,
            int& status,
            std::string& error) {
            if (request == nullptr) {
                error =
                    "O pedido WinINet não foi inicializado.";
                return false;
            }

            DWORD value = 0;
            DWORD size = sizeof(value);

            if (!HttpQueryInfoW(
                static_cast<HINTERNET>(request),
                HTTP_QUERY_STATUS_CODE |
                HTTP_QUERY_FLAG_NUMBER,
                &value,
                &size,
                nullptr)) {
                const DWORD code = GetLastError();

                error = BuildInternetFailure(
                    "HttpQueryInfo(status)",
                    code,
                    {},
                    direct);

                return false;
            }

            status = static_cast<int>(value);
            return true;
        }

        std::uint64_t ContentLength(
            void* request) {
            if (request == nullptr) {
                return 0;
            }

            wchar_t value[64]{};
            DWORD size = sizeof(value);

            if (!HttpQueryInfoW(
                static_cast<HINTERNET>(request),
                HTTP_QUERY_CONTENT_LENGTH,
                value,
                &size,
                nullptr)) {
                return 0;
            }

            try {
                return std::stoull(value);
            }
            catch (...) {
                return 0;
            }
        }

        std::wstring ContentType(
            void* request) {
            if (request == nullptr) {
                return {};
            }

            DWORD size = 0;

            SetLastError(ERROR_SUCCESS);

            HttpQueryInfoW(
                static_cast<HINTERNET>(request),
                HTTP_QUERY_CONTENT_TYPE,
                nullptr,
                &size,
                nullptr);

            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
                size < sizeof(wchar_t)) {
                return {};
            }

            std::vector<wchar_t> value(
                (static_cast<std::size_t>(size) /
                    sizeof(wchar_t)) +
                1,
                L'\0');

            if (!HttpQueryInfoW(
                static_cast<HINTERNET>(request),
                HTTP_QUERY_CONTENT_TYPE,
                value.data(),
                &size,
                nullptr)) {
                return {};
            }

            value.back() = L'\0';

            return std::wstring(value.data());
        }

        HttpResult StatusError(
            int status,
            const std::string& finalUrl) {
            HttpResult result{};
            result.statusCode = status;
            result.error = HttpPolicy::StatusMessage(status, finalUrl);
            result.errorClass = HttpPolicy::ErrorClass::Http;
            return result;
        }

        bool IsHtmlContent(
            const std::wstring& contentType) {
            std::wstring lowered =
                contentType;

            for (wchar_t& character : lowered) {
                character =
                    static_cast<wchar_t>(
                        std::towlower(character));
            }

            return lowered.find(
                L"text/html") !=
                std::wstring::npos;
        }

        void RemoveUtf8Bom(
            std::string& body) {
            if (body.size() >= 3 &&
                static_cast<unsigned char>(
                    body[0]) == 0xEF &&
                static_cast<unsigned char>(
                    body[1]) == 0xBB &&
                static_cast<unsigned char>(
                    body[2]) == 0xBF) {
                body.erase(0, 3);
            }
        }

    } // namespace

    HttpResult GetTextWithWinInet(
        const std::string& url,
        int timeoutMilliseconds,
        std::atomic_bool& cancelled) {
        RequestHandles handles;
        HttpResult result{};

        if (!OpenRequestWithFallback(
            url,
            timeoutMilliseconds,
            L"application/json, "
            L"text/plain;q=0.9, "
            L"application/octet-stream;q=0.8, "
            L"*/*;q=0.1",
            handles,
            result.error)) {
            return result;
        }

        if (!QueryStatus(
            handles.request.get(),
            handles.direct,
            result.statusCode,
            result.error)) {
            return result;
        }

        const std::string finalUrl =
            FinalUrl(handles.request.get());

        if (result.statusCode != 200) {
            return StatusError(
                result.statusCode,
                finalUrl);
        }

        const std::wstring contentType =
            ContentType(handles.request.get());

        if (IsHtmlContent(contentType)) {
            result.error =
                "O servidor devolveu HTML em vez do manifesto JSON.";

            if (!finalUrl.empty()) {
                result.error +=
                    " URL final: " +
                    finalUrl;
            }

            return result;
        }

        const std::uint64_t advertisedLength =
            ContentLength(handles.request.get());

        constexpr std::uint64_t maximumManifestSize =
            HttpPolicy::MaximumManifestBytes;

        if (advertisedLength >
            maximumManifestSize) {
            result.error =
                "O manifesto excede o limite permitido.";

            return result;
        }

        std::array<char, 64 * 1024> buffer{};

        while (!cancelled.load()) {
            DWORD read = 0;

            if (!InternetReadFile(
                static_cast<HINTERNET>(
                    handles.request.get()),
                buffer.data(),
                static_cast<DWORD>(
                    buffer.size()),
                &read)) {
                const DWORD code =
                    GetLastError();

                result.error =
                    BuildInternetFailure(
                        "InternetReadFile",
                        code,
                        finalUrl.empty()
                        ? url
                        : finalUrl,
                        handles.direct);

                return result;
            }

            if (read == 0) {
                break;
            }

            if (result.body.size() +
                static_cast<std::size_t>(read) >
                maximumManifestSize) {
                result.error =
                    "O manifesto excede o limite permitido.";

                return result;
            }

            result.body.append(
                buffer.data(),
                static_cast<std::size_t>(read));
        }

        if (cancelled.load()) {
            result.error =
                "Verificação cancelada.";

            return result;
        }

        RemoveUtf8Bom(result.body);

        if (result.body.empty()) {
            result.error =
                "O servidor devolveu um manifesto vazio.";

            return result;
        }

        return result;
    }

    HttpResult DownloadWithWinInet(
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
            L"application/octet-stream, "
            L"application/zip, "
            L"*/*;q=0.1",
            handles,
            result.error)) {
            return result;
        }

        if (!QueryStatus(
            handles.request.get(),
            handles.direct,
            result.statusCode,
            result.error)) {
            return result;
        }

        const std::string finalUrl =
            FinalUrl(handles.request.get());

        if (result.statusCode != 200) {
            return StatusError(
                result.statusCode,
                finalUrl);
        }

        const std::uint64_t contentLength =
            ContentLength(handles.request.get());

        const std::wstring contentType =
            ContentType(handles.request.get());

        if (IsHtmlContent(contentType)) {
            result.error =
                "O servidor devolveu HTML em vez do pacote.";

            if (!finalUrl.empty()) {
                result.error +=
                    " URL final: " +
                    finalUrl;
            }

            return result;
        }

        if (expectedSize == 0) {
            result.error =
                "O tamanho esperado do pacote é inválido.";

            return result;
        }

        if (maximumSize == 0 ||
            expectedSize > maximumSize) {
            result.error =
                "O tamanho esperado excede o limite permitido.";

            return result;
        }

        if (contentLength != 0 &&
            (contentLength != expectedSize ||
                contentLength > maximumSize)) {
            result.error =
                "O tamanho anunciado pelo servidor é inválido. "
                "Esperado: " +
                std::to_string(expectedSize) +
                "; recebido: " +
                std::to_string(contentLength) +
                ".";

            return result;
        }

        std::error_code fileError;

        const std::filesystem::path parentDirectory =
            partialFile.parent_path();

        if (!parentDirectory.empty()) {
            std::filesystem::create_directories(
                parentDirectory,
                fileError);

            if (fileError) {
                result.error =
                    "Não foi possível criar a pasta temporária: " +
                    fileError.message();

                return result;
            }
        }

        std::ofstream stream(
            partialFile,
            std::ios::binary |
            std::ios::trunc);

        if (!stream) {
            result.error =
                "Não foi possível criar o download temporário.";

            return result;
        }

        std::array<char, 64 * 1024> buffer{};
        std::uint64_t total = 0;

        const auto start =
            std::chrono::steady_clock::now();

        while (!cancelled.load()) {
            DWORD read = 0;

            if (!InternetReadFile(
                static_cast<HINTERNET>(
                    handles.request.get()),
                buffer.data(),
                static_cast<DWORD>(
                    buffer.size()),
                &read)) {
                const DWORD code =
                    GetLastError();

                result.error =
                    BuildInternetFailure(
                        "InternetReadFile",
                        code,
                        finalUrl.empty()
                        ? url
                        : finalUrl,
                        handles.direct);

                break;
            }

            if (read == 0) {
                break;
            }

            total +=
                static_cast<std::uint64_t>(read);

            if (total > maximumSize ||
                total > expectedSize) {
                result.error =
                    "O download excedeu o tamanho permitido.";

                break;
            }

            stream.write(
                buffer.data(),
                static_cast<std::streamsize>(read));

            if (!stream) {
                result.error =
                    "Falha ao escrever o download. "
                    "Verifica o espaço livre.";

                break;
            }

            const double seconds =
                std::chrono::duration<double>(
                    std::chrono::steady_clock::now() -
                    start)
                .count();

            if (progress) {
                progress(
                    total,
                    expectedSize,
                    seconds > 0.0
                    ? static_cast<double>(total) /
                    seconds
                    : 0.0);
            }
        }

        stream.close();

        if (cancelled.load()) {
            result.error =
                "Download cancelado.";
        }

        if (result.error.empty() &&
            total != expectedSize) {
            result.error =
                "O download terminou incompleto. "
                "Esperado: " +
                std::to_string(expectedSize) +
                "; recebido: " +
                std::to_string(total) +
                ".";
        }

        if (!result.error.empty()) {
            fileError.clear();

            std::filesystem::remove(
                partialFile,
                fileError);

            return result;
        }

        return result;
    }

} // namespace OmniGhost::Update