#include "launcher_account_service.h"

#include "../app_version.h"
#include "../platform/internet_handle.h"
#include "../platform/scope_exit.h"
#include "../platform/system_info.h"
#include "../updater/json.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <objbase.h>
#include <wincred.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <sstream>
#include <utility>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winhttp.lib")

namespace OmniGhost::Auth {
namespace {

constexpr wchar_t kApiHost[] = L"omnighost.store";
constexpr wchar_t kCredentialTarget[] = L"OmniGhost/LauncherSession/v1";
constexpr wchar_t kDeviceCredentialTarget[] = L"OmniGhost/InstallationDevice/v1";
constexpr std::size_t kMaximumResponseBytes = 256u * 1024u;
constexpr std::size_t kMaximumTokenBytes = 2048;
constexpr std::size_t kMaximumCredentialBytes = 2400;
constexpr auto kRenewalMargin = std::chrono::minutes(5);

std::string TrimLower(std::string_view input) {
    std::size_t first = 0;
    std::size_t last = input.size();
    while (first < last && std::isspace(static_cast<unsigned char>(input[first]))) ++first;
    while (last > first && std::isspace(static_cast<unsigned char>(input[last - 1]))) --last;
    std::string result(input.substr(first, last - first));
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return result;
}

std::string JsonEscape(std::string_view value) {
    std::string output;
    output.reserve(value.size() + 16);
    static constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char character : value) {
        switch (character) {
        case '\"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (character < 0x20) {
                output += "\\u00";
                output.push_back(hex[(character >> 4) & 0x0f]);
                output.push_back(hex[character & 0x0f]);
            } else {
                output.push_back(static_cast<char>(character));
            }
        }
    }
    return output;
}

std::wstring WidenAscii(std::string_view value) {
    return std::wstring(value.begin(), value.end());
}

std::chrono::system_clock::time_point ParseIso8601(std::string_view value) {
    if (value.size() < 19) return {};
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (sscanf_s(std::string(value.substr(0, 19)).c_str(), "%d-%d-%dT%d:%d:%d",
                 &year, &month, &day, &hour, &minute, &second) != 6)
        return {};
    std::tm parsed{};
    parsed.tm_year = year - 1900;
    parsed.tm_mon = month - 1;
    parsed.tm_mday = day;
    parsed.tm_hour = hour;
    parsed.tm_min = minute;
    parsed.tm_sec = second;
    const __time64_t utc = _mkgmtime64(&parsed);
    if (utc < 0) return {};
    return std::chrono::system_clock::from_time_t(static_cast<std::time_t>(utc));
}

const OmniGhost::Update::Json::Value* JsonField(
    const OmniGhost::Update::Json::Object& object, std::string_view name) {
    return OmniGhost::Update::Json::Field(object, name);
}

bool Text(const OmniGhost::Update::Json::Object& object, std::string_view name,
          std::string& output, bool required = true) {
    const auto* field = JsonField(object, name);
    if (!field) return !required;
    const auto* text = OmniGhost::Update::Json::AsString(*field);
    if (!text) return !required &&
        std::holds_alternative<std::nullptr_t>(field->value);
    output = *text;
    return true;
}

std::string ErrorText(const LauncherApiResponse& response, std::string fallback) {
    OmniGhost::Update::Json::Value root;
    OmniGhost::Update::Json::Limits limits{16, 16u * 1024u, 1000};
    if (OmniGhost::Update::Json::Parse(response.body, root, limits).ok) {
        if (const auto* object = OmniGhost::Update::Json::AsObject(root)) {
            std::string message;
            if (Text(*object, "message", message, false) && !message.empty()) return message;
            if (Text(*object, "error", message, false) && !message.empty()) {
                if (message == "invalid_credentials")
                    return "O email ou a palavra-passe da conta do site estão incorretos. Se usas apenas Google, define primeiro uma palavra-passe no site.";
                if (message == "launcher_account_exists")
                    return "Esta conta já foi ativada no launcher. Usa Iniciar sessão.";
                if (message == "launcher_license_not_owned")
                    return "A licença não pertence à conta indicada ou não está ativa.";
                if (message == "launcher_license_not_active")
                    return "A licença está suspensa, revogada ou expirada.";
                if (message == "no_active_license")
                    return "A conta não possui uma licença ativa.";
                if (message == "unauthorized" || message == "token_expired")
                    return "A sessão terminou. Inicia sessão novamente.";
                if (message == "device_mismatch")
                    return "A sessão pertence a outro dispositivo. Inicia sessão novamente.";
                if (message == "rate_limited")
                    return "Foram feitas demasiadas tentativas. Aguarda um pouco e tenta novamente.";
                return fallback;
            }
        }
    }
    return fallback;
}

LicenseStatus StatusForHttp(int status) {
    if (status == 401 || status == 403) return LicenseStatus::Invalid;
    if (status == 408 || status == 429) return LicenseStatus::Timeout;
    if (status >= 500 || status == 0) return LicenseStatus::Unavailable;
    return LicenseStatus::Invalid;
}

std::string DiagnosticForHttp(int status) {
    if (status == 401) return "website.session_unauthorized";
    if (status == 403) return "website.session_forbidden";
    if (status == 429) return "website.rate_limited";
    if (status >= 500 || status == 0) return "website.unavailable";
    return "website.rejected";
}

bool ParseLicenses(const OmniGhost::Update::Json::Value& root,
                   std::vector<LauncherLicense>& output) {
    const OmniGhost::Update::Json::Array* values = nullptr;
    if (const auto* object = OmniGhost::Update::Json::AsObject(root)) {
        const auto* field = JsonField(*object, "licenses");
        values = field ? OmniGhost::Update::Json::AsArray(*field) : nullptr;
    } else {
        values = OmniGhost::Update::Json::AsArray(root);
    }
    if (!values) return false;
    if (values->size() > 64) return false;
    std::vector<LauncherLicense> parsed;
    parsed.reserve(values->size());
    for (const auto& value : *values) {
        const auto* object = OmniGhost::Update::Json::AsObject(value);
        if (!object) return false;
        LauncherLicense license;
        if (!Text(*object, "id", license.id) || !Text(*object, "product", license.product) ||
            !Text(*object, "plan", license.plan, false) ||
            !Text(*object, "keyauth_state", license.keyauthState, false) ||
            !Text(*object, "expires_at", license.expiresAt, false) ||
            !Text(*object, "real_expires_at", license.realExpiresAt, false) ||
            license.id.empty() || license.id.size() > 128 || license.product.size() > 64)
            return false;
        parsed.push_back(std::move(license));
    }
    output = std::move(parsed);
    return true;
}

class WinHttpLauncherTransport final : public ILauncherApiTransport {
public:
    LauncherApiResponse Request(std::string_view method, std::string_view path,
                                std::string_view body, std::string_view bearerToken,
                                std::string_view deviceId) override {
        LauncherApiResponse result;
        if (path.empty() || path.front() != '/' || path.find("..") != std::string_view::npos ||
            bearerToken.size() > kMaximumTokenBytes || body.size() > 64u * 1024u)
            return {0, {}, "Pedido inválido."};

        auto session = OmniGhost::Platform::MakeWinHttpHandle(WinHttpOpen(
            L"OmniGhost-Launcher/2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session) return {0, {}, "Não foi possível iniciar a ligação segura."};
        WinHttpSetTimeouts(session.get(), 10000, 10000, 15000, 15000);

        auto connection = OmniGhost::Platform::MakeWinHttpHandle(
            WinHttpConnect(session.get(), kApiHost, INTERNET_DEFAULT_HTTPS_PORT, 0));
        if (!connection) return {0, {}, "Não foi possível contactar omnighost.store."};

        const std::wstring wideMethod = WidenAscii(method);
        const std::wstring widePath = WidenAscii(path);
        auto request = OmniGhost::Platform::MakeWinHttpHandle(WinHttpOpenRequest(
            connection.get(), wideMethod.c_str(), widePath.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        if (!request) return {0, {}, "Não foi possível preparar o pedido seguro."};

        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
        protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
        WinHttpSetOption(request.get(), WINHTTP_OPTION_SECURE_PROTOCOLS,
                         &protocols, sizeof(protocols));

        std::wstring headers = L"Accept: application/json\r\nContent-Type: application/json; charset=utf-8\r\n";
        if (!bearerToken.empty()) {
            headers += L"Authorization: Bearer ";
            headers += WidenAscii(bearerToken);
            headers += L"\r\n";
        }
        if (!deviceId.empty()) {
            headers += L"X-Launcher-Device-ID: ";
            headers += WidenAscii(deviceId);
            headers += L"\r\n";
        }
        auto wipeHeaders = OmniGhost::Platform::MakeScopeExit([&] {
            if (!headers.empty()) SecureZeroMemory(headers.data(), headers.size() * sizeof(wchar_t));
        });

        void* bodyPointer = body.empty() ? WINHTTP_NO_REQUEST_DATA
                                         : const_cast<char*>(body.data());
        if (!WinHttpSendRequest(request.get(), headers.c_str(),
                                static_cast<DWORD>(headers.size()), bodyPointer,
                                static_cast<DWORD>(body.size()),
                                static_cast<DWORD>(body.size()), 0) ||
            !WinHttpReceiveResponse(request.get(), nullptr))
            return {0, {}, "O serviço de contas está temporariamente indisponível."};

        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        if (!WinHttpQueryHeaders(request.get(),
                                 WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                                 WINHTTP_NO_HEADER_INDEX))
            return {0, {}, "Resposta HTTP inválida."};
        result.statusCode = static_cast<int>(status);

        while (true) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request.get(), &available)) {
                result.error = "Não foi possível ler a resposta do servidor.";
                return result;
            }
            if (available == 0) break;
            if (available > kMaximumResponseBytes ||
                result.body.size() > kMaximumResponseBytes - available) {
                result.body.clear();
                result.error = "A resposta do servidor excedeu o limite permitido.";
                return result;
            }
            const std::size_t previous = result.body.size();
            result.body.resize(previous + available);
            DWORD read = 0;
            if (!WinHttpReadData(request.get(), result.body.data() + previous,
                                 available, &read)) {
                result.body.clear();
                result.error = "Não foi possível ler a resposta do servidor.";
                return result;
            }
            result.body.resize(previous + read);
            if (read == 0) break;
        }
        return result;
    }
};

class WindowsCredentialSessionStore final : public ILauncherSessionStore {
public:
    bool Save(const StoredLauncherSession& session) override {
        if (session.token.empty() || session.token.size() > kMaximumTokenBytes ||
            session.email.size() > 320 || session.expiresAt.size() > 96 ||
            session.selectedLicenseId.size() > 128)
            return false;
        std::string payload = "OGLS1\n" + session.expiresAt + "\n" +
            session.selectedLicenseId + "\n" + session.email + "\n" + session.token;
        if (payload.size() > kMaximumCredentialBytes) return false;
        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = const_cast<LPWSTR>(kCredentialTarget);
        credential.CredentialBlobSize = static_cast<DWORD>(payload.size());
        credential.CredentialBlob = reinterpret_cast<LPBYTE>(payload.data());
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        credential.UserName = const_cast<LPWSTR>(L"OmniGhost Website Account");
        const bool written = CredWriteW(&credential, 0) != FALSE;
        SecureZeroMemory(payload.data(), payload.size());
        return written;
    }

    bool Load(StoredLauncherSession& session) override {
        session = {};
        PCREDENTIALW credential = nullptr;
        if (!CredReadW(kCredentialTarget, CRED_TYPE_GENERIC, 0, &credential) || !credential)
            return false;
        const DWORD size = credential->CredentialBlobSize;
        if (size == 0 || size > kMaximumCredentialBytes) {
            CredFree(credential);
            return false;
        }
        std::string payload(reinterpret_cast<const char*>(credential->CredentialBlob), size);
        CredFree(credential);
        std::array<std::string*, 4> fields{
            &session.expiresAt, &session.selectedLicenseId, &session.email, &session.token};
        std::size_t offset = 6;
        bool valid = payload.rfind("OGLS1\n", 0) == 0;
        for (std::size_t index = 0; valid && index < fields.size(); ++index) {
            if (index + 1 == fields.size()) {
                *fields[index] = payload.substr(offset);
            } else {
                const auto end = payload.find('\n', offset);
                if (end == std::string::npos) valid = false;
                else {
                    *fields[index] = payload.substr(offset, end - offset);
                    offset = end + 1;
                }
            }
        }
        valid = valid && !session.token.empty() && session.token.size() <= kMaximumTokenBytes;
        SecureZeroMemory(payload.data(), payload.size());
        if (!valid) session = {};
        return valid;
    }

    void Clear() noexcept override {
        CredDeleteW(kCredentialTarget, CRED_TYPE_GENERIC, 0);
    }
};

std::string WindowsVersion() {
    const auto info = OmniGhost::Platform::CaptureSystemInfo();
    std::ostringstream output;
    output << info.windowsMajor << '.' << info.windowsMinor << '.' << info.windowsBuild
           << " " << info.nativeArchitecture;
    return output.str();
}

std::string GetOrCreateInstallationDeviceId() {
    PCREDENTIALW credential = nullptr;
    if (CredReadW(kDeviceCredentialTarget, CRED_TYPE_GENERIC, 0, &credential) && credential) {
        const DWORD size = credential->CredentialBlobSize;
        std::string existing;
        if (size >= 21 && size <= 96)
            existing.assign(reinterpret_cast<const char*>(credential->CredentialBlob), size);
        CredFree(credential);
        if (existing.rfind("ogdi_", 0) == 0) return existing;
    }
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) return {};
    char value[64]{};
    const int length = sprintf_s(value, "ogdi_%08lx%04hx%04hx%02x%02x%02x%02x%02x%02x%02x%02x",
        guid.Data1, guid.Data2, guid.Data3,
        guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
        guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    if (length <= 0) return {};
    std::string created(value, static_cast<std::size_t>(length));
    CREDENTIALW stored{};
    stored.Type = CRED_TYPE_GENERIC;
    stored.TargetName = const_cast<LPWSTR>(kDeviceCredentialTarget);
    stored.CredentialBlobSize = static_cast<DWORD>(created.size());
    stored.CredentialBlob = reinterpret_cast<LPBYTE>(created.data());
    stored.Persist = CRED_PERSIST_LOCAL_MACHINE;
    stored.UserName = const_cast<LPWSTR>(L"OmniGhost Installation");
    if (!CredWriteW(&stored, 0)) return {};
    return created;
}

}

bool LauncherLicense::IsUsable() const noexcept {
    const std::string state = TrimLower(keyauthState);
    return state.empty() || state == "ready" || state == "active";
}

std::shared_ptr<ILauncherApiTransport> CreateLauncherApiTransport() {
    return std::make_shared<WinHttpLauncherTransport>();
}

std::shared_ptr<ILauncherSessionStore> CreateLauncherSessionStore() {
    return std::make_shared<WindowsCredentialSessionStore>();
}

LauncherAccountService::LauncherAccountService(
    std::shared_ptr<ILauncherApiTransport> transport,
    std::shared_ptr<ILauncherSessionStore> store)
    : transport_(transport ? std::move(transport) : CreateLauncherApiTransport()),
      store_(store ? std::move(store) : CreateLauncherSessionStore()),
      deviceId_(GetOrCreateInstallationDeviceId()) {}

LicenseResult LauncherAccountService::Login(std::string_view emailInput,
                                            std::string_view password) {
    std::scoped_lock operation(operationMutex_);
    const std::string email = TrimLower(emailInput);
    if (email.size() < 5 || email.size() > 320 || email.find('@') == std::string::npos ||
        password.empty() || password.size() > 1024)
        return {LicenseStatus::Invalid, "Introduz o email e a palavra-passe da tua conta OmniGhost.",
                "website.invalid_input", {}};
    std::string body = "{\"email\":\"" + JsonEscape(email) +
        "\",\"password\":\"" + JsonEscape(password) +
        "\",\"deviceId\":\"" + JsonEscape(deviceId_) +
        "\",\"launcherVersion\":\"" + JsonEscape(OmniGhost::Version) +
        "\",\"osVersion\":\"" + JsonEscape(WindowsVersion()) + "\"}";
    auto wipeBody = OmniGhost::Platform::MakeScopeExit([&] {
        if (!body.empty()) SecureZeroMemory(body.data(), body.size());
    });
    return ApplySessionResponse(transport_->Request(
        "POST", "/api/launcher/session", body, {}, deviceId_), true, false);
}

LicenseResult LauncherAccountService::Register(std::string_view emailInput,
                                               std::string_view password,
                                               std::string_view licenseKey) {
    std::scoped_lock operation(operationMutex_);
    const std::string email = TrimLower(emailInput);
    if (email.size() < 5 || email.size() > 320 || email.find('@') == std::string::npos ||
        password.size() < 8 || password.size() > 1024 || licenseKey.empty() ||
        licenseKey.size() > 512)
        return {LicenseStatus::Invalid,
                "Indica o email, a palavra-passe da conta do site e a licença comprada nessa conta.",
                "website.invalid_registration", {}};
    std::string body = "{\"email\":\"" + JsonEscape(email) +
        "\",\"password\":\"" + JsonEscape(password) +
        "\",\"licenseKey\":\"" + JsonEscape(licenseKey) +
        "\",\"deviceId\":\"" + JsonEscape(deviceId_) +
        "\",\"launcherVersion\":\"" + JsonEscape(OmniGhost::Version) +
        "\",\"osVersion\":\"" + JsonEscape(WindowsVersion()) + "\"}";
    auto wipeBody = OmniGhost::Platform::MakeScopeExit([&] {
        if (!body.empty()) SecureZeroMemory(body.data(), body.size());
    });
    return ApplySessionResponse(transport_->Request(
        "POST", "/api/launcher/register", body, {}, deviceId_), true, false);
}

LicenseResult LauncherAccountService::Restore() {
    std::scoped_lock operation(operationMutex_);
    StoredLauncherSession stored;
    if (!store_->Load(stored))
        return {LicenseStatus::Invalid, "Não existe uma sessão guardada.",
                "website.no_saved_session", {}};
    {
        std::lock_guard lock(mutex_);
        token_ = std::move(stored.token);
        state_.email = std::move(stored.email);
        expiresAtText_ = std::move(stored.expiresAt);
        state_.expiresAt = ParseIso8601(expiresAtText_);
        state_.selectedLicenseId = std::move(stored.selectedLicenseId);
    }
    LicenseResult result = RefreshLicensesUnlocked();
    if (!result.Ok() && result.status == LicenseStatus::Invalid) {
        std::lock_guard lock(mutex_);
        ClearLocked();
    }
    return result;
}

LicenseResult LauncherAccountService::Renew() {
    std::scoped_lock operation(operationMutex_);
    std::string token;
    {
        std::lock_guard lock(mutex_);
        token = token_;
    }
    if (token.empty())
        return {LicenseStatus::Invalid, "A sessão terminou. Inicia sessão novamente.",
                "website.no_session", {}};
    const std::string body = "{\"deviceId\":\"" + JsonEscape(deviceId_) + "\"}";
    auto result = ApplySessionResponse(transport_->Request(
        "PUT", "/api/launcher/session", body, token, deviceId_), true, true);
    SecureZeroMemory(token.data(), token.size());
    return result;
}

LicenseResult LauncherAccountService::RefreshLicenses() {
    std::scoped_lock operation(operationMutex_);
    return RefreshLicensesUnlocked();
}

LicenseResult LauncherAccountService::RefreshLicensesUnlocked() {
    std::string token;
    {
        std::lock_guard lock(mutex_);
        token = token_;
    }
    if (token.empty())
        return {LicenseStatus::Invalid, "A sessão terminou. Inicia sessão novamente.",
                "website.no_session", {}};
    auto result = ApplyLicenseResponse(transport_->Request(
        "GET", "/api/launcher/licenses", {}, token, deviceId_));
    SecureZeroMemory(token.data(), token.size());
    return result;
}

void LauncherAccountService::Logout() noexcept {
    std::scoped_lock operation(operationMutex_);
    std::string token;
    {
        std::lock_guard lock(mutex_);
        token = token_;
        ClearLocked();
    }
    if (!token.empty()) {
        try {
            (void)transport_->Request("DELETE", "/api/launcher/session", {}, token, deviceId_);
        } catch (...) {
            OutputDebugStringA("[OmniGhost][Auth] Remote session revocation failed.\n");
        }
        SecureZeroMemory(token.data(), token.size());
    }
}

bool LauncherAccountService::IsAuthenticated() const noexcept {
    std::lock_guard lock(mutex_);
    if (!state_.authenticated)
        return false;
    // AU-003: enforce local da expiração quando é conhecida — o epoch
    // significa desconhecida (o NeedsRenewal trata esse caso e o servidor
    // continua a ser o gate real).
    if (state_.expiresAt == std::chrono::system_clock::time_point{})
        return true;
    return std::chrono::system_clock::now() < state_.expiresAt;
}

bool LauncherAccountService::NeedsRenewal(
    std::chrono::system_clock::time_point now) const noexcept {
    std::lock_guard lock(mutex_);
    return state_.authenticated &&
        (state_.expiresAt == std::chrono::system_clock::time_point{} ||
         now + kRenewalMargin >= state_.expiresAt);
}

LauncherSessionState LauncherAccountService::Snapshot() const {
    std::lock_guard lock(mutex_);
    return state_;
}

bool LauncherAccountService::SelectLicense(std::string_view licenseId) {
    std::lock_guard lock(mutex_);
    const auto found = std::find_if(state_.licenses.begin(), state_.licenses.end(),
        [licenseId](const LauncherLicense& license) {
            return license.id == licenseId && license.IsUsable();
        });
    if (found == state_.licenses.end()) return false;
    state_.selectedLicenseId = found->id;
    return PersistLocked();
}

bool LauncherAccountService::HasProduct(std::string_view productInput) const {
    const std::string product = TrimLower(productInput);
    std::lock_guard lock(mutex_);
    return state_.authenticated && (state_.isAdmin ||
        std::any_of(state_.licenses.begin(), state_.licenses.end(),
        [&product](const LauncherLicense& license) {
            const std::string current = TrimLower(license.product);
            return license.IsUsable() &&
                (current == product || current == "all" || current == "admin");
        }));
}

std::string LauncherAccountService::ProductDuration(std::string_view productInput) const {
    const std::string product = TrimLower(productInput);
    std::lock_guard lock(mutex_);
    if (state_.authenticated && state_.isAdmin) return "Administrador";
    for (const auto& license : state_.licenses) {
        const std::string current = TrimLower(license.product);
        if (license.IsUsable() &&
            (current == product || current == "all" || current == "admin")) {
            if (TrimLower(license.plan) == "lifetime" || license.expiresAt.empty()) return "Vitalício";
            return license.expiresAt;
        }
    }
    return {};
}

LauncherApiResponse LauncherAccountService::AuthenticatedRequest(
    std::string_view method, std::string_view path, std::string_view body) {
    std::string token;
    {
        std::lock_guard lock(mutex_);
        if (!state_.authenticated || token_.empty()) return {401, {}, "Sessão indisponível."};
        token = token_;
    }
    auto response = transport_->Request(method, path, body, token, deviceId_);
    SecureZeroMemory(token.data(), token.size());
    return response;
}

LauncherApiResponse LauncherAccountService::BearerRequest(
    std::string_view method, std::string_view path, std::string_view body,
    std::string_view bearerToken) {
    if (bearerToken.empty()) return {401, {}, "Token indisponível."};
    return transport_->Request(method, path, body, bearerToken, deviceId_);
}

LicenseResult LauncherAccountService::ApplySessionResponse(
    LauncherApiResponse response, bool requireToken, bool retainLicenses) {
    auto wipeResponse = OmniGhost::Platform::MakeScopeExit([&] {
        if (!response.body.empty()) SecureZeroMemory(response.body.data(), response.body.size());
    });
    if (response.statusCode < 200 || response.statusCode >= 300 || !response.error.empty()) {
        if (response.statusCode == 401) {
            std::lock_guard lock(mutex_);
            ClearLocked();
        }
        return {StatusForHttp(response.statusCode),
                ErrorText(response, response.error.empty()
                    ? "O servidor recusou o início de sessão."
                    : response.error),
                DiagnosticForHttp(response.statusCode), {}};
    }

    OmniGhost::Update::Json::Value root;
    OmniGhost::Update::Json::Limits limits{24, 64u * 1024u, 5000};
    const auto parsed = OmniGhost::Update::Json::Parse(response.body, root, limits);
    const auto* object = parsed.ok ? OmniGhost::Update::Json::AsObject(root) : nullptr;
    if (!object)
        return {LicenseStatus::Invalid, "O servidor devolveu uma resposta inválida.",
                "website.invalid_response", {}};
    if (const auto* okValue = JsonField(*object, "ok")) {
        const bool* ok = OmniGhost::Update::Json::AsBool(*okValue);
        if (!ok || !*ok)
            return {LicenseStatus::Invalid, ErrorText(response, "O início de sessão foi recusado."),
                    "website.rejected", {}};
    }

    std::string token;
    std::string expiry;
    Text(*object, "sessionToken", token, false);
    Text(*object, "sessionExpiresAt", expiry, false);
    if ((requireToken && token.empty()) || token.size() > kMaximumTokenBytes)
        return {LicenseStatus::Invalid, "O servidor não forneceu uma sessão válida.",
                "website.missing_token", {}};

    std::string email;
    bool isAdmin = false;
    bool hasAdminClaim = false;
    if (const auto* accountValue = JsonField(*object, "account")) {
        if (const auto* account = OmniGhost::Update::Json::AsObject(*accountValue)) {
            Text(*account, "email", email, false);
            if (const auto* adminValue = JsonField(*account, "isAdmin")) {
                if (const bool* admin = OmniGhost::Update::Json::AsBool(*adminValue)) {
                    isAdmin = *admin;
                    hasAdminClaim = true;
                }
            }
        }
    }

    std::vector<LauncherLicense> licenses;
    const bool hasLicenses = JsonField(*object, "licenses") != nullptr;
    if (hasLicenses && !ParseLicenses(root, licenses))
        return {LicenseStatus::Invalid, "A lista de licenças recebida é inválida.",
                "website.invalid_licenses", {}};

    std::chrono::system_clock::time_point appliedExpiry;
    {
        std::lock_guard lock(mutex_);
        if (!token.empty()) {
            if (!token_.empty()) SecureZeroMemory(token_.data(), token_.size());
            token_ = std::move(token);
        }
        if (!email.empty()) state_.email = std::move(email);
        if (hasAdminClaim) state_.isAdmin = isAdmin;
        if (!expiry.empty()) {
            expiresAtText_ = std::move(expiry);
            state_.expiresAt = ParseIso8601(expiresAtText_);
        }
        if (hasLicenses) state_.licenses = std::move(licenses);
        else if (!retainLicenses) state_.licenses.clear();

        const auto usable = std::count_if(state_.licenses.begin(), state_.licenses.end(),
            [](const LauncherLicense& license) { return license.IsUsable(); });
        if (usable == 0 && !state_.isAdmin) {
            ClearLocked();
            return {LicenseStatus::Invalid, "A conta não possui uma licença ativa.",
                    "website.no_active_license", {}};
        }
        if (usable == 1) {
            const auto found = std::find_if(state_.licenses.begin(), state_.licenses.end(),
                [](const LauncherLicense& license) { return license.IsUsable(); });
            state_.selectedLicenseId = found->id;
        } else {
            const auto selected = std::find_if(state_.licenses.begin(), state_.licenses.end(),
                [this](const LauncherLicense& license) {
                    return license.id == state_.selectedLicenseId && license.IsUsable();
                });
            if (selected == state_.licenses.end()) {

                const auto fallback = std::find_if(state_.licenses.begin(), state_.licenses.end(),
                    [](const LauncherLicense& license) { return license.IsUsable(); });
                state_.selectedLicenseId = fallback != state_.licenses.end() ? fallback->id : std::string{};
            }
        }
        state_.authenticated = true;
        if (!PersistLocked()) {
            ClearLocked();
            return {LicenseStatus::Unavailable,
                    "A sessão foi aceite, mas não foi possível guardá-la com segurança.",
                    "website.credential_store_failed", {}};
        }
        appliedExpiry = state_.expiresAt;
    }
    return {LicenseStatus::Valid, "Sessão OmniGhost iniciada.", "website.ok", appliedExpiry};
}

LicenseResult LauncherAccountService::ApplyLicenseResponse(const LauncherApiResponse& response) {
    if (response.statusCode < 200 || response.statusCode >= 300 || !response.error.empty()) {
        if (response.statusCode == 401) {
            std::lock_guard lock(mutex_);
            ClearLocked();
        }
        return {StatusForHttp(response.statusCode),
                ErrorText(response, response.error.empty()
                    ? "Não foi possível validar as licenças da conta."
                    : response.error),
                DiagnosticForHttp(response.statusCode), {}};
    }
    OmniGhost::Update::Json::Value root;
    OmniGhost::Update::Json::Limits limits{24, 64u * 1024u, 5000};
    if (!OmniGhost::Update::Json::Parse(response.body, root, limits).ok)
        return {LicenseStatus::Invalid, "O servidor devolveu uma resposta inválida.",
                "website.invalid_response", {}};
    std::vector<LauncherLicense> licenses;
    if (!ParseLicenses(root, licenses))
        return {LicenseStatus::Invalid, "A lista de licenças recebida é inválida.",
                "website.invalid_licenses", {}};
    bool isAdmin = false;
    bool hasAdminClaim = false;
    if (const auto* rootObject = OmniGhost::Update::Json::AsObject(root)) {
        if (const auto* accountValue = JsonField(*rootObject, "account")) {
            if (const auto* account = OmniGhost::Update::Json::AsObject(*accountValue)) {
                if (const auto* adminValue = JsonField(*account, "isAdmin")) {
                    if (const bool* admin = OmniGhost::Update::Json::AsBool(*adminValue)) {
                        isAdmin = *admin;
                        hasAdminClaim = true;
                    }
                }
            }
        }
    }
    const auto usable = std::count_if(licenses.begin(), licenses.end(),
        [](const LauncherLicense& license) { return license.IsUsable(); });
    if (usable == 0 && !(hasAdminClaim && isAdmin)) {
        std::lock_guard lock(mutex_);
        ClearLocked();
        return {LicenseStatus::Invalid, "A conta não possui uma licença ativa.",
                "website.no_active_license", {}};
    }
    std::chrono::system_clock::time_point appliedExpiry;
    {
        std::lock_guard lock(mutex_);
        state_.licenses = std::move(licenses);
        state_.isAdmin = hasAdminClaim && isAdmin;
        if (usable == 1) {
            const auto found = std::find_if(state_.licenses.begin(), state_.licenses.end(),
                [](const LauncherLicense& license) { return license.IsUsable(); });
            state_.selectedLicenseId = found->id;
        } else {
            const auto selected = std::find_if(state_.licenses.begin(), state_.licenses.end(),
                [this](const LauncherLicense& license) {
                    return license.id == state_.selectedLicenseId && license.IsUsable();
                });
            if (selected == state_.licenses.end()) {

                const auto fallback = std::find_if(state_.licenses.begin(), state_.licenses.end(),
                    [](const LauncherLicense& license) { return license.IsUsable(); });
                state_.selectedLicenseId = fallback != state_.licenses.end() ? fallback->id : std::string{};
            }
        }
        state_.authenticated = true;
        if (!PersistLocked())
            return {LicenseStatus::Unavailable, "Não foi possível atualizar a sessão guardada.",
                    "website.credential_store_failed", {}};
        appliedExpiry = state_.expiresAt;
    }
    return {LicenseStatus::Valid, "Licenças atualizadas.", "website.licenses_ok", appliedExpiry};
}

bool LauncherAccountService::PersistLocked() const {
    if (token_.empty()) return false;
    return store_->Save({token_, state_.email, expiresAtText_, state_.selectedLicenseId});
}

void LauncherAccountService::ClearLocked() noexcept {
    if (!token_.empty()) SecureZeroMemory(token_.data(), token_.size());
    token_.clear();
    expiresAtText_.clear();
    state_ = {};
    store_->Clear();
}

}
