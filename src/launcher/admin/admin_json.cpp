// This file isolates nlohmann/json from the rest of the codebase to avoid STL macro conflicts
#include <nlohmann/json.hpp>
#include "../admin_api_client.h"

using json = nlohmann::json;

struct ParsedJson {
    json value;
};

ParsedJson* ParseAdminResponse(const LauncherAdmin::AdminResponse& resp) {
    try {
        auto* result = new ParsedJson();
        result->value = json::parse(resp.body);
        return result;
    } catch (...) {
        return nullptr;
    }
}

void FreeParsedJson(ParsedJson* json) {
    delete json;
}

bool JsonGetBool(ParsedJson* json, const char* key, bool defaultValue) {
    if (!json || !json->value.contains(key)) return defaultValue;
    try { return json->value[key].get<bool>(); } catch (...) { return defaultValue; }
}

int JsonGetInt(ParsedJson* json, const char* key, int defaultValue) {
    if (!json || !json->value.contains(key)) return defaultValue;
    try { return json->value[key].get<int>(); } catch (...) { return defaultValue; }
}

const char* JsonGetString(ParsedJson* json, const char* key, const char* defaultValue) {
    if (!json || !json->value.contains(key)) return defaultValue;
    try {
        static thread_local std::string cached;
        cached = json->value[key].get<std::string>();
        return cached.c_str();
    } catch (...) { return defaultValue; }
}

ParsedJson* JsonGetArray(ParsedJson* json, const char* key) {
    if (!json || !json->value.contains(key) || !json->value[key].is_array()) return nullptr;
    auto* result = new ParsedJson();
    result->value = json->value[key];
    return result;
}

int JsonArraySize(ParsedJson* array) {
    if (!array || !array->value.is_array()) return 0;
    return static_cast<int>(array->value.size());
}

ParsedJson* JsonArrayGet(ParsedJson* array, int index) {
    if (!array || !array->value.is_array() || index < 0 || index >= static_cast<int>(array->value.size())) return nullptr;
    auto* result = new ParsedJson();
    result->value = array->value[index];
    return result;
}

bool JsonHasKey(ParsedJson* json, const char* key) {
    if (!json) return false;
    return json->value.contains(key);
}
