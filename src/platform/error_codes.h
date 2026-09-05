#pragma once

#include <string_view>

namespace OmniGhost::Platform {

// Centralized error codes for all subsystems
// Used by UI for consistent messaging
enum class ErrorCode : std::uint32_t {
    // Generic
    None = 0,
    Unknown = 1,
    NotImplemented = 2,
    InvalidArgument = 3,
    NotSupported = 4,
    OutOfMemory = 5,
    Timeout = 6,
    Cancelled = 7,
    AlreadyExists = 8,
    NotFound = 9,
    AccessDenied = 10,
    InvalidState = 11,
    CorruptedData = 12,
    VersionMismatch = 13,
    NetworkError = 14,
    DiskError = 15,
    PermissionDenied = 16,
    IntegrityCheckFailed = 16,

    // DMA / Hardware
    DmaDeviceNotFound = 1000,
    DmaDeviceOpenFailed = 1001,
    DmaDeviceDataPathFailed = 1002,
    DmaVmmInitFailed = 1003,
    DmaVmmSessionRecoveryFailed = 1004,
    DmaPluginInitFailed = 1005,
    DmaProcessNotFound = 1006,
    DmaProcInfoGenerating = 1007,
    DmaProcInfoTimeout = 1008,
    DmaProcInfoStuck = 1009,
    DmaProcInfoCancelled = 1010,
    DmaMappingUnavailable = 1011,
    DmaModuleValidationFailed = 1012,
    DmaRuntimeUnavailable = 1013,
    DmaOperationSuperseded = 1014,
    DmaDependencyMismatch = 1015,
    DmaBackendBusy = 1016,
    DmaFpgaLost = 1017,
    DmaInvalidConfiguration = 1018,
    DmaDriverVersionMismatch = 1019,
    DmaFirmwareTooOld = 1020,

    // Authentication / License
    AuthInvalidCredentials = 2000,
    AuthExpired = 2001,
    AuthRevoked = 2002,
    AuthNetworkError = 2003,
    AuthServerUnavailable = 2004,
    AuthInvalidKey = 2005,
    AuthHwidMismatch = 2006,
    AuthEntitlementMissing = 2007,
    AuthTrialExpired = 2008,
    AuthConcurrentLimit = 2009,

    // Offsets / Game
    OffsetsMissing = 3000,
    OffsetsOutdated = 3001,
    OffsetsValidationFailed = 3002,
    OffsetsNotLoaded = 3003,
    GameNotRunning = 3010,
    GameAttachFailed = 3011,
    GameProcessLost = 3012,
    GameModuleNotFound = 3013,
    GameOffsetsOutdated = 3014,
    GameUnsupportedBuild = 3015,

    // Updater
    UpdateCheckFailed = 4000,
    UpdateDownloadFailed = 4001,
    UpdateIntegrityFailed = 4002,
    UpdateApplyFailed = 4003,
    UpdateRollbackFailed = 4004,
    UpdateNoSpace = 4005,
    UpdateLocked = 4006,
    UpdateManifestInvalid = 4007,
    UpdateSignatureInvalid = 4008,
    UpdateVersionIncompatible = 4008,

    // Configuration
    ConfigLoadFailed = 5000,
    ConfigSaveFailed = 5001,
    ConfigCorrupted = 5002,
    ConfigSchemaMismatch = 5003,
    ConfigMigrationFailed = 5004,

    // Network
    NetworkDnsFailed = 6000,
    NetworkConnectionFailed = 6001,
    NetworkTimeout = 6002,
    NetworkSslError = 6003,
    NetworkRateLimited = 6004,

    // Filesystem
    FsNotFound = 7000,
    FsAccessDenied = 7001,
    FsFull = 7002,
    FsCorrupted = 7003,
    FsLocked = 7004,

    // UI / Rendering
    UiRendererInitFailed = 8000,
    UiFontLoadFailed = 8001,
    UiTextureLoadFailed = 8002,
    UiLayoutError = 8003,
};

// Human-readable messages for UI display
[[nodiscard]] std::string_view ErrorMessage(ErrorCode code) noexcept;

// Check if error is retryable
[[nodiscard]] bool IsRetryable(ErrorCode code) noexcept;

// Check if error is user-actionable
[[nodiscard]] bool IsUserActionable(ErrorCode code) noexcept;

// Get subsystem from error code
[[nodiscard]] std::string_view ErrorSubsystem(ErrorCode code) noexcept;

} // namespace OmniGhost::Platform