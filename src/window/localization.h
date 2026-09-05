#pragma once

// Centralized UI localization (PT / EN).
// Display strings only — never rename internal identifiers.

namespace Loc {

    // Returns the translated string for the active language.
    // Fallback: English, then the key itself (never empty).
    const char* Tr(const char* key);

    // Same as Tr, but returns "translated###key" so ImGui widget IDs
    // stay stable across language changes.
    const char* TrID(const char* key);

    // Language display names for the Configs combo (not translated keys).
    const char* LanguageName(int index);
    int LanguageCount();

} // namespace Loc
