#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <shellapi.h>

#include <bootstrap_payload_manifest.h>

#include <array>
#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

namespace {

using OmniGhost::BootstrapPayloadGenerated::Entry;

std::wstring Win32Message(const wchar_t* prefix, DWORD error) {
    wchar_t* raw = nullptr;
    const DWORD chars = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&raw), 0, nullptr);
    std::wstring result(prefix);
    result += L"\n\nWin32=" + std::to_wstring(error);
    if (chars && raw) {
        result += L"\n";
        result.append(raw, chars);
    }
    if (raw) LocalFree(raw);
    return result;
}

void ShowFailure(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), L"OmniGhost — bootstrap", MB_OK | MB_ICONERROR);
}

std::wstring ModulePath() {
    std::wstring buffer(512, L'\0');
    for (;;) {
        const DWORD chars = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (chars == 0) return {};
        if (chars < buffer.size() - 1) {
            buffer.resize(chars);
            return buffer;
        }
        if (buffer.size() >= 32768) return {};
        buffer.resize((buffer.size() * 2 > 32768) ? 32768 : buffer.size() * 2, L'\0');
    }
}

std::wstring LocalAppData() {
    DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required > 1) {
        std::wstring value(required, L'\0');
        const DWORD chars = GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), required);
        if (chars > 0 && chars < required) {
            value.resize(chars);
            return value;
        }
    }
    std::wstring temp(32768, L'\0');
    const DWORD chars = GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
    if (chars > 0 && chars < temp.size()) {
        temp.resize(chars);
        while (!temp.empty() && (temp.back() == L'\\' || temp.back() == L'/')) temp.pop_back();
        return temp;
    }
    return {};
}

bool EnsureDirectory(const std::wstring& path, DWORD& error) {
    error = ERROR_SUCCESS;
    if (CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) {
        const DWORD attrs = GetFileAttributesW(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            error = GetLastError();
            return false;
        }
        if (!(attrs & FILE_ATTRIBUTE_DIRECTORY) || (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
            error = ERROR_ACCESS_DENIED;
            return false;
        }
        return true;
    }
    error = GetLastError();
    return false;
}

std::wstring Join(const std::wstring& left, const std::wstring& right) {
    if (left.empty()) return right;
    if (left.back() == L'\\' || left.back() == L'/') return left + right;
    return left + L"\\" + right;
}

bool ResourceBytes(std::uint16_t id, const void*& data, DWORD& size, DWORD& error) {
    data = nullptr;
    size = 0;
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!resource) { error = GetLastError(); return false; }
    HGLOBAL loaded = LoadResource(nullptr, resource);
    if (!loaded) { error = GetLastError(); return false; }
    size = SizeofResource(nullptr, resource);
    data = LockResource(loaded);
    if (!data || size == 0) { error = ERROR_RESOURCE_DATA_NOT_FOUND; return false; }
    error = ERROR_SUCCESS;
    return true;
}

bool Sha256File(const std::wstring& path, std::array<std::uint8_t, 32>& digest, DWORD& error) {
    error = ERROR_SUCCESS;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) { error = GetLastError(); return false; }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = false;
    do {
        NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        if (status < 0) { error = ERROR_INVALID_FUNCTION; break; }
        status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
        if (status < 0) { error = ERROR_INVALID_FUNCTION; break; }

        std::array<std::uint8_t, 64 * 1024> buffer{};
        for (;;) {
            DWORD read = 0;
            if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
                error = GetLastError();
                break;
            }
            if (read == 0) {
                status = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
                if (status < 0) { error = ERROR_INVALID_DATA; break; }
                ok = true;
                break;
            }
            status = BCryptHashData(hash, buffer.data(), read, 0);
            if (status < 0) { error = ERROR_INVALID_DATA; break; }
        }
    } while (false);

    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return ok;
}

bool DigestEquals(const std::array<std::uint8_t, 32>& a,
                  const std::array<std::uint8_t, 32>& b) {
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) diff |= static_cast<std::uint8_t>(a[i] ^ b[i]);
    return diff == 0;
}

bool FileMatches(const std::wstring& path, const Entry& entry) {
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) return false;
    if (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    ULARGE_INTEGER size{};
    size.HighPart = info.nFileSizeHigh;
    size.LowPart = info.nFileSizeLow;
    if (size.QuadPart != entry.size) return false;
    std::array<std::uint8_t, 32> actual{};
    DWORD error = ERROR_SUCCESS;
    return Sha256File(path, actual, error) && DigestEquals(actual, entry.sha256);
}

bool Materialize(const std::wstring& directory, const Entry& entry, DWORD& error) {
    const std::wstring target = Join(directory, entry.fileName);
    if (FileMatches(target, entry)) {
        error = ERROR_SUCCESS;
        return true;
    }

    const DWORD attrs = GetFileAttributesW(target.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
        error = ERROR_ACCESS_DENIED;
        return false;
    }

    const void* bytes = nullptr;
    DWORD resourceSize = 0;
    if (!ResourceBytes(entry.resourceId, bytes, resourceSize, error)) return false;
    if (resourceSize != entry.size) { error = ERROR_BAD_LENGTH; return false; }

    const std::wstring temporary = target + L".tmp-" + std::to_wstring(GetCurrentProcessId());
    HANDLE output = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (output == INVALID_HANDLE_VALUE) { error = GetLastError(); return false; }

    const auto* cursor = static_cast<const std::uint8_t*>(bytes);
    DWORD remaining = resourceSize;
    bool writeOk = true;
    while (remaining != 0) {
        DWORD written = 0;
        if (!WriteFile(output, cursor, remaining, &written, nullptr) || written == 0) {
            error = GetLastError();
            writeOk = false;
            break;
        }
        cursor += written;
        remaining -= written;
    }
    if (writeOk && !FlushFileBuffers(output)) {
        error = GetLastError();
        writeOk = false;
    }
    CloseHandle(output);
    if (!writeOk) {
        DeleteFileW(temporary.c_str());
        return false;
    }

    std::array<std::uint8_t, 32> actual{};
    if (!Sha256File(temporary, actual, error) || !DigestEquals(actual, entry.sha256)) {
        DeleteFileW(temporary.c_str());
        if (error == ERROR_SUCCESS) error = ERROR_CRC;
        return false;
    }

    if (!MoveFileExW(temporary.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = GetLastError();
        DeleteFileW(temporary.c_str());
        return false;
    }
    error = ERROR_SUCCESS;
    return true;
}

void AppendQuoted(std::wstring& command, const wchar_t* value) {
    if (!command.empty()) command.push_back(L' ');
    command.push_back(L'"');
    unsigned slashes = 0;
    for (const wchar_t* p = value; *p; ++p) {
        if (*p == L'\\') {
            ++slashes;
            continue;
        }
        if (*p == L'"') {
            command.append(slashes * 2 + 1, L'\\');
            command.push_back(L'"');
            slashes = 0;
            continue;
        }
        command.append(slashes, L'\\');
        slashes = 0;
        command.push_back(*p);
    }
    command.append(slashes * 2, L'\\');
    command.push_back(L'"');
}

std::wstring ParentDirectory(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    if (slash == 2 && path.size() >= 3 && path[1] == L':') return path.substr(0, 3);
    return path.substr(0, slash);
}

std::wstring BuildChildCommandLine(const std::wstring& corePath) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring command;
    AppendQuoted(command, corePath.c_str());
    if (argv) {
        for (int i = 1; i < argc; ++i) AppendQuoted(command, argv[i]);
        LocalFree(argv);
    }
    return command;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const std::wstring self = ModulePath();
    const std::wstring local = LocalAppData();
    if (self.empty() || local.empty()) {
        ShowFailure(L"Não foi possível determinar os caminhos necessários para iniciar o OmniGhost.");
        return 10;
    }

    const std::wstring product = Join(local, L"OmniGhost");
    const std::wstring bootstrap = Join(product, L"bootstrap");
    const std::wstring payload = Join(bootstrap, OmniGhost::BootstrapPayloadGenerated::kPayloadKey);
    DWORD error = ERROR_SUCCESS;
    if (!EnsureDirectory(product, error) || !EnsureDirectory(bootstrap, error) || !EnsureDirectory(payload, error)) {
        ShowFailure(Win32Message(L"Não foi possível preparar a pasta privada do bootstrap.", error));
        return 11;
    }

    const Entry* coreEntry = nullptr;
    for (const auto& entry : OmniGhost::BootstrapPayloadGenerated::kEntries) {
        if (!Materialize(payload, entry, error)) {
            ShowFailure(Win32Message((std::wstring(L"Falha ao extrair/verificar ") + entry.fileName + L".").c_str(), error));
            return 12;
        }
        if (entry.core) coreEntry = &entry;
    }
    if (!coreEntry) {
        ShowFailure(L"O bundle não contém a payload OmniGhostCore.exe.");
        return 13;
    }

    const std::wstring corePath = Join(payload, coreEntry->fileName);
    if (!SetEnvironmentVariableW(L"OMNIGHOST_HOST_EXE", self.c_str())) {
        ShowFailure(Win32Message(L"Não foi possível preparar o ambiente do OmniGhost.", GetLastError()));
        return 14;
    }
    if (!SetEnvironmentVariableW(L"OMNIGHOST_BOOTSTRAP_DIR", payload.c_str())) {
        ShowFailure(Win32Message(L"Não foi possível preparar o diretório do bootstrap.", GetLastError()));
        return 14;
    }

    std::wstring commandLine = BuildChildCommandLine(corePath);
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const std::wstring workingDirectory = ParentDirectory(self);
    if (!CreateProcessW(corePath.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
                        0, nullptr, workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
                        &startup, &process)) {
        ShowFailure(Win32Message(L"A payload do OmniGhost foi extraída, mas o Windows não conseguiu iniciá-la.", GetLastError()));
        return 15;
    }

    CloseHandle(process.hThread);

    // Propagate immediate startup failures (missing/corrupt payload, duplicate
    // instance, loader error) while still getting out of the way for a normal
    // long-running UI process. This keeps double-click failures observable and
    // preserves the core's duplicate-instance exit code.
    const DWORD startupWait = WaitForSingleObject(process.hProcess, 5000);
    if (startupWait == WAIT_OBJECT_0) {
        DWORD exitCode = ERROR_GEN_FAILURE;
        if (!GetExitCodeProcess(process.hProcess, &exitCode)) exitCode = GetLastError();
        CloseHandle(process.hProcess);

        wchar_t hexCode[32]{};
        swprintf_s(hexCode, L"0x%08lX", static_cast<unsigned long>(exitCode));
        std::wstring message =
            L"A aplicação privada terminou durante os primeiros segundos do arranque."
            L"\n\nExitCode=" + std::to_wstring(exitCode) +
            L" (" + hexCode + L")" +
            L"\nCore=" + corePath;
        if (exitCode == 3) {
            message += L"\n\nO código 3 normalmente significa que outra instância do OmniGhost já possui o mutex/janela.";
        } else if (exitCode == 1) {
            message += L"\n\nO código 1 indica normalmente falha na preparação do runtime nativo do core.";
        } else if (exitCode == 0xC0000005u) {
            message += L"\n\n0xC0000005 = violação de acesso. O core tentou ler/escrever um endereço inválido.";
            message += L"\nDiagnóstico: " + Join(product, L"logs.txt");
            message += L"\nDumps: " + Join(product, L"crash-dumps");
        }
        ShowFailure(message);
        return static_cast<int>(exitCode);
    }
    if (startupWait == WAIT_FAILED) {
        const DWORD waitError = GetLastError();
        CloseHandle(process.hProcess);
        ShowFailure(Win32Message(L"O OmniGhost iniciou, mas o bootstrap não conseguiu validar o arranque.", waitError));
        return 16;
    }

    CloseHandle(process.hProcess);
    return 0;
}
