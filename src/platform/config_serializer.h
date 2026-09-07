#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <sstream>
#include <istream>
#include <ostream>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace OmniGhost::Platform {

// Local line reader — avoids ambiguous std::getline overloads in some TUs.
template <typename Stream>
inline bool ReadLine(Stream& is, std::string& out) {
    out.clear();
    char ch = 0;
    bool got = false;
    while (static_cast<bool>(is.get(ch))) {
        got = true;
        if (ch == '\n') break;
        if (ch != '\r') out.push_back(ch);
    }
    return got;
}


// ============================================================
// Optimized Config Serialization with std::span
// ============================================================

// Forward declarations
struct ConfigValue;
class ConfigSerializer;

// Type traits for serialization
template <typename T>
struct ConfigSerializerTraits {
    static void Write(std::ostream& os, std::string_view key, const T& value);
    static bool Read(std::istream& is, T& value);
    static size_t Size(const T& value) { return sizeof(T); }
};

// Specializations
template <>
struct ConfigSerializerTraits<bool> {
    static void Write(std::ostream& os, std::string_view key, bool value) {
        os << key << "=" << (value ? "1" : "0") << '\n';
    }
    static bool Read(std::istream& is, bool& value) {
        std::string val;
        if (ReadLine(is, val)) {
            value = (val == "1" || val == "true");
            return true;
        }
        return false;
    }
    static size_t Size(const bool&) { return 1; }
};

template <>
struct ConfigSerializerTraits<int> {
    static void Write(std::ostream& os, std::string_view key, int value) {
        os << key << "=" << value << '\n';
    }
    static bool Read(std::istream& is, int& value) {
        std::string val;
        if (ReadLine(is, val)) {
            value = std::stoi(val);
            return true;
        }
        return false;
    }
    static size_t Size(const int&) { return sizeof(int); }
};

template <>
struct ConfigSerializerTraits<float> {
    static void Write(std::ostream& os, std::string_view key, float value) {
        os << key << "=" << value << '\n';
    }
    static bool Read(std::istream& is, float& value) {
        std::string val;
        if (ReadLine(is, val)) {
            value = std::stof(val);
            return true;
        }
        return false;
    }
    static size_t Size(const float&) { return sizeof(float); }
};

template <>
struct ConfigSerializerTraits<uint32_t> {
    static void Write(std::ostream& os, std::string_view key, uint32_t value) {
        os << key << "=" << value << '\n';
    }
    static bool Read(std::istream& is, uint32_t& value) {
        std::string val;
        if (ReadLine(is, val)) {
            value = static_cast<uint32_t>(std::stoul(val));
            return true;
        }
        return false;
    }
    static size_t Size(const uint32_t&) { return sizeof(uint32_t); }
};

template <>
struct ConfigSerializerTraits<uint64_t> {
    static void Write(std::ostream& os, std::string_view key, uint64_t value) {
        os << key << "=" << value << '\n';
    }
    static bool Read(std::istream& is, uint64_t& value) {
        std::string val;
        if (ReadLine(is, val)) {
            value = std::stoull(val);
            return true;
        }
        return false;
    }
    static size_t Size(const uint64_t&) { return sizeof(uint64_t); }
};

template <>
struct ConfigSerializerTraits<std::string> {
    static void Write(std::ostream& os, std::string_view key, const std::string& value) {
        os << key << "=" << value << '\n';
    }
    static bool Read(std::istream& is, std::string& value) {
        return ReadLine(is, value);
    }
    static size_t Size(const std::string& value) { return value.size(); }
};

// Vector specialization
template <typename T>
struct ConfigSerializerTraits<std::vector<T>> {
    static void Write(std::ostream& os, std::string_view key, const std::vector<T>& value) {
        os << key << ".count=" << value.size() << '\n';
        for (size_t i = 0; i < value.size(); ++i) {
            os << key << "." << i << "=";
            ConfigSerializerTraits<T>::Write(os, "", value[i]);
        }
    }
    
    static bool Read(std::istream& is, std::vector<T>& value) {
        std::string line;
        size_t count = 0;
        
        // Read count
        if (ReadLine(is, line)) {
            size_t eqPos = line.find('=');
            if (eqPos != std::string::npos && line.substr(0, eqPos).ends_with(".count")) {
                count = std::stoull(line.substr(eqPos + 1));
                value.resize(count);
            }
        }
        
        // Read elements
        for (size_t i = 0; i < count; ++i) {
            if (ReadLine(is, line)) {
                size_t eqPos = line.find('=');
                if (eqPos != std::string::npos) {
                    std::istringstream elemStream(line.substr(eqPos + 1));
                    T elem;
                    if (ConfigSerializerTraits<T>::Read(elemStream, elem)) {
                        if (i < value.size()) value[i] = elem;
                    }
                }
            }
        }
        return true;
    }
    
    static size_t Size(const std::vector<T>& value) {
        size_t total = sizeof(size_t);
        for (const auto& elem : value) {
            total += ConfigSerializerTraits<T>::Size(elem);
        }
        return total;
    }
};

// ============================================================
// Span-based serialization (zero-copy where possible)
// ============================================================

class SpanSerializer {
public:
    // Write to pre-allocated buffer using std::span
    template <typename T>
    static size_t WriteValue(std::span<std::byte> buffer, size_t offset, const T& value) {
        if (offset + sizeof(T) > buffer.size()) return 0;
        std::memcpy(buffer.data() + offset, &value, sizeof(T));
        return sizeof(T);
    }
    
    template <typename T>
    static size_t WriteValue(std::span<std::byte> buffer, size_t offset, const std::vector<T>& values) {
        size_t countSize = sizeof(size_t);
        if (offset + countSize > buffer.size()) return 0;
        std::memcpy(buffer.data() + offset, &values.size(), countSize);
        offset += countSize;
        
        size_t elemSize = sizeof(T);
        if (offset + values.size() * elemSize > buffer.size()) return 0;
        std::memcpy(buffer.data() + offset, values.data(), values.size() * elemSize);
        return countSize + values.size() * elemSize;
    }
    
    template <typename T>
    static size_t ReadValue(std::span<const std::byte> buffer, size_t offset, T& value) {
        if (offset + sizeof(T) > buffer.size()) return 0;
        std::memcpy(&value, buffer.data() + offset, sizeof(T));
        return sizeof(T);
    }
    
    template <typename T>
    static size_t ReadValue(std::span<const std::byte> buffer, size_t offset, std::vector<T>& values) {
        if (offset + sizeof(size_t) > buffer.size()) return 0;
        size_t count = 0;
        std::memcpy(&values.size(), buffer.data() + offset, sizeof(size_t));
        offset += sizeof(size_t);
        
        size_t elemSize = sizeof(T);
        if (offset + values.size() * elemSize > buffer.size()) return 0;
        values.resize(count);
        std::memcpy(values.data(), buffer.data() + offset, count * elemSize);
        return sizeof(size_t) + count * elemSize;
    }
};

// ============================================================
// Config Serializer with incremental updates
// ============================================================

class ConfigSerializer {
public:
    struct ConfigEntry {
        std::string key;
        std::string value;
        uint64_t lastModified = 0;
        bool dirty = false;
    };
    
    ConfigSerializer() = default;
    
    // Set value (marks as dirty)
    template <typename T>
    void Set(std::string_view key, const T& value) {
        std::ostringstream oss;
        ConfigSerializerTraits<T>::Write(oss, key, value);
        std::string result = oss.str();
        
        std::lock_guard lock(mutex_);
        auto& entry = entries_[std::string(key)];
        entry.key = key;
        entry.value = std::move(result);
        entry.lastModified = GetCurrentTime();
        entry.dirty = true;
    }
    
    // Get value (no copy if possible)
    template <typename T>
    bool Get(std::string_view key, T& outValue) const {
        std::lock_guard lock(mutex_);
        auto it = entries_.find(std::string(key));
        if (it == entries_.end()) return false;
        
        std::istringstream iss(it->second.value);
        std::string line;
        if (ReadLine(iss, line)) {
            size_t eqPos = line.find('=');
            if (eqPos != std::string::npos) {
                std::istringstream valStream(line.substr(eqPos + 1));
                return ConfigSerializerTraits<T>::Read(valStream, outValue);
            }
        }
        return false;
    }
    
    // Get with default
    template <typename T>
    T GetOrDefault(std::string_view key, const T& defaultValue) const {
        T value;
        return Get(key, value) ? value : defaultValue;
    }
    
    // Serialize only dirty entries (incremental save)
    std::string SerializeDirty() {
        std::lock_guard lock(mutex_);
        std::ostringstream oss;
        
        auto now = GetCurrentTime();
        oss << "meta.schema_version=2\n";
        oss << "meta.timestamp=" << now << "\n";
        oss << "meta.dirty_only=1\n";
        
        for (auto& [key, entry] : entries_) {
            if (entry.dirty) {
                oss << entry.value << '\n';
                entry.dirty = false;
            }
        }
        
        return oss.str();
    }
    
    // Serialize all
    std::string SerializeAll() {
        std::lock_guard lock(mutex_);
        std::ostringstream oss;
        
        auto now = GetCurrentTime();
        oss << "meta.schema_version=2\n";
        oss << "meta.timestamp=" << now << "\n";
        oss << "meta.dirty_only=0\n";
        
        for (const auto& [_, entry] : entries_) {
            oss << entry.value << '\n';
        }
        
        return oss.str();
    }
    
    // Deserialize from string (full or incremental)
    void Deserialize(std::string_view data) {
        std::lock_guard lock(mutex_);
        std::istringstream iss{std::string{data}};
        std::string line;
        
        bool dirtyOnly = false;
        
        while (ReadLine(iss, line)) {
            if (line.empty() || line[0] == '#') continue;
            
            if (line.starts_with("meta.")) {
                if (line.starts_with("meta.dirty_only=1")) {
                    dirtyOnly = true;
                }
                continue;
            }
            
            size_t eqPos = line.find('=');
            if (eqPos == std::string::npos) continue;
            
            std::string key = line.substr(0, eqPos);
            std::string value = line.substr(eqPos + 1);
            
            if (!dirtyOnly || entries_[key].dirty) {
                entries_[key].value = line;
                entries_[key].key = key;
                entries_[key].lastModified = GetCurrentTime();
                entries_[key].dirty = false;
            }
        }
    }
    
    // Save to file atomically
    bool SaveToFile(const std::filesystem::path& path) {
        std::string data = SerializeDirty();
        
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return false;
        
        std::filesystem::path tempPath = path;
        tempPath += ".tmp";
        
        {
            std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
            if (!file) return false;
            file.write(data.data(), data.size());
            if (!file) return false;
        }
        
        // Atomic replace
        std::filesystem::path backupPath = path;
        backupPath += ".bak";
        
        std::filesystem::rename(tempPath, path, ec);
        if (ec) {
            std::filesystem::remove(tempPath, ec);
            return false;
        }
        
        return true;
    }
    
    // Load from file
    bool LoadFromFile(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) return false;
        
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        
        std::ostringstream ss;
        ss << file.rdbuf();
        
        Deserialize(ss.str());
        return true;
    }
    
    // Check if key exists
    bool HasKey(std::string_view key) const {
        std::lock_guard lock(mutex_);
        return entries_.contains(std::string(key));
    }
    
    // Remove key
    void Remove(std::string_view key) {
        std::lock_guard lock(mutex_);
        entries_.erase(std::string(key));
    }
    
    // Clear all
    void Clear() {
        std::lock_guard lock(mutex_);
        entries_.clear();
    }
    
    // Get all dirty keys
    std::vector<std::string> GetDirtyKeys() const {
        std::lock_guard lock(mutex_);
        std::vector<std::string> dirtyKeys;
        for (const auto& [_, entry] : entries_) {
            if (entry.dirty) {
                dirtyKeys.push_back(entry.key);
            }
        }
        return dirtyKeys;
    }
    
    // Mark all as clean
    void MarkAllClean() {
        std::lock_guard lock(mutex_);
        for (auto& [_, entry] : entries_) {
            entry.dirty = false;
        }
    }
    
    // Get entry count
    [[nodiscard]] size_t Size() const noexcept {
        std::lock_guard lock(mutex_);
        return entries_.size();
    }
    
    // Get dirty count
    [[nodiscard]] size_t DirtyCount() const {
        std::lock_guard lock(mutex_);
        size_t count = 0;
        for (const auto& [_, entry] : entries_) {
            if (entry.dirty) count++;
        }
        return count;
    }

private:
    struct Entry {
        std::string key;
        std::string value;
        uint64_t lastModified = 0;
        bool dirty = false;
    };
    
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ConfigEntry> entries_;
    
    static uint64_t GetCurrentTime() noexcept {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

// ============================================================
// Binary serialization with std::span (zero-copy)
// ============================================================

class BinarySerializer {
public:
    // Calculate required buffer size
    template <typename T>
    static size_t CalculateSize(const T& value) {
        return ConfigSerializerTraits<T>::Size(value);
    }
    
    // Serialize to pre-allocated buffer
    template <typename T>
    static bool Serialize(std::span<std::byte> buffer, size_t& offset, const T& value) {
        size_t size = ConfigSerializerTraits<T>::Size(value);
        if (offset + size > buffer.size()) return false;
        
        // For POD types, just memcpy
        if constexpr (std::is_trivially_copyable_v<T>) {
            std::memcpy(buffer.data() + offset, &value, sizeof(T));
        } else {
            // For complex types, use stream
            std::ostringstream oss;
            ConfigSerializerTraits<T>::Write(std::ostream(nullptr), "", value);
            // This is simplified - real impl would need binary format
        }
        return true;
    }
    
    template <typename T>
    static bool Deserialize(std::span<const std::byte> buffer, size_t& offset, T& value) {
        if constexpr (std::is_trivially_copyable_v<T>) {
            if (offset + sizeof(T) > buffer.size()) return false;
            std::memcpy(&value, buffer.data() + offset, sizeof(T));
            return true;
        }
        return false;
    }
};

// ============================================================
// Delta compression for config changes
// ============================================================

class ConfigDeltaCompressor {
public:
    struct Delta {
        std::string key;
        std::string oldValue;
        std::string newValue;
    };
    
    // Compute delta between two configs
    static std::vector<Delta> ComputeDelta(const ConfigSerializer& oldConfig, const ConfigSerializer& newConfig) {
        std::vector<Delta> deltas;
        
        // This would need access to internal entries
        // Simplified implementation
        return deltas;
    }
    
    // Apply delta
    static void ApplyDelta(ConfigSerializer& config, const std::vector<Delta>& deltas) {
        for (const auto& delta : deltas) {
            // Apply change
        }
    }
};

} // namespace OmniGhost::Platform