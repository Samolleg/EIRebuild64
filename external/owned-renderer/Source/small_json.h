#pragma once
// Small independent JSON reader shared by the saved neighbour layout and the
// terrain atlas profile. The limits are the readers' rejection contract
// (malformed or oversized documents throw std::runtime_error), not limits on
// renderer coverage.
#include <cmath>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace owned::small_json {
inline void require(bool valid, const char* reason) {
    if (!valid) throw std::runtime_error(reason);
}
struct Value {
    enum Kind { object, array, string, number, literal } kind = literal;
    std::map<std::string, Value> members;
    std::vector<Value> elements;
    std::string text;
    double numeric = 0;
    const Value& field(const char* key) const {
        const auto i = members.find(key);
        require(kind == object && i != members.end(), "Missing JSON field");
        return i->second;
    }
    const std::vector<Value>& list() const {
        require(kind == array, "JSON field must be an array"); return elements;
    }
    const std::string& str() const {
        require(kind == string, "JSON field must be a string"); return text;
    }
    double num() const {
        require(kind == number, "JSON field must be a number"); return numeric;
    }
};
class Reader {
    const std::string& input;
    std::size_t cursor = 0, nodes = 0;
    void space() { while (cursor < input.size() &&
        std::string(" \t\r\n").find(input[cursor]) != std::string::npos) ++cursor; }
    char next() { require(cursor < input.size(), "Truncated JSON"); return input[cursor++]; }
    bool accept(char c) { space(); if (cursor == input.size() || input[cursor] != c) return false; ++cursor; return true; }
    unsigned unicode() {
        unsigned code = 0;
        for (unsigned i = 0; i < 4; ++i) {
            const char c = next();
            const auto digit = std::string("0123456789abcdef").find(
                c >= 'A' && c <= 'F' ? char(c + 32) : c);
            require(digit != std::string::npos, "Invalid JSON Unicode escape");
            code = code * 16 + unsigned(digit);
        }
        return code;
    }
    std::string quoted() {
        require(next() == '"', "Expected JSON string");
        std::string result;
        for (;;) {
            const unsigned char c = next();
            if (c == '"') return result;
            require(c >= 32, "Control character in JSON string");
            if (c != '\\') result += char(c);
            else {
                const char e = next();
                const auto i = std::string("\"\\/bfnrt").find(e);
                if (i != std::string::npos) result += "\"\\/\b\f\n\r\t"[i];
                else {
                    require(e == 'u', "Invalid JSON escape");
                    unsigned cp = unicode();
                    if (cp >= 0xd800 && cp <= 0xdbff) {
                        require(next() == '\\' && next() == 'u', "Missing JSON surrogate");
                        const unsigned low = unicode();
                        require(low >= 0xdc00 && low <= 0xdfff, "Invalid JSON surrogate");
                        cp = 0x10000 + (cp - 0xd800) * 1024 + low - 0xdc00;
                    } else require(cp < 0xdc00 || cp > 0xdfff, "Unpaired JSON surrogate");
                    unsigned count = cp < 128 ? 1 : cp < 2048 ? 2 : cp < 65536 ? 3 : 4;
                    if (count == 1) result += char(cp);
                    else {
                        result += char((count == 2 ? 0xc0 : count == 3 ? 0xe0 : 0xf0) | (cp >> (6 * (count - 1))));
                        while (--count) result += char(0x80 | ((cp >> (6 * (count - 1))) & 63));
                    }
                }
            }
            require(result.size() <= 65536, "JSON string budget exceeded");
        }
    }
    Value read(unsigned depth) {
        require(depth <= 32 && ++nodes <= 100000, "JSON complexity budget exceeded");
        space(); require(cursor < input.size(), "Truncated JSON");
        Value result;
        const char c = input[cursor];
        if (c == '"') { result.kind = Value::string; result.text = quoted(); }
        else if (c == '[' || c == '{') {
            ++cursor; result.kind = c == '[' ? Value::array : Value::object;
            const char end = c == '[' ? ']' : '}';
            if (accept(end)) return result;
            do {
                space(); std::string key;
                if (c == '{') { key = quoted(); require(accept(':'), "Missing JSON colon"); }
                auto child = read(depth + 1);
                if (c == '[') result.elements.push_back(std::move(child));
                else require(result.members.emplace(key, std::move(child)).second, "Duplicate JSON key");
                if (accept(end)) return result;
            } while (accept(','));
            require(false, "Missing JSON separator");
        } else if (c == 't' || c == 'f' || c == 'n') {
            const std::string word = c == 't' ? "true" : c == 'f' ? "false" : "null";
            require(input.compare(cursor, word.size(), word) == 0, "Invalid JSON literal");
            cursor += word.size(); result.text = word; // kind stays literal
        } else {
            const auto begin = cursor;
            auto digit = [&] { return cursor < input.size() && input[cursor] >= '0' && input[cursor] <= '9'; };
            if (input[cursor] == '-') ++cursor;
            require(digit(), "Invalid JSON number");
            if (input[cursor] == '0') ++cursor; else while (digit()) ++cursor;
            if (cursor < input.size() && input[cursor] == '.') {
                ++cursor; require(digit(), "Invalid JSON fraction"); while (digit()) ++cursor;
            }
            if (cursor < input.size() && (input[cursor] == 'e' || input[cursor] == 'E')) {
                ++cursor; if (cursor < input.size() && (input[cursor] == '+' || input[cursor] == '-')) ++cursor;
                require(digit(), "Invalid JSON exponent"); while (digit()) ++cursor;
            }
            std::istringstream stream(input.substr(begin, cursor - begin));
            stream.imbue(std::locale::classic()); stream >> result.numeric;
            require(!stream.fail() && std::isfinite(result.numeric), "Nonfinite JSON number");
            result.kind = Value::number;
        }
        return result;
    }
public:
    explicit Reader(const std::string& text) : input(text) {}
    Value root() {
        require(input.size() <= 4u * 1024u * 1024u, "JSON byte budget exceeded");
        if (input.compare(0, 3, "\xef\xbb\xbf") == 0) cursor = 3;
        auto result = read(0); space();
        require(cursor == input.size(), "Trailing JSON"); return result;
    }
};
} // namespace owned::small_json
