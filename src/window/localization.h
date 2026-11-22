#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <filesystem>
#include <optional>

namespace Loc {

// Language codes (ISO 639-1)
enum class Language : uint8_t {
    Portuguese = 0,  // pt
    English    = 1,  // en
    German     = 2,  // de
    Spanish    = 3,  // es
    French     = 4,  // fr
    Italian    = 5,  // it
    Russian    = 6,  // ru
    Count      = 7
};

inline constexpr const char* LanguageCode(Language lang) noexcept {
    switch (lang) {
        case Language::Portuguese: return "pt";
        case Language::English:    return "en";
        case Language::German:     return "de";
        case Language::Spanish:    return "es";
        case Language::French:     return "fr";
        case Language::Italian:    return "it";
        case Language::Russian:    return "ru";
        default:                   return "en";
    }
}

inline constexpr const char* LanguageName(Language lang) noexcept {
    switch (lang) {
        case Language::Portuguese: return "Português";
        case Language::English:    return "English";
        case Language::German:     return "Deutsch";
        case Language::Spanish:    return "Español";
        case Language::French:     return "Français";
        case Language::Italian:    return "Italiano";
        case Language::Russian:    return "Русский";
        default:                   return "English";
    }
}

inline constexpr int LanguageCount() noexcept {
    return static_cast<int>(Language::Count);
}

// -----------------------------------------------------------------------------
// Localization Manager
// -----------------------------------------------------------------------------
class LocalizationManager {
public:
    static LocalizationManager& Instance() noexcept {
        static LocalizationManager instance;
        return instance;
    }

    // Initialize with locale directory path
    bool Initialize(const std::filesystem::path& localeDir);

    // Get current language
    Language GetLanguage() const noexcept { return m_currentLang; }

    // Set language (returns true if language is available)
    bool SetLanguage(Language lang) noexcept;

    // Translate a key
    const char* Tr(std::string_view key) const noexcept;

    // Translate with formatting (printf-style)
    std::string TrFmt(std::string_view key, ...) const;

    // Check if key exists
    bool HasKey(std::string_view key) const noexcept;

    // Get all available keys (for debugging/tools)
    std::vector<std::string> GetAllKeys() const;

    // Hot-reload locale files (dev builds only)
    void Reload();

    // Get missing keys for a language (for translation tools)
    std::vector<std::string> GetMissingKeys(Language lang) const;

private:
    LocalizationManager() = default;
    ~LocalizationManager() = default;
    LocalizationManager(const LocalizationManager&) = delete;
    LocalizationManager& operator=(const LocalizationManager&) = delete;

    bool LoadLanguage(Language lang);
    void LoadAllLanguages(const std::filesystem::path& localeDir);

    mutable std::shared_mutex m_mutex;
    Language m_currentLang = Language::Portuguese;
    std::filesystem::path m_localeDir;

    // language -> (key -> translation)
    std::unordered_map<Language, std::unordered_map<std::string, std::string>> m_translations;
    
    // Cache for missing keys
    mutable std::unordered_map<Language, std::vector<std::string>> m_missingKeysCache;
    mutable bool m_missingKeysDirty = true;
};

// Convenience functions
inline const char* Tr(std::string_view key) noexcept {
    return LocalizationManager::Instance().Tr(key);
}

// Compatibility alias for existing UI pages that use translation identifiers.
inline const char* TrID(std::string_view key) noexcept { return Tr(key); }

inline std::string TrFmt(std::string_view key, auto&&... args) {
    return LocalizationManager::Instance().TrFmt(key, std::forward<decltype(args)>(args)...);
}

inline bool HasKey(std::string_view key) noexcept {
    return LocalizationManager::Instance().HasKey(key);
}

inline Language GetLanguage() noexcept {
    return LocalizationManager::Instance().GetLanguage();
}

inline bool SetLanguage(Language lang) noexcept {
    return LocalizationManager::Instance().SetLanguage(lang);
}

inline void ReloadLocalization() {
    LocalizationManager::Instance().Reload();
}

} // namespace Loc
