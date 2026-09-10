// json.cpp — strict JSON parser implementation (runtime/core).
// Hand-rolled because the platform gate must validate JSON with a real parser
// even on a bare toolchain (no external dependency per ADR-0003).
#include "dc/json.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace dc::json {

namespace {

class Parser {
public:
    Parser(const std::string& text, std::string& error) : text_(text), error_(error) {}

    std::unique_ptr<Value> Run() {
        SkipWs();
        auto v = ParseValue();
        if (!v) return nullptr;
        SkipWs();
        if (pos_ != text_.size()) {
            return Fail("trailing characters at offset " + std::to_string(pos_));
        }
        return v;
    }

private:
    size_t pos_ = 0;
    const std::string& text_;
    std::string& error_;
    int depth_ = 0;

    static constexpr int kMaxDepth = 64;

    std::unique_ptr<Value> Fail(const std::string& msg) {
        error_ = msg + " (offset " + std::to_string(pos_) + ")";
        return nullptr;
    }

    void SkipWs() {
        while (pos_ < text_.size()) {
            char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
    }

    bool Consume(char c) {
        if (pos_ < text_.size() && text_[pos_] == c) { ++pos_; return true; }
        return false;
    }

    bool Expect(char c) {
        if (!Consume(c)) return Fail(std::string("expected '") + c + "'") != nullptr;
        return true;
    }

    std::unique_ptr<Value> ParseValue() {
        if (++depth_ > kMaxDepth) return Fail("nesting too deep");
        SkipWs();
        if (pos_ >= text_.size()) { --depth_; return Fail("unexpected end of input"); }
        std::unique_ptr<Value> out;
        char c = text_[pos_];
        if (c == '{') out = ParseObject();
        else if (c == '[') out = ParseArray();
        else if (c == '"') out = ParseString();
        else if (c == 't') out = ParseKeyword("true", Value(true));
        else if (c == 'f') out = ParseKeyword("false", Value(false));
        else if (c == 'n') out = ParseKeyword("null", Value());
        else out = ParseNumber();
        --depth_;
        return out;
    }

    std::unique_ptr<Value> ParseKeyword(const char* kw, Value v) {
        size_t len = std::char_traits<char>::length(kw);
        if (text_.compare(pos_, len, kw) == 0) {
            pos_ += len;
            return std::make_unique<Value>(std::move(v));
        }
        return Fail(std::string("invalid literal, expected ") + kw);
    }

    std::unique_ptr<Value> ParseNumber() {
        size_t start = pos_;
        if (Consume('-')) {}
        if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
            return Fail("invalid number");
        }
        while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        if (Consume('.')) {
            if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') return Fail("invalid fraction");
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') return Fail("invalid exponent");
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        double d = 0.0;
        auto [p, ec] = std::from_chars(text_.data() + start, text_.data() + pos_, d);
        if (ec != std::errc{} || !std::isfinite(d)) return Fail("number out of range or not finite");
        return std::make_unique<Value>(d);
    }

    std::unique_ptr<Value> ParseString() {
        std::string out;
        if (!Consume('"')) return Fail("expected string");
        while (true) {
            if (pos_ >= text_.size()) return Fail("unterminated string");
            unsigned char c = static_cast<unsigned char>(text_[pos_++]);
            if (c == '"') break;
            if (c == '\\') {
                if (pos_ >= text_.size()) return Fail("unterminated escape");
                char e = text_[pos_++];
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
                        uint32_t cp = 0;
                        if (!ParseHex4(cp)) return Fail("bad \\u escape");
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            // high surrogate: require low surrogate
                            if (pos_ + 1 < text_.size() && text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                                pos_ += 2;
                                uint32_t lo = 0;
                                if (!ParseHex4(lo)) return Fail("bad low surrogate");
                                if (lo < 0xDC00 || lo > 0xDFFF) return Fail("orphan surrogate");
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            } else {
                                return Fail("orphan high surrogate");
                            }
                        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                            return Fail("orphan low surrogate");
                        }
                        AppendUtf8(out, cp);
                        break;
                    }
                    default: return Fail("bad escape character");
                }
            } else if (c < 0x20) {
                return Fail("unescaped control character in string");
            } else {
                out += static_cast<char>(c);
            }
        }
        return std::make_unique<Value>(out);
    }

    bool ParseHex4(uint32_t& out) {
        if (pos_ + 4 > text_.size()) return false;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            char h = text_[pos_++];
            v <<= 4;
            if (h >= '0' && h <= '9') v |= static_cast<uint32_t>(h - '0');
            else if (h >= 'a' && h <= 'f') v |= static_cast<uint32_t>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') v |= static_cast<uint32_t>(h - 'A' + 10);
            else return false;
        }
        out = v;
        return true;
    }

    static void AppendUtf8(std::string& out, uint32_t cp) {
        if (cp <= 0x7F) {
            out += static_cast<char>(cp);
        } else if (cp <= 0x7FF) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp <= 0xFFFF) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    std::unique_ptr<Value> ParseArray() {
        if (!Consume('[')) return Fail("expected '['");
        Array arr;
        SkipWs();
        if (Consume(']')) return std::make_unique<Value>(std::move(arr));
        while (true) {
            auto v = ParseValue();
            if (!v) return nullptr;
            arr.push_back(std::move(*v));
            SkipWs();
            if (Consume(',')) { SkipWs(); continue; }
            if (Consume(']')) return std::make_unique<Value>(std::move(arr));
            return Fail("expected ',' or ']' in array");
        }
    }

    std::unique_ptr<Value> ParseObject() {
        if (!Consume('{')) return Fail("expected '{'");
        Object obj;
        SkipWs();
        if (Consume('}')) return std::make_unique<Value>(std::move(obj));
        while (true) {
            SkipWs();
            auto key = ParseString();
            if (!key) return nullptr;
            SkipWs();
            if (!Consume(':')) return Fail("expected ':' in object");
            auto v = ParseValue();
            if (!v) return nullptr;
            obj.emplace(key->as_string(), std::move(*v));
            SkipWs();
            if (Consume(',')) { SkipWs(); continue; }
            if (Consume('}')) return std::make_unique<Value>(std::move(obj));
            return Fail("expected ',' or '}' in object");
        }
    }
};

} // namespace

std::unique_ptr<Value> Parse(const std::string& text, std::string& error) {
    error.clear();
    Parser p(text, error);
    return p.Run();
}

std::unique_ptr<Value> ParseFile(const std::string& path, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "cannot open " + path;
        return nullptr;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return Parse(ss.str(), error);
}

} // namespace dc::json
