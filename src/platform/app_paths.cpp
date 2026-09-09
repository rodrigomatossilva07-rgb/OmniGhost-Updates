#include "app_paths.h"

#include <Windows.h>
#include <ShlObj.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwctype>
#include <system_error>

namespace fs = std::filesystem;

namespace {

fs::path EnvironmentPath(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required <= 1)
        return {};

    std::wstring buffer(static_cast<size_t>(required), L'\0');
    const DWORD length = GetEnvironmentVariableW(
        name,
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
        return {};

    buffer.resize(length);
    return fs::path(buffer);
}

fs::path TemporaryBasePath() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetTempPathW(
        static_cast<DWORD>(buffer.size()),
        buffer.data());

    if (length != 0 && length < buffer.size()) {
        buffer.resize(length);
        return fs::path(buffer);
    }

    std::error_code error;
    const fs::path fallback = fs::temp_directory_path(error);
    return error ? fs::current_path(error) : fallback;
}

fs::path KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    fs::path result;

    const HRESULT status = SHGetKnownFolderPath(
        id,
        KF_FLAG_CREATE,
        nullptr,
        &raw);

    if (SUCCEEDED(status) && raw && *raw)
        result = raw;

    if (raw)
        CoTaskMemFree(raw);

    return result;
}

fs::path LocalApplicationDataBase() {
    fs::path result = KnownFolder(FOLDERID_LocalAppData);
    if (!result.empty())
        return result;

    // SHGetKnownFolderPath can fail when the process starts before COM/Shell
    // initialization. LOCALAPPDATA is a reliable fallback for the same folder.
    result = EnvironmentPath(L"LOCALAPPDATA");
    if (!result.empty())
        return result;

    // Last-resort absolute fallback. Never return a relative path because that
    // would make logs and updater files depend on the current working directory.
    return TemporaryBasePath();
}

void CopyMissing(const fs::path& source, const fs::path& destination) {
    std::error_code error;
    if (!fs::exists(source, error))
        return;

    fs::create_directories(destination, error);

    for (fs::recursive_directory_iterator iterator(
             source,
             fs::directory_options::skip_permission_denied,
             error),
         end;
         iterator != end;
         iterator.increment(error)) {
        if (error) {
            error.clear();
            continue;
        }

        if (iterator->is_symlink(error)) {
            error.clear();
            iterator.disable_recursion_pending();
            continue;
        }
        error.clear();

        const fs::path relative = fs::relative(iterator->path(), source, error);
        if (error) {
            error.clear();
            continue;
        }

        const fs::path target = destination / relative;
        if (iterator->is_directory(error)) {
            fs::create_directories(target, error);
        } else if (iterator->is_regular_file(error) && !fs::exists(target, error)) {
            fs::create_directories(target.parent_path(), error);
            fs::copy_file(iterator->path(), target, fs::copy_options::none, error);
        }

        error.clear();
    }
}

void CopyFileMissing(const fs::path& source, const fs::path& destination) {
    std::error_code error;
    if (!fs::is_regular_file(source, error) || fs::exists(destination, error))
        return;

    fs::create_directories(destination.parent_path(), error);
    if (error)
        return;
    fs::copy_file(source, destination, fs::copy_options::none, error);
}

} // namespace

namespace OmniGhost::Paths {

fs::path Executable() {
    std::wstring buffer(512, L'\0');
    for (;;) {
        SetLastError(ERROR_SUCCESS);
        const DWORD length = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));

        if (length == 0)
            break;

        if (length < buffer.size() - 1) {
            buffer.resize(length);
            std::error_code error;
            const fs::path canonical = fs::weakly_canonical(fs::path(buffer), error);
            return error ? fs::path(buffer) : canonical;
        }

        if (buffer.size() >= 32768)
            break;
        buffer.resize((std::min)(buffer.size() * 2, static_cast<size_t>(32768)), L'\0');
    }

    std::error_code error;
    const fs::path fallback = fs::absolute(L"OmniGhost.exe", error);
    return error ? fs::path(L"OmniGhost.exe") : fallback;
}

fs::path InstallDirectory() {
    return Executable().parent_path();
}

std::wstring InstallIdentity() {
    // Scope process-lifetime state to the concrete installation directory.
    // This lets a developer build in one folder while testing a packaged copy
    // in another without both copies colliding on the same named mutex/marker.
    std::wstring normalized = InstallDirectory().wstring();
    std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
    while (normalized.size() > 3 && !normalized.empty() && normalized.back() == L'\\')
        normalized.pop_back();

    std::uint64_t hash = 14695981039346656037ull; // FNV-1a 64-bit
    for (wchar_t character : normalized) {
        const wchar_t folded = static_cast<wchar_t>(std::towlower(character));
        hash ^= static_cast<std::uint64_t>(static_cast<std::uint16_t>(folded));
        hash *= 1099511628211ull;
    }

    static constexpr wchar_t kHex[] = L"0123456789ABCDEF";
    std::wstring identity(16, L'0');
    for (size_t index = 0; index < identity.size(); ++index) {
        const unsigned shift = static_cast<unsigned>((identity.size() - 1 - index) * 4);
        identity[index] = kHex[(hash >> shift) & 0x0Fu];
    }
    return identity;
}

fs::path LocalData() {
    // Only mutable per-user state lives here. The executable remains wherever
    // the user launched it from and is never copied into LocalAppData.
    return LocalApplicationDataBase() / L"OmniGhost";
}

fs::path NativeRuntime() {
    return LocalData() / L"runtime";
}

fs::path Configs() {
    return LocalData() / L"Configs";
}

fs::path Cs2Configs() {
    return LocalData() / L"CS2";
}

fs::path Cache() {
    return LocalData() / L"cache";
}

fs::path Logs() {
    auto path = LocalData() / L"logs";
    std::error_code ec;
    fs::create_directories(path, ec);
    return path;
}

fs::path Updates() {
    auto path = LocalData() / L"updates";
    std::error_code ec;
    fs::create_directories(path, ec);
    return path;
}

fs::path Backups() {
    auto path = LocalData() / L"backups";
    std::error_code ec;
    fs::create_directories(path, ec);
    return path;
}

bool EnsureUserDirectories() {
    const std::array<fs::path, 5> directories = {
        LocalData(),
        NativeRuntime(),
        Configs(),
        Cs2Configs(),
        Cache()
    };

    for (const fs::path& directory : directories) {
        std::error_code error;
        fs::create_directories(directory, error);
        if (error || !fs::exists(directory, error) || !fs::is_directory(directory, error))
            return false;
    }

    return true;
}

void MigrateLegacyUserData() {
    EnsureUserDirectories();

    // Builds from the short-lived portable-root layout stored mutable state in
    // <drive>:\OmniGhost. Preserve it by copying only known user files and only
    // when the LocalAppData destination does not already exist. Immutable
    // runtime files and OmniGhost.exe are deliberately excluded.
    fs::path volumeRoot = Executable().root_path();
    if (volumeRoot.empty()) {
        volumeRoot = EnvironmentPath(L"SystemDrive");
        if (!volumeRoot.empty() && volumeRoot.filename() == volumeRoot)
            volumeRoot /= L"\\";
    }
    const fs::path portableRoot = volumeRoot / L"OmniGhost";
    std::error_code equivalentError;
    const bool sameRoot = fs::exists(portableRoot, equivalentError) &&
        fs::exists(LocalData(), equivalentError) &&
        fs::equivalent(portableRoot, LocalData(), equivalentError) &&
        !equivalentError;
    if (!sameRoot) {
        static constexpr std::array<const wchar_t*, 4> mutableDirectories = {
            L"Configs", L"CS2", L"cache", L"crash-dumps"
        };
        for (const wchar_t* name : mutableDirectories)
            CopyMissing(portableRoot / name, LocalData() / name);

        static constexpr std::array<const wchar_t*, 6> mutableFiles = {
            L"license.dat", L"auth.dat", L"settings.cfg", L"launcher_state.cfg",
            L"imgui.ini", L"logs.txt"
        };
        for (const wchar_t* name : mutableFiles)
            CopyFileMissing(portableRoot / name, LocalData() / name);
    }

    const fs::path install = InstallDirectory();
    CopyFileMissing(install / L"imgui.ini", LocalData() / L"imgui.ini");
    CopyMissing(install / L"Configs", Configs());
    CopyMissing(install / L"configs", Configs());

#ifdef OMNIGHOST_DEV_EXTERNAL_RESOURCES
    // Legacy source-tree migration is development-only. Customer builds must
    // neither require nor probe an install-adjacent data directory.
    const fs::path legacyData = install / L"data";
    std::error_code error;
    if (fs::exists(legacyData, error)) {
        for (const auto& entry : fs::directory_iterator(legacyData, error)) {
            if (entry.is_regular_file(error) && entry.path().extension() == L".cfg") {
                const fs::path target = Cs2Configs() / entry.path().filename();
                if (!fs::exists(target, error))
                    fs::copy_file(entry.path(), target, error);
            }
            error.clear();
        }
    }
#endif
}

bool IsProtectedInstallPath(const fs::path& relativePath) {
    if (relativePath.empty() || relativePath.is_absolute())
        return true;

    const fs::path normalized = relativePath.lexically_normal();
    if (normalized.empty() || normalized.is_absolute())
        return true;
    for (const auto& segment : normalized) {
        if (segment == L"..")
            return true;
    }

    const std::wstring first = normalized.begin()->wstring();
    const std::array<const wchar_t*, 8> protectedDirectories = {
        L"Configs",
        L"configs",
        L"logs",
        L"plugins",
        L"saves",
        L"licenses",
        L"cache",
        L"user-data"
    };

    for (const wchar_t* value : protectedDirectories) {
        if (_wcsicmp(first.c_str(), value) == 0)
            return true;
    }

    const std::wstring filename = normalized.filename().wstring();
    const std::array<const wchar_t*, 7> protectedFiles = {
        L"logs.txt",
        L"log.txt",
        L"auth.dat",
        L"auth_state.dat",
        L"license.dat",
        L"licence.dat",
        L"settings.json"
    };
    for (const wchar_t* value : protectedFiles) {
        if (_wcsicmp(filename.c_str(), value) == 0)
            return true;
    }

    const std::wstring extension = normalized.extension().wstring();
    const std::array<const wchar_t*, 5> protectedExtensions = {
        L".ogcfg", L".cfg", L".db", L".sqlite", L".sqlite3"
    };
    for (const wchar_t* value : protectedExtensions) {
        if (_wcsicmp(extension.c_str(), value) == 0)
            return true;
    }
    return false;
}

} // namespace OmniGhost::Paths
