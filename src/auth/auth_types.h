#pragma once

#include <string>
#include <string_view>








namespace OmniGhost::Auth {

struct AuthUser {
    std::string email;
};

enum class AuthStatus {
    Success,
    InvalidCredentials,
    AccountAlreadyExists,
    AccountNotFound,
    StorageError,
    InvalidInput
};

struct AuthResult {
    AuthStatus status{};
    std::string message;

    [[nodiscard]] bool Ok() const noexcept { return status == AuthStatus::Success; }
};



class IAuthService {
public:
    virtual ~IAuthService() = default;

    virtual AuthResult Login(std::string_view email, std::string_view password) = 0;
    virtual AuthResult Register(std::string_view email, std::string_view password) = 0;
    virtual void Logout() = 0;

    virtual bool IsAuthenticated() const = 0;
    virtual const AuthUser& CurrentUser() const = 0;
};

}
