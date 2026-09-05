#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <type_traits>
#include <stdexcept>

namespace OmniGhost::Platform {

// Error wrapper to distinguish from value type
struct Error {
    std::string message;

    constexpr Error() noexcept : message() {}
    explicit constexpr Error(const std::string& msg) noexcept : message(msg) {}
    explicit constexpr Error(std::string&& msg) noexcept : message(std::move(msg)) {}
    constexpr Error(const char* msg) noexcept : message(msg) {}

    constexpr bool empty() const noexcept { return message.empty(); }
    constexpr const std::string& str() const noexcept { return message; }
    constexpr operator std::string_view() const noexcept { return message; }
};

// Result<T> - typed result with success value or error
// Inspired by Rust's Result and C++23 std::expected
template <typename T>
class Result {
public:
    using Error = OmniGhost::Platform::Error;

    constexpr Result() noexcept : value_(Error{}) {}
    
    // Value constructors - only enabled when T is not Error type
    template <typename U = T, std::enable_if_t<!std::is_same_v<std::decay_t<U>, Error>, int> = 0>
    constexpr Result(const T& value) noexcept : value_(value) {}
    
    template <typename U = T, std::enable_if_t<!std::is_same_v<std::decay_t<U>, Error>, int> = 0>
    constexpr Result(T&& value) noexcept : value_(std::move(value)) {}

    // Error constructors - explicit to avoid ambiguity
    explicit constexpr Result(const Error& error) noexcept : value_(error) {}
    explicit constexpr Result(Error&& error) noexcept : value_(std::move(error)) {}

    [[nodiscard]] constexpr bool IsOk() const noexcept {
        return std::holds_alternative<T>(value_);
    }
    [[nodiscard]] constexpr bool IsErr() const noexcept {
        return std::holds_alternative<Error>(value_);
    }

    [[nodiscard]] constexpr const T& Unwrap() const& {
        if (IsErr()) {
            throw std::runtime_error("Result is Err: " + std::get<Error>(value_).message);
        }
        return std::get<T>(value_);
    }
    [[nodiscard]] constexpr T&& Unwrap() && {
        if (IsErr()) {
            throw std::runtime_error("Result is Err: " + std::get<Error>(value_).message);
        }
        return std::move(std::get<T>(value_));
    }
    [[nodiscard]] constexpr const Error& UnwrapErr() const& {
        if (IsOk()) {
            throw std::runtime_error("Result is Ok");
        }
        return std::get<Error>(value_);
    }
    [[nodiscard]] constexpr Error&& UnwrapErr() && {
        if (IsOk()) {
            throw std::runtime_error("Result is Ok");
        }
        return std::move(std::get<Error>(value_));
    }

    [[nodiscard]] constexpr T Expect(const char* msg) const& {
        if (IsErr()) throw std::runtime_error(std::string(msg) + ": " + std::get<Error>(value_).message);
        return std::get<T>(value_);
    }
    [[nodiscard]] constexpr T Expect(const char* msg) && {
        if (IsErr()) throw std::runtime_error(std::string(msg) + ": " + std::get<Error>(value_).message);
        return std::move(std::get<T>(value_));
    }

    template <typename U>
    [[nodiscard]] Result<U> Map(auto&& fn) const& {
        if (IsErr()) return Err<U>(UnwrapErr());
        return Ok(fn(std::get<T>(value_)));
    }
    template <typename U>
    [[nodiscard]] Result<U> Map(auto&& fn) && {
        if (IsErr()) return Err<U>(UnwrapErr());
        return Ok(fn(std::move(std::get<T>(value_))));
    }

    [[nodiscard]] constexpr bool operator==(const Result& other) const = default;

private:
    std::variant<T, Error> value_;

    template <typename U>
    friend class Result;
};

// Helper functions
template <typename T>
[[nodiscard]] constexpr Result<T> Ok(const T& value) noexcept {
    return Result<T>(value);
}
template <typename T>
[[nodiscard]] constexpr Result<T> Ok(T&& value) noexcept {
    return Result<T>(std::move(value));
}
template <typename T>
[[nodiscard]] constexpr Result<T> Err(const std::string& error) noexcept {
    return Result<T>(Error(error));
}
template <typename T>
[[nodiscard]] constexpr Result<T> Err(std::string&& error) noexcept {
    return Result<T>(Error(std::move(error)));
}
template <typename T>
[[nodiscard]] constexpr Result<T> Err(const char* error) noexcept {
    return Result<T>(Error(error));
}

// Specialization for void
template <>
class Result<void> {
public:
    using Error = OmniGhost::Platform::Error;

    constexpr Result() noexcept : error_(Error{}) {}
    explicit constexpr Result(const Error& error) noexcept : error_(error) {}
    explicit constexpr Result(Error&& error) noexcept : error_(std::move(error)) {}

    [[nodiscard]] constexpr bool IsOk() const noexcept { return error_.message.empty(); }
    [[nodiscard]] constexpr bool IsErr() const noexcept { return !error_.message.empty(); }

    [[nodiscard]] constexpr void Unwrap() const {
        if (IsErr()) throw std::runtime_error("Result is Err: " + error_.message);
    }
    [[nodiscard]] constexpr const Error& UnwrapErr() const& { return error_; }
    [[nodiscard]] constexpr Error&& UnwrapErr() && { return std::move(error_); }

    template <typename U>
    [[nodiscard]] Result<U> Map(auto&& fn) const& {
        if (IsErr()) return Err<U>(error_);
        return Ok<U>(std::invoke_result_t<decltype(fn)>{});
    }

    [[nodiscard]] constexpr bool operator==(const Result& other) const = default;

private:
    Error error_;

    template <typename U>
    friend class Result;
};

[[nodiscard]] constexpr Result<void> Ok() noexcept { return Result<void>(); }
[[nodiscard]] constexpr Result<void> Err(const std::string& error) noexcept { return Result<void>(Error(error)); }
[[nodiscard]] constexpr Result<void> Err(std::string&& error) noexcept { return Result<void>(Error(std::move(error))); }
[[nodiscard]] constexpr Result<void> Err(const char* error) noexcept { return Result<void>(Error(error)); }

} // namespace OmniGhost::Platform