// Minimal JSON parser / writer helpers for the REST API (no external dependencies).
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

struct JsonValue {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool        b   = false;
    double      num = 0;
    std::string str;
    std::vector<JsonValue> arr;
    std::map<std::string, JsonValue> obj;

    bool has(const std::string& k) const { return type == Object && obj.count(k); }
    const JsonValue& operator[](const std::string& k) const {
        static const JsonValue nullValue;
        auto it = obj.find(k);
        return it == obj.end() ? nullValue : it->second;
    }
};

// Throws std::runtime_error on malformed input.
JsonValue json_parse(const std::string& text);

std::string json_escape(const std::string& s);
inline std::string json_str(const std::string& s) { return "\"" + json_escape(s) + "\""; }
