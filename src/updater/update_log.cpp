#include "update_log.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <regex>
#include <sstream>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace OmniGhost::Update {
namespace {

std::mutex g_logMutex;

std::string RedactLogMessage(std::string message) {
    for (char& character : message) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte < 0x20 && character != '\t')
            character = ' ';
    }

    try {
        message = std::regex_replace(
            message,
            std::regex(R"((https?://)([^/@:\s]+):([^/@\s]+)@)", std::regex::icase),
            "$1<redacted>@");
        message = std::regex_replace(
            message,
            std::regex(R"((Authorization\s*[:=]\s*(?:Bearer|Basic)\s+)[A-Za-z0-9+/_=.-]+)", std::regex::icase),
            "$1<redacted>");
        message = std::regex_replace(
            message,
            std::regex(R"(([?&](?:token|access_token|api_key|apikey|key|password|passwd|pwd)=)[^&#\s]+)", std::regex::icase),
            "$1<redacted>");
    } catch (const std::exception& ex) {
        std::cerr << "[Updater] UpdateLog: Redaction regex failed: " << ex.what() << "\n";
    } catch (...) {
        std::cerr << "[Updater] UpdateLog: Redaction regex failed with unknown exception\n";
    }
    return message;
}

std::string JsonEscape(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 16);
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char character : value) {
        switch (character) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (character < 0x20) {
                result += "\\u00";
                result += hex[(character >> 4) & 0x0f];
                result += hex[character & 0x0f];
            } else {
                result.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    return result;
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

std::string BuildLine(
    LogLevel level,
    const std::string& currentVersion,
    const std::string& targetVersion,
    const std::string& stage,
    const std::string& message) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm utc{};
    gmtime_s(&utc, &time);

    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ")
           << " ["
           << (level == LogLevel::Info
                   ? "INFO"
                   : level == LogLevel::Warning ? "WARN" : "ERROR")
           << "] current=" << currentVersion
           << " target=" << (targetVersion.empty() ? "-" : targetVersion)
           << " stage=" << stage
           << " message=";

    const std::string safeMessage = RedactLogMessage(message);
    for (const char character : safeMessage)
        output << ((character == '\r' || character == '\n') ? ' ' : character);

    output << '\n';
    return output.str();
}

bool AtomicWrite(const fs::path& destination, const std::string& content) {
    std::error_code error;
    fs::create_directories(destination.parent_path(), error);
    if (error)
        return false;

    const fs::path temporary = destination.wstring() + L".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream)
            return false;
        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        stream.flush();
        if (!stream)
            return false;
    }

    if (MoveFileExW(temporary.c_str(), destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }

    fs::remove(temporary, error);
    return false;
}

bool AppendLine(const fs::path& file, const std::string& line) {
    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    if (error)
        return false;

    std::ofstream stream(file, std::ios::binary | std::ios::app);
    if (!stream)
        return false;

    stream.write(line.data(), static_cast<std::streamsize>(line.size()));
    stream.flush();
    return static_cast<bool>(stream);
}

fs::path FallbackLogFile(const fs::path& requested) {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetTempPathW(
        static_cast<DWORD>(buffer.size()),
        buffer.data());

    fs::path base;
    if (length != 0 && length < buffer.size()) {
        buffer.resize(length);
        base = buffer;
    } else {
        std::error_code error;
        base = fs::temp_directory_path(error);
        if (error)
            return {};
    }

    const fs::path filename = requested.filename().empty()
        ? fs::path(L"logs.txt")
        : requested.filename();

    return base / L"OmniGhost" / L"logs" / filename;
}

} // namespace

std::string WindowsError(unsigned long code) {
    char* raw = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        0,
        reinterpret_cast<char*>(&raw),
        0,
        nullptr);

    std::string message = length && raw
        ? std::string(raw, length)
        : "Erro Windows";

    if (raw)
        LocalFree(raw);

    while (!message.empty() &&
           (message.back() == '\r' || message.back() == '\n' || message.back() == ' ')) {
        message.pop_back();
    }

    message += " (código " + std::to_string(code) + ")";
    return message;
}

void WriteLog(
    const fs::path& file,
    LogLevel level,
    const std::string& currentVersion,
    const std::string& targetVersion,
    const std::string& stage,
    const std::string& message) {
    std::lock_guard lock(g_logMutex);

    const std::string line = BuildLine(
        level,
        currentVersion,
        targetVersion,
        stage,
        message);

    if (AppendLine(file, line))
        return;

    const fs::path fallback = FallbackLogFile(file);
    if (!fallback.empty() && AppendLine(fallback, line))
        return;

    std::string debug = "[OmniGhost Log] Não foi possível escrever o log: " +
                        file.string() + " | " + line;
    OutputDebugStringA(debug.c_str());
}

bool WriteUpdateHealth(const fs::path& file, const UpdateHealthRecord& record) {
    std::lock_guard lock(g_logMutex);

    const std::string detail = RedactLogMessage(record.detail);
    std::ostringstream json;
    json << "{\n"
         << "  \"schemaVersion\": 1,\n"
         << "  \"updatedUtc\": \"" << JsonEscape(TimestampUtc()) << "\",\n"
         << "  \"stage\": \"" << JsonEscape(record.stage) << "\",\n"
         << "  \"installedVersion\": \"" << JsonEscape(record.installedVersion) << "\",\n"
         << "  \"targetVersion\": \"" << JsonEscape(record.targetVersion) << "\",\n"
         << "  \"packageSha256\": \"" << JsonEscape(record.packageSha256) << "\",\n"
         << "  \"signerStatus\": \"" << JsonEscape(record.signerStatus) << "\",\n"
         << "  \"rollbackResult\": \"" << JsonEscape(record.rollbackResult) << "\",\n"
         << "  \"detail\": \"" << JsonEscape(detail) << "\"\n"
         << "}\n";
    return AtomicWrite(file, json.str());
}

void RotateLogs(const fs::path& directory, int maximumFiles, int maximumAgeDays) {
    std::error_code error;
    if (!fs::exists(directory, error))
        return;

    std::vector<fs::directory_entry> files;
    const auto cutoff = fs::file_time_type::clock::now() -
                        std::chrono::hours(24 * maximumAgeDays);

    for (const auto& entry : fs::directory_iterator(directory, error)) {
        if (!entry.is_regular_file(error) || entry.path().extension() != L".log")
            continue;

        if (entry.last_write_time(error) < cutoff) {
            fs::remove(entry.path(), error);
        } else {
            files.push_back(entry);
        }
        error.clear();
    }

    std::sort(files.begin(), files.end(), [](const auto& left, const auto& right) {
        std::error_code leftError;
        std::error_code rightError;
        return left.last_write_time(leftError) > right.last_write_time(rightError);
    });

    for (std::size_t index = static_cast<std::size_t>((std::max)(maximumFiles, 1));
         index < files.size();
         ++index) {
        fs::remove(files[index].path(), error);
    }
}

} // namespace OmniGhost::Update
