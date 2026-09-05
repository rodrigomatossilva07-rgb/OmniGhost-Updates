#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace OmniGhost::Update::Json {

struct Value;
using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;

struct Value {
    using Storage = std::variant<std::nullptr_t, bool, std::int64_t, std::uint64_t,
                                 double, std::string, Object, Array>;
    Storage value{nullptr};
};

struct Limits {
    std::size_t maximumDepth = 64;
    std::size_t maximumStringBytes = 1024u * 1024u;
    std::size_t maximumNodes = 100000;
};

struct ParseResult {
    bool ok{};
    std::string error;
    std::size_t errorOffset{};
};

ParseResult Parse(std::string_view text, Value& output, Limits limits = {});

const Value* Field(const Object& object, std::string_view name) noexcept;
const Object* AsObject(const Value& value) noexcept;
const Array* AsArray(const Value& value) noexcept;
const std::string* AsString(const Value& value) noexcept;
const bool* AsBool(const Value& value) noexcept;
bool AsUInt64(const Value& value, std::uint64_t& output) noexcept;
bool AsInt64(const Value& value, std::int64_t& output) noexcept;
bool AsDouble(const Value& value, double& output) noexcept;

} // namespace OmniGhost::Update::Json
