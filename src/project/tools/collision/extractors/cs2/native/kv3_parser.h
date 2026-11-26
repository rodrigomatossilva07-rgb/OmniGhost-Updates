#pragma once

#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace phys_extractor {

struct Kv3Value {
    using Object = std::map<std::string, Kv3Value, std::less<>>;
    using Array = std::vector<Kv3Value>;
    std::variant<std::string, Object, Array> value;
};

class Kv3Parser {
public:
    explicit Kv3Parser(std::string_view content);
    [[nodiscard]] std::string get_value(std::string_view path) const;
private:
    Kv3Value root_;
};

} // namespace phys_extractor
