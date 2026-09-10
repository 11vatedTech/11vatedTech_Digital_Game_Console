// dc/title_context_json.hpp — versioned envelope transport for LaunchContext
// (DC_TITLE_CONTEXT/1). Serialize in the supervisor, parse in the title;
// both sides use the core strict JSON parser. Unknown fields are ignored by
// design (forward-compatible within the schema version); a foreign schema id
// is a hard parse failure (titles must not guess).
#pragma once

#include "dc/json.hpp"
#include "dc/title_context.hpp"

#include <cstdio>
#include <string>

namespace dc {

inline std::string SerializeTitleContext(const TitleLaunchContext& c) {
    std::string o;
    o.reserve(512 + c.host_capability_json.size() +
              c.session_experience_json.size());
    o += "{\"schema\":\"";
    o += c.schema.empty() ? kTitleContextSchema : c.schema.c_str();
    o += "\",\"title_id\":\"";
    o += c.title_id;
    o += "\",\"title_version\":\"";
    o += c.title_version;
    o += "\",\"session_id\":\"";
    o += c.session_id;
    o += "\",\"user_id\":";
    o += std::to_string(c.user_id);
    o += ",\"display\":{\"width\":";
    o += std::to_string(c.display_width);
    o += ",\"height\":";
    o += std::to_string(c.display_height);
    o += ",\"refresh_numerator\":";
    o += std::to_string(c.refresh_numerator);
    o += ",\"refresh_denominator\":";
    o += std::to_string(c.refresh_denominator);
    o += "},\"controller_required\":";
    o += c.controller_required ? "true" : "false";
    o += ",\"guide_owned_by_platform\":";
    o += c.guide_owned_by_platform ? "true" : "false";
    o += ",\"offline_launch\":";
    o += c.offline_launch ? "true" : "false";
    o += ",\"active_session_profiles\":[";
    for (size_t i = 0; i < c.active_session_profiles.size(); ++i) {
        if (i) o += ',';
        o += '"';
        o += c.active_session_profiles[i];
        o += '"';
    }
    o += "],\"host_capability\":";
    // Capability documents are embedded verbatim: they were produced by the
    // qualification pipeline and the title sees exactly what was certified.
    o += c.host_capability_json.empty() ? "null" : c.host_capability_json;
    o += ",\"session_experience\":";
    o += c.session_experience_json.empty() ? "null" : c.session_experience_json;
    o += '}';
    return o;
}

// Strict parse. Returns false and fills `error` on malformed input or a
// foreign/missing schema id. The context struct is left unmodified on failure.
inline bool ParseTitleContext(const std::string& text, TitleLaunchContext& out,
                              std::string& error) {
    std::string perr;
    auto v = json::Parse(text, perr);
    if (!v) {
        error = "json: " + perr;
        return false;
    }
    const json::Value* s = v->find("schema");
    if (!s || !s->is_string() ||
        s->as_string() != (out.schema.empty() ? kTitleContextSchema : out.schema)) {
        error = "unknown or missing schema id";
        return false;
    }
    TitleLaunchContext c;
    c.schema = s->as_string();

    auto get_str = [&](const char* k, std::string& dst) {
        if (const json::Value* x = v->find(k)) dst = x->as_string();
    };
    get_str("title_id", c.title_id);
    get_str("title_version", c.title_version);
    get_str("session_id", c.session_id);
    if (const json::Value* x = v->find("user_id"))
        c.user_id = static_cast<uint64_t>(x->as_number(0.0));

    if (const json::Value* d = v->find("display")) {
        if (const json::Value* x = d->find("width"))
            c.display_width = static_cast<uint32_t>(x->as_number(0.0));
        if (const json::Value* x = d->find("height"))
            c.display_height = static_cast<uint32_t>(x->as_number(0.0));
        if (const json::Value* x = d->find("refresh_numerator"))
            c.refresh_numerator = static_cast<uint32_t>(x->as_number(0.0));
        if (const json::Value* x = d->find("refresh_denominator"))
            c.refresh_denominator = static_cast<uint32_t>(x->as_number(0.0));
    }

    auto get_bool = [&](const char* k, bool& dst) {
        if (const json::Value* x = v->find(k)) dst = x->as_bool(dst);
    };
    get_bool("controller_required", c.controller_required);
    get_bool("guide_owned_by_platform", c.guide_owned_by_platform);
    get_bool("offline_launch", c.offline_launch);

    if (const json::Value* a = v->find("active_session_profiles")) {
        for (const auto& e : a->as_array())
            if (e.is_string()) c.active_session_profiles.push_back(e.as_string());
    }

    // Nested capability documents: re-serialize the parsed subtrees back to
    // canonical text (strict-parser round-trip), so the title consumes
    // well-formed JSON regardless of envelope whitespace.
    if (const json::Value* h = v->find("host_capability")) {
        if (h->is_object()) {
            c.host_capability_json.clear();
            json::WriteCompact(*h, c.host_capability_json);
        }
    }
    if (const json::Value* se = v->find("session_experience")) {
        if (se->is_object()) {
            c.session_experience_json.clear();
            json::WriteCompact(*se, c.session_experience_json);
        }
    }

    out = std::move(c);
    return true;
}

} // namespace dc
