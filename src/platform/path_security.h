#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <cwctype>
#include <string>
#include <string_view>

namespace OmniGhost::Platform {

[[nodiscard]] inline bool IsPathWithinRoot(const std::filesystem::path& root,
                                           const std::filesystem::path& candidate) noexcept
{
    std::error_code error;
    const auto canonicalRoot = std::filesystem::weakly_canonical(root, error);
    if (error)
        return false;
    error.clear();
    const auto canonicalCandidate = std::filesystem::weakly_canonical(candidate, error);
    if (error)
        return false;

#ifdef _WIN32
    auto equalPart = [](const std::filesystem::path& left, const std::filesystem::path& right) {
        std::wstring a = left.native();
        std::wstring b = right.native();
        std::transform(a.begin(), a.end(), a.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        std::transform(b.begin(), b.end(), b.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return a == b;
    };
#else
    auto equalPart = [](const std::filesystem::path& left, const std::filesystem::path& right) {
        return left == right;
    };
#endif

    auto child = canonicalCandidate.begin();
    for (auto parent = canonicalRoot.begin(); parent != canonicalRoot.end(); ++parent, ++child) {
        if (child == canonicalCandidate.end() || !equalPart(*parent, *child))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool IsSafeStaticRequestTarget(std::string_view target) noexcept
{
    const auto query = target.find('?');
    if (query != std::string_view::npos)
        target = target.substr(0, query);
    if (target == "/")
        return true;
    if (target.empty() || target.front() != '/')
        return false;

    // Deliberately reject encoded paths. This keeps the static-file policy simple and
    // prevents alternative encodings from bypassing the lexical checks below.
    if (target.find('%') != std::string_view::npos ||
        target.find('\\') != std::string_view::npos ||
        target.find(':') != std::string_view::npos ||
        target.find("..") != std::string_view::npos)
        return false;

    for (unsigned char c : target) {
        if (c < 0x20 || c == 0x7f)
            return false;
    }
    return true;
}

} // namespace OmniGhost::Platform
