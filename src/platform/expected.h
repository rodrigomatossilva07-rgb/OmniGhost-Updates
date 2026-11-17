#pragma once

#include <tl/expected.hpp>
#include <string>
#include <string_view>
#include <system_error>
#include <exception>
#include <utility>

namespace OmniGhost {

// =============================================================================
// Standardized Error Handling with tl::expected
// =============================================================================
// Replaces bool + out parameter pattern with proper error propagation

// Common error type
struct Error {
    std::string message;
    std::string_view code;      // Machine-readable error code (e.g., "OFFSETS_MISSING")
    std::error_code sysError;   // Optional system error code

    Error() = default;
    Error(std::string_view msg, std::string_view errCode = "UNKNOWN")
        : message(msg), code(errCode) {}
    Error(std::string_view msg, const std::error_code& ec, std::string_view errCode = "SYS_ERROR")
        : message(msg), code(errCode), sysError(ec) {}

    [[nodiscard]] bool empty() const noexcept { return message.empty(); }
    [[nodiscard]] const char* what() const noexcept { return message.c_str(); }
};

// Expected type aliases
template <typename T>
using Expected = tl::expected<T, Error>;

// Void expected (for operations that don't return a value)
using VoidExpected = tl::expected<void, Error>;

// =============================================================================
// Error construction helpers
// =============================================================================

[[nodiscard]] inline Error MakeError(std::string_view message, std::string_view code = "ERROR") {
    return Error(std::string(message), code);
}

[[nodiscard]] inline Error MakeError(const std::error_code& ec, std::string_view message, std::string_view code = "SYS_ERROR") {
    return Error(std::string(message), ec, code);
}

[[nodiscard]] inline Error MakeSystemError(std::string_view message, std::string_view code = "SYS_ERROR") {
    return MakeError(std::error_code(errno, std::generic_category()), message, code);
}

[[nodiscard]] inline Error MakeWin32Error(std::string_view message, DWORD win32Error, std::string_view code = "WIN32_ERROR") {
    return MakeError(std::error_code(win32Error, std::system_category()), std::string(message), code);
}

// =============================================================================
// Expected helpers
// =============================================================================

// Create successful expected
template <typename T>
[[nodiscard]] inline Expected<T> Success(T&& value) {
    return Expected<T>(std::forward<T>(value));
}

[[nodiscard]] inline VoidExpected Success() {
    return VoidExpected();
}

// Create failed expected
template <typename T>
[[nodiscard]] inline Expected<T> Failure(const Error& error) {
    return tl::make_unexpected(error);
}

template <typename T>
[[nodiscard]] inline Expected<T> Failure(Error&& error) {
    return tl::make_unexpected(std::move(error));
}

template <typename T>
[[nodiscard]] inline Expected<T> Failure(std::string_view message, std::string_view code = "ERROR") {
    return tl::make_unexpected(MakeError(message, code));
}

[[nodiscard]] inline VoidExpected Failure(const Error& error) {
    return tl::make_unexpected(error);
}

[[nodiscard]] inline VoidExpected Failure(Error&& error) {
    return tl::make_unexpected(std::move(error));
}

[[nodiscard]] inline VoidExpected Failure(std::string_view message, std::string_view code = "ERROR") {
    return tl::make_unexpected(MakeError(message, code));
}

// =============================================================================
// Monadic operations (C++23 style, available in tl::expected)
// =============================================================================

// Transform value on success
template <typename T, typename F>
[[nodiscard]] inline auto Map(Expected<T>&& exp, F&& func)
    -> Expected<std::invoke_result_t<F, T>> {
    return std::move(exp).map(std::forward<F>(func));
}

template <typename T, typename F>
[[nodiscard]] inline auto Map(const Expected<T>& exp, F&& func)
    -> Expected<std::invoke_result_t<F, T>> {
    return exp.map(std::forward<F>(func));
}

// Transform error on failure
template <typename T, typename F>
[[nodiscard]] inline auto MapError(Expected<T>&& exp, F&& func)
    -> Expected<T> {
    return std::move(exp).map_error(std::forward<F>(func));
}

template <typename T, typename F>
[[nodiscard]] inline auto MapError(const Expected<T>& exp, F&& func)
    -> Expected<T> {
    return exp.map_error(std::forward<F>(func));
}

// Chain operations (and_then)
template <typename T, typename F>
[[nodiscard]] inline auto AndThen(Expected<T>&& exp, F&& func)
    -> std::invoke_result_t<F, T> {
    return std::move(exp).and_then(std::forward<F>(func));
}

template <typename T, typename F>
[[nodiscard]] inline auto AndThen(const Expected<T>& exp, F&& func)
    -> std::invoke_result_t<F, T> {
    return exp.and_then(std::forward<F>(func));
}

// Or else (fallback on error)
template <typename T, typename F>
[[nodiscard]] inline auto OrElse(Expected<T>&& exp, F&& func)
    -> Expected<T> {
    return std::move(exp).or_else(std::forward<F>(func));
}

template <typename T, typename F>
[[nodiscard]] inline auto OrElse(const Expected<T>& exp, F&& func)
    -> Expected<T> {
    return exp.or_else(std::forward<F>(func));
}

// =============================================================================
// Common error codes
// =============================================================================
namespace ErrorCode {
    constexpr std::string_view InvalidArgument = "INVALID_ARGUMENT";
    constexpr std::string_view NotFound = "NOT_FOUND";
    constexpr std::string_view AlreadyExists = "ALREADY_EXISTS";
    constexpr std::string_view PermissionDenied = "PERMISSION_DENIED";
    constexpr std::string_view IOError = "IO_ERROR";
    constexpr std::string_view ParseError = "PARSE_ERROR";
    constexpr std::string_view ValidationError = "VALIDATION_ERROR";
    constexpr std::string_view OffsetsMissing = "OFFSETS_MISSING";
    constexpr std::string_view OffsetsOutdated = "OFFSETS_OUTDATED";
    constexpr std::string_view DmaError = "DMA_ERROR";
    constexpr std::string_view DmaDeviceNotFound = "DMA_DEVICE_NOT_FOUND";
    constexpr std::string_view DmaProcessNotFound = "DMA_PROCESS_NOT_FOUND";
    constexpr std::string_view MemoryReadError = "MEMORY_READ_ERROR";
    constexpr std::string_view ModuleNotFound = "MODULE_NOT_FOUND";
    constexpr std::string_view SecurityError = "SECURITY_ERROR";
    constexpr std::string_view LicenseError = "LICENSE_ERROR";
    constexpr std::string_view NetworkError = "NETWORK_ERROR";
    constexpr std::string_view Timeout = "TIMEOUT";
    constexpr std::string_view Cancelled = "CANCELLED";
    constexpr std::string_view InternalError = "INTERNAL_ERROR";
    constexpr std::string_view UnsupportedOperation = "UNSUPPORTED_OPERATION";
} // namespace ErrorCode

// =============================================================================
// Result inspection helpers
// =============================================================================

template <typename T>
[[nodiscard]] inline bool IsSuccess(const Expected<T>& exp) noexcept {
    return exp.has_value();
}

template <typename T>
[[nodiscard]] inline bool IsFailure(const Expected<T>& exp) noexcept {
    return !exp.has_value();
}

template <typename T>
[[nodiscard]] inline const Error& GetError(const Expected<T>& exp) {
    return exp.error();
}

template <typename T>
[[nodiscard]] inline T& GetValue(Expected<T>& exp) {
    return exp.value();
}

template <typename T>
[[nodiscard]] inline const T& GetValue(const Expected<T>& exp) {
    return exp.value();
}

template <typename T>
[[nodiscard]] inline T& AssumeValue(Expected<T>& exp) noexcept {
    return *exp;
}

template <typename T>
[[nodiscard]] inline const T& AssumeValue(const Expected<T>& exp) noexcept {
    return *exp;
}

// =============================================================================
// Logging helpers
// =============================================================================

template <typename T>
void LogIfError(const Expected<T>& exp, std::string_view context = "") {
    if (!exp) {
        const auto& err = exp.error();
        std::cerr << "[ERROR] " << context;
        if (!context.empty()) std::cerr << ": ";
        std::cerr << err.code << " - " << err.message << "\n";
        if (err.sysError) {
            std::cerr << "  System error: " << err.sysError.message() << " (" << err.sysError.value() << ")\n";
        }
    }
}

template <typename T>
void LogAndReturnIfError(const Expected<T>& exp, std::string_view context = "") {
    LogIfError(exp, context);
}

} // namespace OmniGhost

// =============================================================================
// Macros for common patterns
// =============================================================================

// TRY_EXPECTED: Propagate error from expected expression
#define OGS_TRY(expr) \
    do { \
        auto&& _ogs_result = (expr); \
        if (!_ogs_result) { \
            return OmniGhost::Failure<decltype(_ogs_result.value())>(std::move(_ogs_result.error())); \
        } \
    } while (0)

// TRY_VOID: Propagate error from void expected
#define OGS_TRY_VOID(expr) \
    do { \
        auto&& _ogs_result = (expr); \
        if (!_ogs_result) { \
            return OmniGhost::Failure(std::move(_ogs_result.error())); \
        } \
    } while (0)

// EXPECTED_VAL: Extract value or return error
#define OGS_EXPECT_VAL(exp, ...) \
    ([] (auto&& e) -> decltype(auto) { \
        if (!e) { \
            return OmniGhost::Failure<decltype(e.value())>(OmniGhost::Error(__VA_ARGS__)); \
        } \
        return std::forward<decltype(e)>(e); \
    }(exp))