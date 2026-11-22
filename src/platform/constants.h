#pragma once

#include <cstdint>
#include <chrono>

namespace OmniGhost::Constants {

// =============================================================================
// Network/Port Constants
// =============================================================================
constexpr uint16_t kDefaultPort = 12345;
constexpr uint16_t kMaxPort = 65535;
constexpr uint16_t kMinPort = 1;

// =============================================================================
// Buffer/Buffer Size Constants
// =============================================================================
constexpr size_t kSmallBufferSize = 128;
constexpr size_t kMediumBufferSize = 256;
constexpr size_t kLargeBufferSize = 512;
constexpr size_t kXLargeBufferSize = 1024;
constexpr size_t kXXLargeBufferSize = 4096;
constexpr size_t kMaxCredentialBytes = 4096;
constexpr size_t kMaxPacketSize = 1024;

// =============================================================================
// Timeout Constants (milliseconds unless specified)
// =============================================================================
constexpr std::chrono::milliseconds kShortTimeout{50};
constexpr std::chrono::milliseconds kDefaultTimeout{100};
constexpr std::chrono::milliseconds kMediumTimeout{250};
constexpr std::chrono::milliseconds kLongTimeout{500};
constexpr std::chrono::milliseconds kDefaultTimeoutMs{1000};
constexpr std::chrono::milliseconds kLongTimeoutMs{2000};
constexpr std::chrono::milliseconds kVeryLongTimeoutMs{5000};
constexpr std::chrono::milliseconds kMAKCUTimout{900};
constexpr std::chrono::milliseconds kMAKCURetryInterval{5000};

// =============================================================================
// Serial/COM Constants
// =============================================================================
constexpr DWORD kSerialWriteTimeout = 250;
constexpr DWORD kSerialReadTimeout = 250;
constexpr size_t kSerialBufferSize = 4096;
constexpr size_t kSerialWriteBufferSize = 4096;

// =============================================================================
// Input/Coordinate Limits
// =============================================================================
constexpr int16_t kInt16Min = -32767;
constexpr int16_t kInt16Max = 32767;
constexpr int32_t kInt32Min = -2147483647;
constexpr int32_t kInt32Max = 2147483647;
constexpr float kFloatEpsilon = 1e-5f;
constexpr double kDoubleEpsilon = 1e-10;

// Coordinate bounds
constexpr float kCoordinateMin = -100000.0f;
constexpr float kCoordinateMax = 100000.0f;
constexpr float kCoordinateLargeMax = 1000000.0f;
constexpr float kMatrixValueMax = 1000000.0f;

// Input ranges
constexpr int16_t kMouseMin = -32767;
constexpr int16_t kMouseMax = 32767;

// =============================================================================
// Entity/Array Limits
// =============================================================================
constexpr uint32_t kMaxEntities = 8192;
constexpr uint32_t kMaxEntitiesApex = 10000;
constexpr uint32_t kMaxEntitiesDisplay = 256;
constexpr uint32_t kMaxVehicles = 128;
constexpr uint32_t kMinVehicles = 8;
constexpr uint32_t kEntityListSize = 0x10000;
constexpr uint32_t kEntityEntrySize = 8;

// =============================================================================
// Memory/Pointer Constants
// =============================================================================
constexpr uintptr_t kMinPointer = 0x10000;
constexpr uintptr_t kMaxPointer = 0x7FFFFFFFFFFF;
constexpr size_t kPageSize = 4096;

// =============================================================================
// Timing/Animation Constants
// =============================================================================
constexpr float kAnimationEpsilon = 0.001f;
constexpr float kAnimationSpeedFast = 10.0f;
constexpr float kAnimationSpeedNormal = 10.0f;
constexpr float kAnimationSpeedSlow = 5.0f;
constexpr float kHoverAnimationSpeed = 10.0f;
constexpr float kHoverNudgeAmount = 0.006f;
constexpr float kScaleHoverFactor = 0.006f;
constexpr float kScaleMotionFactor = 0.006f;
constexpr float kScaleBase = 1.0f;
constexpr float kScaleHeld = 0.988f;
constexpr float kMinAnimationDelta = 0.05f;
constexpr float kMaxAnimationDelta = 0.05f;

// =============================================================================
// Distance/Range Constants
// =============================================================================
constexpr float kMinDistance = 50.0f;
constexpr float kMediumDistance = 100.0f;
constexpr float kLongDistance = 150.0f;
constexpr float kMaxDistance = 200.0f;
constexpr float kSniperMinDistance = 50.0f;
constexpr float kSniperMaxDistance = 150.0f;

// =============================================================================
// Entity/Health Limits
// =============================================================================
constexpr int kMinHealth = 1;
constexpr int kMaxHealth = 1000;
constexpr int kMaxHealthApex = 1000;

// =============================================================================
// Telemetry/Logging Constants
// =============================================================================
constexpr int kTelemetryWindow = 256;
constexpr float kSpikeSevereMs = 200.0f;
constexpr float kSpikeCriticalMs = 500.0f;
constexpr int kLogBufferSize = 3072;
constexpr int kSmallLogBuffer = 256;
constexpr int kMediumLogBuffer = 512;

// =============================================================================
// UI/Layout Constants
// =============================================================================
constexpr float kMinWidgetWidth = 40.0f;
constexpr float kDefaultControlHeight = 36.0f;
constexpr float kControlRounding = 8.0f;
constexpr float kCardRounding = 10.0f;
constexpr float kWindowRounding = 14.0f;
constexpr float kCardRoundingSmall = 8.0f;
constexpr float kControlRoundingSmall = 4.0f;
constexpr float kCheckboxSize = 28.0f;
constexpr float kCheckboxKnobRatio = 0.42f;
constexpr float kSwitchWidth = 52.0f;
constexpr float kSwitchHeight = 28.0f;
constexpr float kKnobRatio = 0.42f;
constexpr float kKnobRadiusRatio = 0.42f;

// Spacing
constexpr float kSpacingXs = 4.0f;
constexpr float kSpacingSm = 8.0f;
constexpr float kSpacingMd = 12.0f;
constexpr float kSpacingLg = 16.0f;
constexpr float kSpacingXl = 24.0f;
constexpr float kSpacingXxl = 32.0f;

// Card metrics
constexpr float kCardPadding = 14.0f;
constexpr float kCardHeaderHeight = 42.0f;
constexpr float kCardMinHeight = 0.0f;
constexpr float kCardPaddingInternal = 14.0f;
constexpr float kCardHeaderHeightInternal = 42.0f;
constexpr float kCardRoundingInternal = 10.0f;

// Window metrics
constexpr float kWindowWidth = 1180.0f;
constexpr float kWindowHeight = 720.0f;
constexpr float kHeaderHeight = 70.0f;
constexpr float kFooterHeight = 40.0f;
constexpr float kSidebarWidth = 210.0f;
constexpr float kContentInset = 12.0f;
const constexpr float kContentPaddingX = 14.0f;
const constexpr float kContentPaddingY = 12.0f;
constexpr float kGridGap = 12.0f;

// Shadows
constexpr float kShadowOffsetY = 5.0f;
constexpr float kShadowRadius = 12.0f;
constexpr int kShadowAlpha = 46;

// Typography
constexpr float kFontTitle = 20.0f;
constexpr float kFontHeading = 16.0f;
constexpr float kFontBody = 14.0f;
constexpr float kFontCaption = 11.5f;
constexpr float kFontMono = 11.0f;

// =============================================================================
// Hardware/Device Constants
// =============================================================================
constexpr int kMaxEntitiesLimit = 256;

// =============================================================================
// Time Constants
// =============================================================================
constexpr std::chrono::milliseconds kFrameTime{16}; // ~60 FPS
constexpr std::chrono::milliseconds kInputPollInterval{1};
constexpr std::chrono::milliseconds kUISyncInterval{16};
constexpr std::chrono::seconds kMinProgressDuration{1};
constexpr std::chrono::milliseconds kProgressMinDuration{500};

// =============================================================================
// File/Path Constants
// =============================================================================
constexpr char kDataDir[] = "data";
constexpr char kConfigDir[] = "config";
constexpr char kSchemasDir[] = "schemas";
constexpr char kLogsDir[] = "logs";
constexpr char kResourcesDir[] = "resources";

// =============================================================================
// Version/Protocol Constants
// =============================================================================
constexpr uint32_t kConfigSchemaVersion = 4;
constexpr uint32_t kMinConfigSchemaVersion = 1;
constexpr uint32_t kMaxConfigSchemaVersion = 10;
constexpr uint16_t kContainerVersion = 1;
constexpr uint16_t kPayloadVersion = 1;
constexpr uint8_t kCompressionPackBits = 1;
constexpr uint32_t kContainerMagic = 0x464F474F; // "OGFO"
constexpr uint32_t kPayloadMagic = 0x504E534F; // "PNSO"

// =============================================================================
// Crypto Constants
// =============================================================================
constexpr uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ull;
constexpr uint64_t kFnvPrime = 0x100000001b3ull;
constexpr uint64_t kSplitMixConstant1 = 0x9e3779b97f4a7c15ull;
constexpr uint64_t kSplitMixConstant2 = 0xbf58476d1ce4e5b9ull;
constexpr uint64_t kSplitMixConstant3 = 0x94d049bb133111ebull;

// =============================================================================
// Bit Manipulation Helpers
// =============================================================================
constexpr uint32_t Fnv1a32(const char* str) {
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= static_cast<uint8_t>(*str++);
        hash *= 16777619u;
    }
    return hash;
}

// =============================================================================
// Feature Limits
// =============================================================================
constexpr int kMaxBones = 28;
constexpr int kMaxBoneSlots = 18;
constexpr int kMaxWeaponSlots = 10;
constexpr int kMaxAmmoClips = 5;
constexpr int kMaxInventorySlots = 50;

// =============================================================================
// Distance Constants
// =============================================================================
constexpr float kCloseRange = 50.0f;
constexpr float kMediumRange = 100.0f;
constexpr float kLongRange = 150.0f;
constexpr float kExtremeRange = 200.0f;
constexpr float kSniperMinRange = 50.0f;
constexpr float kSniperMaxRange = 150.0f;

// =============================================================================
// Vehicle Limits
// =============================================================================
constexpr int kDefaultMaxVehicles = 32;
constexpr int kMaxVehiclesLimit = 128;

// =============================================================================
// Validation Limits
// =============================================================================
constexpr int kMinSchemaVersion = 1;
constexpr int kMaxSchemaVersion = 10;
constexpr int kMaxMetadataStringBytes = 16384;
constexpr int kMaxEntries = 8192;
constexpr int kMaxCompressedBytes = 4 * 1024 * 1024;
constexpr int kMaxUncompressedBytes = 8 * 1024 * 1024;

} // namespace OmniGhost::Constants
