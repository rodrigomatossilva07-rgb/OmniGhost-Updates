#include "localization.h"

#include <fstream>
#include <sstream>
#include <cstdarg>
#include <algorithm>
#include <shared_mutex>
#include <nlohmann/json.hpp>

namespace Loc {

bool LocalizationManager::Initialize(const std::filesystem::path& localeDir) {
    std::unique_lock lock(m_mutex);
    m_localeDir = localeDir;

    if (!std::filesystem::exists(localeDir) || !std::filesystem::is_directory(localeDir)) {
        return false;
    }

    LoadAllLanguages(localeDir);
    return true;
}

bool LocalizationManager::SetLanguage(Language lang) noexcept {
    std::shared_lock lock(m_mutex);

    if (static_cast<size_t>(lang) >= static_cast<size_t>(Language::Count)) {
        return false;
    }


    if (m_translations.find(lang) == m_translations.end() || m_translations[lang].empty()) {
        return false;
    }

    m_currentLang = lang;
    m_missingKeysDirty = true;
    return true;
}

const char* LocalizationManager::Tr(std::string_view key) const noexcept {
    std::shared_lock lock(m_mutex);

    auto langIt = m_translations.find(m_currentLang);
    if (langIt == m_translations.end()) {
        return key.data();
    }

    auto keyIt = langIt->second.find(std::string(key));
    if (keyIt == langIt->second.end()) {

        auto defaultIt = m_translations.find(Language::Portuguese);
        if (defaultIt != m_translations.end()) {
            auto defKeyIt = defaultIt->second.find(std::string(key));
            if (defKeyIt != defaultIt->second.end()) {
                return defKeyIt->second.c_str();
            }
        }
        return key.data();
    }

    return keyIt->second.c_str();
}

std::string LocalizationManager::TrFmt(std::string_view key, ...) const {
    const char* format = Tr(key);

    va_list args;
    va_start(args, key);


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

bool LocalizationManager::HasKey(std::string_view key) const noexcept {
    std::shared_lock lock(m_mutex);

    for (const auto& [lang, dict] : m_translations) {
        if (dict.find(std::string(key)) != dict.end()) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> LocalizationManager::GetAllKeys() const {
    std::shared_lock lock(m_mutex);

    std::vector<std::string> keys;
    std::unordered_map<std::string, bool> seen;

    for (const auto& [lang, dict] : m_translations) {
        for (const auto& [key, _] : dict) {
            if (!seen[key]) {
                seen[key] = true;
                keys.push_back(key);
            }
        }
    }

    std::sort(keys.begin(), keys.end());
    return keys;
}

void LocalizationManager::Reload() {
    std::unique_lock lock(m_mutex);
    m_translations.clear();
    m_missingKeysDirty = true;
    LoadAllLanguages(m_localeDir);
}

std::vector<std::string> LocalizationManager::GetMissingKeys(Language lang) const {
    std::shared_lock lock(m_mutex);

    if (m_missingKeysDirty) {

        const auto allKeys = GetAllKeys();
        for (const auto& [l, dict] : m_translations) {
            std::vector<std::string> missing;
            missing.reserve(allKeys.size());
            for (const auto& key : allKeys) {
                if (dict.find(key) == dict.end()) {
                    missing.push_back(key);
                }
            }
            m_missingKeysCache[l] = std::move(missing);
        }
        m_missingKeysDirty = false;
    }

    auto it = m_missingKeysCache.find(lang);
    if (it != m_missingKeysCache.end()) {
        return it->second;
    }
    return {};
}

bool LocalizationManager::LoadLanguage(Language lang) {
    const std::string filename = std::string(LanguageCode(lang)) + ".json";
    const std::filesystem::path filepath = m_localeDir / filename;

    if (!std::filesystem::exists(filepath)) {
        return false;
    }

    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            return false;
        }

        nlohmann::json jsonData;
        file >> jsonData;

        if (!jsonData.is_object()) {
            return false;
        }

        auto& dict = m_translations[lang];
        dict.clear();
        dict.reserve(jsonData.size());

        for (const auto& [key, value] : jsonData.items()) {
            if (value.is_string()) {
                dict.emplace(key, value.get<std::string>());
            }
        }

        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void LocalizationManager::LoadAllLanguages(const std::filesystem::path& localeDir) {

    LoadLanguage(Language::Portuguese);


    for (int i = 1; i < static_cast<int>(Language::Count); ++i) {
        LoadLanguage(static_cast<Language>(i));
    }

    m_missingKeysDirty = true;
}

}
