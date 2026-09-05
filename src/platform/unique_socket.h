#pragma once

#include <winsock2.h>
#include <utility>

namespace OmniGhost::Platform {

class UniqueSocket final {
public:
    UniqueSocket() noexcept = default;
    explicit UniqueSocket(SOCKET socket) noexcept : socket_(socket) {}

    UniqueSocket(const UniqueSocket&) = delete;
    UniqueSocket& operator=(const UniqueSocket&) = delete;

    UniqueSocket(UniqueSocket&& other) noexcept
        : socket_(other.release()) {}

    UniqueSocket& operator=(UniqueSocket&& other) noexcept {
        if (this != &other)
            reset(other.release());
        return *this;
    }

    ~UniqueSocket() noexcept { reset(); }

    [[nodiscard]] SOCKET get() const noexcept { return socket_; }
    [[nodiscard]] explicit operator bool() const noexcept { return socket_ != INVALID_SOCKET; }

    SOCKET release() noexcept { return std::exchange(socket_, INVALID_SOCKET); }

    void reset(SOCKET replacement = INVALID_SOCKET) noexcept {
        if (socket_ != INVALID_SOCKET)
            closesocket(socket_);
        socket_ = replacement;
    }

private:
    SOCKET socket_{INVALID_SOCKET};
};

} // namespace OmniGhost::Platform
