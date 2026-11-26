#include "kv3_parser.h"

#include <charconv>
#include <cctype>
#include <limits>

namespace phys_extractor {
namespace {

std::vector<std::string> tokenize(std::string_view input) {
    std::vector<std::string> tokens;
    // KV3 metadata may contain braces; the main object starts at a line opening with '{'.
    std::size_t start = 0;
    while (start < input.size()) {
        const auto first = input.find_first_not_of(" \t\r", start);
        if (first == std::string_view::npos) return tokens;
        if (input[first] == '{') { start = first; break; }
        const auto end = input.find('\n', first);
        if (end == std::string_view::npos) return tokens;
        start = end + 1;
    }
    for (std::size_t i = start; i < input.size();) {
        const auto c = input[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
        if (c == '/' && i + 1 < input.size() && input[i + 1] == '/') {
            i = input.find('\n', i);
            if (i == std::string_view::npos) break;
            continue;
        }
        if (c == '"') {
            const auto begin = i++;
            bool escaped = false;
            while (i < input.size()) {
                const auto ch = input[i++];
                if (ch == '"' && !escaped) break;
                if (ch == '\\' && !escaped) escaped = true; else escaped = false;
            }
            tokens.emplace_back(input.substr(begin, i - begin));
            continue;
        }
        if (c == '#' && i + 1 < input.size() && input[i + 1] == '[') {
            const auto end = input.find(']', i + 2);
            if (end == std::string_view::npos) break;
            tokens.emplace_back(input.substr(i, end - i + 1));
            i = end + 1;
            continue;
        }
        if (std::string_view("{}[]=,").find(c) != std::string_view::npos) {
            tokens.emplace_back(1, c); ++i; continue;
        }
        const auto begin = i;
        while (i < input.size() && !std::isspace(static_cast<unsigned char>(input[i])) &&
               std::string_view("{}[]=,\"").find(input[i]) == std::string_view::npos &&
               !(input[i] == '/' && i + 1 < input.size() && input[i + 1] == '/')) ++i;
        if (i == begin) ++i; else tokens.emplace_back(input.substr(begin, i - begin));
    }
    return tokens;
}

std::string scalar(std::string token) {
    if (token.size() >= 2 && token.front() == '"' && token.back() == '"') return token.substr(1, token.size() - 2);
    if (token.size() >= 3 && token[0] == '#' && token[1] == '[' && token.back() == ']') return token.substr(2, token.size() - 3);
    return token;
}

Kv3Value parse(const std::vector<std::string>& tokens, std::size_t& index, unsigned depth = 0) {
    if (index >= tokens.size() || depth > 128) return {std::string{}};
    if (tokens[index] == "{") {
        ++index;
        Kv3Value::Object object;
        while (index < tokens.size() && tokens[index] != "}") {
            if (tokens[index] == ",") { ++index; continue; }
            auto key = scalar(tokens[index++]);
            if (index < tokens.size() && tokens[index] == "=") ++index;
            if (index >= tokens.size()) break;
            const auto before = index;
            object.insert_or_assign(std::move(key), parse(tokens, index, depth + 1));
            if (index == before) ++index;
        }
        if (index < tokens.size()) ++index;
        return {std::move(object)};
    }
    if (tokens[index] == "[") {
        ++index;
        Kv3Value::Array array;
        while (index < tokens.size() && tokens[index] != "]") {
            if (tokens[index] == ",") { ++index; continue; }
            const auto before = index;
            array.push_back(parse(tokens, index, depth + 1));
            if (index == before) ++index;
        }
        if (index < tokens.size()) ++index;
        return {std::move(array)};
    }
    return {scalar(tokens[index++])};
}
} // namespace

Kv3Parser::Kv3Parser(std::string_view content) : root_{Kv3Value::Object{}} {
    const auto tokens = tokenize(content);
    std::size_t index = 0;
    if (!tokens.empty() && tokens.front() == "{") root_ = parse(tokens, index);
}

std::string Kv3Parser::get_value(std::string_view path) const {
    const Kv3Value* current = &root_;
    while (!path.empty()) {
        const auto dot = path.find('.');
        const auto part = path.substr(0, dot);
        const auto bracket = part.find('[');
        const auto* object = std::get_if<Kv3Value::Object>(&current->value);
        if (!object) return {};
        const auto found = object->find(part.substr(0, bracket));
        if (found == object->end()) return {};
        current = &found->second;
        if (bracket != std::string_view::npos) {
            if (part.back() != ']' || bracket + 2 > part.size()) return {};
            const auto number = part.substr(bracket + 1, part.size() - bracket - 2);
            std::size_t offset = 0;
            const auto result = std::from_chars(number.data(), number.data() + number.size(), offset);
            if (result.ec != std::errc{} || result.ptr != number.data() + number.size()) return {};
            const auto* array = std::get_if<Kv3Value::Array>(&current->value);
            if (!array || offset >= array->size()) return {};
            current = &(*array)[offset];
        }
        if (dot == std::string_view::npos) break;
        path.remove_prefix(dot + 1);
    }
    const auto* text = std::get_if<std::string>(&current->value);
    return text ? *text : std::string{};
}
} // namespace phys_extractor
