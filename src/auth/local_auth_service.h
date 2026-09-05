#pragma once

#include <string>
#include <string_view>
#include <filesystem>

namespace OmniGhost::Auth {

enum class ResultCode {
    Success,
    InvalidInput,
    AccountNotFound,
    AccountAlreadyExists,
    InvalidCredentials,
    StorageError
};

struct Result {
    ResultCode code = ResultCode::StorageError;
    std::string message;
    [[nodiscard]] bool Ok() const noexcept { return code == ResultCode::Success; }
};

class LocalAuthService {
public:
    static LocalAuthService& Instance();

    void Initialize();
    [[nodiscard]] bool HasAccount() const;
    [[nodiscard]] bool IsAuthenticated() const;
    [[nodiscard]] bool RememberMe() const;
    [[nodiscard]] const std::string& CurrentEmail() const;

    [[nodiscard]] Result Register(std::string_view email, std::string_view password, bool rememberMe);
    [[nodiscard]] Result Login(std::string_view email, std::string_view password, bool rememberMe);
    [[nodiscard]] bool TryAutoLogin();
    void Logout(bool forgetRememberMe = false);

    [[nodiscard]] static std::string NormalizeEmail(std::string_view email);
    [[nodiscard]] static bool IsEmailValid(std::string_view email);
#if defined(OMNIGHOST_TESTING)
    void SetStoragePathForTesting(std::filesystem::path path);
#endif

private:
    LocalAuthService() = default;

    struct StoredAccount;
    [[nodiscard]] bool Load(StoredAccount& account) const;
    [[nodiscard]] bool Save(const StoredAccount& account) const;
    [[nodiscard]] std::filesystem::path AccountPath() const;

    bool initialized_ = false;
    bool authenticated_ = false;
    bool hasAccount_ = false;
    bool rememberMe_ = false;
    std::string currentEmail_;
#if defined(OMNIGHOST_TESTING)
    std::filesystem::path storagePathForTesting_;
#endif
};

} // namespace OmniGhost::Auth
