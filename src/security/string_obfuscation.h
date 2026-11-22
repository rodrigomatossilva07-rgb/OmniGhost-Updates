#pragma once

// String Obfuscation Module
// Compile-time and runtime string obfuscation with XOR encryption

#ifndef OMNIGHOST_STRING_OBFUSCATION_H
#define OMNIGHOST_STRING_OBFUSCATION_H

#include "security_common.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace omnighost::security {

// =============================================================================
// Compile-Time XOR String Obfuscation
// =============================================================================

// XOR key (can be customized per build)
#ifndef OMNIGHOST_STR_OBFUSCATE_KEY
#define OMNIGHOST_STR_OBFUSCATE_KEY 0x5A
#endif

// Compile-time string length
template<size_t N>
constexpr size_t str_len(const char (&)[N]) { return N - 1; }

// Compile-time XOR encryption
template<size_t N>
constexpr auto xor_encrypt(const char (&str)[N], uint8_t key = OMNIGHOST_STR_OBFUSCATE_KEY) {
    std::array<char, N> result{};
    for (size_t i = 0; i < N; ++i) {
        result[i] = static_cast<char>(str[i] ^ key);
    }
    return result;
}

// Compile-time XOR decryption (same as encrypt for XOR)
template<size_t N>
constexpr auto xor_decrypt(const std::array<char, N>& enc, uint8_t key = OMNIGHOST_STR_OBFUSCATE_KEY) {
    std::array<char, N> result{};
    for (size_t i = 0; i < N; ++i) {
        result[i] = static_cast<char>(enc[i] ^ key);
    }
    return result;
}

// =============================================================================
// Obfuscated String Class
// =============================================================================

template<size_t N>
class RuntimeObfuscatedString {
    alignas(16) std::array<char, N> data_;
    bool decrypted_ = false;
    const uint8_t key_;

public:
    // Constructor from encrypted array
    constexpr RuntimeObfuscatedString(const std::array<char, N>& encrypted, uint8_t key = OMNIGHOST_STR_OBFUSCATE_KEY)
        : data_(encrypted), key_(key) {}

    // Decrypt and return pointer (lazy decryption)
    OMNIGHOST_SECURE_INLINE const char* c_str() {
        if (!decrypted_) {
            decrypt();
        }
        return data_.data();
    }

    // Get length
    constexpr size_t size() const { return N - 1; }

    // Explicit decrypt
    OMNIGHOST_SECURE_INLINE void decrypt() {
        if (!decrypted_) {
            for (size_t i = 0; i < N - 1; ++i) {
                data_[i] ^= key_;
            }
            decrypted_ = true;
        }
    }

    // Re-encrypt
    OMNIGHOST_SECURE_INLINE void encrypt() {
        if (decrypted_) {
            for (size_t i = 0; i < N - 1; ++i) {
                data_[i] ^= key_;
            }
            decrypted_ = false;
        }
    }

    // Auto-encrypt on destruction
    ~RuntimeObfuscatedString() {
        if (decrypted_) {
            for (size_t i = 0; i < N - 1; ++i) {
                data_[i] ^= key_;
            }
        }
    }

    // Implicit conversion
    OMNIGHOST_SECURE_INLINE operator const char*() { return c_str(); }

    // Explicit access
    OMNIGHOST_SECURE_INLINE const char* get() { return c_str(); }

    // Secure comparison
    OMNIGHOST_SECURE_INLINE bool equals(const char* other) const {
        if (!other) return false;
        const char* self = c_str();
        size_t len = 0;
        while (other[len]) ++len;
        return secure_memcmp(self, other, len) == 0;
    }
};

// =============================================================================
// Macro for Easy String Obfuscation
// =============================================================================

// Usage: OBFUSCATE("Hello World") -> returns const char*
#ifndef OMNIGHOST_OBFUSCATE
#define OMNIGHOST_OBFUSCATE(str) \
    []() -> const char* { \
        constexpr auto encrypted = xor_encrypt(str); \
        static RuntimeObfuscatedString<sizeof(str)> obf(encrypted); \
        return obf.c_str(); \
    }()
#endif

// For wide strings
template<size_t N>
class ObfuscatedWString {
    alignas(16) std::array<wchar_t, N> data_;
    bool decrypted_ = false;
    const uint16_t key_;

public:
    constexpr ObfuscatedWString(const std::array<wchar_t, N>& encrypted, uint16_t key = 0x5A5A)
        : data_(encrypted), key_(key) {}

    OMNIGHOST_SECURE_INLINE const wchar_t* c_str() {
        if (!decrypted_) {
            for (size_t i = 0; i < N - 1; ++i) {
                data_[i] ^= key_;
            }
            decrypted_ = true;
        }
        return data_.data();
    }

    constexpr size_t size() const { return N - 1; }

    ~ObfuscatedWString() {
        if (decrypted_) {
            for (size_t i = 0; i < N - 1; ++i) {
                data_[i] ^= key_;
            }
        }
    }

    OMNIGHOST_SECURE_INLINE operator const wchar_t*() { return c_str(); }
};

#define OMNIGHOST_OBFUSCATE_W(str) \
    []() -> const wchar_t* { \
        constexpr std::array<wchar_t, sizeof(str)/sizeof(wchar_t)> encrypted = [](){ \
            std::array<wchar_t, sizeof(str)/sizeof(wchar_t)> result{}; \
            for (size_t i = 0; i < sizeof(str)/sizeof(wchar_t); ++i) { \
                result[i] = str[i] ^ 0x5A5A; \
            } \
            return result; \
        }(); \
        static ObfuscatedWString<sizeof(str)/sizeof(wchar_t)> obf(encrypted); \
        return obf.c_str(); \
    }()

// =============================================================================
// Runtime String Obfuscation (for dynamic strings)
// =============================================================================

class RuntimeStringObfuscator {
    std::vector<uint8_t> key_;

public:
    explicit RuntimeStringObfuscator(std::string_view key = "OmniGhost2024") {
        key_.assign(key.begin(), key.end());
    }

    // XOR encrypt/decrypt (same operation)
    std::string xor_string(std::string_view str) const {
        std::string result;
        result.resize(str.size());
        for (size_t i = 0; i < str.size(); ++i) {
            result[i] = static_cast<char>(str[i] ^ key_[i % key_.size()]);
        }
        return result;
    }

    // In-place XOR
    void xor_inplace(std::string& str) const {
        for (size_t i = 0; i < str.size(); ++i) {
            str[i] ^= key_[i % key_.size()];
        }
    }

    // Generate random key
    static std::vector<uint8_t> generate_key(size_t length = 32) {
        std::vector<uint8_t> key(length);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(1, 255);
        for (auto& b : key) b = static_cast<uint8_t>(dis(gen));
        return key;
    }
};

// =============================================================================
// Compile-Time Hash for String Identification
// =============================================================================

// FNV-1a hash at compile time
constexpr uint64_t fnv1a_hash(const char* str, uint64_t hash = 14695981039346656037ULL) {
    return (*str) ? fnv1a_hash(str + 1, (hash ^ static_cast<uint64_t>(*str)) * 1099511628211ULL) : hash;
}

// Usage: OMNIGHOST_STR_HASH("my_string")
#define OMNIGHOST_STR_HASH(str) fnv1a_hash(str)

// String ID type
using StringID = uint64_t;

// =============================================================================
// Secure String Pool (for frequently used strings)
// =============================================================================

class SecureStringPool {
    struct Entry {
        StringID id;
        std::vector<char> encrypted;
        uint8_t key;
    };

    std::vector<Entry> strings_;
    uint8_t global_key_;

public:
    SecureStringPool() : global_key_(OMNIGHOST_STR_OBFUSCATE_KEY) {}

    // Add string to pool
    StringID add_string(std::string_view str, uint8_t key = 0) {
        if (key == 0) key = global_key_;

        Entry entry;
        entry.id = fnv1a_hash(str.data());
        entry.key = key;
        entry.encrypted.resize(str.size());
        for (size_t i = 0; i < str.size(); ++i) {
            entry.encrypted[i] = static_cast<char>(str[i] ^ key);
        }

        strings_.push_back(std::move(entry));
        return strings_.back().id;
    }

    // Get decrypted string
    std::string get_string(StringID id) const {
        auto it = std::find_if(strings_.begin(), strings_.end(),
            [id](const Entry& e) { return e.id == id; });
        if (it == strings_.end()) return {};

        std::string result;
        result.resize(it->encrypted.size());
        for (size_t i = 0; i < it->encrypted.size(); ++i) {
            result[i] = static_cast<char>(it->encrypted[i] ^ it->key);
        }
        return result;
    }

    // Get string view (decrypts to internal buffer)
    const char* get_cstr(StringID id) {
        auto it = std::find_if(strings_.begin(), strings_.end(),
            [id](const Entry& e) { return e.id == id; });
        if (it == strings_.end()) return nullptr;

        // Decrypt to static buffer (thread-local)
        thread_local std::string buffer;
        buffer.resize(it->encrypted.size());
        for (size_t i = 0; i < it->encrypted.size(); ++i) {
            buffer[i] = static_cast<char>(it->encrypted[i] ^ it->key);
        }
        return buffer.c_str();
    }

    // Check if string exists
    bool has_string(StringID id) const {
        return std::any_of(strings_.begin(), strings_.end(),
            [id](const Entry& e) { return e.id == id; });
    }
};

// Global string pool instance
inline SecureStringPool& global_string_pool() {
    static SecureStringPool pool;
    return pool;
}

// =============================================================================
// Secure String Macros
// =============================================================================

// Obfuscate string literal and add to pool
#define OMNIGHOST_SECURE_STR(str) \
    (global_string_pool().get_cstr(global_string_pool().add_string(str)))

// For wide strings
#define OMNIGHOST_SECURE_WSTR(str) L##str // Placeholder for wide

// =============================================================================
// Compile-Time String Array Obfuscation
// =============================================================================

template<size_t Count, size_t MaxLen>
class ObfuscatedStringArray {
    struct EncryptedString {
        std::array<char, MaxLen> data;
        size_t len;
    };

    std::array<EncryptedString, Count> strings_;
    uint8_t key_;

public:
    constexpr ObfuscatedStringArray(const std::array<const char*, Count>& strings, uint8_t key = OMNIGHOST_STR_OBFUSCATE_KEY)
        : key_(key) {
        for (size_t i = 0; i < Count; ++i) {
            strings_[i].len = 0;
            const char* src = strings[i];
            for (size_t j = 0; j < MaxLen - 1 && src[j]; ++j) {
                strings_[i].data[j] = static_cast<char>(src[j] ^ key_);
                strings_[i].len = j + 1;
            }
            strings_[i].data[strings_[i].len] = 0;
        }
    }

    const char* get(size_t index) const {
        if (index >= Count) return nullptr;
        // Decrypt on first access (use mutable cache)
        static thread_local std::array<std::string, Count> cache;
        static thread_local std::array<bool, Count> decrypted = {false};

        if (!decrypted[index]) {
            const auto& enc = strings_[index];
            std::string& cached = const_cast<std::string&>(cache[index]);
            cached.resize(enc.len);
            for (size_t i = 0; i < enc.len; ++i) {
                cached[i] = static_cast<char>(enc.data[i] ^ key_);
            }
            const_cast<bool&>(decrypted[index]) = true;
        }
        return cache[index].c_str();
    }
};

// Macro for string array
#define OMNIGHOST_OBFUSCATE_ARRAY(arr) \
    []() -> const ObfuscatedStringArray<sizeof(arr)/sizeof(arr[0]), 256>& { \
        static const ObfuscatedStringArray<sizeof(arr)/sizeof(arr[0]), 256> arr_obf(arr); \
        return arr_obf; \
    }()

// =============================================================================
// Secure Format String (printf-style with obfuscated format)
// =============================================================================

template<typename... Args>
std::string secure_format(const RuntimeObfuscatedString<256>& fmt, Args&&... args) {
    char buffer[1024];
    std::snprintf(buffer, sizeof(buffer), fmt.c_str(), std::forward<Args>(args)...);
    return std::string(buffer);
}

#define OMNIGHOST_SFMT(fmt, ...) secure_format(OBFUSCATE(fmt), ##__VA_ARGS__)

// =============================================================================
// Secure String View (for zero-copy access)
// =============================================================================

class SecureStringView {
    const char* data_;
    size_t size_;
    bool owned_;

public:
    SecureStringView() : data_(nullptr), size_(0), owned_(false) {}

    SecureStringView(const char* data, size_t size, bool owned = false)
        : data_(data), size_(size), owned_(owned) {}

    ~SecureStringView() {
        if (owned_ && data_) {
            secure_zero(const_cast<char*>(data_), size_);
        }
    }

    SecureStringView(const SecureStringView&) = delete;
    SecureStringView& operator=(const SecureStringView&) = delete;

    SecureStringView(SecureStringView&& other) noexcept
        : data_(other.data_), size_(other.size_), owned_(other.owned_) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.owned_ = false;
    }

    SecureStringView& operator=(SecureStringView&& other) noexcept {
        if (this != &other) {
            if (owned_ && data_) secure_zero(const_cast<char*>(data_), size_);
            data_ = other.data_;
            size_ = other.size_;
            owned_ = other.owned_;
            other.data_ = nullptr;
            other.size_ = 0;
            other.owned_ = false;
        }
        return *this;
    }

    const char* data() const { return data_; }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    std::string to_string() const {
        return std::string(data_, size_);
    }
};

// =============================================================================
// Secure String Builder
// =============================================================================

class SecureStringBuilder {
    std::string buffer_;
    uint8_t key_;

public:
    explicit SecureStringBuilder(uint8_t key = OMNIGHOST_STR_OBFUSCATE_KEY) : key_(key) {}

    SecureStringBuilder& append(const char* str) {
        buffer_ += str;
        return *this;
    }

    SecureStringBuilder& append(const RuntimeObfuscatedString<256>& str) {
        buffer_ += str.c_str();
        return *this;
    }

    template<typename T>
    SecureStringBuilder& append(const T& value) {
        buffer_ += std::to_string(value);
        return *this;
    }

    SecureStringBuilder& append_encrypted(std::string_view str) {
        for (char c : str) {
            buffer_.push_back(static_cast<char>(c ^ key_));
        }
        return *this;
    }

    std::string build() {
        std::string result = buffer_;
        buffer_.clear();
        return result;
    }

    std::string build_encrypted() {
        std::string result = buffer_;
        for (char& c : result) c ^= key_;
        buffer_.clear();
        return result;
    }

    void clear() { buffer_.clear(); }
};

// =============================================================================
// Secure String Literals (User-defined literals)
// =============================================================================

// _obf suffix for obfuscated string literals
constexpr RuntimeObfuscatedString<256> operator""_obf(const char* str, size_t len) {
    std::array<char, 256> encrypted{};
    const auto count = (std::min)(len, encrypted.size() - 1);
    for (size_t i = 0; i < count; ++i) encrypted[i] = static_cast<char>(str[i] ^ OMNIGHOST_STR_OBFUSCATE_KEY);
    return RuntimeObfuscatedString<256>(encrypted);
}

// For compile-time hash
constexpr StringID operator""_sid(const char* str, size_t) {
    return fnv1a_hash(str);
}

// =============================================================================
// Secure API Key / Token Storage
// =============================================================================

class SecureCredentialStore {
    struct Credential {
        StringID service_id;
        std::vector<char> encrypted_username;
        std::vector<char> encrypted_password;
        uint8_t key;
    };

    std::vector<Credential> credentials_;
    uint8_t master_key_;

public:
    explicit SecureCredentialStore(uint8_t master_key = OMNIGHOST_STR_OBFUSCATE_KEY)
        : master_key_(master_key) {}

    bool store(std::string_view service, std::string_view username, std::string_view password) {
        Credential cred;
        cred.service_id = fnv1a_hash(service.data());
        cred.key = master_key_;

        cred.encrypted_username.resize(username.size());
        for (size_t i = 0; i < username.size(); ++i) {
            cred.encrypted_username[i] = static_cast<char>(username[i] ^ master_key_);
        }

        cred.encrypted_password.resize(password.size());
        for (size_t i = 0; i < password.size(); ++i) {
            cred.encrypted_password[i] = static_cast<char>(password[i] ^ master_key_);
        }

        credentials_.push_back(std::move(cred));
        return true;
    }

    bool retrieve(StringID service, std::string& username, std::string& password) const {
        auto it = std::find_if(credentials_.begin(), credentials_.end(),
            [service](const Credential& c) { return c.service_id == service; });
        if (it == credentials_.end()) return false;

        username.resize(it->encrypted_username.size());
        for (size_t i = 0; i < it->encrypted_username.size(); ++i) {
            username[i] = static_cast<char>(it->encrypted_username[i] ^ master_key_);
        }

        password.resize(it->encrypted_password.size());
        for (size_t i = 0; i < it->encrypted_password.size(); ++i) {
            password[i] = static_cast<char>(it->encrypted_password[i] ^ master_key_);
        }

        return true;
    }

    bool has_credential(StringID service) const {
        return std::any_of(credentials_.begin(), credentials_.end(),
            [service](const Credential& c) { return c.service_id == service; });
    }

    void clear() {
        for (auto& cred : credentials_) {
            secure_zero(cred.encrypted_username.data(), cred.encrypted_username.size());
            secure_zero(cred.encrypted_password.data(), cred.encrypted_password.size());
        }
        credentials_.clear();
    }

    ~SecureCredentialStore() { clear(); }
};

} // namespace omnighost::security

#endif // OMNIGHOST_STRING_OBFUSCATION_H
