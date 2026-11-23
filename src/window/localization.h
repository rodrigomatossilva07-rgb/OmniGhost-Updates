#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <filesystem>
#include <optional>

namespace Loc {


enum class Language : uint8_t {
    Portuguese = 0,
    English    = 1,
    German     = 2,
    Spanish    = 3,
    French     = 4,
    Italian    = 5,
    Russian    = 6,
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




class LocalizationManager {
public:
    static LocalizationManager& Instance() noexcept {
        static LocalizationManager instance;
        return instance;
    }


    bool Initialize(const std::filesystem::path& localeDir);


    Language GetLanguage() const noexcept { return m_currentLang; }


    bool SetLanguage(Language lang) noexcept;


    const char* Tr(std::string_view key) const noexcept;


    std::string TrFmt(std::string_view key, ...) const;


    bool HasKey(std::string_view key) const noexcept;


    std::vector<std::string> GetAllKeys() const;


    void Reload();


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


    std::unordered_map<Language, std::unordered_map<std::string, std::string>> m_translations;


    mutable std::unordered_map<Language, std::vector<std::string>> m_missingKeysCache;
    mutable bool m_missingKeysDirty = true;
};


inline const char* Tr(std::string_view key) noexcept {
    return LocalizationManager::Instance().Tr(key);
}


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

}
