#pragma once

#include <string>
#include <string_view>

// Phase 1 local auth types.
// Designed so a future KeyAuthService can implement the same surface
// without rewriting auth_page / launcher UI.
//
// Local auto-login is UX only.
// Remote authentication will replace this in Phase 2.

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

// Abstract service — LocalAuthService is the Phase 1 implementation.
// Phase 2: KeyAuthService implements the same interface.
class IAuthService {
public:
    virtual ~IAuthService() = default;

    virtual AuthResult Login(std::string_view email, std::string_view password) = 0;
    virtual AuthResult Register(std::string_view email, std::string_view password) = 0;
    virtual void Logout() = 0;

    virtual bool IsAuthenticated() const = 0;
    virtual const AuthUser& CurrentUser() const = 0;
};

} // namespace OmniGhost::Auth
