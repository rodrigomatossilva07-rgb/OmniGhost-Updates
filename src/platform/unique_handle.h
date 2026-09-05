#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <utility>

namespace OmniGhost::Platform {

class UniqueHandle final {
public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept
        : handle_(other.release()) {}

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other)
            reset(other.release());
        return *this;
    }

    ~UniqueHandle() noexcept { reset(); }

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    HANDLE release() noexcept {
        return std::exchange(handle_, nullptr);
    }

    void reset(HANDLE replacement = nullptr) noexcept {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(handle_);
        handle_ = replacement;
    }

private:
    HANDLE handle_{nullptr};
};

class UniqueModule final {
public:
    UniqueModule() noexcept = default;
    explicit UniqueModule(HMODULE module) noexcept : module_(module) {}

    UniqueModule(const UniqueModule&) = delete;
    UniqueModule& operator=(const UniqueModule&) = delete;

    UniqueModule(UniqueModule&& other) noexcept
        : module_(other.release()) {}

    UniqueModule& operator=(UniqueModule&& other) noexcept {
        if (this != &other)
            reset(other.release());
        return *this;
    }

    ~UniqueModule() noexcept { reset(); }

    [[nodiscard]] HMODULE get() const noexcept { return module_; }
    [[nodiscard]] explicit operator bool() const noexcept { return module_ != nullptr; }

    HMODULE release() noexcept { return std::exchange(module_, nullptr); }

    void reset(HMODULE replacement = nullptr) noexcept {
        if (module_)
            FreeLibrary(module_);
        module_ = replacement;
    }

private:
    HMODULE module_{nullptr};
};

struct UniqueProcessInformation final {
    PROCESS_INFORMATION value{};

    UniqueProcessInformation() noexcept = default;
    explicit UniqueProcessInformation(PROCESS_INFORMATION processInformation) noexcept
        : value(processInformation) {}

    UniqueProcessInformation(const UniqueProcessInformation&) = delete;
    UniqueProcessInformation& operator=(const UniqueProcessInformation&) = delete;

    UniqueProcessInformation(UniqueProcessInformation&& other) noexcept
        : value(std::exchange(other.value, PROCESS_INFORMATION{})) {}

    UniqueProcessInformation& operator=(UniqueProcessInformation&& other) noexcept {
        if (this != &other) {
            reset();
            value = std::exchange(other.value, PROCESS_INFORMATION{});
        }
        return *this;
    }

    ~UniqueProcessInformation() noexcept { reset(); }

    void reset() noexcept {
        if (value.hThread)
            CloseHandle(value.hThread);
        if (value.hProcess)
            CloseHandle(value.hProcess);
        value = {};
    }

    PROCESS_INFORMATION* put() noexcept {
        reset();
        return &value;
    }

    [[nodiscard]] HANDLE process() const noexcept { return value.hProcess; }
    [[nodiscard]] HANDLE thread() const noexcept { return value.hThread; }
    [[nodiscard]] DWORD process_id() const noexcept { return value.dwProcessId; }
    [[nodiscard]] explicit operator bool() const noexcept { return value.hProcess != nullptr; }

    HANDLE release_process() noexcept {
        return std::exchange(value.hProcess, nullptr);
    }

    HANDLE release_thread() noexcept {
        return std::exchange(value.hThread, nullptr);
    }
};
} // namespace OmniGhost::Platform
