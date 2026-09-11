// dc/package_store.hpp — content-addressed store + generation-based
// activation (DK0-M3; ADR-0025, directive §6/§10/§11/§12).
//
// Layout (library root, e.g. <data>/library):
//   store/objects/<h2>/<h62>          content-addressed objects (h = full hex)
//   titles/<game_id>/generations/gNNNN/<logical paths>   immutable generation views
//   titles/<game_id>/active.json      {"generation": "g0001", "package_id": "..."}
//
// Activation contract (directive §11):
//   - an import that fails ANY validation leaves the previous active
//     generation untouched and returns the precise reason
//   - activation is a single-file atomic-ish pointer swap (write temp, rename)
//   - rollback = point active.json back at the previous generation
//
// Core keeps filesystem primitives (mkdir/copy/rename) here in one place so
// the rest of core remains host-agnostic; the Windows console mounts the
// library under %LOCALAPPDATA%/11vated/console in a later milestone.
#pragma once

#include "dc/package.hpp"
#include "dc/package_container.hpp"

#include <optional>
#include <string>
#include <vector>

namespace dc::package {

// ---- filesystem primitives (implemented in package_store.cpp) --------------
bool MakeDirs(const std::string& path);
bool PathExists(const std::string& path);
bool CopyFileBytes(const std::string& from, const std::string& to);
bool AtomicWriteText(const std::string& path, const std::string& bytes);  // temp+rename
std::string JoinPath(const std::string& a, const std::string& b);
std::string PathSeparator();

// ---- content-addressed store ------------------------------------------------

class ContentStore {
public:
    explicit ContentStore(std::string root) : root_(std::move(root)) {}

    // Import object bytes; returns the store-relative key ("sha256:<hex>").
    // Skips the write if the object already exists (content addressing).
    bool PutObject(const std::string& bytes, std::string* out_key, std::string* err);

    // Fetch object bytes by "sha256:<hex>" key. (Named FetchObject, not
    // GetObject: GetObject is a Win32 macro and must not collide.)
    bool FetchObject(const std::string& key, std::string* out_bytes) const;

    bool HasObject(const std::string& key) const;

private:
    std::string root_;  // .../store
    static std::string ObjectPath(const std::string& root, const std::string& key);
};

// ---- library: generations + activation --------------------------------------

struct InstalledPackage {
    std::string game_id;
    std::string version;
    std::string package_id;     // "sha256:<hex>"
    std::string generation;     // "g0001"
    std::string install_dir;    // generation view directory
    std::string entrypoint;     // resolved, package-relative executable path
    std::vector<std::string> required_profiles;
    std::vector<std::string> optional_profiles;
    std::string save_schema;
    // Lifecycle manifest carried through to the runtime (directive §17).
    int suspend_ack_deadline_ms = 1000;
    std::string quick_resume_tier = "QR1";
    bool honors_exit_request = true;
    bool flushes_saves = true;
};

struct InstallResult {
    Reason reason = Reason::Ok;
    std::string detail;
    std::string reason_code;
    InstalledPackage installed;
};

class Library {
public:
    explicit Library(std::string root) : root_(std::move(root)) {}

    // Full install flow (directive §10): verify container → policy checks →
    // import objects → build candidate generation view → atomic activate.
    // If any step fails, the previously active generation is untouched.
    InstallResult Install(const std::string& container_path,
                          const std::string& host_platform,
                          const std::string& console_abi);

    // Current active package for a game, if any.
    std::optional<InstalledPackage> GetActive(const std::string& game_id) const;

    // Rollback metadata (directive §12): generation history, newest first.
    std::vector<std::string> Generations(const std::string& game_id) const;

    // Point active.json at an existing (fully validated) generation.
    bool ActivateGeneration(const std::string& game_id, const std::string& generation,
                            std::string* err);

    // The authoritative library root.
    const std::string& root() const { return root_; }

private:
    std::string TitlesDir() const;
    std::string GameDir(const std::string& game_id) const;
    std::string ActivePath(const std::string& game_id) const;
    std::string root_;
};

} // namespace dc::package
