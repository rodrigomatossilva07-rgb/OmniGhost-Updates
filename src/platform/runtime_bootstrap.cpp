#include "runtime_bootstrap.h"
#include "text_encoding.h"
#include "session_log.h"
#include "app_paths.h"
#include "file_integrity.h"
#include "embedded_runtime_manifest.h"
#include "embedded_resources.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <shellapi.h>
#include <TlHelp32.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <set>

namespace fs = std::filesystem;

namespace {

bool IsSafeRelative(const fs::path& path) {
    if (path.empty() || path.is_absolute()) return false;
    for (const auto& part : path) {
        if (part == L".." || part == L".") return false;
    }
    return true;
}

void CopyTreeMissing(const fs::path& source, const fs::path& destination) {
    std::error_code ec;
    if (!fs::is_directory(source, ec)) return;
    fs::create_directories(destination, ec);
    for (fs::recursive_directory_iterator it(source,
             fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (it->is_symlink(ec)) { it.disable_recursion_pending(); ec.clear(); continue; }
        const fs::path relative = fs::relative(it->path(), source, ec);
        if (ec) { ec.clear(); continue; }
        const fs::path target = destination / relative;
        if (it->is_directory(ec)) {
            fs::create_directories(target, ec);
        } else if (it->is_regular_file(ec) && !fs::exists(target, ec)) {
            fs::create_directories(target.parent_path(), ec);
            fs::copy_file(it->path(), target, fs::copy_options::none, ec);
        }
        ec.clear();
    }
}

void MigrateLegacyMutableData(const fs::path& legacyRoot) {
    const fs::path destination = OmniGhost::Paths::LocalData();
    std::error_code ec;
    fs::create_directories(destination, ec);

    ec.clear();
    if (fs::exists(legacyRoot, ec) && fs::exists(destination, ec) &&
        fs::equivalent(legacyRoot, destination, ec) && !ec) {
        return;
    }

    static constexpr std::array<const wchar_t*, 7> kMutableDirectories = {
        L"Configs", L"CS2", L"logs", L"cache", L"updates", L"backups", L"crash-dumps"
    };
    for (const wchar_t* name : kMutableDirectories)
        CopyTreeMissing(legacyRoot / name, destination / name);

    static constexpr std::array<const wchar_t*, 6> kMutableFiles = {
        L"license.dat", L"auth.dat", L"settings.cfg", L"launcher_state.cfg",
        L"imgui.ini", L"logs.txt"
    };
    for (const wchar_t* name : kMutableFiles) {
        const fs::path source = legacyRoot / name;
        const fs::path target = destination / name;
        ec.clear();
        if (fs::is_regular_file(source, ec) && !fs::exists(target, ec)) {
            ec.clear();
            fs::copy_file(source, target, fs::copy_options::none, ec);
        }
    }
}

bool ExtractEntry(const OmniGhost::EmbeddedRuntimeGenerated::Entry& entry,
                  const fs::path& root, std::wstring& error) {
    const fs::path relative(entry.relativePath);
    if (!IsSafeRelative(relative)) {
        error = L"O bundle contém um caminho inválido.";
        return false;
    }
    const fs::path target = root / relative;
    std::error_code ec;
    if (fs::is_regular_file(target, ec) && fs::file_size(target, ec) == entry.size) {
        std::string actual;
        std::string hashError;
        if (OmniGhost::Platform::Sha256File(target, actual, hashError) &&
            OmniGhost::Platform::ConstantTimeEquals(actual, OmniGhost::Platform::DigestHex(entry.sha256))) return true;
    }

    const auto resource = OmniGhost::GetPeResourceView(entry.resourceId);
    if (!resource) {
        error = L"Recurso de runtime em falta no executável.";
        return false;
    }
    const std::size_t resourceSize = resource->size();
    const void* bytes = resource->data();
    if (resourceSize != entry.size || resourceSize == 0 || resourceSize > MAXDWORD) {
        error = L"Recurso de runtime inválido ou truncado.";
        return false;
    }

    fs::create_directories(target.parent_path(), ec);
    if (ec) {
        error = L"Não foi possível criar a pasta privada do runtime.";
        return false;
    }
    const fs::path temporary = target.wstring() + L".tmp-" + std::to_wstring(GetCurrentProcessId());
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = L"Não foi possível criar um ficheiro temporário do runtime.";
            return false;
        }
        output.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(resourceSize));
        output.flush();
        if (!output) {
            error = L"Não foi possível escrever o runtime privado.";
            output.close();
            fs::remove(temporary, ec);
            return false;
        }
    }

    std::string actual;
    std::string hashError;
    if (!OmniGhost::Platform::Sha256File(temporary, actual, hashError) ||
        !OmniGhost::Platform::ConstantTimeEquals(actual, OmniGhost::Platform::DigestHex(entry.sha256))) {
        fs::remove(temporary, ec);
        error = L"A validação SHA-256 de um recurso do runtime falhou.";
        return false;
    }
    // Item 42: Validate file permissions and type before replacement
    if (fs::exists(target, ec)) {
        auto status = fs::status(target, ec);
        if (ec) {
            error = L"Não foi possível verificar o ficheiro existente do runtime.";
            return false;
        }
        // Ensure it's a regular file, not a symlink/junction
        if (!fs::is_regular_file(target, ec)) {
            error = L"O destino do runtime não é um ficheiro regular (pode ser symlink/junction)";
            return false;
        }
        // Check write permissions
        if ((status.permissions() & fs::perms::owner_write) == fs::perms::none) {
            error = L"Sem permissão de escrita no ficheiro de runtime existente";
            return false;
        }
    }

    if (!MoveFileExW(temporary.c_str(), target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fs::remove(temporary, ec);
        error = L"Não foi possível instalar ou reparar um ficheiro do runtime.";
        return false;
    }
    return true;
}

bool ExtractBundle(const fs::path& root, std::wstring& error) {
    if (!OmniGhost::Paths::EnsureUserDirectories()) {
        error = L"Não foi possível preparar a pasta de dados local do utilizador.";
        return false;
    }
    
    // Item 41: Validate runtime directory is not a junction/reparse point
    std::error_code ec;
    if (fs::exists(root, ec)) {
        const auto status = fs::status(root, ec);
        if (!ec && (status.type() == fs::file_type::symlink || 
            (status.permissions() & fs::perms::owner_read) == fs::perms::owner_read)) {
            // Check for reparse point on Windows
            DWORD attrs = GetFileAttributesW(root.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
                error = L"Diretório de runtime é um ponto de reparse/junction - recusando materialização";
                return false;
            }
        }
    }

#if defined(OMNIGHOST_PRIVATE_STATIC_VMM)
    // A normal Release may have left these immutable DLLs in the shared runtime
    // directory. The private source-linked build must neither load nor retain
    // them. At this point the single-instance guard is held and no DMA module has
    // been initialized, so deleting these two exact obsolete files is safe.
    std::error_code staleError;
    fs::remove(root / L"libs" / L"vmm.dll", staleError);
    staleError.clear();
    fs::remove(root / L"libs" / L"leechcore.dll", staleError);
#endif
    for (std::size_t _i = 0; _i < OmniGhost::EmbeddedRuntimeGenerated::kEntryCount; ++_i) {
        const auto& entry = OmniGhost::EmbeddedRuntimeGenerated::kEntries[_i];
        // Public Radar is optional. Keep cloudflared inside the PE until the user
        // explicitly enables that feature instead of leaving a daemon binary on
        // disk after every normal launcher start.
        if (_wcsicmp(entry.relativePath, L"libs/cloudflared.exe") == 0)
            continue;
#if defined(OMNIGHOST_DISABLE_VMM_SYMBOLS)
        // Experimental: -disable-symbols + EAT path for gafAsyncKeyState means
        // pdbcrust is not required on disk for startup.
        if (_wcsicmp(entry.relativePath, L"libs/pdbcrust.dll") == 0)
            continue;
#endif
        if (!ExtractEntry(entry, root, error)) return false;
    }
    return true;
}

void ConfigureRuntimeDllSearch(const fs::path& libs) {
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 |
        LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_USER_DIRS);
    static DLL_DIRECTORY_COOKIE cookie = nullptr;
    if (!cookie) cookie = AddDllDirectory(libs.c_str());
    // SetDllDirectoryW removed - AddDllDirectory with restricted LoadLibraryEx flags is sufficient.
}

// Synchronizes runtime libraries from embedded manifest and fallback source locations.
// Returns pair<copied, skipped> count.
std::pair<int, int> SyncRuntimeLibraries(const fs::path& libsDir) {
    int copied = 0;
    int skipped = 0;
    int failed = 0;
    
    std::error_code ec;
    fs::create_directories(libsDir, ec);
    
    // Required DLLs that must be present for DMA operation
    static constexpr const wchar_t* kRequiredDlls[] = {
        L"FTD3XX.dll",
        L"FTD3XXWU.dll",
        L"vmm.dll",
        L"leechcore.dll",
        L"pdbcrust.dll",
        L"dbghelp.dll",
        L"symsrv.dll",
        L"vcruntime140.dll"
    };
    
    // Build a set of DLLs available in embedded manifest
    std::set<std::wstring> embeddedDlls;
    for (std::size_t i = 0; i < OmniGhost::EmbeddedRuntimeGenerated::kEntryCount; ++i) {
        const auto& entry = OmniGhost::EmbeddedRuntimeGenerated::kEntries[i];
        const fs::path relPath(entry.relativePath);
        if (relPath.parent_path() == L"libs") {
            embeddedDlls.insert(relPath.filename().wstring());
        }
    }
    
    // Source locations in priority order
    std::vector<fs::path> sourceDirs;
    
    // 1. ProjectDir\third_party\dma_stack\bin (dev builds)
    {
        fs::path projectDir = OmniGhost::Paths::InstallDirectory();
        // Go up to find project root (where third_party is)
        for (int i = 0; i < 4 && !projectDir.empty(); ++i) {
            fs::path candidate = projectDir / L"third_party" / L"dma_stack" / L"bin";
            if (fs::is_directory(candidate, ec)) {
                sourceDirs.push_back(candidate);
                break;
            }
            projectDir = projectDir.parent_path();
        }
    }
    
    // 2. InstallDirectory\third_party\dma_stack\bin
    {
        fs::path candidate = OmniGhost::Paths::InstallDirectory() / L"third_party" / L"dma_stack" / L"bin";
        if (fs::is_directory(candidate, ec)) {
            sourceDirs.push_back(candidate);
        }
    }
    
    // 3. InstallDirectory\libs
    {
        fs::path candidate = OmniGhost::Paths::InstallDirectory() / L"libs";
        if (fs::is_directory(candidate, ec)) {
            sourceDirs.push_back(candidate);
        }
    }
    
    // 4. ProjectDir\third_party\cloudflared (for cloudflared.exe)
    {
        fs::path projectDir = OmniGhost::Paths::InstallDirectory();
        for (int i = 0; i < 4 && !projectDir.empty(); ++i) {
            fs::path candidate = projectDir / L"third_party" / L"cloudflared";
            if (fs::is_directory(candidate, ec)) {
                sourceDirs.push_back(candidate);
                break;
            }
            projectDir = projectDir.parent_path();
        }
    }
    
    // 5. ProjectDir\runtime\own (for pdbcrust.dll)
    {
        fs::path projectDir = OmniGhost::Paths::InstallDirectory();
        for (int i = 0; i < 4 && !projectDir.empty(); ++i) {
            fs::path candidate = projectDir / L"runtime" / L"own";
            if (fs::is_directory(candidate, ec)) {
                sourceDirs.push_back(candidate);
                break;
            }
            projectDir = projectDir.parent_path();
        }
    }
    
    // 6. ProjectDir\libs (for pdbcrust.dll and other local DLLs)
    {
        fs::path projectDir = OmniGhost::Paths::InstallDirectory();
        for (int i = 0; i < 4 && !projectDir.empty(); ++i) {
            fs::path candidate = projectDir / L"libs";
            if (fs::is_directory(candidate, ec)) {
                sourceDirs.push_back(candidate);
                break;
            }
            projectDir = projectDir.parent_path();
        }
    }
    
    // For each required DLL, ensure it exists in libsDir with correct content
    for (const wchar_t* dllName : kRequiredDlls) {
        fs::path target = libsDir / dllName;
        bool hasEmbedded = embeddedDlls.find(dllName) != embeddedDlls.end();
        bool targetExists = fs::is_regular_file(target, ec);
        ec.clear();
        
        // Check if embedded version matches target
        bool embeddedMatches = false;
        if (hasEmbedded && targetExists) {
            // Find the embedded entry
            for (std::size_t i = 0; i < OmniGhost::EmbeddedRuntimeGenerated::kEntryCount; ++i) {
                const auto& entry = OmniGhost::EmbeddedRuntimeGenerated::kEntries[i];
                if (_wcsicmp(entry.relativePath, (L"libs/" + std::wstring(dllName)).c_str()) == 0) {
                    std::uintmax_t targetSize = fs::file_size(target, ec);
                    if (!ec && targetSize == entry.size) {
                        std::string actualHash;
                        std::string hashError;
                        if (OmniGhost::Platform::Sha256File(target, actualHash, hashError) &&
                            OmniGhost::Platform::ConstantTimeEquals(actualHash, OmniGhost::Platform::DigestHex(entry.sha256))) {
                            embeddedMatches = true;
                        }
                    }
                    break;
                }
            }
        }
        
        if (embeddedMatches) {
            skipped++;
            continue;
        }
        
        // Try to materialize from embedded first
        bool materialized = false;
        if (hasEmbedded) {
            std::wstring error;
            if (OmniGhost::RuntimeBootstrap::MaterializePrivateRuntimeFile((L"libs/" + std::wstring(dllName)).c_str(), error)) {
                materialized = true;
                copied++;
            } else {
                // Embedded materialization failed, will try source dirs
                std::cout << "[Runtime] sync libs: embedded materialize failed for " << std::string(dllName, dllName + wcslen(dllName)) << ": " << std::string(error.begin(), error.end()) << "\n";
            }
        }
        
        if (!materialized) {
            // Try source directories
            for (const auto& srcDir : sourceDirs) {
                fs::path source = srcDir / dllName;
                if (fs::is_regular_file(source, ec)) {
                    // Check if source is newer/different
                    bool shouldCopy = !targetExists;
                    if (!shouldCopy) {
                        std::uintmax_t srcSize = fs::file_size(source, ec);
                        std::uintmax_t tgtSize = fs::file_size(target, ec);
                        if (!ec && srcSize != tgtSize) {
                            shouldCopy = true;
                        } else if (!ec && srcSize == tgtSize) {
                            // Compare hashes
                            std::string srcHash, tgtHash, hashError;
                            if (OmniGhost::Platform::Sha256File(source, srcHash, hashError) &&
                                OmniGhost::Platform::Sha256File(target, tgtHash, hashError) &&
                                srcHash != tgtHash) {
                                shouldCopy = true;
                            }
                        }
                    }
                    
                    if (shouldCopy) {
                        fs::create_directories(target.parent_path(), ec);
                        fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
                        if (!ec) {
                            copied++;
                            materialized = true;
                            break;
                        }
                    } else {
                        skipped++;
                        materialized = true;
                        break;
                    }
                }
                ec.clear();
            }
        }
        
        if (!materialized && !targetExists) {
            // DLL is required but not available anywhere
            failed++;
        } else if (!materialized && targetExists) {
            skipped++;
        }
    }
    
    return {copied, skipped};
}

bool IsUpdaterWorker() {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool updater = false;
    for (int i = 1; argv && i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--updater") == 0 ||
            _wcsicmp(argv[i], L"--apply-update") == 0) {
            updater = true;
            break;
        }
    }
    if (argv) LocalFree(argv);
    return updater;
}

} // namespace

namespace OmniGhost::RuntimeBootstrap {

fs::path PrivateRuntimePath(std::wstring_view relativePath) {
    return OmniGhost::Paths::NativeRuntime() / fs::path(relativePath);
}

bool RetireLegacyPrivateInstall(std::wstring& error) {
    error.clear();
    std::array<wchar_t, 32768> localBuffer{};
    const DWORD chars = GetEnvironmentVariableW(L"LOCALAPPDATA", localBuffer.data(),
        static_cast<DWORD>(localBuffer.size()));
    if (chars == 0 || chars >= localBuffer.size())
        return true;

    const fs::path legacyRoot = fs::path(std::wstring(localBuffer.data(), chars)) / L"OmniGhost";
    const fs::path legacyExe = legacyRoot / L"OmniGhost.exe";
    std::error_code ec;
    const bool legacyExeExists = fs::is_regular_file(legacyExe, ec);
    const bool legacyArtifactsExist = legacyExeExists ||
        fs::exists(legacyRoot / L"libs", ec) ||
        fs::exists(legacyRoot / L"data", ec) ||
        fs::exists(legacyRoot / L"plugins", ec) ||
        fs::exists(legacyRoot / L"resources", ec);
    if (!legacyArtifactsExist)
        return true;
    if (legacyExeExists && fs::equivalent(OmniGhost::Paths::Executable(), legacyExe, ec) && !ec)
        return true;

    std::vector<HANDLE> matches;
    HANDLE snapshot = legacyExeExists
        ? CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
        : INVALID_HANDLE_VALUE;
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (entry.th32ProcessID == GetCurrentProcessId()) continue;
                HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION |
                    SYNCHRONIZE | PROCESS_TERMINATE, FALSE, entry.th32ProcessID);
                if (!process) continue;
                std::array<wchar_t, 32768> image{};
                DWORD imageChars = static_cast<DWORD>(image.size());
                if (QueryFullProcessImageNameW(process, 0, image.data(), &imageChars)) {
                    const fs::path candidate(std::wstring(image.data(), imageChars));
                    ec.clear();
                    const bool same = fs::equivalent(candidate, legacyExe, ec) && !ec;
                    if (same) {
                        const DWORD pid = entry.th32ProcessID;
                        EnumWindows([](HWND window, LPARAM value) -> BOOL {
                            DWORD owner = 0;
                            GetWindowThreadProcessId(window, &owner);
                            if (owner == static_cast<DWORD>(value))
                                PostMessageW(window, WM_CLOSE, 0, 0);
                            return TRUE;
                        }, static_cast<LPARAM>(pid));
                        matches.push_back(process);
                        continue;
                    }
                }
                CloseHandle(process);
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }

    for (HANDLE process : matches) {
        if (WaitForSingleObject(process, 2500) == WAIT_TIMEOUT) {
            TerminateProcess(process, 0);
            WaitForSingleObject(process, 1500);
        }
        CloseHandle(process);
    }

    // Keep account, licence and settings while changing the data root. Copy
    // only known mutable state and never migrate the old executable/resources.
    MigrateLegacyMutableData(legacyRoot);

    // Delete only immutable artifacts from the obsolete bootstrap model.
    DeleteFileW(legacyExe.c_str());
    fs::remove_all(legacyRoot / L"libs", ec);
    fs::remove_all(legacyRoot / L"data", ec);
    fs::remove_all(legacyRoot / L"plugins", ec);
    fs::remove_all(legacyRoot / L"resources", ec);
    ec.clear();
    fs::remove(legacyRoot, ec); // succeeds only when no user data remains
    if (fs::exists(legacyExe, ec)) {
        error = L"A cópia privada antiga do OmniGhost continua bloqueada.";
        return false;
    }
    return true;
}

bool EmbeddedRuntimeFileAvailable(std::wstring_view relativePath) noexcept {
    try {
        const fs::path requested(relativePath);
if (!IsSafeRelative(requested))
            return false;
        for (std::size_t _i = 0; _i < OmniGhost::EmbeddedRuntimeGenerated::kEntryCount; ++_i) {
        const auto& entry = OmniGhost::EmbeddedRuntimeGenerated::kEntries[_i];
            if (_wcsicmp(entry.relativePath, requested.c_str()) != 0)
                continue;
            const auto resource = OmniGhost::GetPeResourceView(entry.resourceId);
            return resource && resource->size() == entry.size && entry.size != 0;
        }
    } catch (const std::exception& ex) {
        std::cerr << "[RuntimeBootstrap] HasPrivateRuntimeFile failed: " << ex.what() << "\n";
        return false;
    } catch (...) {
        std::cerr << "[RuntimeBootstrap] HasPrivateRuntimeFile failed with unknown exception\n";
        return false;
    }
    return false;
}

bool ValidatePrivateRuntimeFile(std::wstring_view relativePath, std::wstring& error) {
    const fs::path requested(relativePath);
    if (!IsSafeRelative(requested)) {
        error = L"O caminho do runtime privado é inválido.";
        return false;
    }
    const OmniGhost::EmbeddedRuntimeGenerated::Entry* expected = nullptr;
    for (std::size_t _i = 0; _i < OmniGhost::EmbeddedRuntimeGenerated::kEntryCount; ++_i) {
        const auto& entry = OmniGhost::EmbeddedRuntimeGenerated::kEntries[_i];
        if (_wcsicmp(entry.relativePath, requested.c_str()) == 0) {
            expected = &entry;
            break;
        }
    }
    if (!expected) {
        error = L"O ficheiro não pertence ao manifesto do runtime privado.";
        return false;
    }
    const fs::path file = PrivateRuntimePath(relativePath);
    std::error_code ec;
    if (!fs::is_regular_file(file, ec) || ec || fs::file_size(file, ec) != expected->size) {
        error = L"O ficheiro do runtime privado está em falta ou tem tamanho inválido.";
        return false;
    }
    std::string actual;
    std::string hashError;
    if (!OmniGhost::Platform::Sha256File(file, actual, hashError) ||
        !OmniGhost::Platform::ConstantTimeEquals(actual, OmniGhost::Platform::DigestHex(expected->sha256))) {
        error = L"A validação SHA-256 do runtime privado falhou.";
        return false;
    }
    error.clear();
    return true;
}

bool MaterializePrivateRuntimeFile(std::wstring_view relativePath, std::wstring& error) {
    const fs::path requested(relativePath);
    if (!IsSafeRelative(requested)) {
        error = L"O caminho do runtime privado é inválido.";
        return false;
    }
    const OmniGhost::EmbeddedRuntimeGenerated::Entry* selected = nullptr;
    for (std::size_t _i = 0; _i < OmniGhost::EmbeddedRuntimeGenerated::kEntryCount; ++_i) {
        const auto& entry = OmniGhost::EmbeddedRuntimeGenerated::kEntries[_i];
        if (_wcsicmp(entry.relativePath, requested.c_str()) == 0) {
            selected = &entry;
            break;
        }
    }
    if (!selected) {
        error = L"O ficheiro não pertence ao manifesto do runtime privado.";
        return false;
    }
    if (!OmniGhost::Paths::EnsureUserDirectories()) {
        error = L"Não foi possível preparar a pasta de dados local do utilizador.";
        return false;
    }
    if (!ExtractEntry(*selected, OmniGhost::Paths::NativeRuntime(), error))
        return false;
    return ValidatePrivateRuntimeFile(relativePath, error);
}

Result Prepare() {
    const fs::path root = OmniGhost::Paths::NativeRuntime();
    const fs::path libs = root / L"libs";
    std::wstring error;
    std::error_code ec;

    // The updater intentionally runs from a temporary copy. It must update the
    // installed EXE directly, not bootstrap/relaunch itself back into the file it
    // is about to replace. DMA is never initialized in updater mode.
    if (IsUpdaterWorker()) {
        ConfigureRuntimeDllSearch(libs);
        return Result::Continue;
    }

    if (!ExtractBundle(root, error)) {
        MessageBoxW(nullptr, error.c_str(), L"OmniGhost — runtime nativo", MB_OK | MB_ICONERROR);
        return Result::Failed;
    }
    
    // Sync runtime libraries from embedded manifest and fallback sources
    auto [copied, skipped] = SyncRuntimeLibraries(libs);
    std::cout << "[Runtime] sync libs: copied=" << copied << " skipped=" << skipped << "\n";
    
    // Verify FTDI presence
    bool ftdiPresent = fs::is_regular_file(libs / L"FTD3XX.dll", ec) || fs::is_regular_file(libs / L"FTD3XXWU.dll", ec);
    std::cout << "[Runtime] FTDI present=" << (ftdiPresent ? "YES" : "NO") << " path=" << libs.string() << "\n";
    ec.clear();
    
    ConfigureRuntimeDllSearch(libs);
    return Result::Continue;
}

// Item 47: Clean startup test that lists everything in %LOCALAPPDATA%\OmniGhost
void LogRuntimeDirectoryContents() noexcept {
    const auto root = OmniGhost::Paths::NativeRuntime();
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) {
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Info,
            OmniGhost::SessionLog::Subsystem::Core,
            "Runtime directory does not exist",
            {{"path", root.string(), false}});
        return;
    }

    std::vector<std::pair<std::string, uintmax_t>> files;
    uintmax_t totalSize = 0;
    try {
for (const auto& entry : std::filesystem::recursive_directory_iterator(
                OmniGhost::Paths::NativeRuntime(),
                std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (ec) { ec.clear(); continue; }
            if (entry.is_regular_file(ec)) {
                const auto rel = std::filesystem::relative(entry.path(), OmniGhost::Paths::NativeRuntime(), ec);
                const uintmax_t size = std::filesystem::file_size(entry.path(), ec);
                files.emplace_back(rel.string(), size);
                totalSize += size;
            }
}
    } catch (const std::exception& ex) {
        std::cerr << "[RuntimeBootstrap] Directory listing failed: " << ex.what() << "\n";
    } catch (...) {
        std::cerr << "[RuntimeBootstrap] Directory listing failed with unknown exception\n";
    }

    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Core,
        "Startup directory listing",
        {
            {"path", OmniGhost::Paths::NativeRuntime().string(), false},
            {"file_count", std::to_string(files.size()), false},
            {"total_size_bytes", std::to_string(totalSize), false}
        });

for (const auto& entry : files) {
        const auto& path = entry.first;
        const auto& size = entry.second;
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Debug,
            OmniGhost::SessionLog::Subsystem::Core,
            "  " + path + " (" + std::to_string(size) + " bytes)",
            {});
    }
}

// Item 48: Confirm no private copy of OmniGhost.exe is created
void VerifyNoPrivateExeCopy() noexcept {
    const auto exePath = OmniGhost::Paths::Executable();
    const auto localAppData = OmniGhost::Paths::NativeRuntime();
    std::error_code ec;

    if (std::filesystem::equivalent(OmniGhost::Paths::Executable(), localAppData / L"OmniGhost.exe", ec) && !ec) {
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Warning,
            OmniGhost::SessionLog::Subsystem::Core,
            "Private copy of OmniGhost.exe detected in AppData - this should not happen",
            {{"executable", OmniGhost::Paths::Executable().string(), false},
             {"local_copy", (OmniGhost::Paths::NativeRuntime() / L"OmniGhost.exe").string(), false}});
    }
}

// Item 49: Separate files created at startup, DMA session, public radar
enum class FileOrigin : uint8_t {
    Startup = 0,
    DmaSession = 1,
    PublicRadar = 2,
    Unknown = 255
};

struct FileOriginMap {
    std::unordered_map<std::string, FileOrigin> origins;
    
    void Register(const std::string& relativePath, FileOrigin origin) {
        origins[relativePath] = origin;
    }
    
    [[nodiscard]] FileOrigin Get(const std::string& relativePath) const noexcept {
        auto it = origins.find(relativePath);
        return it != origins.end() ? it->second : FileOrigin::Unknown;
    }
};

static FileOriginMap g_fileOrigins;

FileOrigin GetFileOrigin(const std::string& relativePath) noexcept {
    return g_fileOrigins.Get(relativePath);
}

void RegisterFileOrigin(const std::string& relativePath, FileOrigin origin) noexcept {
    g_fileOrigins.Register(relativePath, origin);
}

} // namespace OmniGhost::RuntimeBootstrap












