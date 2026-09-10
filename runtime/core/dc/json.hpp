// dc/json.hpp — minimal strict JSON parser (runtime/core, host-agnostic).
// Purpose: real parsing for evidence validation and profile registry loading.
// Not a general-purpose DOM for the whole platform; deliberately small.
#pragma once

#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace dc::json {

class Value;
using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() : type_(Type::Null) {}
    explicit Value(bool b) : type_(Type::Bool), bool_(b) {}
    explicit Value(double n) : type_(Type::Number), num_(n) {}
    explicit Value(std::string s) : type_(Type::String), str_(std::move(s)) {}
    explicit Value(Array a) : type_(Type::Array), arr_(std::move(a)) {}
    explicit Value(Object o) : type_(Type::Object), obj_(std::move(o)) {}

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_bool() const { return type_ == Type::Bool; }
    bool is_number() const { return type_ == Type::Number; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    bool as_bool(bool dflt = false) const { return is_bool() ? bool_ : dflt; }
    double as_number(double dflt = 0.0) const { return is_number() ? num_ : dflt; }
    const std::string& as_string() const { static const std::string e; return is_string() ? str_ : e; }
    const Array& as_array() const { static const Array e; return is_array() ? arr_ : e; }
    const Object& as_object() const { static const Object e; return is_object() ? obj_ : e; }

    // Object member access (empty Value if absent or not an object).
    const Value* find(const std::string& key) const {
        if (!is_object()) return nullptr;
        auto it = obj_.find(key);
        return it == obj_.end() ? nullptr : &it->second;
    }
    bool has(const std::string& key) const { return find(key) != nullptr; }

private:
    Type type_;
    bool bool_ = false;
    double num_ = 0.0;
    std::string str_;
    Array arr_;
    Object obj_;
};

// Strict parse. Returns nullptr and fills `error` on any malformed input
// (no trailing garbage, no NaN/Infinity literals, no comments).
std::unique_ptr<Value> Parse(const std::string& text, std::string& error);

// Canonical compact serialization of a parsed value (parse→serialize
// round-trip support for envelope transports). Integral doubles are emitted
// without a fractional part; strings are JSON-escaped.
inline void WriteCompact(const Value& v, std::string& out);

namespace json_detail {
inline void WriteEscaped(const std::string& s, std::string& out) {
    out += '"';
    for (char ch : s) {
        switch (ch) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
                    out += buf;
                } else {
                    out += ch;
                }
        }
    }
    out += '"';
}
} // namespace json_detail

inline void WriteCompact(const Value& v, std::string& out) {
    switch (v.type()) {
        case Value::Type::Null:   out += "null"; break;
        case Value::Type::Bool:   out += v.as_bool() ? "true" : "false"; break;
        case Value::Type::Number: {
            double n = v.as_number();
            char buf[40];
            if (n == static_cast<double>(static_cast<long long>(n)) &&
                n >= -9.0e15 && n <= 9.0e15) {
                std::snprintf(buf, sizeof(buf), "%lld",
                              static_cast<long long>(n));
            } else {
                std::snprintf(buf, sizeof(buf), "%.17g", n);
            }
            out += buf;
            break;
        }
        case Value::Type::String:
            json_detail::WriteEscaped(v.as_string(), out);
            break;
        case Value::Type::Array: {
            out += '[';
            bool first = true;
            for (const auto& e : v.as_array()) {
                if (!first) out += ',';
                first = false;
                WriteCompact(e, out);
            }
            out += ']';
            break;
        }
        case Value::Type::Object: {
            out += '{';
            bool first = true;
            for (const auto& [key, val] : v.as_object()) {
                if (!first) out += ',';
                first = false;
                json_detail::WriteEscaped(key, out);
                out += ':';
                WriteCompact(val, out);
            }
            out += '}';
            break;
        }
    }
}

// Convenience: parse file contents. Returns nullptr on IO or parse failure.
std::unique_ptr<Value> ParseFile(const std::string& path, std::string& error);

} // namespace dc::json
