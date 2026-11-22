// String Obfuscation Implementation
// Runtime string obfuscation utilities

#include "string_obfuscation.h"
#include <random>
#include <chrono>

namespace omnighost::security {

// =============================================================================
// RuntimeStringObfuscator Implementation
// =============================================================================

RuntimeStringObfuscator::RuntimeStringObfuscator(std::string_view key) {
    key_.assign(key.begin(), key.end());
    if (key_.empty()) {
        // Generate default key from current time
        uint64_t seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        key_.resize(32);
        for (size_t i = 0; i < 32; ++i) {
            seed = seed * 1664525 + 1013904223;
            key_[i] = static_cast<uint8_t>((seed >> 24) & 0xFF);
        }
    }
}

std::string RuntimeStringObfuscator::xor_string(std::string_view str) const {
    std::string result;
    result.resize(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        result[i] = static_cast<char>(str[i] ^ key_[i % key_.size()]);
    }
    return result;
}

void RuntimeStringObfuscator::xor_inplace(std::string& str) const {
    for (size_t i = 0; i < str.size(); ++i) {
        str[i] ^= key_[i % key_.size()];
    }
}

std::vector<uint8_t> RuntimeStringObfuscator::generate_key(size_t length) {
    std::vector<uint8_t> key(length);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1, 255);
    for (auto& b : key) b = static_cast<uint8_t>(dis(gen));
    return key;
}

// =============================================================================
// SecureStringPool Implementation
// =============================================================================

StringID SecureStringPool::add_string(std::string_view str, uint8_t key) {
    if (key == 0) key = global_key_;

    Entry entry;
    entry.id = fnv1a_hash(str.data());
    entry.key = key;
    entry.encrypted.resize(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        entry.encrypted[i] = static_cast<char>(str[i] ^ key);
    }

    // Check if already exists
    auto it = std::find_if(strings_.begin(), strings_.end(),
        [entry](const Entry& e) { return e.id == entry.id; });
    if (it != strings_.end()) {
        *it = std::move(entry);
        return it->id;
    }

    strings_.push_back(std::move(entry));
    return strings_.back().id;
}

std::string SecureStringPool::get_string(StringID id) const {
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

const char* SecureStringPool::get_cstr(StringID id) {
    auto it = std::find_if(strings_.begin(), strings_.end(),
        [id](const Entry& e) { return e.id == id; });
    if (it == strings_.end()) return nullptr;

    thread_local std::string buffer;
    buffer.resize(it->encrypted.size());
    for (size_t i = 0; i < it->encrypted.size(); ++i) {
        buffer[i] = static_cast<char>(it->encrypted[i] ^ it->key);
    }
    return buffer.c_str();
}

bool SecureStringPool::has_string(StringID id) const {
    return std::any_of(strings_.begin(), strings_.end(),
        [id](const Entry& e) { return e.id == id; });
}

// =============================================================================
// SecureCredentialStore Implementation
// =============================================================================

SecureCredentialStore::SecureCredentialStore(uint8_t master_key)
    : master_key_(master_key) {}

bool SecureCredentialStore::store(std::string_view service, std::string_view username, std::string_view password) {
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

    // Check if already exists
    auto it = std::find_if(credentials_.begin(), credentials_.end(),
        [&cred](const Credential& c) { return c.service_id == cred.service_id; });
    if (it != credentials_.end()) {
        *it = std::move(cred);
    } else {
        credentials_.push_back(std::move(cred));
    }
    return true;
}

bool SecureCredentialStore::retrieve(StringID service, std::string& username, std::string& password) const {
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

bool SecureCredentialStore::has_credential(StringID service) const {
    return std::any_of(credentials_.begin(), credentials_.end(),
        [service](const Credential& c) { return c.service_id == service; });
}

void SecureCredentialStore::clear() {
    for (auto& cred : credentials_) {
        secure_zero(cred.encrypted_username.data(), cred.encrypted_username.size());
        secure_zero(cred.encrypted_password.data(), cred.encrypted_password.size());
    }
    credentials_.clear();
}

SecureCredentialStore::~SecureCredentialStore() {
    clear();
}

// =============================================================================
// SecureStringBuilder Implementation
// =============================================================================

SecureStringBuilder::SecureStringBuilder(uint8_t key) : key_(key) {}

SecureStringBuilder& SecureStringBuilder::append(const char* str) {
    buffer_ += str;
    return *this;
}

SecureStringBuilder& SecureStringBuilder::append(const ObfuscatedString<256>& str) {
    buffer_ += str.c_str();
    return *this;
}

template<typename T>
SecureStringBuilder& SecureStringBuilder::append(const T& value) {
    buffer_ += std::to_string(value);
    return *this;
}

// Explicit instantiation for common types
template SecureStringBuilder& SecureStringBuilder::append<int>(const int&);
template SecureStringBuilder& SecureStringBuilder::append<unsigned int>(const unsigned int&);
template SecureStringBuilder& SecureStringBuilder::append<long>(const long&);
template SecureStringBuilder& SecureStringBuilder::append<unsigned long>(const unsigned long&);
template SecureStringBuilder& SecureStringBuilder::append<long long>(const long long&);
template SecureStringBuilder& SecureStringBuilder::append<unsigned long long>(const unsigned long long&);
template SecureStringBuilder& SecureStringBuilder::append<float>(const float&);
template SecureStringBuilder& SecureStringBuilder::append<double>(const double&);
template SecureStringBuilder& SecureStringBuilder::append<bool>(const bool&);

SecureStringBuilder& SecureStringBuilder::append_encrypted(std::string_view str) {
    for (char c : str) {
        buffer_.push_back(static_cast<char>(c ^ key_));
    }
    return *this;
}

std::string SecureStringBuilder::build() {
    std::string result = buffer_;
    buffer_.clear();
    return result;
}

std::string SecureStringBuilder::build_encrypted() {
    std::string result = buffer_;
    for (char& c : result) c ^= key_;
    buffer_.clear();
    return result;
}

void SecureStringBuilder::clear() {
    buffer_.clear();
}

// =============================================================================
// SecureCredentialStore Implementation
// =============================================================================

SecureCredentialStore::SecureCredentialStore(uint8_t master_key)
    : master_key_(master_key) {}

bool SecureCredentialStore::store(std::string_view service, std::string_view username, std::string_view password) {
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

    auto it = std::find_if(credentials_.begin(), credentials_.end(),
        [&cred](const Credential& c) { return c.service_id == cred.service_id; });
    if (it != credentials_.end()) {
        *it = std::move(cred);
    } else {
        credentials_.push_back(std::move(cred));
    }
    return true;
}

bool SecureCredentialStore::retrieve(StringID service, std::string& username, std::string& password) const {
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

bool SecureCredentialStore::has_credential(StringID service) const {
    return std::any_of(credentials_.begin(), credentials_.end(),
        [service](const Credential& c) { return c.service_id == service; });
}

void SecureCredentialStore::clear() {
    for (auto& cred : credentials_) {
        secure_zero(cred.encrypted_username.data(), cred.encrypted_username.size());
        secure_zero(cred.encrypted_password.data(), cred.encrypted_password.size());
    }
    credentials_.clear();
}

SecureCredentialStore::~SecureCredentialStore() {
    clear();
}

// =============================================================================
// SecureStringBuilder Template Instantiations
// =============================================================================

template SecureStringBuilder& SecureStringBuilder::append<int>(const int&);
template SecureStringBuilder& SecureStringBuilder::append<unsigned int>(const unsigned int&);
template SecureStringBuilder& SecureStringBuilder::append<long>(const long&);
template SecureStringBuilder& SecureStringBuilder::append<unsigned long>(const unsigned long&);
template SecureStringBuilder& SecureStringBuilder::append<long long>(const long long&);
template SecureStringBuilder& SecureStringBuilder::append<unsigned long long>(const unsigned long long&);
template SecureStringBuilder& SecureStringBuilder::append<float>(const float&);
template SecureStringBuilder& SecureStringBuilder::append<double>(const double&);
template SecureStringBuilder& SecureStringBuilder::append<bool>(const bool&);

} // namespace omnighost::security