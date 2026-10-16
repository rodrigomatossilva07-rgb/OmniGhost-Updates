#pragma once

#include "launcher_data.h"
#include "../globals.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace OmniGhost::Launcher {

enum class AdapterMaturity : std::uint8_t {
    Stable,
    Beta,
    Experimental,
    Unavailable,
};

enum class AdapterCapability : std::uint32_t {
    None = 0,
    Menu = 1u << 0,
    ReadOnlyMemory = 1u << 1,
    Overlay = 1u << 2,
    Radar = 1u << 3,
};

constexpr AdapterCapability operator|(AdapterCapability left, AdapterCapability right) noexcept {
    return static_cast<AdapterCapability>(static_cast<std::uint32_t>(left) |
                                          static_cast<std::uint32_t>(right));
}
constexpr bool HasCapability(AdapterCapability set, AdapterCapability value) noexcept {
    return (static_cast<std::uint32_t>(set) & static_cast<std::uint32_t>(value)) != 0;
}

enum class AdapterErrorCode : std::uint8_t {
    None,
    UnsupportedGame,
    AttachFailed,
    DeviceOpenFailed,
    ProcessNotFound,
    ProcessBindFailed,
    ModuleNotFound,
    OffsetsMissing,
    UnsupportedBuild,
    GameDataUnverified,
    UnexpectedException,
    ValidationFailed,
    InitializationFailed,
    Cancelled,
};

struct AdapterError {
    AdapterErrorCode code = AdapterErrorCode::None;

    std::string detail{};
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return code != AdapterErrorCode::None;
    }
};

struct AdapterStartResult {
    bool succeeded = false;
    AdapterError error{};
};

struct AdapterDescriptor {
    ::Launcher::GameId id = ::Launcher::GameId::None;
    std::string_view displayName{};
    AdapterMaturity maturity = AdapterMaturity::Unavailable;
    AdapterCapability capabilities = AdapterCapability::None;
};

[[nodiscard]] std::string_view AdapterErrorMessage(AdapterErrorCode code) noexcept;



class IGameAdapter {
public:
    virtual ~IGameAdapter() = default;


    virtual bool Attach() = 0;


    virtual bool Validate() = 0;


    virtual bool Initialize() = 0;


    virtual void Tick() = 0;


    virtual void Shutdown() noexcept = 0;


    [[nodiscard]] virtual std::string_view GetStatus() const noexcept = 0;
    [[nodiscard]] virtual AdapterDescriptor GetDescriptor() const noexcept = 0;
    [[nodiscard]] virtual AdapterError GetLastError() const = 0;




    virtual bool IsGameProcessAlive() const = 0;



    virtual bool ValidateLiveOffsets() = 0;



    [[nodiscard]] virtual std::string_view GetTerminationReason() const noexcept = 0;


    virtual ActiveGame GetGameId() const noexcept = 0;
};

}
