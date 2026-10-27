#include "session_log.h"
#include "build_info.h"

#include "app_paths.h"
#include "process_metrics.h"
#include "system_info.h"
#include "text_encoding.h"
#include "../app_version.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <dpapi.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <iterator>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "crypt32.lib")

namespace fs = std::filesystem;

namespace OmniGhost::SessionLog {
namespace {

constexpr std::uintmax_t kMaximumLogBytes = 10u * 1024u * 1024u;
constexpr std::uintmax_t kMaximumStructuredLogBytes = 5u * 1024u * 1024u;
constexpr size_t kMaximumArchivedLogs = 8;
constexpr std::uintmax_t kMaximumArchiveBytes = 40u * 1024u * 1024u;
constexpr size_t kMaximumRecentEvents = 256;


constexpr bool kEncryptLogFiles = true;
constexpr wchar_t kDpapiEntropy[] = L"OmniGhost-SessionLog-v1";


std::optional<std::vector<BYTE>> DpapiEncrypt(std::string_view plaintext) {
    DATA_BLOB inBlob{};
    inBlob.pbData = const_cast<BYTE*>(reinterpret_cast<const BYTE*>(plaintext.data()));
    inBlob.cbData = static_cast<DWORD>(plaintext.size());

    DATA_BLOB entropyBlob{};
    entropyBlob.pbData = const_cast<BYTE*>(reinterpret_cast<const BYTE*>(kDpapiEntropy));
    entropyBlob.cbData = static_cast<DWORD>((wcslen(kDpapiEntropy) + 1) * sizeof(wchar_t));

    DATA_BLOB outBlob{};


    if (CryptProtectData(&inBlob, L"OmniGhost-SessionLog", &entropyBlob, nullptr, nullptr,
                         CRYPTPROTECT_UI_FORBIDDEN, &outBlob)) {
        std::vector<BYTE> result(outBlob.pbData, outBlob.pbData + outBlob.cbData);
        LocalFree(outBlob.pbData);
        return result;
    }
    return std::nullopt;
}

std::optional<std::string> DpapiDecrypt(const std::vector<BYTE>& ciphertext) {
    DATA_BLOB inBlob{};
    inBlob.pbData = const_cast<BYTE*>(ciphertext.data());
    inBlob.cbData = static_cast<DWORD>(ciphertext.size());

    DATA_BLOB entropyBlob{};
    entropyBlob.pbData = const_cast<BYTE*>(reinterpret_cast<const BYTE*>(kDpapiEntropy));
    entropyBlob.cbData = static_cast<DWORD>((wcslen(kDpapiEntropy) + 1) * sizeof(wchar_t));

    DATA_BLOB outBlob{};
    if (CryptUnprotectData(&inBlob, nullptr, &entropyBlob, nullptr, nullptr,
                           CRYPTPROTECT_UI_FORBIDDEN, &outBlob)) {
        std::string result(reinterpret_cast<char*>(outBlob.pbData), outBlob.cbData);
        LocalFree(outBlob.pbData);
        return result;
    }
    return std::nullopt;
}

std::mutex g_outputMutex;
std::mutex g_lifecycleMutex;
std::mutex g_eventMutex;
std::uintmax_t g_fileBytes = 0;
std::uintmax_t g_structuredFileBytes = 0;
std::atomic_bool g_fileTruncated{false};
std::atomic_bool g_structuredFileTruncated{false};
std::atomic_bool g_sinkClosed{false};

enum class ConsoleStreamKind {
    StdOut,
    StdErr,
    StdLog
};

bool RotateHumanLogIfNeeded(std::uintmax_t incomingBytes);

bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool ContainsAny(std::string_view value, std::initializer_list<std::string_view> needles) {
    for (const auto needle : needles) {
        if (!needle.empty() && value.find(needle) != std::string_view::npos)
            return true;
    }
    return false;
}

bool IsEssentialConsoleLine(std::string_view line, ConsoleStreamKind kind) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        line.remove_suffix(1);

    if (kind == ConsoleStreamKind::StdErr)
        return true;
    if (line.empty())
        return false;


    if (line.front() != '[')
        return true;


    if (ContainsAny(line, {"[ERROR]", "[CRITICAL]", "[WARN]", "[WARNING]", "[FAIL]"}))
        return true;


    if (StartsWith(line, "[INFO][") || StartsWith(line, "[DEBUG][") ||
        StartsWith(line, "[TRACE][") || StartsWith(line, "[LOG]") ||
        StartsWith(line, "[SESSION]") || StartsWith(line, "[DMA]") ||
        StartsWith(line, "[VMM]") || StartsWith(line, "[VFS]") ||
        StartsWith(line, "[PROCESS]")) {
        return false;
    }

    if (StartsWith(line, "[OK]"))
        return true;


    if (StartsWith(line, "[Warzone]") || StartsWith(line, "[CS2]") ||
        StartsWith(line, "[FiveM]") || StartsWith(line, "[OmniGhost]") ||
        StartsWith(line, "[Kmbox]") || StartsWith(line, "[OffsetAuto]")) {
        return ContainsAny(line, {
            "OK", "ready", "Ready", "ativo", "ativa", "backend", "Backend",
            "Anexado", "anexado", "attached", "Attached", "offset", "Offset",
            "Module base", "module base", "Processo terminou", "processo terminou",
            "falha", "Falha", "erro", "Erro", "failed", "Failed", "invalid", "Invalid",
            "nao encontrado", "não encontrado", "parado", "shutdown", "cancel"
        });
    }


    return false;
}

class TeeBuffer final : public std::streambuf {
public:
    TeeBuffer(std::streambuf* console, std::streambuf* file, ConsoleStreamKind kind)
        : console_(console), file_(file), kind_(kind) {}

protected:
    int overflow(int value) override {
        if (traits_type::eq_int_type(value, traits_type::eof()))
            return traits_type::not_eof(value);

        if (g_sinkClosed.load(std::memory_order_acquire))
            return value;

        std::lock_guard<std::mutex> lock(g_outputMutex);
        const char character = traits_type::to_char_type(value);
        WriteFile(&character, 1);
        QueueConsole(&character, 1);
        return value;
    }

    std::streamsize xsputn(const char* data, std::streamsize size) override {
        if (!data || size <= 0)
            return 0;

        if (g_sinkClosed.load(std::memory_order_acquire))
            return size;

        std::lock_guard<std::mutex> lock(g_outputMutex);
        WriteFile(data, size);
        QueueConsole(data, size);

        return size;
    }

    int sync() override {
        if (g_sinkClosed.load(std::memory_order_acquire))
            return 0;
        std::lock_guard<std::mutex> lock(g_outputMutex);
        FlushConsolePending();
        const int consoleResult = console_ ? console_->pubsync() : 0;
        const int fileResult = file_ ? file_->pubsync() : 0;
        return consoleResult == 0 && fileResult == 0 ? 0 : -1;
    }

private:
    void WriteFile(const char* data, std::streamsize size) {
        if (!file_ || size <= 0)
            return;

        if (!RotateHumanLogIfNeeded(static_cast<std::uintmax_t>(size))) {
            g_fileTruncated = true;
            return;
        }

        const std::uintmax_t remaining = g_fileBytes < kMaximumLogBytes
            ? kMaximumLogBytes - g_fileBytes
            : 0;
        const std::streamsize allowed = static_cast<std::streamsize>((std::min)(
            static_cast<std::uintmax_t>(size), remaining));
        if (allowed > 0) {
            const std::streamsize written = file_->sputn(data, allowed);
            g_fileBytes += static_cast<std::uintmax_t>((std::max)(written, std::streamsize{0}));
        }
        if (allowed < size)
            g_fileTruncated = true;
    }

    void QueueConsole(const char* data, std::streamsize size) {
        if (!console_ || !data || size <= 0)
            return;

        for (std::streamsize i = 0; i < size; ++i) {
            pendingConsole_.push_back(data[i]);
            if (data[i] == '\n')
                FlushConsolePending();
        }
    }

    void FlushConsolePending() {
        if (!console_ || pendingConsole_.empty())
            return;

        if (IsEssentialConsoleLine(pendingConsole_, kind_))
            console_->sputn(pendingConsole_.data(), static_cast<std::streamsize>(pendingConsole_.size()));
        pendingConsole_.clear();
    }

    std::streambuf* console_{};
    std::streambuf* file_{};
    ConsoleStreamKind kind_{ConsoleStreamKind::StdOut};
    std::string pendingConsole_;
};

std::ofstream g_file;
std::ofstream g_structuredFile;
std::unique_ptr<TeeBuffer> g_outTee;
std::unique_ptr<TeeBuffer> g_errTee;
std::unique_ptr<TeeBuffer> g_logTee;



std::streambuf* g_originalOut{};
std::streambuf* g_originalErr{};
std::streambuf* g_originalLog{};
fs::path g_logPath;
fs::path g_structuredLogPath;
std::string g_sessionId;
std::deque<std::string> g_recentEvents;
std::string g_lastErrorId;
std::uint64_t g_errorSequence = 0;
std::uint64_t g_liveRotationSequence = 0;
std::string g_logDay;
std::string g_structuredLogDay;
std::chrono::steady_clock::time_point g_sessionStart{};
std::string g_startReason{"normal"};

std::string TimestampForFilename() {
    const std::time_t now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm local{};
    localtime_s(&local, &now);
    std::ostringstream value;
    value << std::put_time(&local, "%Y%m%d-%H%M%S");
    return value.str();
}

std::string CurrentLocalDay() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_s(&local, &now);
    std::ostringstream value;
    value << std::put_time(&local, "%Y%m%d");
    return value.str();
}

std::string TimestampUtc() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_s(&utc, &time);
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string BuildSessionId() {
    return TimestampForFilename() + "-pid" + std::to_string(GetCurrentProcessId());
}

std::string JsonEscape(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 16);
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char c : value) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                out += "\\u00";
                out += hex[(c >> 4) & 0x0f];
                out += hex[c & 0x0f];
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    return out;
}

void ReplaceAllCaseInsensitive(std::string& text, const std::string& needle, std::string_view replacement) {
    if (needle.empty())
        return;

    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    std::string foldedText = text;
    std::string foldedNeedle = needle;
    std::transform(foldedText.begin(), foldedText.end(), foldedText.begin(), lower);
    std::transform(foldedNeedle.begin(), foldedNeedle.end(), foldedNeedle.begin(), lower);

    std::size_t position = 0;
    while ((position = foldedText.find(foldedNeedle, position)) != std::string::npos) {
        text.replace(position, needle.size(), replacement);
        foldedText.replace(position, needle.size(), replacement);
        position += replacement.size();
    }
}

std::string SanitizedFieldValue(const Field& field) {
    if (field.sensitive)
        return "<redacted>";
    return RedactForSupport(field.value);
}

void AddRecentEvent(const std::string& event) {
    g_recentEvents.push_back(event);
    while (g_recentEvents.size() > kMaximumRecentEvents)
        g_recentEvents.pop_front();
}

void PruneArchives() {
    std::error_code error;
    const fs::path archiveDir = Paths::Logs() / L"archive";

    if (!fs::is_directory(archiveDir, error)) {
        error.clear();
        return;
    }
    error.clear();

    struct Candidate {
        fs::path path;
        fs::file_time_type time{};
    };
    std::vector<Candidate> candidates;
    for (fs::directory_iterator it(archiveDir, fs::directory_options::skip_permission_denied, error), end;
         it != end;
         it.increment(error)) {
        if (error) {
            error.clear();
            continue;
        }
        if (!it->is_regular_file(error)) {
            error.clear();
            continue;
        }
        Candidate item{it->path(), it->last_write_time(error)};
        if (!error)
            candidates.push_back(std::move(item));
        error.clear();
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.time > b.time;
    });

    std::uintmax_t retainedBytes = 0;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const std::uintmax_t size = fs::file_size(candidates[i].path, error);
        if (error) {
            error.clear();
            continue;
        }
        const bool overCount = i >= kMaximumArchivedLogs * 2;
        const bool overBytes = retainedBytes > kMaximumArchiveBytes ||
            size > (kMaximumArchiveBytes - (std::min)(retainedBytes, kMaximumArchiveBytes));
        if (overCount || overBytes) {
            fs::remove(candidates[i].path, error);
            error.clear();
            continue;
        }
        retainedBytes += size;
    }
}


bool EncryptLogFile(const fs::path& sourcePath, const fs::path& destPath) {
    if (!kEncryptLogFiles) return false;

    std::error_code error;
    if (!fs::is_regular_file(sourcePath, error)) return false;

    std::ifstream inFile(sourcePath, std::ios::binary);
    if (!inFile) return false;

    std::ostringstream content;
    content << inFile.rdbuf();
    inFile.close();

    auto encrypted = DpapiEncrypt(content.str());
    if (!encrypted) return false;

    std::ofstream outFile(destPath, std::ios::binary | std::ios::trunc);
    if (!outFile) return false;

    outFile.write(reinterpret_cast<const char*>(encrypted->data()), static_cast<std::streamsize>(encrypted->size()));
    outFile.close();


    fs::remove(sourcePath, error);
    return true;
}

bool RotateOpenLog(std::ofstream& stream, const fs::path& path, const char* suffix,
                   std::uintmax_t& bytes, std::string& day) {
    std::error_code error;
    stream.flush();
    stream.close();




    if (fs::is_regular_file(path, error) && fs::file_size(path, error) > 0) {
        error.clear();
        const fs::path dir = path.parent_path();
        const std::wstring stem = path.stem().wstring();
        const fs::path enc3 = dir / (stem + L".3.txt.enc");
        const fs::path enc2 = dir / (stem + L".2.txt.enc");
        const fs::path enc1 = dir / (stem + L".1.txt.enc");
        fs::remove(enc3, error); error.clear();
        fs::rename(enc2, enc3, error); error.clear();
        fs::rename(enc1, enc2, error); error.clear();
        if (EncryptLogFile(path, enc1)) {
            error.clear();
            fs::remove(path, error);
        }
        error.clear();
        (void)suffix;
        ++g_liveRotationSequence;
    }

    stream.clear();
    stream.open(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!stream) return false;
    bytes = 0;
    day = CurrentLocalDay();
    PruneArchives();
    return true;
}

bool RotateHumanLogIfNeeded(std::uintmax_t incomingBytes) {
    if (!g_file.is_open()) return false;
    const std::string today = CurrentLocalDay();
    if ((g_logDay.empty() || g_logDay == today) &&
        incomingBytes <= kMaximumLogBytes - (std::min)(g_fileBytes, kMaximumLogBytes))
        return true;
    return RotateOpenLog(g_file, g_logPath, ".logs.txt", g_fileBytes, g_logDay);
}

bool RotateStructuredLogIfNeeded(std::uintmax_t incomingBytes) {
    if (!g_structuredFile.is_open()) return false;
    const std::string today = CurrentLocalDay();
    if ((g_structuredLogDay.empty() || g_structuredLogDay == today) &&
        incomingBytes <= kMaximumStructuredLogBytes - (std::min)(g_structuredFileBytes, kMaximumStructuredLogBytes))
        return true;
    return RotateOpenLog(g_structuredFile, g_structuredLogPath, ".events.jsonl",
                         g_structuredFileBytes, g_structuredLogDay);
}

void ArchiveExistingLog(const fs::path& path, const char* ) {

    std::error_code error;
    if (!fs::is_regular_file(path, error))
        return;

    const std::uintmax_t size = fs::file_size(path, error);
    if (error || size == 0) {
        error.clear();
        fs::remove(path, error);
        return;
    }

    const fs::path archive = Paths::Logs() / L"archive";
    fs::create_directories(archive, error);
    error.clear();
    const fs::path encrypted = archive /
        (L"session-" + Platform::Utf8ToWide(TimestampForFilename()) + L"-pid" +
         std::to_wstring(GetCurrentProcessId()) + L".logs.enc");
    if (!EncryptLogFile(path, encrypted))
        fs::remove(path, error);
    PruneArchives();
}

fs::path PreferredLogPath() {


    return Paths::LocalData() / L"logs.txt";
}

void WriteSessionHeader() {
    const std::time_t now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm local{};
    localtime_s(&local, &now);
    std::clog << "\n[" << std::put_time(&local, "%Y-%m-%d %H:%M:%S")
              << "] [SESSION] OmniGhost iniciado"
              << " version=" << OmniGhost::Version
              << " pid=" << GetCurrentProcessId()
              << " session_id=" << g_sessionId
              << " build_id=" << OmniGhost::BuildInfo::BuildId
              << " commit=" << OmniGhost::BuildInfo::CommitId
              << " build_utc=" << OmniGhost::BuildInfo::BuildUtc
              << " vc_tools=" << OmniGhost::BuildInfo::ToolchainVersion
              << " windows_sdk=" << OmniGhost::BuildInfo::WindowsSdkVersion
              << " memprocfs=" << OmniGhost::BuildInfo::MemProcFSVersion
              << " leechcore=" << OmniGhost::BuildInfo::LeechCoreVersion << "\n";
    std::clog << "[SESSION] system "
              << Platform::FormatSystemInfo(Platform::CaptureSystemInfo())
              << "\n";
    std::clog << "[SESSION] metrics_start "
              << Platform::FormatProcessMetrics(Platform::CaptureProcessMetrics())
              << "\n";
}

}

const char* SeverityName(Severity severity) noexcept {
    switch (severity) {
    case Severity::Trace: return "TRACE";
    case Severity::Debug: return "DEBUG";
    case Severity::Info: return "INFO";
    case Severity::Warning: return "WARN";
    case Severity::Error: return "ERROR";
    case Severity::Critical: return "CRITICAL";
    default: return "UNKNOWN";
    }
}

const char* SubsystemName(Subsystem subsystem) noexcept {


    switch (subsystem) {
    case Subsystem::Auth: return "AUTH";
    case Subsystem::DMA: return "DMA";
    case Subsystem::VMM: return "VMM";
    case Subsystem::LeechCore: return "LEECHCORE";
    case Subsystem::Ftdi: return "FTDI";
    case Subsystem::Pnp: return "PNP";
    case Subsystem::ProcInfo: return "PROCINFO";
    case Subsystem::Process: return "PROCESS";
    case Subsystem::Runtime:
    case Subsystem::Radar: return "GAME";
    case Subsystem::Input: return "INPUT";
    case Subsystem::Updater: return "UPDATE";
    case Subsystem::UI: return "UI";
    case Subsystem::Core:
    case Subsystem::Platform:
    case Subsystem::Config: return "APP";
    case Subsystem::Adapter: return "ADAPTER";
    case Subsystem::Read: return "READ";
    case Subsystem::Watchdog: return "WATCHDOG";
    default: return "APP";
    }
}

std::string RedactForSupport(std::string_view input) {
    std::string text(input);
    for (char& c : text) {
        if (static_cast<unsigned char>(c) < 0x20 && c != '\t')
            c = ' ';
    }

    const fs::path localData = Paths::LocalData();
    const fs::path userProfile = localData.parent_path().parent_path().parent_path();
    const std::string profileUtf8 = Platform::WideToUtf8(userProfile.wstring());
    if (!profileUtf8.empty())
        ReplaceAllCaseInsensitive(text, profileUtf8, "<user-profile>");

    wchar_t username[256]{};
    DWORD usernameLength = static_cast<DWORD>(std::size(username));
    if (GetUserNameW(username, &usernameLength) && username[0]) {
        const std::string utf8 = Platform::WideToUtf8(username);
        if (!utf8.empty())
            ReplaceAllCaseInsensitive(text, utf8, "<user>");
    }





    static const std::regex reUrlCred(R"((https?://)([^/@:\s]+):([^/@\s]+)@)", std::regex::icase);
    static const std::regex reAuthHdr(R"((Authorization\s*[:=]\s*(?:Bearer|Basic)\s+)[A-Za-z0-9+/_=.-]+)", std::regex::icase);
    static const std::regex reQueryToken(R"(([?&](?:token|access_token|api_key|apikey|key|password|passwd|pwd)=)[^&#\s]+)", std::regex::icase);
    static const std::regex reKvSecret(R"(((?:password|passwd|pwd|token|access_token|refresh_token|api_key|apikey|license|licence|license_key|licence_key)\s*[:=]\s*)[^,;\s]+)", std::regex::icase);
    static const std::regex reJsonSecret(R"((\"(?:password|passwd|pwd|token|access_token|refresh_token|api_key|apikey|license|licence|license_key|licence_key)\"\s*:\s*\")[^\"]*(\"))", std::regex::icase);
    static const std::regex reLicenseKey(R"((license[_\-]?key\s*[:=]\s*)[A-Za-z0-9\-]{16,})", std::regex::icase);
    static const std::regex reLicenseGroup(R"(([A-Za-z0-9]{5}(?:-[A-Za-z0-9]{5}){3,})[A-Za-z0-9\-]*)", std::regex::icase);
    static const std::regex reHwid(R"((hwid|hardware[_\-]?id\s*[:=]\s*)[A-Fa-f0-9\:\-]{12,})", std::regex::icase);
    static const std::regex reMac(R"(\b([A-Fa-f0-9]{2}[:-]){5}[A-Fa-f0-9]{2}\b)");
    static const std::regex reDevicePath(R"((device[_\-]?path\s*[:=]\s*)[^\s,;]+)", std::regex::icase);
    static const std::regex reSerial(R"((serial[_\-]?number\s*[:=]\s*)[A-Za-z0-9\-]{8,})", std::regex::icase);
    static const std::regex rePtr(R"(\b(0x[0-9A-Fa-f]{8,16})\b)");
    static const std::regex rePath(R"(([A-Za-z]:[\\/][^\\/\s,;]+))", std::regex::icase);
    static const std::regex reKeyAuth(R"((keyauth[_\-]?(?:api|key|token)\s*[:=]\s*)[A-Za-z0-9\-_\.]{16,})", std::regex::icase);
    try {
        text = std::regex_replace(text, reUrlCred, "$1<redacted>@");
        text = std::regex_replace(text, reAuthHdr, "$1<redacted>");
        text = std::regex_replace(text, reQueryToken, "$1<redacted>");
        text = std::regex_replace(text, reKvSecret, "$1<redacted>");
        text = std::regex_replace(text, reJsonSecret, "$1<redacted>$2");


        text = std::regex_replace(text, reLicenseKey, "$1<redacted>");
        text = std::regex_replace(text, reLicenseGroup, "<redacted-license>");


        text = std::regex_replace(text, reHwid, "$1<redacted>");
        text = std::regex_replace(text, reMac, "<redacted-mac>");


        text = std::regex_replace(text, reDevicePath, "$1<redacted>");
        text = std::regex_replace(text, reSerial, "$1<redacted>");

        text = std::regex_replace(text, rePtr, "<redacted-ptr>");

        text = std::regex_replace(text, rePath, "<redacted-path>");


        text = std::regex_replace(text, reKeyAuth, "$1<redacted>");
    } catch (const std::exception& ex) {
        std::cerr << "[SessionLog] Redaction regex failed: " << ex.what() << "\n";
    } catch (...) {
        std::cerr << "[SessionLog] Redaction regex failed with unknown exception\n";
    }
    return text;
}

void SetStartReason(std::string_view reason) {
    std::lock_guard<std::mutex> lifecycle(g_lifecycleMutex);
    g_startReason = reason.empty() ? "normal" : RedactForSupport(reason);
}

bool Initialize() {
    std::lock_guard<std::mutex> lifecycle(g_lifecycleMutex);
    if (g_file.is_open())
        return true;

    if (!Paths::EnsureUserDirectories())
        return false;


    ArchiveExistingLog(Paths::InstallDirectory() / L"log.txt", ".legacy-log.txt");
    ArchiveExistingLog(Paths::Logs() / L"log.txt", ".legacy-log.txt");
    g_logPath = PreferredLogPath();
    g_structuredLogPath.clear();
    ArchiveExistingLog(g_logPath, ".logs.txt");

    g_file.open(g_logPath, std::ios::out | std::ios::trunc);
    if (!g_file && g_logPath != Paths::Logs() / L"logs.txt") {
        g_logPath = Paths::Logs() / L"logs.txt";
        ArchiveExistingLog(g_logPath, ".logs.txt");
        g_file.open(g_logPath, std::ios::out | std::ios::trunc);
    }
    if (!g_file)
        return false;


    g_fileBytes = 0;
    g_structuredFileBytes = 0;
    g_logDay = CurrentLocalDay();
    g_structuredLogDay = g_logDay;
    g_fileTruncated = false;
    g_structuredFileTruncated = false;
    g_sessionId = BuildSessionId();
    g_recentEvents.clear();
    g_lastErrorId.clear();
    g_errorSequence = 0;
    g_originalOut = std::cout.rdbuf();
    g_originalErr = std::cerr.rdbuf();
    g_originalLog = std::clog.rdbuf();
    g_outTee = std::make_unique<TeeBuffer>(g_originalOut, g_file.rdbuf(), ConsoleStreamKind::StdOut);
    g_errTee = std::make_unique<TeeBuffer>(g_originalErr, g_file.rdbuf(), ConsoleStreamKind::StdErr);
    g_logTee = std::make_unique<TeeBuffer>(g_originalLog, g_file.rdbuf(), ConsoleStreamKind::StdLog);
    std::cout.rdbuf(g_outTee.get());
    std::cerr.rdbuf(g_errTee.get());
    std::clog.rdbuf(g_logTee.get());

    g_sessionStart = std::chrono::steady_clock::now();
    WriteSessionHeader();
    std::clog << "[LOG] path=" << Platform::WideToUtf8(g_logPath.wstring()) << " max_bytes="
              << kMaximumLogBytes << " archive_limit=" << kMaximumArchivedLogs
              << " archive_max_bytes=" << kMaximumArchiveBytes << "\n";
    Write(Severity::Info, Subsystem::Platform, "runtime dependency manifest", {
        {"storage", "embedded", false}
    });

    if (!g_structuredFile.is_open())
        std::clog << "[LOG] unified_sink=READY file=logs.txt structured_sidecar=DISABLED\n";
    else
        std::clog << "[LOG] structured_events=READY path="
                  << Platform::WideToUtf8(g_structuredLogPath.wstring())
                  << " max_bytes=" << kMaximumStructuredLogBytes << "\n";

    Write(Severity::Info, Subsystem::Core, "session initialized", {
        {"version", OmniGhost::Version, false},
        {"pid", std::to_string(GetCurrentProcessId()), false},
        {"session_id", g_sessionId, false},
        {"build_id", OmniGhost::BuildInfo::BuildId, false},
        {"commit_id", OmniGhost::BuildInfo::CommitId, false},
        {"start_reason", g_startReason, false}
    });
    return true;
}

void Write(Severity severity,
           Subsystem subsystem,
           std::string_view message,
           std::initializer_list<Field> fields) {


    if (g_sinkClosed.load(std::memory_order_acquire))
        return;
    std::lock_guard<std::mutex> eventLock(g_eventMutex);

    const std::string timestamp = TimestampUtc();
    const std::string safeMessage = RedactForSupport(message);
    std::string errorId;
    if (severity == Severity::Error || severity == Severity::Critical) {
        ++g_errorSequence;
        std::ostringstream id;
        id << "OG-" << SubsystemName(subsystem) << '-' << std::setw(3) << std::setfill('0')
           << ((g_errorSequence - 1) % 999 + 1);
        errorId = id.str();
        g_lastErrorId = errorId;
    }
    std::ostringstream human;
    human << '[' << timestamp << "][" << SeverityName(severity) << "][" << SubsystemName(subsystem) << ']';
#ifdef _DEBUG
    human << "[T" << GetCurrentThreadId() << ']';
#endif
    if (!errorId.empty()) human << '[' << errorId << ']';
    human << ' ' << safeMessage;
    for (const Field& field : fields) {
        human << ' ' << field.key << '=' << SanitizedFieldValue(field);
    }
    const std::string humanLine = human.str();
    AddRecentEvent(timestamp + " " + humanLine);


    std::clog << humanLine << '\n';
    if (severity == Severity::Error || severity == Severity::Critical)
        std::clog.flush();

    if (!g_structuredFile.is_open() || g_structuredFileTruncated)
        return;

    std::ostringstream json;
    json << "{\"timestamp\":\"" << JsonEscape(timestamp)
         << "\",\"session_id\":\"" << JsonEscape(g_sessionId)
         << "\",\"severity\":\"" << JsonEscape(SeverityName(severity))
         << "\",\"subsystem\":\"" << JsonEscape(SubsystemName(subsystem))
         << "\",\"message\":\"" << JsonEscape(safeMessage) << '"';
#ifdef _DEBUG
    json << ",\"thread_id\":" << GetCurrentThreadId();
#endif
    if (!errorId.empty())
        json << ",\"error_id\":\"" << JsonEscape(errorId) << '"';
    json << ",\"fields\":{";
    bool first = true;
    for (const Field& field : fields) {
        if (!first)
            json << ',';
        first = false;
        json << '"' << JsonEscape(field.key) << "\":\""
             << JsonEscape(SanitizedFieldValue(field)) << '"';
    }
    json << "}}\n";
    const std::string line = json.str();
    if (!RotateStructuredLogIfNeeded(line.size())) {
        g_structuredFileTruncated = true;
        return;
    }
    g_structuredFile.write(line.data(), static_cast<std::streamsize>(line.size()));
    if (g_structuredFile.is_open()) {
        g_structuredFileBytes += line.size();
        if (severity == Severity::Error || severity == Severity::Critical)
            g_structuredFile.flush();
    }
}

void Flush() {
    std::lock_guard<std::mutex> eventLock(g_eventMutex);
    std::clog.flush();
    if (g_structuredFile.is_open())
        g_structuredFile.flush();
}

fs::path CurrentLogPath() {
    std::lock_guard<std::mutex> lifecycle(g_lifecycleMutex);
    return g_logPath;
}

fs::path CurrentStructuredLogPath() {
    std::lock_guard<std::mutex> lifecycle(g_lifecycleMutex);
    return g_structuredLogPath;
}

std::string CurrentSessionId() {
    std::lock_guard<std::mutex> lifecycle(g_lifecycleMutex);
    return g_sessionId;
}

bool FileWasTruncated() {
    std::lock_guard<std::mutex> lifecycle(g_lifecycleMutex);
    return g_fileTruncated || g_structuredFileTruncated;
}

std::vector<std::string> RecentEvents(std::size_t maximum) {
    std::lock_guard<std::mutex> eventLock(g_eventMutex);
    maximum = (std::min)(maximum, g_recentEvents.size());
    std::vector<std::string> result;
    result.reserve(maximum);
    const auto begin = g_recentEvents.end() - static_cast<std::ptrdiff_t>(maximum);
    result.insert(result.end(), begin, g_recentEvents.end());
    return result;
}

std::string LastErrorId() {
    std::lock_guard<std::mutex> eventLock(g_eventMutex);
    return g_lastErrorId;
}



std::size_t RecentEventCount() {
    std::lock_guard<std::mutex> eventLock(g_eventMutex);
    return g_recentEvents.size();
}

void Shutdown() {


    {
        std::lock_guard<std::mutex> lifecycle(g_lifecycleMutex);
        if (!g_file.is_open())
            return;
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - g_sessionStart);
    Write(Severity::Info, Subsystem::Core, "session shutdown", {
        {"duration_ms", std::to_string(elapsed.count()), false},
        {"log_truncated", (g_fileTruncated || g_structuredFileTruncated) ? "true" : "false", false},
        {"recent_event_count", std::to_string(RecentEventCount()), false},
        {"build_id", OmniGhost::BuildInfo::BuildId, false},
        {"start_reason", g_startReason, false}
    });
    std::clog << "[SESSION] metrics_end "
              << Platform::FormatProcessMetrics(Platform::CaptureProcessMetrics())
              << "\n";
    std::clog << "[SESSION] OmniGhost terminado duration_ms=" << elapsed.count() << "\n";
    std::clog.flush();
    if (g_structuredFile.is_open())
        g_structuredFile.flush();





    {
        std::lock_guard<std::mutex> eventLock(g_eventMutex);
        std::lock_guard<std::mutex> outputLock(g_outputMutex);
        std::lock_guard<std::mutex> lifecycle(g_lifecycleMutex);
        if (!g_file.is_open())
            return;


        g_sinkClosed.store(true, std::memory_order_release);
        std::cout.rdbuf(g_originalOut);
        std::cerr.rdbuf(g_originalErr);
        std::clog.rdbuf(g_originalLog);




        g_file.close();
        g_structuredFile.close();
    }
    if ((g_fileTruncated || g_structuredFileTruncated) && g_originalErr)
        std::cerr << "[LOG] Um ficheiro de diagnostico atingiu o limite configurado; o processo continuou sem crescimento ilimitado.\n";
}

}
