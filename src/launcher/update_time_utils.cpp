#include "update_time_utils.h"

#include "../window/ui_format.h"

#include <charconv>
#include <ctime>
#include <string_view>
#include <system_error>

namespace LauncherUpdates::UpdateTime {
namespace {

bool ParseDecimal(std::string_view text, int& output) {
    if (text.empty()) return false;
    int value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return false;
    output = value;
    return true;
}

bool IsLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int DaysInMonth(int year, int month) {
    static constexpr int days[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) return 0;
    if (month == 2 && IsLeapYear(year)) return 29;
    return days[month];
}

bool ParseUtcTime(const std::string& iso, std::time_t& output) {
    output = {};
    if (iso.size() < 19 || iso[4] != '-' || iso[7] != '-' ||
        (iso[10] != 'T' && iso[10] != 't' && iso[10] != ' ') ||
        iso[13] != ':' || iso[16] != ':') return false;
    const std::string_view value(iso.data(), 19);
    int year=0, month=0, day=0, hour=0, minute=0, second=0;
    if (!ParseDecimal(value.substr(0,4), year) || !ParseDecimal(value.substr(5,2), month) ||
        !ParseDecimal(value.substr(8,2), day) || !ParseDecimal(value.substr(11,2), hour) ||
        !ParseDecimal(value.substr(14,2), minute) || !ParseDecimal(value.substr(17,2), second)) return false;
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > DaysInMonth(year, month) ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) return false;
    std::tm utc{};
    utc.tm_year=year-1900; utc.tm_mon=month-1; utc.tm_mday=day;
    utc.tm_hour=hour; utc.tm_min=minute; utc.tm_sec=second; utc.tm_isdst=0;
    const std::time_t converted = _mkgmtime(&utc);
    if (converted == static_cast<std::time_t>(-1)) return false;
    output = converted;
    return true;
}

} // namespace

bool ParseIsoDatePrefix(const std::string& iso, int& year, int& month, int& day) {
    if (iso.size() < 10 || iso[4] != '-' || iso[7] != '-') return false;
    const std::string_view value(iso.data(), 10);
    if (!ParseDecimal(value.substr(0,4), year) || !ParseDecimal(value.substr(5,2), month) ||
        !ParseDecimal(value.substr(8,2), day)) return false;
    return year >= 1 && month >= 1 && month <= 12 && day >= 1 && day <= DaysInMonth(year, month);
}

std::string DateDisplay(const std::string& iso) {
    return UiFormat::Date(iso);
}

std::string RelativePublishedTime(const std::string& iso) {
    std::time_t published{};
    if (!ParseUtcTime(iso, published)) return DateDisplay(iso);
    long long seconds = static_cast<long long>(std::difftime(std::time(nullptr), published));
    if (seconds < 0) seconds = 0;
    if (seconds < 60) return "há menos de um minuto";
    const long long minutes = seconds / 60;
    if (minutes < 60) return "há " + std::to_string(minutes) + (minutes == 1 ? " minuto" : " minutos");
    const long long hours = minutes / 60;
    if (hours < 24) return "há " + std::to_string(hours) + (hours == 1 ? " hora" : " horas");
    const long long days = hours / 24;
    if (days < 30) return "há " + std::to_string(days) + (days == 1 ? " dia" : " dias");
    return "em " + DateDisplay(iso);
}

} // namespace LauncherUpdates::UpdateTime
