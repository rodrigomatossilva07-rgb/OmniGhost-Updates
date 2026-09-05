#include "json.h"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>

namespace OmniGhost::Update::Json {
namespace {

class Parser {
public:
    Parser(std::string_view text, Limits limits)
        : text_(text), limits_(limits) {}

    ParseResult Run(Value& output) {
        SkipWhitespace();
        if (!ParseValue(output, 0))
            return Failure();
        SkipWhitespace();
        if (position_ != text_.size()) {
            SetError("unexpected trailing JSON content");
            return Failure();
        }
        return {true, {}, 0};
    }

private:
    ParseResult Failure() const {
        return {false, error_.empty() ? "invalid JSON" : error_, errorOffset_};
    }

    void SetError(const char* message) {
        if (!error_.empty()) return;
        error_ = message ? message : "invalid JSON";
        errorOffset_ = position_;
    }

    bool CountNode() {
        if (++nodes_ > limits_.maximumNodes) {
            SetError("JSON node limit exceeded");
            return false;
        }
        return true;
    }

    bool ParseValue(Value& output, std::size_t depth) {
        if (depth > limits_.maximumDepth) {
            SetError("JSON nesting depth limit exceeded");
            return false;
        }
        if (!CountNode()) return false;
        SkipWhitespace();
        if (position_ >= text_.size()) {
            SetError("unexpected end of JSON");
            return false;
        }

        switch (text_[position_]) {
        case '{': {
            Object object;
            if (!ParseObject(object, depth + 1)) return false;
            output.value = std::move(object);
            return true;
        }
        case '[': {
            Array array;
            if (!ParseArray(array, depth + 1)) return false;
            output.value = std::move(array);
            return true;
        }
        case '"': {
            std::string string;
            if (!ParseString(string)) return false;
            output.value = std::move(string);
            return true;
        }
        default:
            break;
        }

        if (ConsumeLiteral("true")) { output.value = true; return true; }
        if (ConsumeLiteral("false")) { output.value = false; return true; }
        if (ConsumeLiteral("null")) { output.value = nullptr; return true; }
        return ParseNumber(output);
    }

    bool ParseObject(Object& output, std::size_t depth) {
        if (text_[position_++] != '{') return false;
        SkipWhitespace();
        if (position_ < text_.size() && text_[position_] == '}') {
            ++position_;
            return true;
        }

        while (position_ < text_.size()) {
            SkipWhitespace();
            if (position_ >= text_.size() || text_[position_] != '"') {
                SetError("object key must be a string");
                return false;
            }
            std::string key;
            if (!ParseString(key)) return false;
            SkipWhitespace();
            if (position_ >= text_.size() || text_[position_++] != ':') {
                SetError("missing ':' after object key");
                return false;
            }
            Value value;
            if (!ParseValue(value, depth)) return false;
            if (output.contains(key)) {
                SetError("duplicate object key");
                return false;
            }
            output.emplace(std::move(key), std::move(value));
            SkipWhitespace();
            if (position_ < text_.size() && text_[position_] == '}') {
                ++position_;
                return true;
            }
            if (position_ >= text_.size() || text_[position_++] != ',') {
                SetError("missing ',' between object members");
                return false;
            }
        }
        SetError("unterminated object");
        return false;
    }

    bool ParseArray(Array& output, std::size_t depth) {
        if (text_[position_++] != '[') return false;
        SkipWhitespace();
        if (position_ < text_.size() && text_[position_] == ']') {
            ++position_;
            return true;
        }
        while (position_ < text_.size()) {
            Value value;
            if (!ParseValue(value, depth)) return false;
            output.push_back(std::move(value));
            SkipWhitespace();
            if (position_ < text_.size() && text_[position_] == ']') {
                ++position_;
                return true;
            }
            if (position_ >= text_.size() || text_[position_++] != ',') {
                SetError("missing ',' between array elements");
                return false;
            }
        }
        SetError("unterminated array");
        return false;
    }

    static bool HexValue(char character, unsigned& value) noexcept {
        if (character >= '0' && character <= '9') { value = static_cast<unsigned>(character - '0'); return true; }
        if (character >= 'a' && character <= 'f') { value = 10u + static_cast<unsigned>(character - 'a'); return true; }
        if (character >= 'A' && character <= 'F') { value = 10u + static_cast<unsigned>(character - 'A'); return true; }
        return false;
    }

    bool ParseHex4(unsigned& codeUnit) {
        if (position_ + 4 > text_.size()) {
            SetError("truncated Unicode escape");
            return false;
        }
        codeUnit = 0;
        for (int i = 0; i < 4; ++i) {
            unsigned nibble = 0;
            if (!HexValue(text_[position_ + static_cast<std::size_t>(i)], nibble)) {
                SetError("invalid Unicode escape");
                return false;
            }
            codeUnit = (codeUnit << 4u) | nibble;
        }
        position_ += 4;
        return true;
    }

    static bool AppendUtf8(std::string& output, unsigned codepoint) {
        if (codepoint > 0x10FFFFu || (codepoint >= 0xD800u && codepoint <= 0xDFFFu))
            return false;
        if (codepoint <= 0x7Fu) {
            output.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FFu) {
            output.push_back(static_cast<char>(0xC0u | (codepoint >> 6u)));
            output.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        } else if (codepoint <= 0xFFFFu) {
            output.push_back(static_cast<char>(0xE0u | (codepoint >> 12u)));
            output.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
            output.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        } else {
            output.push_back(static_cast<char>(0xF0u | (codepoint >> 18u)));
            output.push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3Fu)));
            output.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
            output.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        }
        return true;
    }

    bool ParseString(std::string& output) {
        if (position_ >= text_.size() || text_[position_++] != '"') {
            SetError("expected string");
            return false;
        }
        while (position_ < text_.size()) {
            const unsigned char character = static_cast<unsigned char>(text_[position_++]);
            if (character == '"') return true;
            if (character < 0x20u) {
                SetError("unescaped control character in string");
                return false;
            }
            if (character != '\\') {
                output.push_back(static_cast<char>(character));
            } else {
                if (position_ >= text_.size()) {
                    SetError("truncated string escape");
                    return false;
                }
                const char escape = text_[position_++];
                switch (escape) {
                case '"': output.push_back('"'); break;
                case '\\': output.push_back('\\'); break;
                case '/': output.push_back('/'); break;
                case 'b': output.push_back('\b'); break;
                case 'f': output.push_back('\f'); break;
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 't': output.push_back('\t'); break;
                case 'u': {
                    unsigned first = 0;
                    if (!ParseHex4(first)) return false;
                    unsigned codepoint = first;
                    if (first >= 0xD800u && first <= 0xDBFFu) {
                        if (position_ + 2 > text_.size() || text_[position_] != '\\' || text_[position_ + 1] != 'u') {
                            SetError("high surrogate without low surrogate");
                            return false;
                        }
                        position_ += 2;
                        unsigned second = 0;
                        if (!ParseHex4(second) || second < 0xDC00u || second > 0xDFFFu) {
                            SetError("invalid low surrogate");
                            return false;
                        }
                        codepoint = 0x10000u + ((first - 0xD800u) << 10u) + (second - 0xDC00u);
                    } else if (first >= 0xDC00u && first <= 0xDFFFu) {
                        SetError("unexpected low surrogate");
                        return false;
                    }
                    if (!AppendUtf8(output, codepoint)) {
                        SetError("invalid Unicode codepoint");
                        return false;
                    }
                    break;
                }
                default:
                    SetError("invalid string escape");
                    return false;
                }
            }
            if (output.size() > limits_.maximumStringBytes) {
                SetError("JSON string limit exceeded");
                return false;
            }
        }
        SetError("unterminated string");
        return false;
    }

    bool ParseNumber(Value& output) {
        const std::size_t start = position_;
        bool negative = false;
        if (position_ < text_.size() && text_[position_] == '-') {
            negative = true;
            ++position_;
        }
        if (position_ >= text_.size()) {
            SetError("invalid number");
            return false;
        }

        if (text_[position_] == '0') {
            ++position_;
            if (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') {
                SetError("leading zero in number");
                return false;
            }
        } else {
            if (text_[position_] < '1' || text_[position_] > '9') {
                SetError("invalid number");
                return false;
            }
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
        }

        bool floating = false;
        if (position_ < text_.size() && text_[position_] == '.') {
            floating = true;
            ++position_;
            const std::size_t fractionStart = position_;
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
            if (position_ == fractionStart) {
                SetError("fraction requires digits");
                return false;
            }
        }
        if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E')) {
            floating = true;
            ++position_;
            if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-')) ++position_;
            const std::size_t exponentStart = position_;
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
            if (position_ == exponentStart) {
                SetError("exponent requires digits");
                return false;
            }
        }

        const std::string_view token = text_.substr(start, position_ - start);
        if (!floating) {
            if (negative) {
                std::int64_t signedValue{};
                const auto result = std::from_chars(token.data(), token.data() + token.size(), signedValue, 10);
                if (result.ec != std::errc{} || result.ptr != token.data() + token.size()) {
                    SetError("signed integer out of range");
                    return false;
                }
                output.value = signedValue;
                return true;
            }
            std::uint64_t unsignedValue{};
            const auto result = std::from_chars(token.data(), token.data() + token.size(), unsignedValue, 10);
            if (result.ec != std::errc{} || result.ptr != token.data() + token.size()) {
                SetError("unsigned integer out of range");
                return false;
            }
            output.value = unsignedValue;
            return true;
        }

        std::string owned(token);
        char* end = nullptr;
        errno = 0;
        const double value = std::strtod(owned.c_str(), &end);
        if (errno == ERANGE || end != owned.c_str() + owned.size() || !std::isfinite(value)) {
            SetError("floating-point number out of range");
            return false;
        }
        output.value = value;
        return true;
    }

    bool ConsumeLiteral(std::string_view literal) {
        if (text_.substr(position_, literal.size()) != literal) return false;
        position_ += literal.size();
        return true;
    }

    void SkipWhitespace() {
        while (position_ < text_.size()) {
            const char c = text_[position_];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
            ++position_;
        }
    }

    std::string_view text_;
    Limits limits_{};
    std::size_t position_{};
    std::size_t nodes_{};
    std::string error_;
    std::size_t errorOffset_{};
};

} // namespace

ParseResult Parse(std::string_view text, Value& output, Limits limits) {
    return Parser(text, limits).Run(output);
}

const Value* Field(const Object& object, std::string_view name) noexcept {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

const Object* AsObject(const Value& value) noexcept { return std::get_if<Object>(&value.value); }
const Array* AsArray(const Value& value) noexcept { return std::get_if<Array>(&value.value); }
const std::string* AsString(const Value& value) noexcept { return std::get_if<std::string>(&value.value); }
const bool* AsBool(const Value& value) noexcept { return std::get_if<bool>(&value.value); }

bool AsUInt64(const Value& value, std::uint64_t& output) noexcept {
    if (const auto number = std::get_if<std::uint64_t>(&value.value)) { output = *number; return true; }
    if (const auto signedNumber = std::get_if<std::int64_t>(&value.value)) {
        if (*signedNumber < 0) return false;
        output = static_cast<std::uint64_t>(*signedNumber);
        return true;
    }
    return false;
}

bool AsInt64(const Value& value, std::int64_t& output) noexcept {
    if (const auto number = std::get_if<std::int64_t>(&value.value)) { output = *number; return true; }
    if (const auto unsignedNumber = std::get_if<std::uint64_t>(&value.value)) {
        if (*unsignedNumber > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) return false;
        output = static_cast<std::int64_t>(*unsignedNumber);
        return true;
    }
    return false;
}

bool AsDouble(const Value& value, double& output) noexcept {
    if (const auto number = std::get_if<double>(&value.value)) { output = *number; return true; }
    if (const auto unsignedNumber = std::get_if<std::uint64_t>(&value.value)) { output = static_cast<double>(*unsignedNumber); return true; }
    if (const auto signedNumber = std::get_if<std::int64_t>(&value.value)) { output = static_cast<double>(*signedNumber); return true; }
    return false;
}

} // namespace OmniGhost::Update::Json
