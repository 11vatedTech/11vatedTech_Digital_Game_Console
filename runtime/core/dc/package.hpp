// dc/package.hpp — .11g logical package model (DK0-M3; ADR-0025).
//
// Separation of concerns (directive §3):
//   - logical model      : this header — what a package IS
//   - physical container : dc/package_container.hpp — how it is stored
//   - content store      : dc/package_store.hpp     — where objects live
//   - activation         : dc/package_install.hpp  — generations/rollback
//
// Identity rules (directive §5):
//   - identity = SHA-256 over the canonical manifest bytes (strict JSON,
//     sorted keys, no timestamps/paths/randoms in canonical fields)
//   - the declared "package_id" field is NOT hashed (self-reference); the
//     verifier recomputes the identity and compares it to the declaration
//   - filename never contributes to identity; renaming is invisible
//   - any content change produces a different manifest → different identity
//   - the executable content hash lives in content.descriptors
//
// Trust is orthogonal to identity (directive §9): an unsigned developer
// package has valid identity but an explicit trust state.
#pragma once

#include "dc/result.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace dc::package {

inline constexpr const char* kSchemaId = "dc.package/1";
inline constexpr const char* kRuntimeAbi = "dc-runtime/1";

// Structured reason codes (directive §8): the verifier fails closed with the
// precise reason, never a bare "invalid".
enum class Reason {
    Ok = 0,
    ContainerInvalid,          // not an .11g container at all
    ManifestMissing,           // no package manifest present
    ManifestMalformed,         // manifest present but not parseable JSON
    SchemaInvalid,             // schema field missing / foreign / unsupported
    SchemaVersionUnsupported,  // known schema id, unknown major version
    ManifestFieldMissing,      // required field absent
    ManifestFieldInvalid,      // present but wrong type/shape
    ManifestRefMissing,        // referenced sub-manifest not in package
    ContentHashMismatch,       // object bytes != descriptor hash
    EntrypointMissing,         // game manifest entrypoint not backed by content
    RuntimeIncompatible,       // runtime_abi unsupported by this console build
    PlatformUnsupported,       // package targets a platform this host is not
    SignatureInvalid,          // signature present but does not verify
    SignatureMissing,          // policy required a signature; none present
    IdentityMismatch,          // recomputed canonical identity != declared
    DescriptorConflict,        // same logical name, two different hashes
    TruncatedObject,           // object shorter than declared size
    InstallIncomplete,         // activation attempted with missing objects
    StoreCorrupt,              // content store objects missing/corrupt
};

const char* ReasonName(Reason r);

// ---- canonical manifest model ---------------------------------------------

struct ContentDescriptor {
    std::string logical_path;   // package-relative path, e.g. "windows-x64/title.exe"
    std::string sha256;         // lowercase hex of object bytes
    uint64_t size_bytes = 0;
    std::string object_sha256;  // content-addressed store key (== sha256 for DK0)
};

// Reuses formats/schemas/dc.game/1 semantics (no second manifest style).
struct GameManifest {
    std::string schema = "dc.game/1";
    std::string game_id;        // reverse-DNS, e.g. "tech.11vated.fidelitylab"
    std::string version;        // semver "MAJOR.MINOR.PATCH"
    std::string name;
    std::string publisher;
    std::map<std::string, std::string> entrypoints;  // platform -> package-relative path
    std::vector<std::string> required_profiles;      // DCP-*
    std::vector<std::string> optional_profiles;
    bool controller_required = true;
    bool offline_launch = true;
};

// Reuses formats/schemas/dc.lifecycle/1 semantics.
struct LifecycleManifest {
    std::string schema = "dc.lifecycle/1";
    std::string title_id;
    int suspend_ack_deadline_ms = 1000;
    std::string quick_resume_tier = "QR1";
    std::string display_change = "recreate_swapchain";
    std::string audio_route_change = "hot_switch";
    bool user_switching = false;
    bool honors_exit_request = true;
    bool flushes_saves = true;
    bool no_orphan_children = true;
};

// Save-compatibility boundary (directive §18): saves are owned by
// (user_id, game_id, save_schema); patch versions within a compatible
// save_schema must not destroy saves.
struct SaveManifest {
    std::string schema = "dc.save/1";
    std::string game_id;
    std::string save_schema = "s1";        // bump only on breaking save change
    std::vector<std::string> migratable_from;  // older save_schemas readable
    bool cloud_eligible = false;
};

struct PackageManifest {
    // --- canonical identity fields (deterministic; hashed) ---
    std::string schema = kSchemaId;
    std::string package_id;     // "sha256:<hex>" — DECLARED; verified, not trusted
    std::string game_id;
    std::string version;
    std::string runtime_abi = kRuntimeAbi;
    std::vector<std::string> platforms;  // e.g. ["windows-x64"]
    GameManifest game;
    LifecycleManifest lifecycle;
    SaveManifest save;
    std::vector<ContentDescriptor> content;

    // --- non-canonical (excluded from identity; tooling may add freely) ---
    // build provenance, signing envelope, packaging tool version, etc.
    std::map<std::string, std::string> provenance;  // non-deterministic-friendly
};

// Deterministic canonical JSON of the manifest (sorted keys, no whitespace,
// package_id excluded). Identity = "sha256:" + SHA-256(canonical_bytes).
std::string CanonicalManifestJson(const PackageManifest& m);
// Deterministic manifest JSON INCLUDING the declared package_id — this is the
// form stored in a container; verification parses it, re-derives the canonical
// (id-excluded) bytes and compares the hash against the declaration.
std::string FullManifestJson(const PackageManifest& m);
std::string PackageIdentity(const PackageManifest& m);  // "sha256:<hex>"
// Compare recomputed identity against the manifest's declared package_id.
// Returns Reason::IdentityMismatch (with detail) on any mismatch.
Reason VerifyIdentity(const PackageManifest& m, std::string* detail);

// Strict lowercase-hex-64 check for descriptor hashes and package ids.
bool IsLowerHex64(const std::string& s);

// Parse + structural validation. On failure returns the precise Reason.
// Does NOT verify content hashes (needs the object bytes) — see VerifyContent.
struct ParseResult {
    Reason reason = Reason::Ok;
    std::string detail;
    PackageManifest manifest;
};
ParseResult ParseManifestJson(const std::string& json_bytes);

// Verify every descriptor against actual object bytes fetched by the
// callback (keeps core free of filesystem/policy policy).
using ObjectFetcher = bool (*)(const std::string& object_sha256, std::string* out_bytes,
                               void* user);
struct ContentResult {
    Reason reason = Reason::Ok;
    std::string detail;
    std::string logical_path;  // offending object when reason is content-related
};
ContentResult VerifyContent(const PackageManifest& m, ObjectFetcher fetch, void* user);

// Entrypoint/platform/ABI checks against the parsed manifest itself.
Reason CheckEntrypoint(const PackageManifest& m, std::string* detail);
Reason CheckPlatform(const PackageManifest& m, const std::string& host_platform,
                     std::string* detail);
Reason CheckRuntimeAbi(const PackageManifest& m, const std::string& console_abi,
                       std::string* detail);

// Full verify pipeline in one call (identity + structure + content + refs).
struct VerifyResult {
    Reason reason = Reason::Ok;
    std::string detail;
    PackageManifest manifest;
    std::string identity;      // "sha256:<hex>" recomputed
};
VerifyResult Verify(ObjectFetcher fetch, void* user, const std::string& host_platform,
                    const std::string& console_abi);

} // namespace dc::package
