#include "install_engine.h"
#include "../platform/app_paths.h"
#include "../platform/unique_handle.h"
#include <Windows.h>
#include <Sddl.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <vector>
#include <unordered_set>
#include <cwctype>
#include <set>
#include <sstream>

namespace fs = std::filesystem;
namespace OmniGhost::Update {
namespace {
std::uint16_t U16(const unsigned char* value) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(value[0]) |
        (static_cast<std::uint16_t>(value[1]) << 8));
}
std::uint32_t U32(const unsigned char* value) {
    return static_cast<std::uint32_t>(value[0]) |
           (static_cast<std::uint32_t>(value[1]) << 8) |
           (static_cast<std::uint32_t>(value[2]) << 16) |
           (static_cast<std::uint32_t>(value[3]) << 24);
}

std::wstring UpperInvariant(std::wstring value) {
    for (wchar_t& c : value)
        c = static_cast<wchar_t>(std::towupper(c));
    return value;
}

bool ReservedWindowsName(const fs::path& part) {
    std::wstring name = part.filename().wstring();
    while (!name.empty() && (name.back() == L' ' || name.back() == L'.'))
        name.pop_back();
    const auto dot = name.find(L'.');
    if (dot != std::wstring::npos)
        name.resize(dot);
    name = UpperInvariant(std::move(name));
    if (name == L"CON" || name == L"PRN" || name == L"AUX" || name == L"NUL")
        return true;
    if (name.size() == 4 && (name.rfind(L"COM", 0) == 0 || name.rfind(L"LPT", 0) == 0))
        return name[3] >= L'1' && name[3] <= L'9';
    return false;
}

bool SafeArchiveRelative(const fs::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory())
        return false;
    // Reject traversal/dot segments as supplied instead of accepting them after
    // normalization. The updater never needs ambiguous archive spellings.
    for (const auto& rawPart : path) {
        const std::wstring raw = rawPart.wstring();
        if (raw == L"." || raw == L"..")
            return false;
    }
    const fs::path normalized = path.lexically_normal();
    if (normalized.empty() || normalized.is_absolute())
        return false;
    if (normalized.native().size() > 2048)
        return false;
    for (const auto& part : normalized) {
        const std::wstring text = part.wstring();
        if (text.empty() || text == L"." || text == L".." || text.size() > 255)
            return false;
        if (text.back() == L' ' || text.back() == L'.' || ReservedWindowsName(part))
            return false;
        for (wchar_t c : text) {
            if (c == L':' || c == L'\0' || c < 0x20)
                return false;
        }
    }
    return true;
}

std::wstring ArchiveKey(const fs::path& path) {
    std::wstring key = path.lexically_normal().generic_wstring();
    for (wchar_t& c : key) {
        if (c == L'/') c = L'\\';
        c = static_cast<wchar_t>(std::towupper(c));
    }
    return key;
}

fs::path SystemTarPath() {
    UINT capacity = 260;
    for (;;) {
        std::wstring buffer(capacity, L'\0');
        const UINT length = GetSystemDirectoryW(buffer.data(), capacity);
        if (length == 0)
            return {};
        if (length < capacity) {
            buffer.resize(length);
            return fs::path(buffer) / L"tar.exe";
        }
        if (length > 32768)
            return {};
        capacity = length + 1;
    }
}
bool SafeRelative(const fs::path& path) {
    return SafeArchiveRelative(path);
}
bool Descendant(const fs::path& child, const fs::path& parent) {
    std::error_code ec; const fs::path c = fs::weakly_canonical(child, ec); if (ec) return false;
    const fs::path p = fs::weakly_canonical(parent, ec); if (ec) return false;
    auto ci = c.begin(), pi = p.begin();
    for (; pi != p.end(); ++pi, ++ci) if (ci == c.end() || _wcsicmp(ci->c_str(), pi->c_str()) != 0) return false;
    return true;
}
std::wstring Quote(const fs::path& path) { std::wstring value = path.wstring(); std::wstring result = L"\""; for (wchar_t c : value) { if (c == L'\"') result += L'\\'; result += c; } return result + L"\""; }
bool CopyTree(const fs::path& source, const fs::path& destination, bool skipProtected, std::string& error) {
    std::error_code ec; fs::create_directories(destination, ec);
    for (fs::recursive_directory_iterator it(source, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec) { error = "Falha ao percorrer ficheiros: " + ec.message(); return false; }
        const fs::path relative = fs::relative(it->path(), source, ec);
        if (ec || !SafeRelative(relative)) { error = "Foi encontrado um caminho inseguro."; return false; }
        if (skipProtected && Paths::IsProtectedInstallPath(relative)) { if (it->is_directory()) it.disable_recursion_pending(); continue; }
        const fs::path target = destination / relative;
        if (it->is_symlink(ec) || it->is_other(ec)) { error = "Links e ficheiros especiais não são permitidos."; return false; }
        if (it->is_directory(ec)) fs::create_directories(target, ec);
        else if (it->is_regular_file(ec)) { fs::create_directories(target.parent_path(), ec); fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, ec); }
        if (ec) { error = "Falha ao copiar " + relative.string() + ": " + ec.message(); return false; }
    }
    return true;
}
std::uint64_t TreeSize(const fs::path& path) {
    std::uint64_t total = 0; std::error_code ec;
    for (fs::recursive_directory_iterator it(path, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec))
        if (!ec && it->is_regular_file(ec)) total += it->file_size(ec);
    return total;
}
bool RemoveManaged(const fs::path& target, std::string& error) {
    std::error_code ec;
    for (fs::directory_iterator it(target, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec) { error = ec.message(); return false; }
        const fs::path relative = it->path().filename(); if (Paths::IsProtectedInstallPath(relative)) continue;
        fs::remove_all(it->path(), ec); if (ec) { error = "Não foi possível remover " + relative.string() + ": " + ec.message(); return false; }
    }
    return true;
}

bool ContainsOnlyExecutable(const fs::path& root, const std::wstring& executableName) {
    std::error_code ec;
    std::size_t files = 0;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec || it->is_symlink(ec) || it->is_other(ec)) return false;
        if (!it->is_regular_file(ec)) continue;
        const fs::path relative = fs::relative(it->path(), root, ec);
        if (ec || relative != fs::path(executableName)) return false;
        ++files;
    }
    return !ec && files == 1;
}
 }

bool RestrictPathToCurrentUser(const fs::path& path, bool directory, std::string& error) {
    HANDLE rawToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken)) {
        error = "OpenProcessToken failed: " + std::to_string(GetLastError());
        return false;
    }
    Platform::UniqueHandle token(rawToken);

    DWORD bytes = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes);
    if (bytes == 0) {
        error = "GetTokenInformation size failed: " + std::to_string(GetLastError());
        return false;
    }
    std::vector<unsigned char> buffer(bytes);
    if (!GetTokenInformation(token.get(), TokenUser, buffer.data(), bytes, &bytes)) {
        error = "GetTokenInformation failed: " + std::to_string(GetLastError());
        return false;
    }
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
    LPWSTR rawSid = nullptr;
    if (!ConvertSidToStringSidW(user->User.Sid, &rawSid) || !rawSid) {
        error = "ConvertSidToStringSidW failed: " + std::to_string(GetLastError());
        return false;
    }
    const std::wstring sid(rawSid);
    LocalFree(rawSid);

    const wchar_t* inherit = directory ? L"OICI" : L"";
    const std::wstring sddl =
        L"D:P(A;" + std::wstring(inherit) + L";FA;;;SY)" +
        L"(A;" + std::wstring(inherit) + L";FA;;;BA)" +
        L"(A;" + std::wstring(inherit) + L";FA;;;" + sid + L")";

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr) || !descriptor) {
        error = "ConvertStringSecurityDescriptor failed: " + std::to_string(GetLastError());
        return false;
    }
    const BOOL secured = SetFileSecurityW(
        path.c_str(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptor);
    const DWORD lastError = secured ? ERROR_SUCCESS : GetLastError();
    LocalFree(descriptor);
    if (!secured) {
        error = "SetFileSecurityW failed: " + std::to_string(lastError);
        return false;
    }
    return true;
}

bool ValidateZip(const fs::path& package, std::uint32_t maximumEntries, std::uint64_t maximumExtractedBytes,
                 ArchiveInfo& info, std::string& error) {
    info = {}; std::ifstream stream(package, std::ios::binary); if (!stream) { error = "Não foi possível abrir o ZIP."; return false; }
    stream.seekg(0, std::ios::end);
    const std::streamoff end = stream.tellg();
    if (end < 22) { error = "ZIP truncado."; return false; }
    const std::uint64_t size = static_cast<std::uint64_t>(end);
    const std::uint64_t tailSize = (std::min<std::uint64_t>)(size, 65557); std::vector<unsigned char> tail(static_cast<std::size_t>(tailSize));
    stream.seekg(static_cast<std::streamoff>(size - tailSize)); stream.read(reinterpret_cast<char*>(tail.data()), tail.size());
    std::size_t eocd = std::string::npos;
    for (std::size_t i = tail.size() >= 22 ? tail.size() - 22 : 0; i != static_cast<std::size_t>(-1); --i) {
        if (U32(tail.data() + i) == 0x06054b50) { eocd = i; break; }
        if (i == 0) break;
    }
    if (eocd == std::string::npos) { error = "ZIP corrompido: diretório central em falta."; return false; }
    const std::uint16_t diskNumber = U16(tail.data() + eocd + 4);
    const std::uint16_t centralDisk = U16(tail.data() + eocd + 6);
    const std::uint16_t entriesOnDisk = U16(tail.data() + eocd + 8);
    const std::uint16_t entries = U16(tail.data() + eocd + 10);
    const std::uint32_t directorySize = U32(tail.data() + eocd + 12);
    const std::uint32_t directoryOffset = U32(tail.data() + eocd + 16);
    const std::uint16_t commentLength = U16(tail.data() + eocd + 20);
    if (diskNumber != 0 || centralDisk != 0 || entriesOnDisk != entries) {
        error = "ZIP multi-disco não é suportado.";
        return false;
    }
    if (eocd + 22u + commentLength != tail.size()) {
        error = "ZIP contém dados inesperados depois do diretório final.";
        return false;
    }
    if (!entries || entries == 0xffff || directoryOffset == 0xffffffff || directorySize == 0xffffffff || entries > maximumEntries ||
        static_cast<std::uint64_t>(directoryOffset) + directorySize > size) { error = "ZIP64 ou quantidade de ficheiros não suportada."; return false; }
    const std::uint64_t directoryEnd = static_cast<std::uint64_t>(directoryOffset) + directorySize;
    stream.seekg(directoryOffset);
    std::unordered_set<std::wstring> normalizedEntries;
    normalizedEntries.reserve(entries);
    for (std::uint32_t index = 0; index < entries; ++index) {
        const std::streamoff entryStart = stream.tellg();
        if (entryStart < 0 || static_cast<std::uint64_t>(entryStart) + 46u > directoryEnd) {
            error = "Diretório central ZIP truncado.";
            return false;
        }
        std::array<unsigned char, 46> header{}; stream.read(reinterpret_cast<char*>(header.data()), header.size());
        if (!stream || U32(header.data()) != 0x02014b50) { error = "Diretório central ZIP inválido."; return false; }
        const std::uint16_t flags = U16(header.data() + 8), method = U16(header.data() + 10);
        const std::uint32_t compressed = U32(header.data() + 20);
        const std::uint32_t uncompressed = U32(header.data() + 24); const std::uint16_t nameLength = U16(header.data() + 28);
        const std::uint16_t extraLength = U16(header.data() + 30), entryCommentLength = U16(header.data() + 32);
        const std::uint32_t external = U32(header.data() + 38);
        if ((flags & 1) || (method != 0 && method != 8) || !nameLength || nameLength > 4096) { error = "Entrada ZIP encriptada ou não suportada."; return false; }
        const std::uint64_t variableBytes = static_cast<std::uint64_t>(nameLength) + extraLength + entryCommentLength;
        const std::streamoff variableStart = stream.tellg();
        if (variableStart < 0 || static_cast<std::uint64_t>(variableStart) + variableBytes > directoryEnd) {
            error = "Entrada do diretório central excede os limites declarados.";
            return false;
        }
        std::string name(nameLength, '\0'); stream.read(name.data(), nameLength); if (!stream) { error = "Nome ZIP truncado."; return false; }
        stream.seekg(static_cast<std::streamoff>(extraLength + entryCommentLength), std::ios::cur);
        if (!stream || name.find('\0') != std::string::npos) { error = "Entrada ZIP inválida."; return false; }
        fs::path relative;
        try {
            const std::u8string utf8Name(name.begin(), name.end());
            relative = fs::path(utf8Name).lexically_normal();
        } catch (const fs::filesystem_error&) {
            error = "O ZIP contém um nome de ficheiro inválido.";
            return false;
        }
        if (!SafeArchiveRelative(relative) || name.front() == '/' || name.front() == '\\') {
            error = "O ZIP contém um caminho inválido, reservado ou inseguro.";
            return false;
        }
        const std::wstring key = ArchiveKey(relative);
        if (!normalizedEntries.insert(key).second) {
            error = "O ZIP contém caminhos duplicados (incluindo colisões de maiúsculas/minúsculas).";
            return false;
        }
        // Reject extreme compression ratios before invoking the system extractor.
        // Small files are exempt so normal metadata files are not penalized.
        if (uncompressed >= 1024u * 1024u && (compressed == 0 || uncompressed / (std::max)(compressed, 1u) > 250u)) {
            error = "O ZIP contém uma entrada com taxa de compressão suspeita.";
            return false;
        }
        const std::uint32_t unixMode = external >> 16;
        if ((unixMode & 0170000) == 0120000) { error = "O ZIP contém um link simbólico."; return false; }
        if (uncompressed > maximumExtractedBytes || info.totalUncompressed > maximumExtractedBytes - uncompressed) { error = "O ZIP excede o limite de extração."; return false; }
        info.totalUncompressed += uncompressed; info.files.push_back(relative);
    }
    const std::streamoff consumed = stream.tellg();
    if (consumed < 0 || static_cast<std::uint64_t>(consumed) != directoryEnd) {
        error = "O tamanho real do diretório central não corresponde ao declarado.";
        return false;
    }
    info.entries = entries; return true;
}

bool ExtractZipSecurely(const fs::path& package, const fs::path& staging, const Configuration& config,
                        ArchiveInfo& info, std::string& error) {
    if (!ValidateZip(package, config.maximumArchiveEntries, config.maximumExtractedBytes, info, error)) return false;
    std::error_code ec; fs::remove_all(staging, ec); fs::create_directories(staging, ec); if (ec) { error = ec.message(); return false; }
    if (!RestrictPathToCurrentUser(staging, true, error)) {
        fs::remove_all(staging, ec);
        return false;
    }
    const fs::path tar = SystemTarPath();
    if (tar.empty() || !fs::exists(tar, ec)) { error = "O extrator seguro do Windows (tar.exe) não está disponível."; return false; }
    std::wstring command = Quote(tar) + L" -xf " + Quote(package) + L" -C " + Quote(staging);
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
    std::vector<wchar_t> mutableCommand(command.begin(), command.end()); mutableCommand.push_back(L'\0');
    if (!CreateProcessW(tar.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) { error = "Não foi possível iniciar a extração."; return false; }
    Platform::UniqueProcessInformation processOwner(process);
    const DWORD wait = WaitForSingleObject(processOwner.process(), 120000);
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(processOwner.process(), 124);
        WaitForSingleObject(processOwner.process(), 5000);
        error = wait == WAIT_TIMEOUT ? "A extração excedeu o tempo limite." : "Falha ao aguardar pelo extrator.";
        return false;
    }
    DWORD exitCode = 1;
    if (!GetExitCodeProcess(processOwner.process(), &exitCode) || exitCode != 0) { error = "O pacote ZIP está corrompido ou não pôde ser extraído."; return false; }
    std::uint64_t extractedBytes = 0;
    for (fs::recursive_directory_iterator it(staging, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec) { error = "Falha ao validar a árvore extraída."; return false; }
        const fs::path relative = fs::relative(it->path(), staging, ec);
        if (ec || !SafeArchiveRelative(relative) || !Descendant(it->path(), staging)) {
            error = "A extração produziu um caminho inseguro."; return false;
        }
        const DWORD attributes = GetFileAttributesW(it->path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || fs::is_symlink(it->symlink_status(ec)) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            error = "A extração produziu um reparse point ou link inseguro."; return false;
        }
        if (it->is_regular_file(ec)) {
            const std::uint64_t fileSize = static_cast<std::uint64_t>(it->file_size(ec));
            if (ec || fileSize > config.maximumExtractedBytes || extractedBytes > config.maximumExtractedBytes - fileSize) {
                error = "A árvore extraída excede o limite configurado."; return false;
            }
            extractedBytes += fileSize;
        }
    }
    if (extractedBytes > info.totalUncompressed) {
        error = "A árvore extraída é maior do que o tamanho declarado pelo ZIP."; return false;
    }
    return true;
}

bool HasEnoughDiskSpace(const fs::path& path, std::uint64_t requiredBytes, std::string& error) {
    ULARGE_INTEGER available{}; if (!GetDiskFreeSpaceExW(path.c_str(), &available, nullptr, nullptr)) { error = "Não foi possível verificar o espaço livre."; return false; }
    if (available.QuadPart < requiredBytes) { error = "Espaço em disco insuficiente para atualização e backup."; return false; }
    return true;
}

bool RollbackInstallation(const InstallOptions& options, std::string& error) {
    if (!fs::exists(options.backup)) { error = "O backup necessário para rollback não existe."; return false; }
    if (ContainsOnlyExecutable(options.backup, options.executableName)) {
        std::error_code ec;
        fs::copy_file(options.backup / options.executableName,
            options.target / options.executableName, fs::copy_options::overwrite_existing, ec);
        if (ec) error = "Não foi possível restaurar o executável: " + ec.message();
        return !ec;
    }
    if (!RemoveManaged(options.target, error)) return false;
    return CopyTree(options.backup, options.target, false, error);
}

InstallResult InstallWithRollback(const InstallOptions& options) {
    InstallResult result{}; std::string error;
    auto failInjected = [&](const char* stage) { return options.failureInjector && options.failureInjector(stage); };
    if (!SafeRelative(fs::path(options.executableName)) || !fs::exists(options.staging / options.executableName)) { result.error = "O pacote não contém o executável principal."; return result; }
    const bool singleExecutable = ContainsOnlyExecutable(options.staging, options.executableName);
    const std::uint64_t required = (singleExecutable
        ? TreeSize(options.staging) * 2
        : TreeSize(options.target) + TreeSize(options.staging)) + 64ull * 1024ull * 1024ull;
    if (!HasEnoughDiskSpace(options.target, required, result.error)) return result;
    std::error_code ec; fs::remove_all(options.backup, ec); fs::create_directories(options.backup, ec);
    if (singleExecutable) {
        fs::copy_file(options.target / options.executableName,
            options.backup / options.executableName, fs::copy_options::overwrite_existing, ec);
    } else if (!ec) {
        (void)CopyTree(options.target, options.backup, true, error);
    }
    if (ec || !error.empty() || failInjected("backup")) { result.error = error.empty() ? "Falha simulada no backup." : error; return result; }
    if (singleExecutable) {
        if (failInjected("replace")) {
            result.error = "Falha simulada ao substituir ficheiros.";
            result.rolledBack = RollbackInstallation(options, error);
            return result;
        }
        fs::copy_file(options.staging / options.executableName,
            options.target / options.executableName, fs::copy_options::overwrite_existing, ec);
        if (ec || failInjected("copy")) {
            result.error = ec ? "Falha ao instalar o novo executável: " + ec.message() : "Falha simulada ao copiar.";
            result.rolledBack = RollbackInstallation(options, error);
            return result;
        }
        result.success = true;
        return result;
    }
    if (!RemoveManaged(options.target, error) || failInjected("replace")) {
        result.error = error.empty() ? "Falha ao substituir ficheiros." : error; result.rolledBack = RollbackInstallation(options, error); return result;
    }
    if (!CopyTree(options.staging, options.target, true, error) || failInjected("copy") || !fs::exists(options.target / options.executableName)) {
        result.error = error.empty() ? "Falha ao instalar os novos ficheiros." : error; result.rolledBack = RollbackInstallation(options, error); return result;
    }
    result.success = true; return result;
}

void PruneBackups(const fs::path& backupRoot, int backupsToKeep) {
    std::error_code ec; std::vector<fs::directory_entry> backups;
    for (const auto& entry : fs::directory_iterator(backupRoot, fs::directory_options::skip_permission_denied, ec)) if (entry.is_directory(ec)) backups.push_back(entry);
    std::sort(backups.begin(), backups.end(), [](const auto& a, const auto& b) { std::error_code ea, eb; return a.last_write_time(ea) > b.last_write_time(eb); });
    for (std::size_t i = static_cast<std::size_t>((std::max)(backupsToKeep, 1)); i < backups.size(); ++i) fs::remove_all(backups[i].path(), ec);
}
}
