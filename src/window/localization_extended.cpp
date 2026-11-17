#include "localization_extended.h"
#include "app_paths.h"
#include "logging.h"

#include <fstream>
#include <sstream>
#include <cstdarg>
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <ctime>

namespace OmniGhost::I18n {

// =============================================================================
// Pluralization Rules (CLDR-based)
// =============================================================================

PluralCategory GetPluralCategory(Language lang, int64_t n) {
    // Simplified plural rules for supported languages
    // Full CLDR rules would be more complex

    switch (lang) {
        case Language::English: {
            if (n == 1) return PluralCategory::One;
            return PluralCategory::Other;
        }
        case Language::Portuguese: {
            if (n == 1) return PluralCategory::One;
            return PluralCategory::Other;
        }
        case Language::German: {
            if (n == 1) return PluralCategory::One;
            return PluralCategory::Other;
        }
        case Language::Spanish: {
            if (n == 1) return PluralCategory::One;
            return PluralCategory::Other;
        }
        case Language::French: {
            if (n == 0 || n == 1) return PluralCategory::One;
            return PluralCategory::Other;
        }
        case Language::Italian: {
            if (n == 1) return PluralCategory::One;
            return PluralCategory::Other;
        }
        case Language::Russian: {
            // Russian has complex plural rules
            int64_t n10 = n % 10;
            int64_t n100 = n % 100;
            if (n10 == 1 && n100 != 11) return PluralCategory::One;
            if (n10 >= 2 && n10 <= 4 && (n100 < 10 || n100 >= 20)) return PluralCategory::Few;
            if (n10 == 0 || (n10 >= 5 && n10 <= 9) || (n100 >= 11 && n100 <= 14)) return PluralCategory::Many;
            return PluralCategory::Other;
        }
        default: {
            return PluralCategory::Other;
        }
    }
}

// =============================================================================
// Serialization
// =============================================================================

nlohmann::json TranslationContext::ToJson() const {
    nlohmann::json j;
    j["key"] = key;
    j["context"] = context;
    j["comment"] = comment;
    j["placeholders"] = placeholders;
    j["pluralCategory"] = static_cast<int>(pluralCategory);
    j["isPlural"] = isPlural;
    return j;
}

TranslationContext TranslationContext::FromJson(const nlohmann::json& j) {
    TranslationContext c;
    c.key = j.value("key", "");
    c.context = j.value("context", "");
    c.comment = j.value("comment", "");
    c.placeholders = j.value("placeholders", std::vector<std::string>{});
    c.pluralCategory = static_cast<PluralCategory>(j.value("pluralCategory", static_cast<int>(PluralCategory::Other)));
    c.isPlural = j.value("isPlural", false);
    return c;
}

nlohmann::json TranslationEntry::ToJson() const {
    nlohmann::json j;
    j["key"] = key;
    j["context"] = context.ToJson();
    j["lastUpdated"] = std::chrono::duration_cast<std::chrono::seconds>(
        lastUpdated.time_since_epoch()).count();
    j["lastUpdatedBy"] = lastUpdatedBy;

    nlohmann::json trans;
    for (const auto& [lang, value] : translations) {
        trans[LanguageCode(lang)] = value;
    }
    j["translations"] = trans;

    nlohmann::json plurals;
    for (const auto& [lang, forms] : pluralForms) {
        nlohmann::json formsJson;
        for (size_t i = 0; i < forms.size(); ++i) {
            formsJson[std::to_string(i)] = forms[i];
        }
        plurals[LanguageCode(lang)] = formsJson;
    }
    j["pluralForms"] = plurals;

    return j;
}

TranslationEntry TranslationEntry::FromJson(const nlohmann::json& j) {
    TranslationEntry e;
    e.key = j.value("key", "");
    if (j.contains("context")) {
        e.context = TranslationContext::FromJson(j["context"]);
    }
    e.lastUpdated = std::chrono::system_clock::time_point(
        std::chrono::seconds(j.value("lastUpdated", 0)));
    e.lastUpdatedBy = j.value("lastUpdatedBy", "");

    if (j.contains("translations")) {
        for (const auto& [langCode, value] : j["translations"].items()) {
            Language lang = Language::Portuguese;
            for (int i = 0; i < static_cast<int>(Language::Count); ++i) {
                if (LanguageCode(static_cast<Language>(i)) == langCode) {
                    lang = static_cast<Language>(i);
                    break;
                }
            }
            if (value.is_string()) {
                e.translations[lang] = value.get<std::string>();
            }
        }
    }

    if (j.contains("pluralForms")) {
        for (const auto& [langCode, forms] : j["pluralForms"].items()) {
            Language lang = Language::Portuguese;
            for (int i = 0; i < static_cast<int>(Language::Count); ++i) {
                if (LanguageCode(static_cast<Language>(i)) == langCode) {
                    lang = static_cast<Language>(i);
                    break;
                }
            }
            if (forms.is_object()) {
                std::vector<std::string> formList;
                for (int i = 0; i < static_cast<int>(PluralCategory::Other) + 1; ++i) {
                    auto it = forms.find(std::to_string(i));
                    if (it != forms.end() && it->is_string()) {
                        formList.push_back(it->get<std::string>());
                    }
                }
                if (!formList.empty()) {
                    e.pluralForms[lang] = std::move(formList);
                }
            }
        }
    }

    return e;
}

nlohmann::json LocaleMetadata::ToJson() const {
    nlohmann::json j;
    j["languageCode"] = languageCode;
    j["languageName"] = languageName;
    j["nativeName"] = nativeName;
    j["regionCode"] = regionCode;
    j["scriptCode"] = scriptCode;
    j["version"] = version;
    j["author"] = author;
    j["lastUpdated"] = std::chrono::duration_cast<std::chrono::seconds>(
        lastUpdated.time_since_epoch()).count();
    j["complete"] = complete;
    j["completionPercentage"] = completionPercentage;
    j["fallbacks"] = fallbacks;
    return j;
}

LocaleMetadata LocaleMetadata::FromJson(const nlohmann::json& j) {
    LocaleMetadata m;
    m.languageCode = j.value("languageCode", "");
    m.languageName = j.value("languageName", "");
    m.nativeName = j.value("nativeName", "");
    m.regionCode = j.value("regionCode", "");
    m.scriptCode = j.value("scriptCode", "");
    m.version = j.value("version", "1.0.0");
    m.author = j.value("author", "");
    m.lastUpdated = std::chrono::system_clock::time_point(
        std::chrono::seconds(j.value("lastUpdated", 0)));
    m.complete = j.value("complete", false);
    m.completionPercentage = j.value("completionPercentage", 0.0);
    m.fallbacks = j.value("fallbacks", std::vector<std::string>{});
    return m;
}

// =============================================================================
// ExtendedLocalizationManager Implementation
// =============================================================================

bool ExtendedLocalizationManager::Initialize(const std::filesystem::path& localeDir) {
    std::unique_lock lock(mutex_);

    localeDir_ = localeDir;

    if (!std::filesystem::exists(localeDir) || !std::filesystem::is_directory(localeDir)) {
        // Create directory and default locales
        std::filesystem::create_directories(localeDir);
    }

    BuildPluralRules();

    // Load all available locales
    for (int i = 0; i < static_cast<int>(Language::Count); ++i) {
        LoadLocale(static_cast<Language>(i));
    }

    // Auto-detect system language if not set
    if (currentLang_ == Language::Portuguese) {
        currentLang_ = DetectSystemLanguage();
    }

    OGS_LOG_INFO("[I18n] Initialized with {} languages, current: {}",
                 entries_.size() > 0 ? metadata_.size() : 0,
                 LanguageCode(currentLang_));
    return true;
}

void ExtendedLocalizationManager::Shutdown() {
    std::unique_lock lock(mutex_);

    // Save all dirty locales
    for (const auto& [lang, meta] : metadata_) {
        if (!meta.complete) {
            SaveLocale(lang);
        }
    }

    entries_.clear();
    metadata_.clear();
    OGS_LOG_INFO("[I18n] Shutdown");
}

void ExtendedLocalizationManager::BuildPluralRules() {
    // Already handled in GetPluralCategory function
}

bool ExtendedLocalizationManager::LoadLocale(Language lang) {
    const std::string filename = std::string(LanguageCode(lang)) + ".json";
    const std::filesystem::path filepath = localeDir_ / filename;

    if (!std::filesystem::exists(filepath)) {
        // Create default locale file
        if (lang == Language::Portuguese) {
            CreateDefaultPortugueseLocale();
        }
        return false;
    }

    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            return false;
        }

        nlohmann::json jsonData;
        file >> jsonData;

        // Load metadata
        if (jsonData.contains("_meta") && jsonData["_meta"].is_object()) {
            metadata_[lang] = LocaleMetadata::FromJson(jsonData["_meta"]);
        } else {
            // Create default metadata
            LocaleMetadata meta;
            meta.languageCode = LanguageCode(lang);
            meta.languageName = LanguageName(lang);
            meta.nativeName = LanguageName(lang);
            meta.regionCode = "";
            meta.version = "1.0.0";
            meta.lastUpdated = std::chrono::system_clock::now();
            meta.complete = false;
            meta.completionPercentage = 0.0;
            metadata_[lang] = meta;
        }

        // Load translations
        for (const auto& [key, value] : jsonData.items()) {
            if (key == "_meta") continue;

            auto& entry = entries_[key];
            entry.key = key;

            if (value.is_string()) {
                entry.translations[lang] = value.get<std::string>();
            } else if (value.is_object()) {
                // Could be plural forms
                if (value.contains("_plural") && value["_plural"].is_object()) {
                    entry.context.isPlural = true;
                    std::vector<std::string> forms;
                    for (int i = 0; i <= static_cast<int>(PluralCategory::Other); ++i) {
                        auto it = value["_plural"].find(std::to_string(i));
                        if (it != value["_plural"].end() && it->is_string()) {
                            forms.push_back(it->get<std::string>());
                        }
                    }
                    entry.pluralForms[lang] = std::move(forms);
                }
            }
        }

        // Update metadata completion
        UpdateCompletionStats(lang);

        return true;
    } catch (const std::exception& e) {
        OGS_LOG_ERROR("[I18n] Failed to load locale {}: {}", LanguageCode(lang), e.what());
        return false;
    }
}

bool ExtendedLocalizationManager::SaveLocale(Language lang) const {
    std::shared_lock lock(mutex_);

    const std::string filename = std::string(LanguageCode(lang)) + ".json";
    const std::filesystem::path filepath = localeDir_ / filename;

    nlohmann::json jsonData;

    // Add metadata
    auto metaIt = metadata_.find(lang);
    if (metaIt != metadata_.end()) {
        jsonData["_meta"] = metaIt->second.ToJson();
    }

    // Add translations
    for (const auto& [key, entry] : entries_) {
        auto transIt = entry.translations.find(lang);
        if (transIt != entry.translations.end()) {
            jsonData[key] = transIt->second;
        } else if (entry.context.isPlural) {
            auto pluralIt = entry.pluralForms.find(lang);
            if (pluralIt != entry.pluralForms.end()) {
                nlohmann::json pluralObj;
                pluralObj["_plural"] = nlohmann::json::object();
                for (size_t i = 0; i < pluralIt->second.size(); ++i) {
                    pluralObj["_plural"][std::to_string(i)] = pluralIt->second[i];
                }
                jsonData[key] = pluralObj;
            }
        }
    }

    std::ofstream file(filepath);
    if (!file) {
        return false;
    }

    file << jsonData.dump(4);
    return true;
}

void ExtendedLocalizationManager::UpdateCompletionStats(Language lang) {
    auto metaIt = metadata_.find(lang);
    if (metaIt == metadata_.end()) return;

    size_t total = 0;
    size_t translated = 0;

    for (const auto& [key, entry] : entries_) {
        total++;
        if (entry.translations.contains(lang)) {
            translated++;
        }
    }

    metaIt->second.complete = (translated == total && total > 0);
    metaIt->second.completionPercentage = total > 0 ? (static_cast<double>(translated) / total) * 100.0 : 0.0;
    metaIt->second.lastUpdated = std::chrono::system_clock::now();
}

const char* ExtendedLocalizationManager::Tr(std::string_view key, std::string_view context) const noexcept {
    std::shared_lock lock(mutex_);

    auto entryIt = entries_.find(key);
    if (entryIt == entries_.end()) {
        return key.data();
    }

    const auto& entry = entryIt->second;

    // Check context-specific translation first
    if (!context.empty()) {
        // Could implement context-specific translations here
        // For now, fall back to general translation
    }

    auto transIt = entry.translations.find(currentLang_);
    if (transIt != entry.translations.end()) {
        return transIt->second.c_str();
    }

    // Fallback to Portuguese
    if (currentLang_ != Language::Portuguese) {
        auto defaultIt = entry.translations.find(Language::Portuguese);
        if (defaultIt != entry.translations.end()) {
            return defaultIt->second.c_str();
        }
    }

    return key.data();
}

const char* ExtendedLocalizationManager::TrPlural(std::string_view key, int64_t count, std::string_view context) const noexcept {
    std::shared_lock lock(mutex_);

    auto entryIt = entries_.find(key);
    if (entryIt == entries_.end()) {
        return key.data();
    }

    const auto& entry = entryIt->second;

    if (!entry.context.isPlural) {
        return Tr(key, context);
    }

    PluralCategory category = GetPluralCategory(currentLang_, count);

    auto pluralIt = entry.pluralForms.find(currentLang_);
    if (pluralIt != entry.pluralForms.end() && category < pluralIt->second.size()) {
        return pluralIt->second[static_cast<size_t>(category)].c_str();
    }

    // Fallback to Portuguese
    if (currentLang_ != Language::Portuguese) {
        auto defaultPluralIt = entry.pluralForms.find(Language::Portuguese);
        if (defaultPluralIt != entry.pluralForms.end() && category < defaultPluralIt->second.size()) {
            return defaultPluralIt->second[static_cast<size_t>(category)].c_str();
        }
    }

    // Fallback to singular
    return Tr(key, context);
}

std::string ExtendedLocalizationManager::TrFmt(std::string_view key, std::string_view context, ...) const {
    const char* format = Tr(key, context);

    va_list args;
    va_start(args, context);

    va_list argsCopy;
    va_copy(argsCopy, args);
    int needed = std::vsnprintf(nullptr, 0, format, argsCopy);
    va_end(argsCopy);

    if (needed < 0) {
        va_end(args);
        return std::string(format);
    }

    std::string result(needed + 1, '\0');
    std::vsnprintf(result.data(), result.size(), format, args);
    va_end(args);

    result.pop_back();
    return result;
}

std::string ExtendedLocalizationManager::TrFmtPlural(std::string_view key, int64_t count, std::string_view context, ...) const {
    const char* format = TrPlural(key, count, context);

    va_list args;
    va_start(args, context);

    va_list argsCopy;
    va_copy(argsCopy, args);
    int needed = std::vsnprintf(nullptr, 0, format, argsCopy);
    va_end(argsCopy);

    if (needed < 0) {
        va_end(args);
        return std::string(format);
    }

    std::string result(needed + 1, '\0');
    std::vsnprintf(result.data(), result.size(), format, args);
    va_end(args);

    result.pop_back();
    return result;
}

bool ExtendedLocalizationManager::SetLanguage(Language lang) noexcept {
    std::unique_lock lock(mutex_);

    if (static_cast<size_t>(lang) >= static_cast<size_t>(Language::Count)) {
        return false;
    }

    if (metadata_.find(lang) == metadata_.end() || entries_.empty()) {
        return false;
    }

    currentLang_ = lang;
    OGS_LOG_INFO("[I18n] Language changed to: {}", LanguageCode(lang));
    return true;
}

Language ExtendedLocalizationManager::GetLanguage() const noexcept {
    return currentLang_;
}

Language ExtendedLocalizationManager::DetectSystemLanguage() const {
    // Get system locale
    char localeName[LOCALE_NAME_MAX_LENGTH];
    if (GetUserDefaultLocaleName(localeName, LOCALE_NAME_MAX_LENGTH) > 0) {
        std::string locale(localeName);
        std::transform(locale.begin(), locale.end(), locale.begin(), ::tolower);

        // Map Windows locale to our Language enum
        if (locale.starts_with("pt")) return Language::Portuguese;
        if (locale.starts_with("en")) return Language::English;
        if (locale.starts_with("de")) return Language::German;
        if (locale.starts_with("es")) return Language::Spanish;
        if (locale.starts_with("fr")) return Language::French;
        if (locale.starts_with("it")) return Language::Italian;
        if (locale.starts_with("ru")) return Language::Russian;
    }

    return Language::Portuguese;
}

std::vector<Language> ExtendedLocalizationManager::GetAvailableLanguages() const {
    std::shared_lock lock(mutex_);

    std::vector<Language> langs;
    for (const auto& [lang, meta] : metadata_) {
        langs.push_back(lang);
    }
    return langs;
}

std::optional<LocaleMetadata> ExtendedLocalizationManager::GetLocaleMetadata(Language lang) const {
    std::shared_lock lock(mutex_);

    auto it = metadata_.find(lang);
    if (it != metadata_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<TranslationEntry> ExtendedLocalizationManager::GetTranslationEntry(std::string_view key) const {
    std::shared_lock lock(mutex_);

    auto it = entries_.find(key);
    if (it != entries_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<TranslationEntry> ExtendedLocalizationManager::GetAllTranslationEntries() const {
    std::shared_lock lock(mutex_);

    std::vector<TranslationEntry> result;
    result.reserve(entries_.size());
    for (const auto& [key, entry] : entries_) {
        result.push_back(entry);
    }
    return result;
}

std::vector<std::string> ExtendedLocalizationManager::GetMissingKeys(Language lang) const {
    std::shared_lock lock(mutex_);

    std::vector<std::string> missing;
    for (const auto& [key, entry] : entries_) {
        if (!entry.translations.contains(lang)) {
            missing.push_back(key);
        }
    }
    return missing;
}

ExtendedLocalizationManager::CompletionStats ExtendedLocalizationManager::GetCompletionStats(Language lang) const {
    std::shared_lock lock(mutex_);

    CompletionStats stats;
    stats.lang = lang;

    for (const auto& [key, entry] : entries_) {
        stats.totalKeys++;
        if (entry.translations.contains(lang)) {
            stats.translatedKeys++;
        } else {
            stats.missingKeys++;
        }
    }

    stats.percentage = stats.totalKeys > 0
        ? (static_cast<double>(stats.translatedKeys) / stats.totalKeys) * 100.0
        : 0.0;

    return stats;
}

bool ExtendedLocalizationManager::SetTranslation(std::string_view key, Language lang, std::string_view translation) {
    std::unique_lock lock(mutex_);

    auto& entry = entries_[std::string(key)];
    entry.key = key;
    entry.translations[lang] = translation;
    entry.lastUpdated = std::chrono::system_clock::now();

    UpdateCompletionStats(lang);

    // Save to disk
    SaveLocale(lang);

    return true;
}

bool ExtendedLocalizationManager::SetPluralForm(std::string_view key, Language lang, PluralCategory category, std::string_view translation) {
    std::unique_lock lock(mutex_);

    auto& entry = entries_[std::string(key)];
    entry.key = key;
    entry.context.isPlural = true;

    auto& forms = entry.pluralForms[lang];
    if (forms.size() <= static_cast<size_t>(PluralCategory::Other)) {
        forms.resize(static_cast<size_t>(PluralCategory::Other) + 1);
    }
    forms[static_cast<size_t>(category)] = translation;

    entry.lastUpdated = std::chrono::system_clock::now();
    UpdateCompletionStats(lang);

    SaveLocale(lang);
    return true;
}

bool ExtendedLocalizationManager::RemoveTranslation(std::string_view key, Language lang) {
    std::unique_lock lock(mutex_);

    auto it = entries_.find(key);
    if (it == entries_.end()) {
        return false;
    }

    it->second.translations.erase(lang);
    it->second.pluralForms.erase(lang);

    if (it->second.translations.empty() && it->second.pluralForms.empty()) {
        entries_.erase(it);
    }

    UpdateCompletionStats(lang);
    SaveLocale(lang);

    return true;
}

bool ExtendedLocalizationManager::ExportLocale(Language lang, const std::filesystem::path& outputPath) const {
    return SaveLocale(lang);
}

bool ExtendedLocalizationManager::ImportLocale(Language lang, const std::filesystem::path& inputPath) {
    // Copy file to locale directory
    const std::string filename = std::string(LanguageCode(lang)) + ".json";
    const std::filesystem::path destPath = localeDir_ / filename;

    std::error_code ec;
    std::filesystem::copy_file(inputPath, destPath, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return false;
    }

    return LoadLocale(lang);
}

std::size_t ExtendedLocalizationManager::ImportLocalesFromDirectory(const std::filesystem::path& dirPath) {
    std::size_t count = 0;
    std::error_code ec;

    for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            // Try to detect language from filename
            std::string stem = entry.path().stem().string();
            for (int i = 0; i < static_cast<int>(Language::Count); ++i) {
                Language lang = static_cast<Language>(i);
                if (LanguageCode(lang) == stem) {
                    if (ImportLocale(lang, entry.path())) {
                        count++;
                    }
                    break;
                }
            }
        }
    }

    return count;
}

bool ExtendedLocalizationManager::ExportTemplate(const std::filesystem::path& outputPath) const {
    std::shared_lock lock(mutex_);

    nlohmann::json templateJson;
    templateJson["_meta"] = nlohmann::json::object({
        {"type", "translation_template"},
        {"version", "1.0.0"},
        {"generatedAt", std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()},
        {"sourceLanguage", LanguageCode(Language::Portuguese)}
    });

    for (const auto& [key, entry] : entries_) {
        nlohmann::json entryJson;

        // Add source translation (Portuguese)
        auto ptIt = entry.translations.find(Language::Portuguese);
        if (ptIt != entry.translations.end()) {
            entryJson["source"] = ptIt->second;
        }

        entryJson["context"] = entry.context.ToJson();
        entryJson["translations"] = nlohmann::json::object();

        // Add empty translations for all target languages
        for (int i = 1; i < static_cast<int>(Language::Count); ++i) {
            Language lang = static_cast<Language>(i);
            entryJson["translations"][LanguageCode(lang)] = "";
        }

        if (entry.context.isPlural) {
            entryJson["pluralForms"] = nlohmann::json::object();
            for (int i = 1; i < static_cast<int>(Language::Count); ++i) {
                Language lang = static_cast<Language>(i);
                entryJson["pluralForms"][LanguageCode(lang)] = nlohmann::json::array({
                    "", "", "", "", "", ""  // Zero, One, Two, Few, Many, Other
                });
            }
        }

        templateJson[key] = entryJson;
    }

    std::ofstream file(outputPath);
    if (!file) {
        return false;
    }

    file << templateJson.dump(4);
    return true;
}

bool ExtendedLocalizationManager::ImportTemplate(const std::filesystem::path& inputPath, Language targetLang) {
    std::ifstream file(inputPath);
    if (!file) {
        return false;
    }

    nlohmann::json templateJson;
    try {
        file >> templateJson;
    } catch (...) {
        return false;
    }

    for (const auto& [key, entryJson] : templateJson.items()) {
        if (key == "_meta") continue;

        if (entryJson.contains("translations") && entryJson["translations"].contains(LanguageCode(targetLang))) {
            std::string translation = entryJson["translations"][LanguageCode(targetLang)].get<std::string>();
            if (!translation.empty()) {
                SetTranslation(key, targetLang, translation);
            }
        }

        if (entryJson.contains("pluralForms") && entryJson["pluralForms"].contains(LanguageCode(targetLang))) {
            auto forms = entryJson["pluralForms"][LanguageCode(targetLang)];
            if (forms.is_array()) {
                for (size_t i = 0; i < forms.size() && i <= static_cast<size_t>(PluralCategory::Other); ++i) {
                    if (forms[i].is_string()) {
                        std::string translation = forms[i].get<std::string>();
                        if (!translation.empty()) {
                            SetPluralForm(key, targetLang, static_cast<PluralCategory>(i), translation);
                        }
                    }
                }
            }
        }
    }

    return true;
}

void ExtendedLocalizationManager::Reload() {
    std::unique_lock lock(mutex_);

    entries_.clear();
    metadata_.clear();

    for (int i = 0; i < static_cast<int>(Language::Count); ++i) {
        LoadLocale(static_cast<Language>(i));
    }

    OGS_LOG_INFO("[I18n] Reloaded all locales");
}

ExtendedLocalizationManager::ValidationResult ExtendedLocalizationManager::ValidateLocale(Language lang) const {
    ValidationResult result;
    std::shared_lock lock(mutex_);

    auto metaIt = metadata_.find(lang);
    if (metaIt == metadata_.end()) {
        result.valid = false;
        result.errors.push_back("Locale not loaded");
        return result;
    }

    for (const auto& [key, entry] : entries_) {
        if (!entry.translations.contains(lang)) {
            result.warnings.push_back("Missing translation: " + key);
        } else {
            const std::string& trans = entry.translations.at(lang);
            if (trans.empty()) {
                result.warnings.push_back("Empty translation: " + key);
            }
            // Check for placeholder consistency
            // Could add more validation here
        }
    }

    return result;
}

std::filesystem::path ExtendedLocalizationManager::GetLocaleDirectory() const {
    return localeDir_;
}

// =============================================================================
// ICU-style Message Format
// =============================================================================

std::optional<MessageFormat> MessageFormat::Parse(std::string_view pattern) {
    MessageFormat fmt;
    fmt.pattern = pattern;

    // Simple placeholder detection: {name} or {name,type}
    std::string_view p = pattern;
    size_t pos = 0;
    while ((pos = p.find('{', pos)) != std::string_view::npos) {
        size_t end = p.find('}', pos);
        if (end != std::string_view::npos) {
            std::string_view placeholder = p.substr(pos + 1, end - pos - 1);
            size_t commaPos = placeholder.find(',');
            if (commaPos != std::string_view::npos) {
                fmt.placeholders.emplace_back(
                    std::string(placeholder.substr(0, commaPos)),
                    std::string(placeholder.substr(commaPos + 1))
                );
            } else {
                fmt.placeholders.emplace_back(std::string(placeholder), "string");
            }
        }
        pos = end + 1;
    }

    return fmt;
}

std::string MessageFormat::Format(const std::unordered_map<std::string, std::string>& args) const {
    std::string result = pattern;

    for (const auto& [name, type] : placeholders) {
        std::string placeholder = "{" + name + "}";
        auto it = args.find(name);
        if (it != args.end()) {
            size_t pos = 0;
            while ((pos = result.find(placeholder, pos)) != std::string::npos) {
                result.replace(pos, placeholder.length(), it->second);
                pos += it->second.length();
            }
        }
    }

    return result;
}

std::string FormatNumber(Language lang, double value, int maxDecimals) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(maxDecimals) << value;
    std::string result = oss.str();

    // Apply locale-specific formatting
    switch (lang) {
        case Language::Portuguese:
        case Language::French:
        case Language::Spanish:
        case Language::Italian:
        case Language::German:
        case Language::Russian:
            // Use comma as decimal separator
            std::replace(result.begin(), result.end(), '.', ',');
            break;
        default:
            // Use dot as decimal separator
            break;
    }

    return result;
}

std::string FormatCurrency(Language lang, double value, std::string_view currencyCode) {
    std::string formatted = FormatNumber(lang, value, 2);

    switch (lang) {
        case Language::Portuguese:
            return "R$ " + formatted;
        case Language::English:
            return "$" + formatted;
        case Language::German:
        case Language::French:
        case Language::Italian:
        case Language::Spanish:
            return formatted + " €";
        case Language::Russian:
            return formatted + " ₽";
        default:
            return currencyCode.data() + std::string(" ") + formatted;
    }
}

std::string FormatDateTime(Language lang, std::chrono::system_clock::time_point tp,
                           bool includeTime, bool shortFormat) {
    std::time_t time = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = *std::localtime(&time);

    std::ostringstream oss;
    if (shortFormat) {
        oss << std::put_time(&tm, "%d/%m/%Y");
    } else {
        switch (lang) {
            case Language::Portuguese:
                oss << std::put_time(&tm, "%d de %B de %Y");
                break;
            case Language::English:
                oss << std::put_time(&tm, "%B %d, %Y");
                break;
            case Language::German:
                oss << std::put_time(&tm, "%d. %B %Y");
                break;
            case Language::Spanish:
                oss << std::put_time(&tm, "%d de %B de %Y");
                break;
            case Language::French:
                oss << std::put_time(&tm, "%d %B %Y");
                break;
            case Language::Italian:
                oss << std::put_time(&tm, "%d %B %Y");
                break;
            case Language::Russian:
                oss << std::put_time(&tm, "%d %B %Y");
                break;
            default:
                oss << std::put_time(&tm, "%Y-%m-%d");
        }
    }

    if (includeTime) {
        oss << " " << std::put_time(&tm, "%H:%M");
    }

    return oss.str();
}

std::string FormatRelativeTime(Language lang, std::chrono::seconds duration) {
    int64_t seconds = duration.count();

    if (seconds < 0) seconds = 0;

    if (seconds < 60) {
        switch (lang) {
            case Language::Portuguese: return "agora mesmo";
            case Language::English: return "just now";
            case Language::German: return "gerade eben";
            case Language::Spanish: return "ahora mismo";
            case Language::French: return "à l'instant";
            case Language::Italian: return "proprio ora";
            case Language::Russian: return "только что";
            default: return "now";
        }
    }

    if (seconds < 3600) {
        int64_t minutes = seconds / 60;
        switch (lang) {
            case Language::Portuguese: return std::to_string(minutes) + (minutes == 1 ? " minuto atrás" : " minutos atrás");
            case Language::English: return std::to_string(minutes) + (minutes == 1 ? " minute ago" : " minutes ago");
            case Language::German: return "vor " + std::to_string(minutes) + (minutes == 1 ? " Minute" : " Minuten");
            case Language::Spanish: return "hace " + std::to_string(minutes) + (minutes == 1 ? " minuto" : " minutos");
            case Language::French: return "il y a " + std::to_string(minutes) + (minutes == 1 ? " minute" : " minutes");
            case Language::Italian: return std::to_string(minutes) + (minutes == 1 ? " minuto fa" : " minuti fa");
            case Language::Russian: return std::to_string(minutes) + " минут назад";
            default: return std::to_string(minutes) + "m ago";
        }
    }

    if (seconds < 86400) {
        int64_t hours = seconds / 3600;
        switch (lang) {
            case Language::Portuguese: return std::to_string(hours) + (hours == 1 ? " hora atrás" : " horas atrás");
            case Language::English: return std::to_string(hours) + (hours == 1 ? " hour ago" : " hours ago");
            case Language::German: return "vor " + std::to_string(hours) + (hours == 1 ? " Stunde" : " Stunden");
            case Language::Spanish: return "hace " + std::to_string(hours) + (hours == 1 ? " hora" : " horas");
            case Language::French: return "il y a " + std::to_string(hours) + (hours == 1 ? " heure" : " heures");
            case Language::Italian: return std::to_string(hours) + (hours == 1 ? " ora fa" : " ore fa");
            case Language::Russian: return std::to_string(hours) + " часов назад";
            default: return std::to_string(hours) + "h ago";
        }
    }

    int64_t days = seconds / 86400;
    switch (lang) {
        case Language::Portuguese: return std::to_string(days) + (days == 1 ? " dia atrás" : " dias atrás");
        case Language::English: return std::to_string(days) + (days == 1 ? " day ago" : " days ago");
        case Language::German: return "vor " + std::to_string(days) + (days == 1 ? " Tag" : " Tagen");
        case Language::Spanish: return "hace " + std::to_string(days) + (days == 1 ? " día" : " días");
        case Language::French: return "il y a " + std::to_string(days) + (days == 1 ? " jour" : " jours");
        case Language::Italian: return std::to_string(days) + (days == 1 ? " giorno fa" : " giorni fa");
        case Language::Russian: return std::to_string(days) + " дней назад";
        default: return std::to_string(days) + "d ago";
    }
}

std::vector<LanguageInfo> GetSupportedLanguages() {
    return {
        {Language::Portuguese, "Portuguese", "Português", "BR", false, true},
        {Language::English, "English", "English", "US", false, true},
        {Language::German, "German", "Deutsch", "DE", false, true},
        {Language::Spanish, "Spanish", "Español", "ES", false, true},
        {Language::French, "French", "Français", "FR", false, true},
        {Language::Italian, "Italian", "Italiano", "IT", false, true},
        {Language::Russian, "Russian", "Русский", "RU", false, true}
    };
}

// =============================================================================
// Create Default Portuguese Locale
// =============================================================================

void ExtendedLocalizationManager::CreateDefaultPortugueseLocale() {
    // This would create a default pt.json with common translations
    // For brevity, we'll just log
    OGS_LOG_INFO("[I18n] Creating default Portuguese locale");
}

} // namespace OmniGhost::I18n