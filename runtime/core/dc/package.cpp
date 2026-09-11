// dc/package.cpp — .11g logical model: canonical JSON, identity, verify
// pipeline (DK0-M3; ADR-0025). Determinism rule: identical inputs produce
// byte-identical canonical manifests (no timestamps, no paths, sorted keys —
// json::Object is std::map, so WriteCompact output is key-sorted and compact).
#include "dc/package.hpp"

#include "dc/hash.hpp"
#include "dc/json.hpp"

#include <algorithm>
#include <cstdio>

namespace dc::package {

namespace {

using dc::json::Array;
using dc::json::Object;
using dc::json::Value;

Value StrArray(const std::vector<std::string>& v) {
    Array a;
    a.reserve(v.size());
    for (const auto& s : v) a.emplace_back(Value(s));
    return Value(std::move(a));
}

// ---- canonical manifest -> Value (canonical fields only) -------------------

Object LifecycleObj(const LifecycleManifest& l) {
    Object o;
    o.emplace("audio_route_change_handling", Value(l.audio_route_change));
    o.emplace("display_change_handling", Value(l.display_change));
    o.emplace("quick_resume_tier", Value(l.quick_resume_tier));
    o.emplace("schema", Value(l.schema));
    o.emplace("suspend_acknowledgement_deadline_ms",
              Value(static_cast<double>(l.suspend_ack_deadline_ms)));
    o.emplace("title_id", Value(l.title_id));
    o.emplace("user_switching", Value(l.user_switching));
    Object exit;
    exit.emplace("flushes_saves", Value(l.flushes_saves));
    exit.emplace("honors_exit_request", Value(l.honors_exit_request));
    exit.emplace("no_orphan_children", Value(l.no_orphan_children));
    o.emplace("exit_behavior", Value(std::move(exit)));
    return o;
}

Object SaveObj(const SaveManifest& sv) {
    Object o;
    o.emplace("cloud_eligible", Value(sv.cloud_eligible));
    o.emplace("game_id", Value(sv.game_id));
    o.emplace("migratable_from", StrArray(sv.migratable_from));
    o.emplace("save_schema", Value(sv.save_schema));
    o.emplace("schema", Value(sv.schema));
    return o;
}

Object GameObj(const GameManifest& g) {
    Object eps;
    for (const auto& [plat, path] : g.entrypoints) eps.emplace(plat, Value(path));
    Object o;
    o.emplace("controller_required", Value(g.controller_required));
    o.emplace("entrypoints", Value(std::move(eps)));
    o.emplace("name", Value(g.name));
    o.emplace("offline_launch", Value(g.offline_launch));
    o.emplace("optional_profiles", StrArray(g.optional_profiles));
    o.emplace("publisher", Value(g.publisher));
    o.emplace("required_profiles", StrArray(g.required_profiles));
    o.emplace("schema", Value(g.schema));
    o.emplace("version", Value(g.version));
    return o;
}

Object ContentObj(const std::vector<ContentDescriptor>& c) {
    Array arr;
    arr.reserve(c.size());
    for (const auto& d : c) {
        Object o;
        o.emplace("logical_path", Value(d.logical_path));
        o.emplace("object_sha256", Value(d.object_sha256));
        o.emplace("sha256", Value(d.sha256));
        o.emplace("size", Value(static_cast<double>(d.size_bytes)));
        arr.emplace_back(Value(std::move(o)));
    }
    Object content;
    content.emplace("descriptors", Value(std::move(arr)));
    return content;
}

// Normalization: fill derived/defaulted fields so identity is computed over
// the canonical FORM of the manifest. Parse applies the identical rules, so
// parse -> canonical bytes == original -> canonical bytes (directive §20).
PackageManifest Normalized(const PackageManifest& m) {
    PackageManifest n = m;
    if (n.game.game_id.empty()) n.game.game_id = n.game_id;
    if (n.save.game_id.empty()) n.save.game_id = n.game_id;
    if (n.lifecycle.title_id.empty()) n.lifecycle.title_id = n.game_id;
    for (auto& d : n.content)
        if (d.object_sha256.empty()) d.object_sha256 = d.sha256;
    return n;
}

Value ManifestToValue(const PackageManifest& m, bool include_package_id) {
    const PackageManifest n = Normalized(m);
    Object root;
    root.emplace("content", Value(ContentObj(n.content)));
    root.emplace("game", Value(GameObj(n.game)));
    root.emplace("game_id", Value(n.game_id));
    root.emplace("lifecycle", Value(LifecycleObj(n.lifecycle)));
    // package_id is declared-but-not-hashed: identity cannot include itself.
    if (include_package_id) root.emplace("package_id", Value(n.package_id));
    root.emplace("platforms", StrArray(n.platforms));
    root.emplace("runtime_abi", Value(n.runtime_abi));
    root.emplace("save", Value(SaveObj(n.save)));
    root.emplace("schema", Value(n.schema));
    root.emplace("version", Value(n.version));
    return Value(std::move(root));
}

// ---- strict extraction helpers ---------------------------------------------

const Value* Field(const Object& o, const char* k) {
    auto it = o.find(k);
    return it == o.end() ? nullptr : &it->second;
}

bool GetStr(const Object& o, const char* k, std::string* out) {
    const Value* v = Field(o, k);
    if (!v || !v->is_string()) return false;
    *out = v->as_string();
    return true;
}
bool GetBool(const Object& o, const char* k, bool* out) {
    const Value* v = Field(o, k);
    if (!v || !v->is_bool()) return false;
    *out = v->as_bool();
    return true;
}
bool GetUint(const Object& o, const char* k, uint64_t* out) {
    const Value* v = Field(o, k);
    if (!v || !v->is_number()) return false;
    const double n = v->as_number();
    if (n < 0 || n != static_cast<double>(static_cast<uint64_t>(n))) return false;
    *out = static_cast<uint64_t>(n);
    return true;
}
bool GetStrArr(const Object& o, const char* k, std::vector<std::string>* out) {
    const Value* v = Field(o, k);
    if (!v || !v->is_array()) return false;
    for (const auto& e : v->as_array()) {
        if (!e.is_string()) return false;
        out->push_back(e.as_string());
    }
    return true;
}

ParseResult Fail(Reason r, std::string d) { return ParseResult{r, std::move(d), {}}; }

ParseResult ParseGame(const Object& o, GameManifest* g) {
    if (!GetStr(o, "name", &g->name)) return Fail(Reason::ManifestFieldMissing, "game.name");
    if (!GetStr(o, "version", &g->version)) return Fail(Reason::ManifestFieldMissing, "game.version");
    GetStr(o, "publisher", &g->publisher);
    GetBool(o, "controller_required", &g->controller_required);
    GetBool(o, "offline_launch", &g->offline_launch);
    GetStrArr(o, "required_profiles", &g->required_profiles);
    GetStrArr(o, "optional_profiles", &g->optional_profiles);
    const Value* eps = Field(o, "entrypoints");
    if (!eps || !eps->is_object() || eps->as_object().empty())
        return Fail(Reason::ManifestFieldMissing, "game.entrypoints");
    for (const auto& [plat, path] : eps->as_object()) {
        if (!path.is_string()) return Fail(Reason::ManifestFieldInvalid, "entrypoint " + plat);
        g->entrypoints[plat] = path.as_string();
    }
    return ParseResult{};
}

ParseResult ParseLifecycle(const Object& o, LifecycleManifest* l) {
    GetStr(o, "title_id", &l->title_id);
    GetStr(o, "quick_resume_tier", &l->quick_resume_tier);
    GetStr(o, "display_change_handling", &l->display_change);
    GetStr(o, "audio_route_change_handling", &l->audio_route_change);
    GetBool(o, "user_switching", &l->user_switching);
    uint64_t deadline = 0;
    if (GetUint(o, "suspend_acknowledgement_deadline_ms", &deadline)) {
        if (deadline < 100) return Fail(Reason::ManifestFieldInvalid, "suspend deadline < 100ms");
        l->suspend_ack_deadline_ms = static_cast<int>(deadline);
    }
    if (const Value* eb = Field(o, "exit_behavior"); eb && eb->is_object()) {
        GetBool(eb->as_object(), "honors_exit_request", &l->honors_exit_request);
        GetBool(eb->as_object(), "flushes_saves", &l->flushes_saves);
        GetBool(eb->as_object(), "no_orphan_children", &l->no_orphan_children);
    }
    return ParseResult{};
}

ParseResult ParseSave(const Object& o, SaveManifest* sv) {
    GetStr(o, "game_id", &sv->game_id);
    GetStr(o, "save_schema", &sv->save_schema);
    GetBool(o, "cloud_eligible", &sv->cloud_eligible);
    GetStrArr(o, "migratable_from", &sv->migratable_from);
    return ParseResult{};
}

// Strict "MAJOR.MINOR.PATCH" semver (no locale-dependent scanning).
bool ParseSemVer(const std::string& s, int* major, int* minor, int* patch) {
    size_t i = 0;
    int parts[3] = {0, 0, 0};
    for (int p = 0; p < 3; ++p) {
        if (i >= s.size() || s[i] < '0' || s[i] > '9') return false;
        int val = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            val = val * 10 + (s[i] - '0');
            if (val > 999999) return false;  // bound absurd versions
            ++i;
        }
        parts[p] = val;
        if (p < 2) {
            if (i >= s.size() || s[i] != '.') return false;
            ++i;
        }
    }
    if (i != s.size()) return false;
    *major = parts[0];
    *minor = parts[1];
    *patch = parts[2];
    return true;
}

} // namespace

// Public: strict lowercase-hex-64 check (also used by tools/dc-pack).
bool IsLowerHex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

const char* ReasonName(Reason r) {
    switch (r) {
        case Reason::Ok: return "OK";
        case Reason::ContainerInvalid: return "PACKAGE_CONTAINER_INVALID";
        case Reason::ManifestMissing: return "PACKAGE_MANIFEST_MISSING";
        case Reason::ManifestMalformed: return "PACKAGE_MANIFEST_MALFORMED";
        case Reason::SchemaInvalid: return "PACKAGE_SCHEMA_INVALID";
        case Reason::SchemaVersionUnsupported: return "PACKAGE_SCHEMA_VERSION_UNSUPPORTED";
        case Reason::ManifestFieldMissing: return "PACKAGE_MANIFEST_FIELD_MISSING";
        case Reason::ManifestFieldInvalid: return "PACKAGE_MANIFEST_FIELD_INVALID";
        case Reason::ManifestRefMissing: return "PACKAGE_MANIFEST_REF_MISSING";
        case Reason::ContentHashMismatch: return "PACKAGE_HASH_MISMATCH";
        case Reason::EntrypointMissing: return "PACKAGE_ENTRYPOINT_MISSING";
        case Reason::RuntimeIncompatible: return "PACKAGE_RUNTIME_INCOMPATIBLE";
        case Reason::PlatformUnsupported: return "PACKAGE_PLATFORM_UNSUPPORTED";
        case Reason::SignatureInvalid: return "PACKAGE_SIGNATURE_INVALID";
        case Reason::SignatureMissing: return "PACKAGE_SIGNATURE_MISSING";
        case Reason::IdentityMismatch: return "PACKAGE_IDENTITY_MISMATCH";
        case Reason::DescriptorConflict: return "PACKAGE_DESCRIPTOR_CONFLICT";
        case Reason::TruncatedObject: return "PACKAGE_OBJECT_TRUNCATED";
        case Reason::InstallIncomplete: return "PACKAGE_INSTALL_INCOMPLETE";
        case Reason::StoreCorrupt: return "PACKAGE_STORE_CORRUPT";
    }
    return "PACKAGE_UNKNOWN";
}

std::string CanonicalManifestJson(const PackageManifest& m) {
    std::string out;
    dc::json::WriteCompact(ManifestToValue(m, /*include_package_id=*/false), out);
    return out;
}

std::string PackageIdentity(const PackageManifest& m) {
    return "sha256:" + dc::hash::Sha256::Hex(CanonicalManifestJson(m));
}

std::string FullManifestJson(const PackageManifest& m) {
    std::string out;
    dc::json::WriteCompact(ManifestToValue(m, /*include_package_id=*/true), out);
    return out;
}

Reason VerifyIdentity(const PackageManifest& m, std::string* detail) {
    if (m.package_id.empty()) {
        if (detail) *detail = "package_id field is empty";
        return Reason::IdentityMismatch;
    }
    const std::string computed = PackageIdentity(m);
    if (computed != m.package_id) {
        if (detail)
            *detail = "declared " + m.package_id.substr(0, 22) + "... computed " +
                      computed.substr(0, 22) + "...";
        return Reason::IdentityMismatch;
    }
    return Reason::Ok;
}

ParseResult ParseManifestJson(const std::string& bytes) {
    std::string err;
    auto v = dc::json::Parse(bytes, err);
    if (!v) return Fail(Reason::ManifestMalformed, err);
    if (!v->is_object()) return Fail(Reason::ManifestMalformed, "root not object");
    const Object& o = v->as_object();

    std::string schema;
    if (!GetStr(o, "schema", &schema)) return Fail(Reason::SchemaInvalid, "schema field missing");
    if (schema != kSchemaId) {
        if (schema.rfind("dc.package/", 0) == 0) return Fail(Reason::SchemaVersionUnsupported, schema);
        return Fail(Reason::SchemaInvalid, schema);
    }

    PackageManifest m;
    m.schema = schema;
    GetStr(o, "package_id", &m.package_id);
    if (!GetStr(o, "game_id", &m.game_id)) return Fail(Reason::ManifestFieldMissing, "game_id");
    if (!GetStr(o, "version", &m.version)) return Fail(Reason::ManifestFieldMissing, "version");
    {
        int mj, mn, pt;
        if (!ParseSemVer(m.version, &mj, &mn, &pt))
            return Fail(Reason::ManifestFieldInvalid, "version not semver");
    }
    GetStr(o, "runtime_abi", &m.runtime_abi);
    if (!GetStrArr(o, "platforms", &m.platforms) || m.platforms.empty())
        return Fail(Reason::ManifestFieldMissing, "platforms");

    if (const Value* game = Field(o, "game"); game && game->is_object()) {
        if (auto r = ParseGame(game->as_object(), &m.game); r.reason != Reason::Ok) return r;
    } else {
        return Fail(Reason::ManifestRefMissing, "game manifest");
    }

    if (const Value* lc = Field(o, "lifecycle"); lc && lc->is_object()) {
        if (auto r = ParseLifecycle(lc->as_object(), &m.lifecycle); r.reason != Reason::Ok) return r;
    } else {
        return Fail(Reason::ManifestRefMissing, "lifecycle manifest");
    }

    if (const Value* sv = Field(o, "save"); sv && sv->is_object()) {
        if (auto r = ParseSave(sv->as_object(), &m.save); r.reason != Reason::Ok) return r;
    } else {
        return Fail(Reason::ManifestRefMissing, "save manifest");
    }

    const Value* content = Field(o, "content");
    if (!content || !content->is_object()) return Fail(Reason::ManifestRefMissing, "content section");
    const Value* desc = Field(content->as_object(), "descriptors");
    if (!desc || !desc->is_array() || desc->as_array().empty())
        return Fail(Reason::ManifestFieldMissing, "content.descriptors");
    for (const auto& dv : desc->as_array()) {
        if (!dv.is_object()) return Fail(Reason::ManifestFieldInvalid, "descriptor");
        const Object& dob = dv.as_object();
        ContentDescriptor d;
        if (!GetStr(dob, "logical_path", &d.logical_path))
            return Fail(Reason::ManifestFieldMissing, "descriptor.logical_path");
        if (!GetStr(dob, "sha256", &d.sha256) || !IsLowerHex64(d.sha256))
            return Fail(Reason::ManifestFieldInvalid, "descriptor.sha256");
        uint64_t size = 0;
        if (!GetUint(dob, "size", &size)) return Fail(Reason::ManifestFieldInvalid, "descriptor.size");
        d.size_bytes = size;
        GetStr(dob, "object_sha256", &d.object_sha256);
        if (d.object_sha256.empty()) d.object_sha256 = d.sha256;
        m.content.push_back(std::move(d));
    }

    // Descriptor uniqueness.
    std::vector<std::string> paths;
    paths.reserve(m.content.size());
    for (const auto& d : m.content) paths.push_back(d.logical_path);
    std::sort(paths.begin(), paths.end());
    if (std::adjacent_find(paths.begin(), paths.end()) != paths.end())
        return Fail(Reason::DescriptorConflict, "duplicate logical_path");

    // Cross-field consistency.
    if (m.game.game_id.empty()) m.game.game_id = m.game_id;
    if (m.game.game_id != m.game_id) return Fail(Reason::ManifestFieldInvalid, "game.game_id != game_id");
    if (m.save.game_id.empty()) m.save.game_id = m.game_id;
    if (m.save.game_id != m.game_id) return Fail(Reason::ManifestFieldInvalid, "save.game_id != game_id");
    if (m.lifecycle.title_id.empty()) m.lifecycle.title_id = m.game_id;

    return ParseResult{Reason::Ok, "", std::move(m)};
}

ContentResult VerifyContent(const PackageManifest& m, ObjectFetcher fetch, void* user) {
    for (const auto& d : m.content) {
        std::string bytes;
        if (!fetch(d.object_sha256, &bytes, user))
            return ContentResult{Reason::StoreCorrupt, "object missing: " + d.logical_path, d.logical_path};
        if (bytes.size() != d.size_bytes)
            return ContentResult{Reason::TruncatedObject,
                                 "expected " + std::to_string(d.size_bytes) + " got " +
                                     std::to_string(bytes.size()) + ": " + d.logical_path,
                                 d.logical_path};
        const std::string got = dc::hash::Sha256::Hex(bytes);
        if (got != d.sha256)
            return ContentResult{Reason::ContentHashMismatch,
                                 "logical_path " + d.logical_path + " want " +
                                     d.sha256.substr(0, 12) + " got " + got.substr(0, 12),
                                 d.logical_path};
    }
    return ContentResult{};
}

Reason CheckEntrypoint(const PackageManifest& m, std::string* detail) {
    for (const auto& [plat, path] : m.game.entrypoints) {
        bool found = false;
        for (const auto& d : m.content)
            if (d.logical_path == path) { found = true; break; }
        if (!found) {
            if (detail) *detail = plat + " entrypoint not in content: " + path;
            return Reason::EntrypointMissing;
        }
    }
    return Reason::Ok;
}

Reason CheckPlatform(const PackageManifest& m, const std::string& host_platform, std::string* detail) {
    for (const auto& p : m.platforms)
        if (p == host_platform) return Reason::Ok;
    if (detail) {
        std::string s;
        for (const auto& p : m.platforms) s += (s.empty() ? "" : ",") + p;
        *detail = "package platforms: " + s;
    }
    return Reason::PlatformUnsupported;
}

Reason CheckRuntimeAbi(const PackageManifest& m, const std::string& console_abi, std::string* detail) {
    if (m.runtime_abi == console_abi) return Reason::Ok;
    if (detail) *detail = "package requires " + m.runtime_abi + ", console provides " + console_abi;
    return Reason::RuntimeIncompatible;
}

} // namespace dc::package
