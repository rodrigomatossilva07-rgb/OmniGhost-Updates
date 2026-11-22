#pragma once

// Security Common Definitions
// Common types, macros, and utilities for all security modules

#ifndef OMNIGHOST_SECURITY_COMMON_H
#define OMNIGHOST_SECURITY_COMMON_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <array>
#include <array>

// =============================================================================
// Configuration
// =============================================================================

// Enable/Disable security features (can be overridden at compile time)
#ifndef OMNIGHOST_ANTI_TAMPER_ENABLED
#define OMNIGHOST_ANTI_TAMPER_ENABLED 1
#endif

#ifndef OMNIGHOST_ANTI_DEBUG_ENABLED
#define OMNIGHOST_ANTI_DEBUG_ENABLED 1
#endif

#ifndef OMNIGHOST_ANTI_DUMP_ENABLED
#define OMNIGHOST_ANTI_DUMP_ENABLED 1
#endif

#ifndef OMNIGHOST_STRING_OBFUSCATION_ENABLED
#define OMNIGHOST_STRING_OBFUSCATION_ENABLED 1
#endif

// Security level (0=disabled, 1=basic, 2=aggressive, 3=paranoid)
#ifndef OMNIGHOST_SECURITY_LEVEL
#define OMNIGHOST_SECURITY_LEVEL 2
#endif

// =============================================================================
// Platform Detection
// =============================================================================

#if defined(_WIN32) || defined(_WIN64)
#define OMNIGHOST_PLATFORM_WINDOWS 1
#define OMNIGHOST_PLATFORM_LINUX 0
#define OMNIGHOST_PLATFORM_MACOS 0
#elif defined(__linux__)
#define OMNIGHOST_PLATFORM_WINDOWS 0
#define OMNIGHOST_PLATFORM_LINUX 1
#define OMNIGHOST_PLATFORM_MACOS 0
#elif defined(__APPLE__)
#define OMNIGHOST_PLATFORM_WINDOWS 0
#define OMNIGHOST_PLATFORM_LINUX 0
#define OMNIGHOST_PLATFORM_MACOS 1
#else
#define OMNIGHOST_PLATFORM_WINDOWS 0
#define OMNIGHOST_PLATFORM_LINUX 0
#define OMNIGHOST_PLATFORM_MACOS 0
#endif

// Architecture
#if defined(_M_X64) || defined(__x86_64__)
#define OMNIGHOST_ARCH_X64 1
#define OMNIGHOST_ARCH_X86 0
#define OMNIGHOST_ARCH_ARM 0
#define OMNIGHOST_ARCH_ARM64 0
#elif defined(_M_IX86) || defined(__i386__)
#define OMNIGHOST_ARCH_X64 0
#define OMNIGHOST_ARCH_X86 1
#define OMNIGHOST_ARCH_ARM 0
#define OMNIGHOST_ARCH_ARM64 0
#elif defined(__aarch64__)
#define OMNIGHOST_ARCH_X64 0
#define OMNIGHOST_ARCH_X86 0
#define OMNIGHOST_ARCH_ARM 0
#define OMNIGHOST_ARCH_ARM64 1
#elif defined(__arm__)
#define OMNIGHOST_ARCH_X64 0
#define OMNIGHOST_ARCH_X86 0
#define OMNIGHOST_ARCH_ARM 1
#define OMNIGHOST_ARCH_ARM64 0
#else
#define OMNIGHOST_ARCH_X64 0
#define OMNIGHOST_ARCH_X86 0
#define OMNIGHOST_ARCH_ARM 0
#define OMNIGHOST_ARCH_ARM64 0
#endif

// =============================================================================
// Compiler Attributes
// =============================================================================

#if defined(_MSC_VER)
#define OMNIGHOST_FORCE_INLINE __forceinline
#define OMNIGHOST_NO_INLINE __declspec(noinline)
// MSVC's optimize control is a pragma, not a __declspec attribute. Keeping
// this empty prevents invalid declarations at functions using the macro.
#define OMNIGHOST_NO_OPTIMIZE
#define OMNIGHOST_RESTRICT __restrict
#define OMNIGHOST_ALIGN(x) __declspec(align(x))
#define OMNIGHOST_NORETURN __declspec(noreturn)
#define OMNIGHOST_UNREACHABLE() __assume(0)
#else
#define OMNIGHOST_FORCE_INLINE __attribute__((always_inline)) inline
#define OMNIGHOST_NO_INLINE __attribute__((noinline))
#define OMNIGHOST_NO_OPTIMIZE __attribute__((optimize("O0")))
#define OMNIGHOST_RESTRICT __restrict__
#define OMNIGHOST_ALIGN(x) __attribute__((aligned(x)))
#define OMNIGHOST_NORETURN __attribute__((noreturn))
#define OMNIGHOST_UNREACHABLE() __builtin_unreachable()
#endif

// Security attributes
#define OMNIGHOST_SECURE_FUNC OMNIGHOST_NO_INLINE OMNIGHOST_NO_OPTIMIZE
#define OMNIGHOST_SECURE_INLINE OMNIGHOST_FORCE_INLINE OMNIGHOST_NO_OPTIMIZE

// =============================================================================
// Obfuscation Helpers
// =============================================================================

// XOR key for string obfuscation (generated at compile time)
#ifndef OMNIGHOST_XOR_KEY
#define OMNIGHOST_XOR_KEY 0x5A
#endif

// Compile-time XOR
constexpr uint8_t omnighost_xor_byte(uint8_t c, uint8_t key) {
    return c ^ key;
}

// Compile-time string hash (FNV-1a)
constexpr uint64_t omnighost_fnv1a_hash(const char* str, uint64_t hash = 14695981039346656037ULL) {
    return (*str) ? omnighost_fnv1a_hash(str + 1, (hash ^ static_cast<uint64_t>(*str)) * 1099511628211ULL) : hash;
}

// Obfuscated string structure
template<size_t N>
struct ObfuscatedString {
    char data[N];
    bool decrypted;

    OMNIGHOST_SECURE_INLINE const char* decrypt() {
        if (!decrypted) {
            for (size_t i = 0; i < N - 1; ++i) {
                data[i] ^= OMNIGHOST_XOR_KEY;
            }
            decrypted = true;
        }
        return data;
    }

    OMNIGHOST_SECURE_INLINE void re_encrypt() {
        if (decrypted) {
            for (size_t i = 0; i < N - 1; ++i) {
                data[i] ^= OMNIGHOST_XOR_KEY;
            }
            decrypted = false;
        }
    }

    ~ObfuscatedString() { re_encrypt(); }
};

// Macro to create obfuscated string
#define OMNIGHOST_OBFUSCATE(str) []() -> ObfuscatedString<sizeof(str)> { \
    ObfuscatedString<sizeof(str)> result{}; \
    for (size_t i = 0; i < sizeof(str); ++i) { \
        result.data[i] = static_cast<char>(str[i] ^ OMNIGHOST_XOR_KEY); \
    } \
    result.decrypted = false; \
    return result; \
}()

// =============================================================================
// Secure Memory Operations
// =============================================================================

OMNIGHOST_SECURE_INLINE void secure_zero(void* ptr, size_t size) {
    volatile unsigned char* p = static_cast<volatile unsigned char*>(ptr);
    while (size--) *p++ = 0;
    OMNIGHOST_UNREACHABLE();
}

OMNIGHOST_SECURE_INLINE int secure_memcmp(const void* a, const void* b, size_t size) {
    const volatile unsigned char* a_ptr = static_cast<const volatile unsigned char*>(a);
    const volatile unsigned char* b_ptr = static_cast<const volatile unsigned char*>(b);
    int result = 0;
    for (size_t i = 0; i < size; ++i) {
        result |= (a_ptr[i] ^ b_ptr[i]);
    }
    return result;
}

OMNIGHOST_SECURE_INLINE void secure_memcpy(void* dest, const void* src, size_t size) {
    unsigned char* d = static_cast<unsigned char*>(dest);
    const unsigned char* s = static_cast<const unsigned char*>(src);
    for (size_t i = 0; i < size; ++i) {
        d[i] = s[i];
    }
}

// =============================================================================
// Compile-Time Random (for diversity)
// =============================================================================

// Compile-time seed from __TIME__
constexpr uint32_t omnighost_compile_seed() {
    constexpr const char* time = __TIME__;
    uint32_t seed = 0;
    for (int i = 0; time[i]; ++i) {
        seed = seed * 31 + time[i];
    }
    return seed;
}

// Linear Congruential Generator for compile-time pseudo-random
constexpr uint32_t omnighost_lcg(uint32_t seed) {
    return seed * 1664525 + 1013904223;
}

// =============================================================================
// Integrity Check Types
// =============================================================================

enum class IntegrityStatus : uint8_t {
    Valid = 0,
    Modified = 1,
    DebuggerDetected = 2,
    MemoryCorrupted = 3,
    InvalidSignature = 4,
    TimestampMismatch = 5,
    Unknown = 255
};

struct IntegrityReport {
    IntegrityStatus status;
    uint64_t expected_crc;
    uint64_t actual_crc;
    uintptr_t violation_address;
    const char* violation_type;
    uint64_t timestamp;
};

// =============================================================================
// Callback Types
// =============================================================================

using IntegrityViolationCallback = void(*)(const IntegrityReport&);
using DebuggerDetectedCallback = void(*)(uint32_t detection_method);
using IntegrityCheckCallback = void(*)(bool success);

#endif // OMNIGHOST_SECURITY_COMMON_H
