#pragma once


























#include "strprotect_crypto.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ogs {

namespace detail {


template <std::size_t N>
std::array<char, N> DecryptRuntimeText(const ogs::detail::CipherText<N>& blob) noexcept;
}



void ZeroRange(void* data, std::size_t bytes) noexcept;


std::wstring ToWide(std::string_view utf8);





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

    // Implicit conversions to the standard text containers/cursors. The
    // plaintext is still produced lazily at runtime; these simply let protected
    // literals flow into existing APIs that take std::string/string_view.
    operator std::string_view() const noexcept { return view(); }
    operator std::string() const { return std::string(view()); }
};




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
    operator std::wstring_view() const noexcept { return view(); }
    operator std::wstring() const { return Storage(); }
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





class SecretText {
public:
    template <std::size_t N>
    explicit SecretText(const ogs::detail::CipherText<N>& blob)
        : buf_(N, '\0')
    {


        const volatile char* cipher = blob.data.data();
        for (std::size_t i = 0; i < N; ++i)
            buf_[i] = cipher[i];

        std::array<ogs::detail::Word, 3> nonce{};
        for (std::size_t i = 0; i < 3; ++i)
            nonce[i] = blob.nonce[i];
        ogs::detail::XorKeystream(ogs::detail::ReconstructKey(), 0x1u, nonce,
                                  buf_.data(), buf_.data(), N);
        len_ = N - 1;
    }



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





struct LocPackRef {
    using GetFn = const std::array<const char*, 7>& (*)();
    GetFn get = nullptr;
};





bool SelfTest();

namespace detail {




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

}
}





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
