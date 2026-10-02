#pragma once

#include <string_view>

namespace OmniGhost::Support {

enum class ErrorCode {
    None,
    RendererUnavailable,
    DmaUnavailable,
    DmaOpenFailed,
    ProcessInventoryUnavailable,
    MemoryReadUnavailable,
    GameDataUnverified,
    RuntimeDependenciesInvalid,
    GameInitializationFailed,
    GameModuleUnavailable,
    GameAdapterUnavailable,
    LaunchCancelled,
    InputUnavailable,
    GameNotFound,
    OffsetsUnsupported,
    RadarAuthentication,
    UpdaterFailed,
    DiagnosticExportFailed,
};

std::string_view Code(ErrorCode error) noexcept;
std::string_view Message(ErrorCode error) noexcept;

} // namespace OmniGhost::Support
