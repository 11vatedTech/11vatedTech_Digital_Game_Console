// test_json_parser.cpp — strict JSON parser contract tests (real parsing,
// not substring checks; the previous session's trailing-comma bug proved
// substring tests insufficient).
#include "dc/json.hpp"

#include <cassert>
#include <cstdio>
#include <string>

using dc::json::Value;

namespace {

void TestBasics() {
    std::string err;
    auto v = dc::json::Parse(R"({"a": 1, "b": [true, false, null], "c": "x"})", err);
    assert(v != nullptr);
    assert(v->is_object());
    assert(v->find("a")->as_number() == 1.0);
    assert(v->find("b")->as_array().size() == 3);
    assert(v->find("b")->as_array()[0].as_bool() == true);
    assert(v->find("b")->as_array()[2].is_null());
    assert(v->find("c")->as_string() == "x");
}

void TestNested() {
    std::string err;
    auto v = dc::json::Parse(R"({"o": {"deep": {"deeper": [1, {"k": 2.5}]}}})", err);
    assert(v != nullptr);
    const Value* deeper = v->find("o")->find("deep")->find("deeper");
    assert(deeper != nullptr);
    assert(deeper->as_array()[1].find("k")->as_number() == 2.5);
}

void TestRejectsTrailingComma() {
    std::string err;
    auto v = dc::json::Parse(R"({"a": 1,})", err);
    assert(v == nullptr);
    assert(!err.empty());
}

void TestRejectsTrailingGarbage() {
    std::string err;
    auto v = dc::json::Parse(R"({"a": 1} extra)", err);
    assert(v == nullptr);
}

void TestRejectsNaNInfinity() {
    std::string err;
    assert(dc::json::Parse(R"({"a": NaN})", err) == nullptr);
    assert(dc::json::Parse(R"({"a": Infinity})", err) == nullptr);
    assert(dc::json::Parse(R"({"a": -Infinity})", err) == nullptr);
    // JSON number overflow to non-finite double is rejected too.
    assert(dc::json::Parse(R"({"a": 1e999})", err) == nullptr);
}

void TestRejectsMalformed() {
    std::string err;
    assert(dc::json::Parse(R"({"a":})", err) == nullptr);
    assert(dc::json::Parse(R"({"a" 1})", err) == nullptr);
    assert(dc::json::Parse(R"([1, 2)", err) == nullptr);
    assert(dc::json::Parse(R"("unterminated)", err) == nullptr);
    assert(dc::json::Parse(R"({"a": 1", "b": 2})", err) == nullptr);
    assert(dc::json::Parse("", err) == nullptr);
    // Duplicate keys: allowed (last wins) by this parser.
    auto dup = dc::json::Parse(R"({"a": 1, "a": 2})", err);
    assert(dup != nullptr);
    assert(dup->find("a")->as_number() == 2.0);
}

void TestStrings() {
    std::string err;
    auto v = dc::json::Parse(R"({"esc": "a\"b\\c\ndA2", "u": "\u0041\u00e9", "surrogate": "\ud83d\ude00"})", err);
    assert(v != nullptr);
    assert(v->find("esc")->as_string() == "a\"b\\c\ndA2");
    assert(v->find("u")->as_string() == "A\xC3\xA9");
    assert(v->find("surrogate")->as_string() == "\xF0\x9F\x98\x80");
    // Reject orphan surrogates.
    assert(dc::json::Parse(R"("\ud83d")", err) == nullptr);
    assert(dc::json::Parse(R"("\ude00")", err) == nullptr);
}

void TestUnicodeNames() {
    std::string err;
    auto v = dc::json::Parse(R"({"caf\u00e9": 1})", err);
    assert(v != nullptr);
    assert(v->find("caf\xC3\xA9") != nullptr);
}

void TestNumbers() {
    std::string err;
    auto v = dc::json::Parse(R"({"int": 42, "neg": -17, "frac": 3.125, "exp": 1.5e3, "negexp": 2E-2})", err);
    assert(v != nullptr);
    assert(v->find("int")->as_number() == 42.0);
    assert(v->find("neg")->as_number() == -17.0);
    assert(v->find("frac")->as_number() == 3.125);
    assert(v->find("exp")->as_number() == 1500.0);
    assert(v->find("negexp")->as_number() == 0.02);
}

void TestDeepNestingRejected() {
    std::string err;
    std::string deep;
    for (int i = 0; i < 100; ++i) deep += "[";
    for (int i = 0; i < 100; ++i) deep += "]";
    assert(dc::json::Parse(deep, err) == nullptr);
}

} // namespace

int main() {
    TestBasics();
    TestNested();
    TestRejectsTrailingComma();
    TestRejectsTrailingGarbage();
    TestRejectsNaNInfinity();
    TestRejectsMalformed();
    TestStrings();
    TestUnicodeNames();
    TestNumbers();
    TestDeepNestingRejected();
    std::printf("json parser tests: all passed\n");
    return 0;
}
