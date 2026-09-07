#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <type_traits>
#include <stdexcept>

namespace OmniGhost::Platform {

struct Error {
    std::string message;

    Error() noexcept : message() {}
    explicit Error(const std::string& msg) noexcept : message(msg) {}
    explicit Error(std::string&& msg) noexcept : message(std::move(msg)) {}
    Error(const char* msg) noexcept : message(msg ? msg : "") {}

    [[nodiscard]] bool empty() const noexcept { return message.empty(); }
    [[nodiscard]] const std::string& str() const noexcept { return message; }
    [[nodiscard]] operator std::string_view() const noexcept { return message; }
};

// Primary: Result<T>
template <typename T>
class Result {
public:
    Result() noexcept : value_(Error{}) {}
    Result(const T& value) noexcept : value_(value) {}
    Result(T&& value) noexcept : value_(std::move(value)) {}
    explicit Result(const Error& error) noexcept : value_(error) {}
    explicit Result(Error&& error) noexcept : value_(std::move(error)) {}

    [[nodiscard]] bool IsOk() const noexcept { return std::holds_alternative<T>(value_); }
    [[nodiscard]] bool IsErr() const noexcept { return std::holds_alternative<Error>(value_); }
    [[nodiscard]] explicit operator bool() const noexcept { return IsOk(); }

    [[nodiscard]] const T& operator*() const& { return value(); }
    [[nodiscard]] T& operator*() & { return value(); }
    [[nodiscard]] T&& operator*() && { return std::move(value()); }

    [[nodiscard]] const T& value() const& { return Unwrap(); }
    [[nodiscard]] T& value() & { return const_cast<T&>(Unwrap()); }

    [[nodiscard]] const std::string& error() const& { return UnwrapErr().message; }

    [[nodiscard]] const T& Unwrap() const& {
        if (IsErr()) throw std::runtime_error("Result is Err: " + std::get<Error>(value_).message);
        return std::get<T>(value_);
    }
    [[nodiscard]] T&& Unwrap() && {
        if (IsErr()) throw std::runtime_error("Result is Err: " + std::get<Error>(value_).message);
        return std::move(std::get<T>(value_));
    }
    [[nodiscard]] const Error& UnwrapErr() const& {
        if (IsOk()) throw std::runtime_error("Result is Ok");
        return std::get<Error>(value_);
    }

private:
    std::variant<T, Error> value_;
};

// Specialization: Result<void>
template <>
class Result<void> {
public:
    Result() noexcept : error_() {}
    explicit Result(const Error& error) noexcept : error_(error) {}
    explicit Result(Error&& error) noexcept : error_(std::move(error)) {}

    [[nodiscard]] bool IsOk() const noexcept { return error_.message.empty(); }
    [[nodiscard]] bool IsErr() const noexcept { return !error_.message.empty(); }
    [[nodiscard]] explicit operator bool() const noexcept { return IsOk(); }

    [[nodiscard]] const std::string& error() const& { return error_.message; }

    void Unwrap() const {
        if (IsErr()) throw std::runtime_error("Result is Err: " + error_.message);
    }
    [[nodiscard]] const Error& UnwrapErr() const& { return error_; }

private:
    Error error_;
};

// Helpers
template <typename T>
[[nodiscard]] inline Result<T> Ok(const T& value) noexcept { return Result<T>(value); }

template <typename T>
[[nodiscard]] inline Result<T> Ok(T&& value) noexcept { return Result<T>(std::move(value)); }

[[nodiscard]] inline Result<void> Ok() noexcept { return Result<void>(); }

template <typename T>
[[nodiscard]] inline Result<T> Err(const std::string& error) noexcept { return Result<T>(Error(error)); }

template <typename T>
[[nodiscard]] inline Result<T> Err(std::string&& error) noexcept { return Result<T>(Error(std::move(error))); }

template <typename T>
[[nodiscard]] inline Result<T> Err(const char* error) noexcept { return Result<T>(Error(error)); }

template <typename T>
[[nodiscard]] inline Result<T> Err(const Error& error) noexcept { return Result<T>(error); }

[[nodiscard]] inline Result<void> Err(const std::string& error) noexcept { return Result<void>(Error(error)); }
[[nodiscard]] inline Result<void> Err(std::string&& error) noexcept { return Result<void>(Error(std::move(error))); }
[[nodiscard]] inline Result<void> Err(const char* error) noexcept { return Result<void>(Error(error)); }

} // namespace OmniGhost::Platform
