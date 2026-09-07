#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace OmniGhost::Constants {

// ============================================================
// Time Constants
// ============================================================
inline constexpr std::chrono::milliseconds kDefaultTimeout{100};
inline constexpr std::chrono::milliseconds kShortTimeout{250};
inline constexpr std::chrono::milliseconds kMediumTimeout{500};
inline constexpr std::chrono::milliseconds kLongTimeout{2000};
inline constexpr std::chrono::milliseconds kVeryLongTimeout{5000};

inline constexpr std::chrono::milliseconds kSerialTimeout{2000};
inline constexpr std::chrono::milliseconds kSerialShortTimeout{250};
inline constexpr std::chrono::milliseconds kDeviceProbeTimeout{900};
inline constexpr std::chrono::milliseconds kFadeDuration{220};

// ============================================================
// Buffer Sizes
// ============================================================
inline constexpr std::size_t kSmallBufferSize{128};
inline constexpr std::size_t kMediumBufferSize{512};
inline constexpr std::size_t kLargeBufferSize{1024};
inline constexpr std::size_t kHugeBufferSize{4096};
inline constexpr std::size_t kMaxBufferSize{32768};
inline constexpr std::size_t kSerialBufferSize{8192};
inline constexpr std::size_t kPacketSize{1024};

// ============================================================
// Serial Communication
// ============================================================
inline constexpr uint32_t kDefaultBaudRate{115200};
inline constexpr uint32_t kMinBaudRate{1};
inline constexpr uint32_t kMaxBaudRate{921600};
inline constexpr std::chrono::milliseconds kSerialWriteTimeout{2000};
inline constexpr std::chrono::milliseconds kSerialReadTimeout{250};

// ============================================================
// Input Device Limits
// ============================================================
inline constexpr int16_t kMinAxisValue{-32768};
inline constexpr int16_t kMaxAxisValue{32767};
inline constexpr int16_t kClampedMinAxis{-32767};
inline constexpr int16_t kClampedMaxAxis{32767};
inline constexpr uint8_t kMaxButtonMask{0xFF};

// ============================================================
// UI Constants
// ============================================================
inline constexpr float kDefaultCardWidth{120.0f};
inline constexpr float kMinCardWidth{110.0f};
inline constexpr float kDefaultFontSize{11.0f};
inline constexpr float kMinFontSize{8.0f};
inline constexpr float kMaxFontSize{16.0f};
inline constexpr float kDefaultSpacing{12.0f};
inline constexpr float kDefaultEdgeMargin{48.0f};
inline constexpr float kDefaultQuietWidth{0.0f};

// ============================================================
// Performance Constants
// ============================================================
inline constexpr float kDefaultTargetFPS{60.0f};
inline constexpr float kMinTargetFPS{30.0f};
inline constexpr float kMaxTargetFPS{144.0f};
inline constexpr float kFrameBudgetMs{16.667f};
inline constexpr float kDefaultSmoothingRate{7.5f};

// ============================================================
// Digital Rain
// ============================================================
inline constexpr int kDefaultMaxColumns{96};
inline constexpr int kMinTrailLength{10};
inline constexpr int kMaxTrailLength{28};
inline constexpr int kMaxStars{48};
inline constexpr int kMaxClusters{7};
inline constexpr float kDefaultFadeSeconds{0.22f};

// ============================================================
// Input Device
// ============================================================
inline constexpr std::chrono::milliseconds kInputPollInterval{1};
inline constexpr uint32_t kMaxSerialRetries{3};
inline constexpr std::chrono::milliseconds kDeviceReconnectDelay{1000};

// ============================================================
// Network
// ============================================================
inline constexpr std::size_t kMaxUrlLength{2048};
inline constexpr std::size_t kMaxResponseSize{4096};
inline constexpr std::chrono::seconds kNetworkTimeout{30};
inline constexpr std::size_t kMaxReleaseCount{100};
inline constexpr std::size_t kMaxAssetCount{128};

// ============================================================
// Security
// ============================================================
inline constexpr uint32_t kPbkdf2Iterations{120000};
inline constexpr std::size_t kMaxEmailBytes{320};
inline constexpr std::size_t kMaxPasswordBytes{1024};
inline constexpr std::size_t kMaxLicenseBytes{512};

// ============================================================
// File System
// ============================================================
inline constexpr std::size_t kMaxConfigBytes{2 * 1024 * 1024};
inline constexpr std::size_t kMaxLogBytes{5 * 1024 * 1024};
inline constexpr std::size_t kMaxStructuredLogBytes{5 * 1024 * 1024};
inline constexpr std::size_t kMaxArchiveBytes{40 * 1024 * 1024};
inline constexpr size_t kMaximumArchivedLogs{8};
inline constexpr size_t kMaximumRecentEvents{256};

// ============================================================
// Ports
// ============================================================
inline constexpr uint16_t kMinPort{1};
inline constexpr uint16_t kMaxPort{65535};

// ============================================================
// String Limits
// ============================================================
inline constexpr std::size_t kMaxConfigName{96};
inline constexpr std::size_t kMaxStringLength{4096};
inline constexpr std::size_t kMaxEmailLength{320};
inline constexpr std::size_t kMaxPasswordLength{1024};

// ============================================================
// Error Codes
// ============================================================
inline constexpr int kErrorSuccess{0};
inline constexpr int kErrorGeneric{-1};
inline constexpr int kErrorTimeout{-2};
inline constexpr int kErrorNotFound{-3};
inline constexpr int kErrorInvalidArg{-4};
inline constexpr int kErrorOutOfMemory{-5};
inline constexpr int kErrorNotConnected{-6};
inline constexpr int kErrorAlreadyExists{-7};
inline constexpr int kErrorPermission{-8};

} // namespace OmniGhost::Constants