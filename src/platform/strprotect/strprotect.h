#pragma once

// ============================================================================
// OmniGhost String Protection (OGSP) - public API.
//
// Usage model
// -----------
//   * OGS_LIT("text")   -> ogs::Lit<...>: lazily decrypted ONCE, pointer stays
//                          valid for the process lifetime. Use for UI text,
//                          ImGui labels, localization values, identifiers.
//   * OGS_ONE("text")   -> ogs::SecretText: RAII buffer decrypted on
//                          construction and securely zeroed when the full
//                          expression ends. Use for URLs, tokens, credentials
//                          and one-shot protocol data. Do NOT store the
//                          pointer beyond the statement.
//   * OGS_PACK6/7(...)  -> ogs::LocPackRef: localization pack (6-7 languages)
//                          consumed by src\window\localization.cpp.
//
// The plaintext only exists inside the compiler during constexpr evaluation;
// the binary contains ChaCha20 ciphertext (RFC 8439). Keys are generated per
// build by src\project\tools\Protect-Strings.ps1 (omni_strprotect_key.h).
//
// Enforcement of unprotected eligible strings is handled by the same tool:
// Release/Publish builds fail with OGPS001 when sensitive areas contain bare
// string literals. See src\project\config\strings\README.md.
// ============================================================================

#include "strprotect_crypto.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ogs {

namespace detail {
// Forward declaration: Lit below lazily decrypts through this runtime helper
// (defined after Lit; deliberately non-constexpr, see its definition).
template <std::size_t N>
std::array<char, N> DecryptRuntimeText(const ogs::detail::CipherText<N>& blob) noexcept;
} // namespace detail


// Securely zeroes a memory range (volatile writes; not optimized away).
void ZeroRange(void* data, std::size_t bytes) noexcept;

// UTF-8 -> UTF-16 conversion helper (mirrors platform\text_encoding.h).
std::wstring ToWide(std::string_view utf8);

// ---------------------------------------------------------------------------
// ogs::Lit - lazily decrypted, process-lifetime text.
// ---------------------------------------------------------------------------

template <ogs::detail::CipherText Blob>
class Lit {
public:
    constexpr Lit() noexcept = default;

    const char* c_str() const noexcept
    {
        static const std::array<char, Blob.kBuf> text = detail::DecryptRuntimeText(Blob);
        return text.data();
    }

    std::string_view view() const noexcept
    {
        return std::string_view{c_str(), Blob.kLen};
    }

    operator const char*() const noexcept { return c_str(); }
};

// String concatenation helpers. std::operator+ templates cannot deduce from a
// class-type left operand, so these free operators make Lit usable directly
// in concatenation expressions (Lit + string, char* + Lit, Lit + Lit).
template <ogs::detail::CipherText Blob>
std::string operator+(const Lit<Blob>& lit, const std::string& rhs)
{
    return std::string(lit.c_str()) + rhs;
}

template <ogs::detail::CipherText Blob>
std::string operator+(const Lit<Blob>& lit, const char* rhs)
{
    return std::string(lit.c_str()) + rhs;
}

template <ogs::detail::CipherText Blob>
std::string operator+(const char* lhs, const Lit<Blob>& lit)
{
    return std::string(lhs) + lit.c_str();
}

template <ogs::detail::CipherText Blob>
std::string operator+(const std::string& lhs, const Lit<Blob>& lit)
{
    return lhs + lit.c_str();
}

template <ogs::detail::CipherText BlobA, ogs::detail::CipherText BlobB>
std::string operator+(const Lit<BlobA>& a, const Lit<BlobB>& b)
{
    return std::string(a.c_str()) + b.c_str();
}

// ---------------------------------------------------------------------------
// ogs::WideLit - lazily decrypted, process-lifetime wide (UTF-16) text.
// The ciphertext stores the UTF-8 form; conversion happens once, on first use.
// ---------------------------------------------------------------------------

template <ogs::detail::CipherText Blob>
class WideLit {
public:
    constexpr WideLit() noexcept = default;

    static const std::wstring& Storage() noexcept
    {
        static const std::wstring text = ToWide(Lit<Blob>{}.view());
        return text;
    }

    const wchar_t* c_str() const noexcept { return Storage().c_str(); }
    std::wstring_view view() const noexcept { return Storage(); }
    operator const wchar_t*() const noexcept { return c_str(); }
};

template <ogs::detail::CipherText Blob>
std::wstring operator+(const WideLit<Blob>& lit, const std::wstring& rhs)
{
    return std::wstring(lit.c_str()) + rhs;
}

template <ogs::detail::CipherText Blob>
std::wstring operator+(const WideLit<Blob>& lit, const wchar_t* rhs)
{
    return std::wstring(lit.c_str()) + rhs;
}

template <ogs::detail::CipherText Blob>
std::wstring operator+(const wchar_t* lhs, const WideLit<Blob>& lit)
{
    return std::wstring(lhs) + lit.c_str();
}

template <ogs::detail::CipherText Blob>
std::wstring operator+(const std::wstring& lhs, const WideLit<Blob>& lit)
{
    return lhs + lit.c_str();
}

template <ogs::detail::CipherText BlobA, ogs::detail::CipherText BlobB>
std::wstring operator+(const WideLit<BlobA>& a, const WideLit<BlobB>& b)
{
    return std::wstring(a.c_str()) + b.c_str();
}

// ---------------------------------------------------------------------------
// ogs::SecretText - RAII one-shot text, zeroized on destruction.
// ---------------------------------------------------------------------------

class SecretText {
public:
    template <std::size_t N>
    explicit SecretText(const ogs::detail::CipherText<N>& blob)
        : buf_(N, '\0')
    {
        // Volatile reads prevent the optimizer from constant-folding the
        // plaintext; the ciphertext is fetched from memory at runtime.
        const volatile char* cipher = blob.data.data();
        for (std::size_t i = 0; i < N; ++i)
            buf_[i] = cipher[i];
        // XOR keystream in place: ChaCha20 encryption == decryption.
        std::array<ogs::detail::Word, 3> nonce{};
        for (std::size_t i = 0; i < 3; ++i)
            nonce[i] = blob.nonce[i];
        ogs::detail::XorKeystream(ogs::detail::ReconstructKey(), 0x1u, nonce,
                                  buf_.data(), buf_.data(), N);
        len_ = N - 1;
    }

    // Wraps plaintext obtained at runtime (e.g. DPAPI unseal) under the same
    // zeroization-on-destruction discipline.
    explicit SecretText(std::string_view plaintext)
        : buf_(plaintext.begin(), plaintext.end()), len_(plaintext.size()) {}

    ~SecretText() { Wipe(); }

    SecretText(const SecretText&) = delete;
    SecretText& operator=(const SecretText&) = delete;

    SecretText(SecretText&& other) noexcept
        : buf_(std::move(other.buf_)), len_(other.len_)
    {
        other.Invalidate();
    }

    SecretText& operator=(SecretText&& other) noexcept
    {
        if (this != &other) {
            Wipe();
            buf_ = std::move(other.buf_);
            len_ = other.len_;
            other.Invalidate();
        }
        return *this;
    }

    const char* c_str() const noexcept { return buf_.empty() ? "" : buf_.data(); }
    std::string_view view() const noexcept { return std::string_view{c_str(), len_}; }
    std::wstring wide() const { return ToWide(view()); }

private:
    void Wipe() noexcept
    {
        if (!buf_.empty())
            ZeroRange(buf_.data(), buf_.size());
        len_ = 0;
        buf_.clear();
    }

    void Invalidate() noexcept
    {
        if (!buf_.empty())
            ZeroRange(buf_.data(), buf_.size());
        len_ = 0;
        buf_.clear();
    }

    std::vector<char> buf_;
    std::size_t len_ = 0;
};

// ---------------------------------------------------------------------------
// ogs::LocPackRef - localization pack reference (7 language slots).
// ---------------------------------------------------------------------------

struct LocPackRef {
    using GetFn = const std::array<const char*, 7>& (*)();
    GetFn get = nullptr;
};

// ---------------------------------------------------------------------------
// Self-test (RFC 8439 vectors + roundtrips + concurrency smoke).
// ---------------------------------------------------------------------------

bool SelfTest();

namespace detail {

// Runtime decryptor used by Lit. Deliberately NOT constexpr: the ciphertext is
// read through a volatile pointer so the optimizer cannot constant-fold the
// decrypted text into .rdata.
template <std::size_t N>
std::array<char, N> DecryptRuntimeText(const ogs::detail::CipherText<N>& blob) noexcept
{
    ogs::detail::CipherText<N> local{};
    const volatile char* source = blob.data.data();
    for (std::size_t i = 0; i < N; ++i)
        local.data[i] = source[i];
    for (std::size_t i = 0; i < 3; ++i)
        local.nonce[i] = blob.nonce[i];
    return ogs::detail::DecryptCipher(local);
}

template <ogs::detail::CipherText B0, ogs::detail::CipherText B1, ogs::detail::CipherText B2,
          ogs::detail::CipherText B3, ogs::detail::CipherText B4, ogs::detail::CipherText B5>
struct PackTable6 {
    static const std::array<const char*, 7>& Get() noexcept
    {
        static const std::array<const char*, 7> table = {Lit<B0>{}.c_str(), Lit<B1>{}.c_str(),
                                                         Lit<B2>{}.c_str(), Lit<B3>{}.c_str(),
                                                         Lit<B4>{}.c_str(), Lit<B5>{}.c_str(),
                                                         nullptr};
        return table;
    }
};

template <ogs::detail::CipherText B0, ogs::detail::CipherText B1, ogs::detail::CipherText B2,
          ogs::detail::CipherText B3, ogs::detail::CipherText B4, ogs::detail::CipherText B5,
          ogs::detail::CipherText B6>
struct PackTable7 {
    static const std::array<const char*, 7>& Get() noexcept
    {
        static const std::array<const char*, 7> table = {Lit<B0>{}.c_str(), Lit<B1>{}.c_str(),
                                                         Lit<B2>{}.c_str(), Lit<B3>{}.c_str(),
                                                         Lit<B4>{}.c_str(), Lit<B5>{}.c_str(),
                                                         Lit<B6>{}.c_str()};
        return table;
    }
};

} // namespace detail
} // namespace ogs

// ---------------------------------------------------------------------------
// Macros.
// ---------------------------------------------------------------------------

#define OGS_LIT(s) ::ogs::Lit<::ogs::detail::EncryptLiteral(s)>{}
#define OGS_ONE(s) ::ogs::SecretText(::ogs::detail::EncryptLiteral(s))
#define OGS_WIDE(s) ::ogs::WideLit<::ogs::detail::EncryptLiteral(s)>{}

#define OGS_PACK6(a, b, c, d, e, f) \
    ::ogs::LocPackRef{&::ogs::detail::PackTable6<::ogs::detail::EncryptLiteral(a),  \
                                                 ::ogs::detail::EncryptLiteral(b),  \
                                                 ::ogs::detail::EncryptLiteral(c),  \
                                                 ::ogs::detail::EncryptLiteral(d),  \
                                                 ::ogs::detail::EncryptLiteral(e),  \
                                                 ::ogs::detail::EncryptLiteral(f)>::Get}
#define OGS_PACK7(a, b, c, d, e, f, g) \
    ::ogs::LocPackRef{&::ogs::detail::PackTable7<::ogs::detail::EncryptLiteral(a),  \
                                                 ::ogs::detail::EncryptLiteral(b),  \
                                                 ::ogs::detail::EncryptLiteral(c),  \
                                                 ::ogs::detail::EncryptLiteral(d),  \
                                                 ::ogs::detail::EncryptLiteral(e),  \
                                                 ::ogs::detail::EncryptLiteral(f),  \
                                                 ::ogs::detail::EncryptLiteral(g)>::Get}
