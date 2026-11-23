#pragma once

#include "localization.h"
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <optional>
#include <mutex>
#include <chrono>

namespace OmniGhost::I18n {

// The established launcher localization API owns the Language enum in Loc.
// Reuse it here instead of introducing a second incompatible enum.
using Language = Loc::Language;

// =============================================================================
// Extended Internationalization System
// =============================================================================

// Pluralization rules (CLDR-based)
enum class PluralCategory : uint8_t {
    Zero = 0,
    One = 1,
    Two = 2,
    Few = 3,
    Many = 4,
    Other = 5
};

// Plural rule function type
using PluralRuleFn = std::function<PluralCategory(int64_t n)>;

// Get plural category for a number in a specific language
PluralCategory GetPluralCategory(Language lang, int64_t n);

// Context for translation
struct TranslationContext {
    std::string key;
    std::string context;        // Disambiguation context (e.g., "button", "menu")
    std::string comment;        // Translator comment
    std::vector<std::string> placeholders;  // Expected placeholders
    PluralCategory pluralCategory = PluralCategory::Other;
    bool isPlural = false;

    nlohmann::json ToJson() const;
    static TranslationContext FromJson(const nlohmann::json& j);
};

// Extended translation entry
struct TranslationEntry {
    std::string key;
    std::unordered_map<Language, std::string> translations;
    std::unordered_map<Language, std::vector<std::string>> pluralForms;  // For pluralization
    TranslationContext context;
    std::chrono::system_clock::time_point lastUpdated;
    std::string lastUpdatedBy;

    nlohmann::json ToJson() const;
    static TranslationEntry FromJson(const nlohmann::json& j);
};

// Locale file metadata
struct LocaleMetadata {
    std::string languageCode;
    std::string languageName;
    std::string nativeName;
    std::string regionCode;
    std::string scriptCode;
    std::string version = "1.0.0";
    std::string author;
    std::chrono::system_clock::time_point lastUpdated;
    bool complete = false;           // All keys translated
    double completionPercentage = 0.0;
    std::vector<std::string> fallbacks;  // Fallback languages

    nlohmann::json ToJson() const;
    static LocaleMetadata FromJson(const nlohmann::json& j);
};

// Extended Localization Manager
class ExtendedLocalizationManager {
public:
    static ExtendedLocalizationManager& Instance() noexcept {
        static ExtendedLocalizationManager instance;
        return instance;
    }

    // Initialize with locale directory
    bool Initialize(const std::filesystem::path& localeDir);

    // Shutdown
    void Shutdown();

    // Get translation with context
    const char* Tr(std::string_view key, std::string_view context = "") const noexcept;

    // Get translation with pluralization
    const char* TrPlural(std::string_view key, int64_t count, std::string_view context = "") const noexcept;

    // Get translation with formatting (printf-style)
    std::string TrFmt(std::string_view key, std::string_view context = "", ...) const;

    // Get translation with formatting and pluralization
    std::string TrFmtPlural(std::string_view key, int64_t count, std::string_view context = "", ...) const;

    // Set current language
    bool SetLanguage(Language lang) noexcept;

    // Get current language
    Language GetLanguage() const noexcept;

    // Auto-detect system language
    Language DetectSystemLanguage() const;

    // Get available languages
    std::vector<Language> GetAvailableLanguages() const;

    // Get locale metadata
    std::optional<LocaleMetadata> GetLocaleMetadata(Language lang) const;

    // Get translation entry (for editor/management)
    std::optional<TranslationEntry> GetTranslationEntry(std::string_view key) const;

    // Get all translation entries
    std::vector<TranslationEntry> GetAllTranslationEntries() const;

    // Get missing translations for a language
    std::vector<std::string> GetMissingKeys(Language lang) const;

    // Get completion stats
    struct CompletionStats {
        Language lang;
        std::size_t totalKeys = 0;
        std::size_t translatedKeys = 0;
        std::size_t missingKeys = 0;
        double percentage = 0.0;
    };
    CompletionStats GetCompletionStats(Language lang) const;

    // Add/update translation (for live editing)
    bool SetTranslation(std::string_view key, Language lang, std::string_view translation);

    // Add plural form
    bool SetPluralForm(std::string_view key, Language lang, PluralCategory category, std::string_view translation);

    // Remove translation
    bool RemoveTranslation(std::string_view key, Language lang);

    // Export locale to file
    bool ExportLocale(Language lang, const std::filesystem::path& outputPath) const;

    // Import locale from file
    bool ImportLocale(Language lang, const std::filesystem::path& inputPath);

    // Import all locales from directory
    std::size_t ImportLocalesFromDirectory(const std::filesystem::path& dirPath);

    // Export translation template (for translators)
    bool ExportTemplate(const std::filesystem::path& outputPath) const;

    // Import from translation template
    bool ImportTemplate(const std::filesystem::path& inputPath, Language targetLang);

    // Hot-reload locale files
    void Reload();

    // Validate locale files
    struct ValidationResult {
        bool valid = true;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };
    ValidationResult ValidateLocale(Language lang) const;

    // Get locale directory
    std::filesystem::path GetLocaleDirectory() const;

private:
    ExtendedLocalizationManager() = default;
    ~ExtendedLocalizationManager() { Shutdown(); }
    ExtendedLocalizationManager(const ExtendedLocalizationManager&) = delete;
    ExtendedLocalizationManager& operator=(const ExtendedLocalizationManager&) = delete;

    bool LoadLocale(Language lang);
    bool SaveLocale(Language lang) const;
    void BuildPluralRules();
    void UpdateCompletionStats(Language lang);
    void CreateDefaultPortugueseLocale();

    mutable std::shared_mutex mutex_;
    std::filesystem::path localeDir_;
    Language currentLang_ = Language::Portuguese;

    // key -> TranslationEntry
    std::unordered_map<std::string, TranslationEntry> entries_;

    // lang -> LocaleMetadata
    std::unordered_map<Language, LocaleMetadata> metadata_;

    // Plural rules
    std::unordered_map<Language, PluralRuleFn> pluralRules_;
};

// =============================================================================
// Convenience Functions
// =============================================================================

inline ExtendedLocalizationManager& I18n() {
    return ExtendedLocalizationManager::Instance();
}

inline bool InitializeI18n(const std::filesystem::path& localeDir) {
    return ExtendedLocalizationManager::Instance().Initialize(localeDir);
}

inline void ShutdownI18n() {
    ExtendedLocalizationManager::Instance().Shutdown();
}

inline const char* Tr(std::string_view key, std::string_view context = "") {
    return ExtendedLocalizationManager::Instance().Tr(key, context);
}

inline const char* TrPlural(std::string_view key, int64_t count, std::string_view context = "") {
    return ExtendedLocalizationManager::Instance().TrPlural(key, count, context);
}

inline std::string TrFmt(std::string_view key, std::string_view context = "", auto&&... args) {
    return ExtendedLocalizationManager::Instance().TrFmt(key, context, std::forward<decltype(args)>(args)...);
}

inline std::string TrFmtPlural(std::string_view key, int64_t count, std::string_view context = "", auto&&... args) {
    return ExtendedLocalizationManager::Instance().TrFmtPlural(key, count, context, std::forward<decltype(args)>(args)...);
}

// =============================================================================
// ICU-style Message Format Support
// =============================================================================

// Parse ICU MessageFormat string
struct MessageFormat {
    std::string pattern;
    std::vector<std::pair<std::string, std::string>> placeholders;  // name -> type

    static std::optional<MessageFormat> Parse(std::string_view pattern);
    std::string Format(const std::unordered_map<std::string, std::string>& args) const;
};

// Format number according to locale
std::string FormatNumber(Language lang, double value, int maxDecimals = 2);

// Format currency according to locale
std::string FormatCurrency(Language lang, double value, std::string_view currencyCode = "BRL");

// Format date/time according to locale
std::string FormatDateTime(Language lang, std::chrono::system_clock::time_point tp,
                           bool includeTime = true, bool shortFormat = false);

// Format relative time (e.g., "2 hours ago")
std::string FormatRelativeTime(Language lang, std::chrono::seconds duration);

// Get list of supported languages with metadata
struct LanguageInfo {
    Language code;
    std::string englishName;
    std::string nativeName;
    std::string regionCode;
    bool rtl = false;
    bool complete = false;
};

std::vector<LanguageInfo> GetSupportedLanguages();

} // namespace OmniGhost::I18n
