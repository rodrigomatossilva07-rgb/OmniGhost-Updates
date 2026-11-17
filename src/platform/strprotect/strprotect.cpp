







#include "strprotect.h"

#include <atomic>
#include <cstdio>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace ogs {

void ZeroRange(void* data, std::size_t bytes) noexcept
{
    if (data == nullptr || bytes == 0)
        return;
    volatile unsigned char* bytes_ptr = static_cast<volatile unsigned char*>(data);
    for (std::size_t i = 0; i < bytes; ++i)
        bytes_ptr[i] = 0u;
}

std::wstring ToWide(std::string_view utf8)
{
    if (utf8.empty())
        return {};
    const int count = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                            static_cast<int>(utf8.size()), nullptr, 0);
    if (count <= 0)
        return {};
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    const int written = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                              static_cast<int>(utf8.size()), wide.data(), count);
    if (written <= 0)
        return {};
    return wide;
}

}

namespace {

using ogs::detail::Word;

constexpr std::array<Word, 8> Rfc8439Key() noexcept
{
    return {0x03020100u, 0x07060504u, 0x0b0a0908u, 0x0f0e0d0cu,
            0x13121110u, 0x17161514u, 0x1b1a1918u, 0x1f1e1d1cu};
}

constexpr std::array<Word, 3> Rfc8439BlockNonce() noexcept
{
    return {0x09000000u, 0x4a000000u, 0x00000000u};
}

constexpr std::array<Word, 3> Rfc8439CipherNonce() noexcept
{
    return {0x00000000u, 0x4a000000u, 0x00000000u};
}


constexpr std::array<std::uint8_t, 64> kRfc8439Block{
    0x10, 0xf1, 0xe7, 0xe4, 0xd1, 0x3b, 0x59, 0x15, 0x50, 0x0f, 0xdd, 0x1f, 0xa3, 0x20, 0x71, 0xc4,
    0xc7, 0xd1, 0xf4, 0xc7, 0x33, 0xc0, 0x68, 0x03, 0x04, 0x22, 0xaa, 0x9a, 0xc3, 0xd4, 0x6c, 0x4e,
    0xd2, 0x82, 0x64, 0x46, 0x07, 0x9f, 0xaa, 0x09, 0x14, 0xc2, 0xd7, 0x05, 0xd9, 0x8b, 0x02, 0xa2,
    0xb5, 0x12, 0x9c, 0xd1, 0xde, 0x16, 0x4e, 0xb9, 0xcb, 0xd0, 0x83, 0xe8, 0xa2, 0x50, 0x3c, 0x4e};


constexpr std::array<std::uint8_t, 114> kRfc8439Ciphertext{
    0x6e, 0x2e, 0x35, 0x9a, 0x25, 0x68, 0xf9, 0x80, 0x41, 0xba, 0x07, 0x28, 0xdd, 0x0d, 0x69, 0x81,
    0xe9, 0x7e, 0x7a, 0xec, 0x1d, 0x43, 0x60, 0xc2, 0x0a, 0x27, 0xaf, 0xcc, 0xfd, 0x9f, 0xae, 0x0b,
    0xf9, 0x1b, 0x65, 0xc5, 0x52, 0x47, 0x33, 0xab, 0x8f, 0x59, 0x3d, 0xab, 0xcd, 0x62, 0xb3, 0x57,
    0x16, 0x39, 0xd6, 0x24, 0xe6, 0x51, 0x52, 0xab, 0x8f, 0x53, 0x0c, 0x35, 0x9f, 0x08, 0x61, 0xd8,
    0x07, 0xca, 0x0d, 0xbf, 0x50, 0x0d, 0x6a, 0x61, 0x56, 0xa3, 0x8e, 0x08, 0x8a, 0x22, 0xb6, 0x5e,
    0x52, 0xbc, 0x51, 0x4d, 0x16, 0xcc, 0xf8, 0x06, 0x81, 0x8c, 0xe9, 0x1a, 0xb7, 0x79, 0x37, 0x36,
    0x5a, 0xf9, 0x0b, 0xbf, 0x74, 0xa3, 0x5b, 0xe6, 0xb4, 0x0b, 0x8e, 0xed, 0xf2, 0x78, 0x5e, 0x42,
    0x87, 0x4d};

constexpr std::string_view kRfc8439Plaintext =
    "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the "
    "future, sunscreen would be it.";

bool CompareBytes(const char* actual, const std::array<std::uint8_t, 64>& expected) noexcept
{
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (ogs::detail::ToByte(actual[i]) != expected[i])
            return false;
    }
    return true;
}

bool TestRfc8439Block() noexcept
{
    std::array<char, 64> keystream{};
    ogs::detail::XorKeystream(Rfc8439Key(), 1u, Rfc8439BlockNonce(), keystream.data(),
                              keystream.data(), keystream.size());
    return CompareBytes(keystream.data(), kRfc8439Block);
}

bool TestRfc8439Cipher() noexcept
{
    std::array<char, kRfc8439Ciphertext.size()> cipher{};
    const std::size_t plainLen = kRfc8439Plaintext.size();
    if (plainLen != cipher.size())
        return false;
    for (std::size_t i = 0; i < plainLen; ++i)
        cipher[i] = kRfc8439Plaintext[i];
    ogs::detail::XorKeystream(Rfc8439Key(), 1u, Rfc8439CipherNonce(), cipher.data(), cipher.data(),
                              cipher.size());
    for (std::size_t i = 0; i < cipher.size(); ++i) {
        if (ogs::detail::ToByte(cipher[i]) != kRfc8439Ciphertext[i])
            return false;
    }

    ogs::detail::XorKeystream(Rfc8439Key(), 1u, Rfc8439CipherNonce(), cipher.data(), cipher.data(),
                              cipher.size());
    for (std::size_t i = 0; i < plainLen; ++i) {
        if (cipher[i] != kRfc8439Plaintext[i])
            return false;
    }
    return true;
}

bool TestLiteralRoundtrip() noexcept
{
    constexpr auto blob = ogs::detail::EncryptLiteral("OmniGhost ✓ ünïcøde UTF-8 roundtrip");
    constexpr auto plain = ogs::detail::DecryptCipher(blob);
    constexpr std::string_view expected{"OmniGhost ✓ ünïcøde UTF-8 roundtrip"};
    if (std::string_view(plain.data(), blob.kLen) != expected)
        return false;


    const auto runtime = ogs::detail::DecryptRuntimeText(blob);
    for (std::size_t i = 0; i < blob.kBuf; ++i) {
        if (runtime[i] != plain[i])
            return false;
    }

    const char* lit = OGS_CSTR("Lazy decryption stays cached and stable");
    if (std::string_view(lit) != "Lazy decryption stays cached and stable")
        return false;
    if (lit != OGS_LIT("Lazy decryption stays cached and stable"))
        return false;

    const auto& wide = ogs::ToWide(std::string_view{OGS_CSTR("Acentuação: ção, ünïcøde")});
    if (wide.empty())
        return false;
    return true;
}

bool TestSecretLifetime() noexcept
{
    const char* leaked = nullptr;
    {
        ogs::SecretText secret(ogs::detail::EncryptLiteral("https://one-shot.example/token"));
        leaked = secret.c_str();
        if (std::string_view(secret.view()) != "https://one-shot.example/token")
            return false;
        if (secret.wide() != ogs::ToWide("https://one-shot.example/token"))
            return false;
    }


    (void)leaked;

    ogs::SecretText first(ogs::detail::EncryptLiteral("first secret value"));
    ogs::SecretText second(ogs::detail::EncryptLiteral("second secret value"));
    second = std::move(first);
    if (second.view() != "first secret value")
        return false;
    if (!first.view().empty())
        return false;
    return true;
}

bool TestLocPack() noexcept
{
    const auto& pack6 = OGS_PACK6("pt6", "en6", "de6", "es6", "fr6", "it6").get();
    if (std::string_view(pack6[0]) != "pt6" || std::string_view(pack6[5]) != "it6")
        return false;
    if (pack6[6] != nullptr)
        return false;

    const auto& pack7 = OGS_PACK7("pt7", "en7", "de7", "es7", "fr7", "it7", "ru7").get();
    if (std::string_view(pack7[6]) != "ru7")
        return false;
    return true;
}

bool TestWideLit() noexcept
{
    const wchar_t* wide = OGS_WCSTR("Wide UTF-16: ção ✓");
    if (std::wstring_view(wide) != ogs::ToWide("Wide UTF-16: ção ✓"))
        return false;
    if (wide != OGS_WIDE("Wide UTF-16: ção ✓"))
        return false;
    const auto& stored = ogs::WideLit<ogs::detail::EncryptLiteral("stable storage")>::Storage();
    if (stored != ogs::ToWide("stable storage"))
        return false;
    return true;
}

bool TestConcurrency() noexcept
{
    const char* lit = OGS_CSTR("Concurrent first-touch must stay consistent");
    constexpr std::string_view expected{"Concurrent first-touch must stay consistent"};
    std::atomic<int> failures{0};
    std::atomic<int> ready{0};
    std::vector<std::thread> workers;
    workers.reserve(8);
    for (int t = 0; t < 8; ++t) {
        workers.emplace_back([&failures, &ready, lit]() {
            ++ready;
            while (ready.load() < 8) {

            }
            for (int i = 0; i < 2000; ++i) {
                if (std::string_view(lit) != expected)
                    failures.fetch_add(1);
            }
        });
    }
    for (auto& worker : workers)
        worker.join();
    return failures.load() == 0;
}

bool TestZeroRange() noexcept
{
    std::array<char, 32> buffer{};
    for (std::size_t i = 0; i < buffer.size(); ++i)
        buffer[i] = static_cast<char>(i + 1);
    ogs::ZeroRange(buffer.data(), buffer.size());
    for (const char value : buffer) {
        if (value != '\0')
            return false;
    }
    return true;
}

}

namespace ogs {

bool SelfTest()
{
    bool ok = true;
#ifdef OGSP_SELFTEST_VERBOSE
    const auto report = [](const char* name, bool result) {
        std::printf("[OGSP] %-28s %s\n", name, result ? "OK" : "FAIL");
        std::fflush(stdout);
    };
    bool r = TestRfc8439Block(); ok = r && ok; report("rfc8439_block", r);
    r = TestRfc8439Cipher(); ok = r && ok; report("rfc8439_cipher", r);
    r = TestLiteralRoundtrip(); ok = r && ok; report("literal_roundtrip", r);
    r = TestSecretLifetime(); ok = r && ok; report("secret_lifetime", r);
    r = TestLocPack(); ok = r && ok; report("loc_pack", r);
    r = TestWideLit(); ok = r && ok; report("wide_lit", r);
    r = TestConcurrency(); ok = r && ok; report("concurrency", r);
    r = TestZeroRange(); ok = r && ok; report("zero_range", r);
#else
    ok = TestRfc8439Block() && ok;
    ok = TestRfc8439Cipher() && ok;
    ok = TestLiteralRoundtrip() && ok;
    ok = TestSecretLifetime() && ok;
    ok = TestLocPack() && ok;
    ok = TestWideLit() && ok;
    ok = TestConcurrency() && ok;
    ok = TestZeroRange() && ok;
#endif
    return ok;
}

}

