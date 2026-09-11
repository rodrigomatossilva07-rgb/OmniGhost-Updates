#include "keyauth_remote_provider.h"

#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
#include "keyauth/x64/auth.hpp"
#include "keyauth/x64/skStr.h"
#endif

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include <Windows.h>

namespace OmniGhost::Auth {
namespace {

LicenseResult MakeResult(LicenseStatus status, std::string message, const char* code) {
    return {status, std::move(message), code, {}};
}

LicenseStatus StatusForMessage(std::string_view message) {
    std::string normalized(message);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    if (normalized.find("expired") != std::string::npos) return LicenseStatus::Expired;
    if (normalized.find("hwid") != std::string::npos) return LicenseStatus::HwidMismatch;
    if (normalized.find("banned") != std::string::npos || normalized.find("revoked") != std::string::npos)
        return LicenseStatus::Revoked;
    return LicenseStatus::Invalid;
}

#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
int __cdecl RunCommandWithoutWindow(const char* command) noexcept {
    if (!command)
        return 1;

    try {
        const int required = MultiByteToWideChar(CP_ACP, 0, command, -1, nullptr, 0);
        if (required <= 1)
            return -1;
        std::wstring payload(static_cast<std::size_t>(required), L'\0');
        MultiByteToWideChar(CP_ACP, 0, command, -1, payload.data(), required);
        payload.pop_back();

        // The bundled KeyAuth SDK prefixes one diagnostic command with
        // "start cmd /C", which explicitly creates a visible console. Execute
        // the same payload in our hidden command host instead.
        std::wstring lowered(payload);
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
            [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
        constexpr std::wstring_view startCmd = L"start cmd /c ";
        constexpr std::wstring_view startCmdExe = L"start cmd.exe /c ";
        if (lowered.starts_with(startCmd))
            payload.erase(0, startCmd.size());
        else if (lowered.starts_with(startCmdExe))
            payload.erase(0, startCmdExe.size());

        wchar_t shellBuffer[MAX_PATH]{};
        DWORD shellLength = GetEnvironmentVariableW(L"ComSpec", shellBuffer, MAX_PATH);
        std::wstring shell = shellLength > 0 && shellLength < MAX_PATH
            ? std::wstring(shellBuffer, shellLength)
            : std::wstring(L"C:\\Windows\\System32\\cmd.exe");
        std::wstring commandLine = L"\"" + shell + L"\" /D /S /C \"" + payload + L"\"";
        std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
        mutableCommand.push_back(L'\0');

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(shell.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                            nullptr, nullptr, &startup, &process))
            return -1;
        CloseHandle(process.hThread);
        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD exitCode = 1;
        GetExitCodeProcess(process.hProcess, &exitCode);
        CloseHandle(process.hProcess);
        return static_cast<int>(exitCode);
    } catch (...) {
        return -1;
    }
}

bool InstallHiddenSystemImport() noexcept {
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (!base)
        return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;
    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress)
        return false;

    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (; descriptor->Name; ++descriptor) {
        if (!descriptor->OriginalFirstThunk)
            continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
        auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addresses) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            const auto* import = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
                base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(import->Name), "system") != 0)
                continue;
            DWORD oldProtection = 0;
            if (!VirtualProtect(&addresses->u1.Function, sizeof(addresses->u1.Function),
                                PAGE_READWRITE, &oldProtection))
                return false;
            addresses->u1.Function = reinterpret_cast<ULONG_PTR>(&RunCommandWithoutWindow);
            DWORD ignored = 0;
            VirtualProtect(&addresses->u1.Function, sizeof(addresses->u1.Function),
                           oldProtection, &ignored);
            FlushInstructionCache(GetCurrentProcess(), &addresses->u1.Function,
                                  sizeof(addresses->u1.Function));
            return true;
        }
    }
    return false;
}

void EnsureKeyAuthCommandsAreHidden() noexcept {
    static std::once_flag installed;
    std::call_once(installed, [] { (void)InstallHiddenSystemImport(); });
}
#endif

} // namespace

struct KeyAuthRemoteLicenseProvider::Impl {
#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
    Impl()
        : api(skCrypt("OmniGhost").decrypt(),
              skCrypt("TMXuFRFSR1").decrypt(),
              skCrypt("1.0").decrypt(),
              skCrypt("https://keyauth.win/api/1.3/").decrypt(),
              skCrypt("").decrypt()) {}

    KeyAuth::api api;
#endif
};

KeyAuthRemoteLicenseProvider::KeyAuthRemoteLicenseProvider()
    : impl_(std::make_unique<Impl>()) {}

KeyAuthRemoteLicenseProvider::~KeyAuthRemoteLicenseProvider() = default;

bool KeyAuthRemoteLicenseProvider::IsConfigured() const noexcept {
#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
    return true;
#else
    return false;
#endif
}

LicenseResult KeyAuthRemoteLicenseProvider::EnsureInitialized(std::stop_token stopToken) {
    if (stopToken.stop_requested())
        return MakeResult(LicenseStatus::Cancelled, "Autenticação cancelada.", "keyauth.cancelled");
    if (!IsConfigured())
        return MakeResult(LicenseStatus::NotConfigured, "KeyAuth não está configurado nesta build.", "keyauth.not_configured");
    if (initialized_)
        return MakeResult(LicenseStatus::Valid, "KeyAuth pronto.", "keyauth.ready");

#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
    EnsureKeyAuthCommandsAreHidden();
    impl_->api.init();
    if (!impl_->api.response.success)
        return MakeResult(StatusForMessage(impl_->api.response.message),
            impl_->api.response.message.empty() ? "Não foi possível inicializar KeyAuth." : impl_->api.response.message,
            "keyauth.init_failed");
    initialized_ = true;
    return MakeResult(LicenseStatus::Valid, "KeyAuth pronto.", "keyauth.ready");
#else
    return MakeResult(LicenseStatus::NotConfigured, "KeyAuth não está configurado nesta build.", "keyauth.not_configured");
#endif
}

LicenseResult KeyAuthRemoteLicenseProvider::ResponseResult() const {
#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
    const std::string message = impl_->api.response.message.empty()
        ? (impl_->api.response.success ? "Sessão KeyAuth iniciada." : "KeyAuth recusou o pedido.")
        : impl_->api.response.message;
    return MakeResult(impl_->api.response.success ? LicenseStatus::Valid : StatusForMessage(message),
        message, impl_->api.response.success ? "keyauth.ok" : "keyauth.rejected");
#else
    return MakeResult(LicenseStatus::NotConfigured, "KeyAuth não está configurado nesta build.", "keyauth.not_configured");
#endif
}

LicenseResult KeyAuthRemoteLicenseProvider::Authenticate(std::string_view licenseKey,
                                                         std::string_view,
                                                         std::stop_token stopToken) {
    return Activate(licenseKey, stopToken);
}

LicenseResult KeyAuthRemoteLicenseProvider::Login(std::string_view username, std::string_view password,
                                                   std::stop_token stopToken) {
    std::scoped_lock lock(mutex_);
    LicenseResult ready = EnsureInitialized(stopToken);
    if (!ready.Ok()) return ready;
#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
    impl_->api.login(std::string(username), std::string(password));
    LicenseResult result = ResponseResult();
    if (result.Ok()) username_ = impl_->api.user_data.username.empty() ? std::string(username) : impl_->api.user_data.username;
    else username_.clear();
    return result;
#else
    return MakeResult(LicenseStatus::NotConfigured, "KeyAuth não está configurado nesta build.", "keyauth.not_configured");
#endif
}

LicenseResult KeyAuthRemoteLicenseProvider::Register(std::string_view username, std::string_view password,
                                                      std::string_view licenseKey, std::stop_token stopToken) {
    std::scoped_lock lock(mutex_);
    LicenseResult ready = EnsureInitialized(stopToken);
    if (!ready.Ok()) return ready;
#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
    // This is the account-creating KeyAuth call. It consumes/associates the
    // supplied license key in KeyAuth and makes the user visible in its panel.
    impl_->api.regstr(std::string(username), std::string(password), std::string(licenseKey));
    LicenseResult result = ResponseResult();
    if (result.Ok()) username_ = impl_->api.user_data.username.empty() ? std::string(username) : impl_->api.user_data.username;
    else username_.clear();
    return result;
#else
    return MakeResult(LicenseStatus::NotConfigured, "KeyAuth não está configurado nesta build.", "keyauth.not_configured");
#endif
}

LicenseResult KeyAuthRemoteLicenseProvider::Activate(std::string_view licenseKey, std::stop_token stopToken) {
    std::scoped_lock lock(mutex_);
    LicenseResult ready = EnsureInitialized(stopToken);
    if (!ready.Ok()) return ready;
#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
    impl_->api.license(std::string(licenseKey));
    LicenseResult result = ResponseResult();
    if (result.Ok()) username_ = impl_->api.user_data.username;
    else username_.clear();
    return result;
#else
    return MakeResult(LicenseStatus::NotConfigured, "KeyAuth não está configurado nesta build.", "keyauth.not_configured");
#endif
}

LicenseResult KeyAuthRemoteLicenseProvider::Upgrade(std::string_view username, std::string_view licenseKey,
                                                     std::stop_token stopToken) {
    std::scoped_lock lock(mutex_);
    LicenseResult ready = EnsureInitialized(stopToken);
    if (!ready.Ok()) return ready;
#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
    impl_->api.upgrade(std::string(username), std::string(licenseKey));
    LicenseResult result = ResponseResult();
    if (result.Ok()) username_ = std::string(username);
    else username_.clear();
    return result;
#else
    return MakeResult(LicenseStatus::NotConfigured, "KeyAuth não está configurado nesta build.", "keyauth.not_configured");
#endif
}

void KeyAuthRemoteLicenseProvider::Logout() noexcept {
    std::scoped_lock lock(mutex_);
    username_.clear();
}

std::string KeyAuthRemoteLicenseProvider::CurrentUsername() const {
    std::scoped_lock lock(mutex_);
    return username_;
}

std::vector<RemoteEntitlement> KeyAuthRemoteLicenseProvider::CurrentEntitlements() const {
    std::scoped_lock lock(mutex_);
    std::vector<RemoteEntitlement> result;
#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
    result.reserve(impl_->api.user_data.subscriptions.size());
    for (const auto& subscription : impl_->api.user_data.subscriptions) {
        if (!subscription.name.empty()) {
            result.push_back({subscription.name, subscription.expiry,
                              KeyAuth::api::expiry_remaining(subscription.expiry)});
        }
    }
#endif
    return result;
}

} // namespace OmniGhost::Auth
