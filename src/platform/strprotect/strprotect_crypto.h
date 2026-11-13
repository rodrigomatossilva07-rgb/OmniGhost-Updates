#pragma once

// ============================================================================
// OmniGhost String Protection (OGSP) - compile-time crypto primitives.
//
// Implements the ChaCha20 stream cipher exactly as specified in RFC 8439
// (https://www.rfc-editor.org/rfc/rfc8439) so that:
//   * string literals are encrypted during constexpr evaluation (the plaintext
//     exists only inside the compiler, never in the binary);
//   * decryption happens lazily at runtime with the same algorithm.
//
// The key material comes from the generated header omni_strprotect_key.h
// (produced by src\project\tools\Protect-Strings.ps1). When that header has
// not been generated (e.g. standalone tooling builds) a deterministic dev
// fallback is used; Release/Publish builds always generate the real key.
//
// NOTE: this raises the bar for static extraction, it is not absolute
// protection. See src\project\config\strings\README.md for the threat model.
// ============================================================================

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#if __has_include("omni_strprotect_key.h")
#include "omni_strprotect_key.h"
#define OGSP_HAS_BUILD_KEY 1
#else
#define OGSP_HAS_BUILD_KEY 0
#endif

namespace ogs::detail {

using Word = std::uint32_t;

constexpr std::uint8_t ToByte(char value) noexcept
{
    return static_cast<std::uint8_t>(static_cast<unsigned char>(value));
}

constexpr char ToChar(std::uint8_t value) noexcept
{
    return static_cast<char>(static_cast<unsigned char>(value));
}

inline constexpr Word kRotl(Word value, int count) noexcept
{
    return (value << count) | (value >> (32 - count));
}

constexpr void QuarterRound(Word& a, Word& b, Word& c, Word& d) noexcept
{
    a += b; d ^= a; d = kRotl(d, 16);
    c += d; b ^= c; b = kRotl(b, 12);
    a += b; d ^= a; d = kRotl(d, 8);
    c += d; b ^= c; b = kRotl(b, 7);
}

struct ChaChaState {
    std::array<Word, 16> w{};
};

constexpr ChaChaState InitChaChaState(const std::array<Word, 8>& key, Word counter,
                                      const std::array<Word, 3>& nonce) noexcept
{
    ChaChaState state{};
    state.w[0] = 0x61707865u;
    state.w[1] = 0x3320646eu;
    state.w[2] = 0x79622d32u;
    state.w[3] = 0x6b206574u;
    for (int i = 0; i < 8; ++i)
        state.w[static_cast<std::size_t>(4) + static_cast<std::size_t>(i)] = key[static_cast<std::size_t>(i)];
    state.w[12] = counter;
    state.w[13] = nonce[0];
    state.w[14] = nonce[1];
    state.w[15] = nonce[2];
    return state;
}

constexpr ChaChaState ChaChaInnerBlock(ChaChaState state) noexcept
{
    for (int round = 0; round < 10; ++round) {
        QuarterRound(state.w[0], state.w[4], state.w[8], state.w[12]);
        QuarterRound(state.w[1], state.w[5], state.w[9], state.w[13]);
        QuarterRound(state.w[2], state.w[6], state.w[10], state.w[14]);
        QuarterRound(state.w[3], state.w[7], state.w[11], state.w[15]);
        QuarterRound(state.w[0], state.w[5], state.w[10], state.w[15]);
        QuarterRound(state.w[1], state.w[6], state.w[11], state.w[12]);
        QuarterRound(state.w[2], state.w[7], state.w[8], state.w[13]);
        QuarterRound(state.w[3], state.w[4], state.w[9], state.w[14]);
    }
    return state;
}

constexpr std::uint8_t SerializeByte(const ChaChaState& input, const ChaChaState& rounded,
                                     std::size_t index) noexcept
{
    const Word word = rounded.w[index / 4] + input.w[index / 4];
    return static_cast<std::uint8_t>(word >> (static_cast<int>(index % 4) * 8));
}

// XORs `length` bytes of `in` into `out` with the ChaCha20 keystream.
// Valid in constexpr evaluation and at runtime (no reinterpret_cast).
constexpr void XorKeystream(const std::array<Word, 8>& key, Word counter,
                            const std::array<Word, 3>& nonce,
                            const char* in, char* out, std::size_t length) noexcept
{
    std::size_t offset = 0;
    Word block = counter;
    while (offset < length) {
        const ChaChaState input = InitChaChaState(key, block, nonce);
        const ChaChaState rounded = ChaChaInnerBlock(input);
        const std::size_t chunk = (length - offset) < 64 ? (length - offset) : 64;
        for (std::size_t i = 0; i < chunk; ++i) {
            out[offset + i] = ToChar(static_cast<std::uint8_t>(
                ToByte(in[offset + i]) ^ SerializeByte(input, rounded, i)));
        }
        offset += chunk;
        ++block;
    }
}

// ---------------------------------------------------------------------------
// Fingerprint / nonce derivation hashes (FNV-1a 64 + SplitMix64).
// Nonces are derived from the string CONTENT plus per-build salts, never from
// file paths or __LINE__, so identical builds stay reproducible.
// ---------------------------------------------------------------------------

constexpr std::uint64_t Fnv1a64(const char* data, std::size_t length) noexcept
{
    std::uint64_t hash = 0xcbf29ce484222325ull;
    for (std::size_t i = 0; i < length; ++i) {
        hash ^= static_cast<std::uint64_t>(ToByte(data[i]));
        hash *= 0x100000001b3ull;
    }
    return hash;
}

constexpr std::uint64_t Fnv1a64(std::string_view text) noexcept
{
    return Fnv1a64(text.data(), text.size());
}

constexpr std::uint64_t SplitMix64(std::uint64_t value) noexcept
{
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}

// ---------------------------------------------------------------------------
// Key material reconstruction.
// ---------------------------------------------------------------------------

#if OGSP_HAS_BUILD_KEY

constexpr std::array<Word, 8> ReconstructKey() noexcept
{
    std::array<Word, 8> key{};
    for (std::size_t i = 0; i < 4; ++i) {
        const std::uint64_t part = buildkeys::kKeyPartsMasked[i] ^ buildkeys::kKeyMasks[i];
        const Word low = static_cast<Word>(part & 0xffffffffull);
        const Word high = static_cast<Word>((part >> 32) & 0xffffffffull);
        const std::size_t lowIndex = static_cast<std::size_t>(buildkeys::kKeyPermutation[i * 2]);
        const std::size_t highIndex = static_cast<std::size_t>(buildkeys::kKeyPermutation[i * 2 + 1]);
        key[lowIndex] = low;
        key[highIndex] = high;
    }
    return key;
}

constexpr std::array<Word, 3> DeriveNonce(std::uint64_t contentHash, std::size_t length) noexcept
{
    const std::uint64_t low = SplitMix64(contentHash ^ buildkeys::kNonceSaltA);
    const std::uint64_t high = SplitMix64(contentHash ^ (static_cast<std::uint64_t>(length) << 40) ^
                                          buildkeys::kNonceSaltB);
    return {static_cast<Word>(low & 0xffffffffull), static_cast<Word>(high & 0xffffffffull),
            static_cast<Word>((low >> 32) ^ (high >> 32))};
}

#else // fallback for standalone tooling builds; Release/Publish always generate the real key

constexpr std::array<Word, 8> ReconstructKey() noexcept
{
    return {0x0b1ec7a5u, 0x6f3d9128u, 0x44782c1du, 0x95e0a63bu,
            0x2b74d9f1u, 0x8ac35602u, 0xd14e77c9u, 0x590b3e86u};
}

constexpr std::array<Word, 3> DeriveNonce(std::uint64_t contentHash, std::size_t length) noexcept
{
    const std::uint64_t low = SplitMix64(contentHash ^ 0x0b5e17a35ull);
    const std::uint64_t high = SplitMix64(contentHash ^ (static_cast<std::uint64_t>(length) << 40) ^
                                          0x9d2c6f1e8ull);
    return {static_cast<Word>(low & 0xffffffffull), static_cast<Word>(high & 0xffffffffull),
            static_cast<Word>((low >> 32) ^ (high >> 32))};
}

#endif

// ---------------------------------------------------------------------------
// Encrypted literal blob (structural type usable as a C++20 class NTTP).
// kBuf is the protected byte count INCLUDING the terminating NUL (>= 1).
// ---------------------------------------------------------------------------

template <std::size_t BufN>
struct CipherText {
    static constexpr std::size_t kBuf = BufN;
    static constexpr std::size_t kLen = BufN - 1; // content length without the NUL

    std::array<char, BufN> data{};
    std::array<Word, 3> nonce{};
};

// Encrypts a string literal (content plus trailing NUL) at compile time.
template <std::size_t N>
constexpr CipherText<N> EncryptLiteral(const char (&literal)[N]) noexcept
{
    CipherText<N> result{};
    result.nonce = DeriveNonce(Fnv1a64(literal, N), N);
    const std::array<Word, 8> key = ReconstructKey();
    XorKeystream(key, 0x1u, result.nonce, literal, result.data.data(), N);
    return result;
}

// Pure decryption of a blob (constexpr-evaluable; used by the runtime wrapper
// in strprotect.h and cross-checked by the selftest).
template <std::size_t N>
constexpr std::array<char, N> DecryptCipher(const CipherText<N>& blob) noexcept
{
    std::array<char, N> out{};
    const std::array<Word, 8> key = ReconstructKey();
    XorKeystream(key, 0x1u, blob.nonce, blob.data.data(), out.data(), N);
    return out;
}

} // namespace ogs::detail
