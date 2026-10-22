#include "runtime_bootstrap.h"
#include "text_encoding.h"
#include "session_log.h"
#include "app_paths.h"
#include "file_integrity.h"



#include <embedded_runtime_manifest.h>
#include "embedded_resources.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <shellapi.h>
#include <TlHelp32.h>
#include <objbase.h>

#pragma comment(lib, "ole32.lib")

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
#include <tuple>

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


    static constexpr std::array<const wchar_t*, 2> kMutableDirectories = {
        L"Configs", L"CS2"
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
    // PL-010: nome do temporário imprevisível (GUID além do pid) e criado
    // com CREATE_NEW — falha se já existir, impedindo pré-criação/symlink
    // por um atacante local que adivinhe o nome previsível *.tmp-<pid>.
    GUID tempGuid{};
    wchar_t guidText[64]{};
    fs::path temporary = target.wstring() + L".tmp-" + std::to_wstring(GetCurrentProcessId());
    if (SUCCEEDED(CoCreateGuid(&tempGuid)) &&
        StringFromGUID2(tempGuid, guidText, 64) > 0) {
        temporary += guidText;
    }
    HANDLE tempHandle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (tempHandle == INVALID_HANDLE_VALUE) {
        error = L"Não foi possível criar um ficheiro temporário do runtime.";
        return false;
    }
    {
        const BYTE* cursor = static_cast<const BYTE*>(bytes);
        DWORD remaining = static_cast<DWORD>(resourceSize);
        while (remaining > 0) {
            const DWORD chunk = (std::min)(remaining, static_cast<DWORD>(8u * 1024u * 1024u));
            DWORD written = 0;
            if (!WriteFile(tempHandle, cursor, chunk, &written, nullptr) || written == 0) {
                CloseHandle(tempHandle);
                fs::remove(temporary, ec);
                error = L"Não foi possível escrever o runtime privado.";
                return false;
            }
            cursor += written;
            remaining -= written;
        }
        if (!FlushFileBuffers(tempHandle)) {
            CloseHandle(tempHandle);
            fs::remove(temporary, ec);
            error = L"Não foi possível escrever o runtime privado.";
            return false;
        }
        CloseHandle(tempHandle);
    }

    std::string actual;
    std::string hashError;
    if (!OmniGhost::Platform::Sha256File(temporary, actual, hashError) ||
        !OmniGhost::Platform::ConstantTimeEquals(actual, OmniGhost::Platform::DigestHex(entry.sha256))) {
        fs::remove(temporary, ec);
        error = L"A validação SHA-256 de um recurso do runtime falhou.";
        return false;
    }

    if (fs::exists(target, ec)) {
        // PL-010: o check de reparse examina o PRÓPRIO link
        // (GetFileAttributesW não segue o alvo), ao contrário de
        // fs::status que segue symlinks.
        const DWORD targetAttrs = GetFileAttributesW(target.c_str());
        if (targetAttrs != INVALID_FILE_ATTRIBUTES &&
            (targetAttrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
            fs::remove(temporary, ec);
            error = L"O destino do runtime é um symlink/junction — recusando.";
            return false;
        }
        auto status = fs::status(target, ec);
        if (ec) {
            fs::remove(temporary, ec);
            error = L"Não foi possível verificar o ficheiro existente do runtime.";
            return false;
        }

        if (!fs::is_regular_file(target, ec)) {
            fs::remove(temporary, ec);
            error = L"O destino do runtime não é um ficheiro regular (pode ser symlink/junction)";
            return false;
        }

        if ((status.permissions() & fs::perms::owner_write) == fs::perms::none) {
            fs::remove(temporary, ec);
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


    std::error_code ec;
    if (fs::exists(root, ec)) {
        const auto status = fs::status(root, ec);
        if (!ec && (status.type() == fs::file_type::symlink ||
            (status.permissions() & fs::perms::owner_read) == fs::perms::owner_read)) {

            DWORD attrs = GetFileAttributesW(root.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
                error = L"Diretório de runtime é um ponto de reparse/junction - recusando materialização";
                return false;
            }
        }
    }

#if defined(OMNIGHOST_PRIVATE_STATIC_VMM)




    std::error_code staleError;
    fs::remove(root / L"libs" / L"vmm.dll", staleError);
    staleError.clear();
    fs::remove(root / L"libs" / L"leechcore.dll", staleError);
#endif
    for (std::size_t _i = 0; _i < OmniGhost::EmbeddedRuntimeGenerated::kEntryCount; ++_i) {
        const auto& entry = OmniGhost::EmbeddedRuntimeGenerated::kEntries[_i];



        if (_wcsicmp(entry.relativePath, L"libs/cloudflared.exe") == 0)
            continue;
#if defined(OMNIGHOST_DISABLE_VMM_SYMBOLS)


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

}



 std::tuple<int, int, int> SyncRuntimeLibraries(const fs::path& libsDir) {
     int copied = 0;
     int skipped = 0;
     int failed = 0;

     std::error_code ec;
     fs::create_directories(libsDir, ec);


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

     auto Narrow = [](const std::wstring& text) -> std::string {
         std::string out;
         out.reserve(text.size());
         for (wchar_t ch : text)
             out.push_back(ch < 0x80 ? static_cast<char>(ch) : '?');
         return out;
     };


     std::set<std::wstring> embeddedDlls;
     for (std::size_t i = 0; i < OmniGhost::EmbeddedRuntimeGenerated::kEntryCount; ++i) {
         const auto& entry = OmniGhost::EmbeddedRuntimeGenerated::kEntries[i];
         const fs::path relPath(entry.relativePath);
         if (relPath.parent_path() == L"libs") {
             embeddedDlls.insert(relPath.filename().wstring());
         }
     }


     std::vector<fs::path> sourceDirs;


     {
         fs::path candidate = OmniGhost::Paths::InstallDirectory() / L"libs";
         if (fs::is_directory(candidate, ec)) {
             sourceDirs.push_back(candidate);
         }
     }


    for (const wchar_t* dllName : kRequiredDlls) {
        fs::path target = libsDir / dllName;
        bool hasEmbedded = embeddedDlls.find(dllName) != embeddedDlls.end();
        bool targetExists = fs::is_regular_file(target, ec);
        ec.clear();


        bool embeddedMatches = false;
        if (hasEmbedded && targetExists) {

            for (std::size_t i = 0; i < OmniGhost::EmbeddedRuntimeGenerated::kEntryCount; ++i) {
                const auto& entry = OmniGhost::EmbeddedRuntimeGenerated::kEntries[i];
                std::wstring lookupPath = std::wstring(L"libs/") + dllName;
                if (_wcsicmp(entry.relativePath, lookupPath.c_str()) == 0) {
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


        bool materialized = false;
        if (hasEmbedded) {
            std::wstring error;
            std::wstring lookupPath = std::wstring(L"libs/") + dllName;
            if (OmniGhost::RuntimeBootstrap::MaterializePrivateRuntimeFile(lookupPath.c_str(), error)) {
                materialized = true;
                copied++;
                OmniGhost::SessionLog::Write(
                    OmniGhost::SessionLog::Severity::Info,
                    OmniGhost::SessionLog::Subsystem::Runtime,
                    "Runtime library materialized from embedded",
                    {{"dll", Narrow(dllName)}, {"source", "embedded"}});
            } else {

                OmniGhost::SessionLog::Write(
                    OmniGhost::SessionLog::Severity::Warning,
                    OmniGhost::SessionLog::Subsystem::Runtime,
                    "Embedded materialization failed, trying fallback",
                    {{"dll", Narrow(dllName)}, {"error", Narrow(error)}});
            }
        }

        if (!materialized) {





            std::string expectedHashHex;
            if (hasEmbedded) {
                const std::wstring lookupPath = std::wstring(L"libs/") + dllName;
                for (std::size_t i = 0; i < OmniGhost::EmbeddedRuntimeGenerated::kEntryCount; ++i) {
                    const auto& entry = OmniGhost::EmbeddedRuntimeGenerated::kEntries[i];
                    if (_wcsicmp(entry.relativePath, lookupPath.c_str()) == 0) {
                        expectedHashHex = OmniGhost::Platform::DigestHex(entry.sha256);
                        break;
                    }
                }
            }

            for (const auto& srcDir : sourceDirs) {
                fs::path source = srcDir / dllName;
                if (fs::is_regular_file(source, ec)) {

                    bool shouldCopy = !targetExists;
                    std::string srcHash;
                    if (!shouldCopy) {
                        std::uintmax_t srcSize = fs::file_size(source, ec);
                        std::uintmax_t tgtSize = fs::file_size(target, ec);
                        if (!ec && srcSize != tgtSize) {
                            shouldCopy = true;
                        } else if (!ec && srcSize == tgtSize) {

                            std::string tgtHash, hashError;
                            if (OmniGhost::Platform::Sha256File(source, srcHash, hashError) &&
                                OmniGhost::Platform::Sha256File(target, tgtHash, hashError) &&
                                srcHash != tgtHash) {
                                shouldCopy = true;
                            }
                        }
                    }

                    if (shouldCopy && !expectedHashHex.empty()) {



                        if (srcHash.empty()) {
                            std::string hashError;
                            if (!OmniGhost::Platform::Sha256File(source, srcHash, hashError))
                                srcHash.clear();
                        }
                        if (srcHash.empty() ||
                            !OmniGhost::Platform::ConstantTimeEquals(srcHash, expectedHashHex)) {
                            OmniGhost::SessionLog::Write(
                                OmniGhost::SessionLog::Severity::Warning,
                                OmniGhost::SessionLog::Subsystem::Runtime,
                                "Runtime library source rejected (hash mismatch with embedded manifest)",
                                {{"dll", Narrow(dllName)}, {"source", Narrow(srcDir.wstring())}});
                            ec.clear();
                            continue;
                        }
                    }

                    if (shouldCopy) {
                        fs::create_directories(target.parent_path(), ec);
                        fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
                        if (!ec) {
                            copied++;
                            materialized = true;
                            OmniGhost::SessionLog::Write(
                                OmniGhost::SessionLog::Severity::Info,
                                OmniGhost::SessionLog::Subsystem::Runtime,
                                "Runtime library copied from source",
                                {{"dll", Narrow(dllName)}, {"source", Narrow(srcDir.wstring())}});
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

            failed++;
            OmniGhost::SessionLog::Write(
                OmniGhost::SessionLog::Severity::Error,
                OmniGhost::SessionLog::Subsystem::Runtime,
                "Required runtime library not found",
                {{"dll", Narrow(dllName)}});
        } else if (!materialized && targetExists) {
            skipped++;
        }
    }

    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Runtime,
        "Runtime library sync complete",
        {{"copied", std::to_string(copied)}, {"skipped", std::to_string(skipped)}, {"failed", std::to_string(failed)}});

    return {copied, skipped, failed};
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

}

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



    MigrateLegacyMutableData(legacyRoot);


    DeleteFileW(legacyExe.c_str());
    fs::remove_all(legacyRoot / L"libs", ec);
    fs::remove_all(legacyRoot / L"data", ec);
    fs::remove_all(legacyRoot / L"plugins", ec);
    fs::remove_all(legacyRoot / L"resources", ec);
    ec.clear();
    fs::remove(legacyRoot, ec);
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
    const DWORD fileAttributes = GetFileAttributesW(file.c_str());
    const DWORD directoryAttributes = GetFileAttributesW(file.parent_path().c_str());
    if (fileAttributes == INVALID_FILE_ATTRIBUTES || directoryAttributes == INVALID_FILE_ATTRIBUTES ||
        (fileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        (directoryAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        error = L"O caminho do runtime privado não é um ficheiro/diretório normal.";
        return false;
    }
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




    if (IsUpdaterWorker()) {
        ConfigureRuntimeDllSearch(libs);
        return Result::Continue;
    }

    if (!ExtractBundle(root, error)) {
        MessageBoxW(nullptr, error.c_str(), L"OmniGhost — runtime nativo", MB_OK | MB_ICONERROR);
        return Result::Failed;
    }



    {
        std::wstring matErr;
        (void)MaterializePrivateRuntimeFile(L"libs/FTD3XX.dll", matErr);
        (void)MaterializePrivateRuntimeFile(L"libs/FTD3XXWU.dll", matErr);
        (void)MaterializePrivateRuntimeFile(L"libs/pdbcrust.dll", matErr);
        (void)MaterializePrivateRuntimeFile(L"libs/vcruntime140.dll", matErr);
    }


    auto [copied, skipped, syncFailed] = SyncRuntimeLibraries(libs);







    ConfigureRuntimeDllSearch(libs);
    {
        const auto NarrowName = [](const wchar_t* text) -> std::string {
            std::string out;
            for (const wchar_t* p = text; p && *p; ++p)
                out.push_back(*p < 0x80 ? static_cast<char>(*p) : '?');
            return out;
        };
        const wchar_t* ftdiNames[] = { L"FTD3XX.dll", L"FTD3XXWU.dll" };
        for (const wchar_t* name : ftdiNames) {
            const fs::path candidate = libs / name;
            if (fs::is_regular_file(candidate, ec) && !ec) {
                const std::wstring relativePath = std::wstring(L"libs/") + name;
                bool coveredByManifest = false;
                for (std::size_t i = 0; i < OmniGhost::EmbeddedRuntimeGenerated::kEntryCount; ++i) {
                    if (_wcsicmp(OmniGhost::EmbeddedRuntimeGenerated::kEntries[i].relativePath,
                                 relativePath.c_str()) == 0) {
                        coveredByManifest = true;
                        break;
                    }
                }
                if (coveredByManifest) {
                    // PL-010: TOCTOU — mantém um handle com FILE_SHARE_READ
                    // (sem share de escrita) do validate ao load; a share
                    // mode impede substituição concorrente do binário na
                    // janela entre a validação e o LoadLibraryExW.
                    HANDLE dllHandle = CreateFileW(candidate.c_str(), GENERIC_READ,
                        FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (dllHandle == INVALID_HANDLE_VALUE) {
                        OmniGhost::SessionLog::Write(
                            OmniGhost::SessionLog::Severity::Error,
                            OmniGhost::SessionLog::Subsystem::Runtime,
                            "Runtime DLL preload blocked: file could not be locked",
                            {{"dll", NarrowName(name)}});
                        ec.clear();
                        continue;
                    }
                    std::wstring validationError;
                    const bool validated = OmniGhost::RuntimeBootstrap::ValidatePrivateRuntimeFile(
                        relativePath, validationError);
                    if (!validated) {
                        CloseHandle(dllHandle);
                        OmniGhost::SessionLog::Write(
                            OmniGhost::SessionLog::Severity::Error,
                            OmniGhost::SessionLog::Subsystem::Runtime,
                            "Runtime DLL preload blocked: integrity validation failed",
                            {{"dll", NarrowName(name)}});
                        ec.clear();
                        continue;
                    }
                    HMODULE mod = LoadLibraryExW(
                        candidate.c_str(),
                        nullptr,
                        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
                    CloseHandle(dllHandle);
                    if (mod) {
                        std::wcout << L"[Runtime] preloaded " << candidate.wstring() << L"\n";
                    } else {
                        std::wcout << L"[Runtime] preload failed " << candidate.wstring()
                                   << L" win32=" << GetLastError() << L"\n";
                    }
                } else {
                    OmniGhost::SessionLog::Write(
                        OmniGhost::SessionLog::Severity::Error,
                        OmniGhost::SessionLog::Subsystem::Runtime,
                        "Runtime DLL preload blocked: not covered by the embedded manifest",
                        {{"dll", NarrowName(name)}});
                    ec.clear();
                    continue;
                }
            }
            ec.clear();
        }
    }


    bool ftdiPresent = fs::is_regular_file(libs / L"FTD3XX.dll", ec) || fs::is_regular_file(libs / L"FTD3XXWU.dll", ec);
    ec.clear();

    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Runtime,
        "Runtime bootstrap complete",
        {{"ftdi_present", ftdiPresent ? "YES" : "NO"},
         {"libs_path", libs.string()},
         {"copied", std::to_string(copied)},
         {"skipped", std::to_string(skipped)},
         {"failed", std::to_string(syncFailed)}});

    return Result::Continue;
}


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

}
