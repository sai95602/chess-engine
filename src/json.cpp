#include "json.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace {

struct Parser {
    const std::string& s;
    size_t i = 0;
    int depth = 0;

    explicit Parser(const std::string& t) : s(t) {}

    [[noreturn]] void error(const std::string& msg) {
        throw std::runtime_error("JSON parse error at offset " + std::to_string(i) + ": " + msg);
    }
    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }
    bool match(const char* lit) {
        size_t n = 0;
        while (lit[n]) ++n;
        if (s.compare(i, n, lit) == 0) { i += n; return true; }
        return false;
    }

    static void append_utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F));
        } else {
            out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F));
        }
    }

    unsigned hex4() {
        if (i + 4 > s.size()) error("bad \\u escape");
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') v |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= unsigned(c - 'A' + 10);
            else error("bad hex digit");
        }
        return v;
    }

    std::string parse_string() {
        if (s[i] != '"') error("expected string");
        ++i;
        std::string out;
        while (true) {
            if (i >= s.size()) error("unterminated string");
            char c = s[i++];
            if (c == '"') break;
            if (c == '\\') {
                if (i >= s.size()) error("bad escape");
                char e = s[i++];
                switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        i += 2;
                        unsigned lo = hex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    append_utf8(out, cp);
                    break;
                }
                default: error("bad escape");
                }
            } else
                out += c;
        }
        return out;
    }

    JsonValue parse_value() {
        if (++depth > 64) error("nesting too deep");
        ws();
        if (i >= s.size()) error("unexpected end");
        JsonValue v;
        char c = s[i];
        if (c == '{') {
            v.type = JsonValue::Object;
            ++i;
            ws();
            if (i < s.size() && s[i] == '}') { ++i; --depth; return v; }
            while (true) {
                ws();
                if (i >= s.size()) error("unexpected end");
                std::string key = parse_string();
                ws();
                if (i >= s.size() || s[i] != ':') error("expected ':'");
                ++i;
                v.obj[key] = parse_value();
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; break; }
                error("expected ',' or '}'");
            }
        } else if (c == '[') {
            v.type = JsonValue::Array;
            ++i;
            ws();
            if (i < s.size() && s[i] == ']') { ++i; --depth; return v; }
            while (true) {
                v.arr.push_back(parse_value());
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; break; }
                error("expected ',' or ']'");
            }
        } else if (c == '"') {
            v.type = JsonValue::String;
            v.str  = parse_string();
        } else if (match("true")) {
            v.type = JsonValue::Bool; v.b = true;
        } else if (match("false")) {
            v.type = JsonValue::Bool; v.b = false;
        } else if (match("null")) {
            v.type = JsonValue::Null;
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            size_t start = i;
            ++i;
            while (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i] == '.' || s[i] == 'e' || s[i] == 'E' ||
                                    s[i] == '+' || s[i] == '-'))
                ++i;
            v.type = JsonValue::Number;
            char* endp = nullptr;
            std::string numStr = s.substr(start, i - start);
            v.num = std::strtod(numStr.c_str(), &endp);
            if (!endp || *endp != '\0') error("bad number");
        } else
            error("unexpected character");
        --depth;
        return v;
    }
};

}  // namespace

JsonValue json_parse(const std::string& text) {
    Parser p(text);
    JsonValue v = p.parse_value();
    p.ws();
    if (p.i != text.size()) p.error("trailing characters");
    return v;
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else
                out += char(c);
        }
    }
    return out;
}
