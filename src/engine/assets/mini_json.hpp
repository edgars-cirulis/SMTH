
#pragma once
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace mini_json {

struct Value;
using Object = std::unordered_map<std::string, Value>;
using Array = std::vector<Value>;

struct Value {
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;
    Storage v{ nullptr };

    bool is_null() const { return std::holds_alternative<std::nullptr_t>(v); }
    bool is_bool() const { return std::holds_alternative<bool>(v); }
    bool is_num() const { return std::holds_alternative<double>(v); }
    bool is_str() const { return std::holds_alternative<std::string>(v); }
    bool is_arr() const { return std::holds_alternative<Array>(v); }
    bool is_obj() const { return std::holds_alternative<Object>(v); }

    const auto& as_bool() const { return std::get<bool>(v); }
    const auto& as_num() const { return std::get<double>(v); }
    const auto& as_str() const { return std::get<std::string>(v); }
    const auto& as_arr() const { return std::get<Array>(v); }
    const auto& as_obj() const { return std::get<Object>(v); }

    const Value* get(std::string_view key) const
    {
        if (!is_obj())
            return nullptr;
        auto& o = as_obj();
        auto it = o.find(std::string(key));
        if (it == o.end())
            return nullptr;
        return &it->second;
    }

    const Value* at(size_t i) const
    {
        if (!is_arr())
            return nullptr;
        auto& a = as_arr();
        if (i >= a.size())
            return nullptr;
        return &a[i];
    }
};

class Parser {
   public:
    explicit Parser(std::string_view s) : src(s) {}

    Value parse()
    {
        skip_ws();
        Value out = parse_value();
        skip_ws();
        if (pos != src.size())
            throw err("trailing characters");
        return out;
    }

   private:
    std::string_view src;
    size_t pos = 0;

    [[noreturn]] void throw_err(std::string_view msg) const { throw std::runtime_error(std::string("mini_json: ") + std::string(msg)); }
    std::runtime_error err(std::string_view msg) const { return std::runtime_error(std::string("mini_json: ") + std::string(msg)); }

    char peek() const { return pos < src.size() ? src[pos] : '\0'; }
    char getc() { return pos < src.size() ? src[pos++] : '\0'; }

    void skip_ws()
    {
        while (pos < src.size() && std::isspace((unsigned char)src[pos]))
            pos++;
    }

    bool consume(char c)
    {
        if (peek() == c) {
            pos++;
            return true;
        }
        return false;
    }

    Value parse_value()
    {
        char c = peek();
        if (c == '"')
            return Value{ parse_string() };
        if (c == '{')
            return Value{ parse_object() };
        if (c == '[')
            return Value{ parse_array() };
        if (c == 't' || c == 'f')
            return Value{ parse_bool() };
        if (c == 'n') {
            parse_null();
            return Value{ nullptr };
        }
        if (c == '-' || (c >= '0' && c <= '9'))
            return Value{ parse_number() };
        throw err("unexpected token");
    }

    void expect(char c)
    {
        if (!consume(c))
            throw err(std::string("expected '") + c + "'");
    }

    std::string parse_string()
    {
        expect('"');
        std::string out;
        while (true) {
            if (pos >= src.size())
                throw err("unterminated string");
            char c = getc();
            if (c == '"')
                break;
            if (c == '\\') {
                if (pos >= src.size())
                    throw err("bad escape");
                char e = getc();
                switch (e) {
                    case '"':
                        out.push_back('"');
                        break;
                    case '\\':
                        out.push_back('\\');
                        break;
                    case '/':
                        out.push_back('/');
                        break;
                    case 'b':
                        out.push_back('\b');
                        break;
                    case 'f':
                        out.push_back('\f');
                        break;
                    case 'n':
                        out.push_back('\n');
                        break;
                    case 'r':
                        out.push_back('\r');
                        break;
                    case 't':
                        out.push_back('\t');
                        break;
                    case 'u': {
                        if (pos + 4 > src.size())
                            throw err("bad unicode escape");
                        unsigned code = 0;
                        for (int i = 0; i < 4; i++) {
                            char h = getc();
                            code <<= 4;
                            if (h >= '0' && h <= '9')
                                code |= (h - '0');
                            else if (h >= 'a' && h <= 'f')
                                code |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F')
                                code |= (h - 'A' + 10);
                            else
                                throw err("bad unicode escape");
                        }
                        if (code <= 0x7F)
                            out.push_back((char)code);
                        else
                            out.push_back('?');
                    } break;
                    default:
                        throw err("bad escape");
                }
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

    double parse_number()
    {
        size_t start = pos;
        if (consume('-')) {
        }
        if (consume('0')) {
        } else {
            if (!std::isdigit((unsigned char)peek()))
                throw err("bad number");
            while (std::isdigit((unsigned char)peek()))
                getc();
        }
        if (consume('.')) {
            if (!std::isdigit((unsigned char)peek()))
                throw err("bad number");
            while (std::isdigit((unsigned char)peek()))
                getc();
        }
        if (peek() == 'e' || peek() == 'E') {
            getc();
            if (peek() == '+' || peek() == '-')
                getc();
            if (!std::isdigit((unsigned char)peek()))
                throw err("bad number");
            while (std::isdigit((unsigned char)peek()))
                getc();
        }
        auto sv = src.substr(start, pos - start);
        try {
            return std::stod(std::string(sv));
        } catch (...) {
            throw err("bad number");
        }
    }

    bool parse_bool()
    {
        if (src.substr(pos, 4) == "true") {
            pos += 4;
            return true;
        }
        if (src.substr(pos, 5) == "false") {
            pos += 5;
            return false;
        }
        throw err("bad bool");
    }

    void parse_null()
    {
        if (src.substr(pos, 4) != "null")
            throw err("bad null");
        pos += 4;
    }

    Array parse_array()
    {
        expect('[');
        skip_ws();
        Array arr;
        if (consume(']'))
            return arr;
        while (true) {
            skip_ws();
            arr.push_back(parse_value());
            skip_ws();
            if (consume(']'))
                break;
            expect(',');
        }
        return arr;
    }

    Object parse_object()
    {
        expect('{');
        skip_ws();
        Object obj;
        if (consume('}'))
            return obj;
        while (true) {
            skip_ws();
            if (peek() != '"')
                throw err("expected key string");
            std::string key = parse_string();
            skip_ws();
            expect(':');
            skip_ws();
            obj.emplace(std::move(key), parse_value());
            skip_ws();
            if (consume('}'))
                break;
            expect(',');
        }
        return obj;
    }
};

inline Value parse(std::string_view s)
{
    return Parser(s).parse();
}

}  // namespace mini_json
