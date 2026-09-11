// dc/package_store.cpp — content store, generations, atomic activation
// (DK0-M3; ADR-0025). Fail-closed: an import that fails any validation never
// touches the previously active generation (directive §11/§12).
#include "dc/package_store.hpp"

#include "dc/hash.hpp"
#include "dc/json.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace dc::package {

namespace fs = std::filesystem;

// ---- filesystem primitives --------------------------------------------------

bool MakeDirs(const std::string& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    return !ec || fs::exists(path);
}

bool PathExists(const std::string& path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

bool CopyFileBytes(const std::string& from, const std::string& to) {
    std::string bytes;
    if (!ReadFileBytes(from, &bytes)) return false;
    return WriteFileBytes(to, bytes);
}

bool AtomicWriteText(const std::string& path, const std::string& bytes) {
    const std::string tmp = path + ".tmp";
    if (!WriteFileBytes(tmp, bytes)) return false;
    std::error_code ec;
    fs::rename(tmp, path, ec);  // same-volume rename is atomic on NTFS
    return !ec;
}

std::string JoinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == '/' || a.back() == '\\') return a + b;
    return a + PathSeparator() + b;
}

std::string PathSeparator() { return "/"; }  // store-internal layout is POSIX-style

// ---- content store ----------------------------------------------------------

std::string ContentStore::ObjectPath(const std::string& root, const std::string& key) {
    // key = "sha256:<64 hex>"
    const std::string hex = key.substr(key.find(':') + 1);
    return JoinPath(root, "objects/" + hex.substr(0, 2) + "/" + hex);
}

bool ContentStore::PutObject(const std::string& bytes, std::string* out_key, std::string* err) {
    if (bytes.empty()) {
        if (err) *err = "refusing to store empty object";
        return false;
    }
    const std::string hex = dc::hash::Sha256::Hex(bytes);
    const std::string key = "sha256:" + hex;
    const std::string obj_path = ObjectPath(root_, key);
    if (PathExists(obj_path)) {  // content-addressed: identical bytes are done
        if (out_key) *out_key = key;
        return true;
    }
    if (!MakeDirs(obj_path.substr(0, obj_path.find_last_of('/')))) {
        if (err) *err = "cannot create object directory";
        return false;
    }
    // Write-then-verify-then-rename: a partial object must never be visible.
    const std::string tmp = obj_path + ".tmp";
    if (!WriteFileBytes(tmp, bytes)) {
        if (err) *err = "object write failed";
        return false;
    }
    std::string verify;
    if (!ReadFileBytes(tmp, &verify) || dc::hash::Sha256::Hex(verify) != hex) {
        fs::remove(tmp);
        if (err) *err = "object failed post-write verification";
        return false;
    }
    std::error_code ec;
    fs::rename(tmp, obj_path, ec);
    if (ec) {
        fs::remove(tmp);
        if (err) *err = "object finalize rename failed";
        return false;
    }
    if (out_key) *out_key = key;
    return true;
}

bool ContentStore::FetchObject(const std::string& key, std::string* out_bytes) const {
    return ReadFileBytes(ObjectPath(root_, key), out_bytes);
}

bool ContentStore::HasObject(const std::string& key) const {
    return PathExists(ObjectPath(root_, key));
}

// ---- library ----------------------------------------------------------------

std::string Library::TitlesDir() const { return JoinPath(root_, "titles"); }
std::string Library::GameDir(const std::string& game_id) const {
    return JoinPath(TitlesDir(), game_id);
}
std::string Library::ActivePath(const std::string& game_id) const {
    return JoinPath(GameDir(game_id), "active.json");
}

InstallResult InstallFail(Reason r, std::string detail) {
    InstallResult res;
    res.reason = r;
    res.detail = std::move(detail);
    res.reason_code = ReasonName(r);
    return res;
}

InstallResult Library::Install(const std::string& container_path,
                               const std::string& host_platform,
                               const std::string& console_abi) {
    std::string container_bytes;
    if (!ReadFileBytes(container_path, &container_bytes))
        return InstallFail(Reason::ContainerInvalid, "cannot read " + container_path);

    // 1. Full verification of the container artifact.
    ContainerVerifyResult v = VerifyPackage(container_bytes, host_platform, console_abi);
    if (v.reason != Reason::Ok)
        return InstallFail(v.reason, v.detail);

    // 1b. Idempotent reinstall (directive §6): if the active generation is
    // already this exact package (content identity + version), activation is
    // a no-op — repeated installs of the same artifact must not stack
    // generations. A CORRUPTED variant of the same version still fails above
    // (identity differs), so this shortcut never masks damage.
    const std::string game_id_early = v.manifest.game_id;
    const std::string identity_early = PackageIdentity(v.manifest);
    if (auto cur = GetActive(game_id_early)) {
        if (cur->package_id == identity_early && cur->version == v.manifest.version) {
            InstallResult res;
            res.installed = std::move(*cur);
            return res;
        }
    }

    ContentStore store(JoinPath(root_, "store"));

    // 2. Import content-addressed objects (idempotent).
    for (const auto& d : v.manifest.content) {
        std::string bytes;
        const std::string key = "sha256:" + d.sha256;
        if (!store.HasObject(key)) {
            // Object bytes come from the container itself.
            ContainerParseResult c = ParseContainer(container_bytes);
            if (c.reason != Reason::Ok)
                return InstallFail(c.reason, c.detail);
            bool found = false;
            for (const auto& m : c.members) {
                if (m.path == "objects/" + d.sha256) {
                    std::string k;
                    std::string err;
                    if (!store.PutObject(m.bytes, &k, &err))
                        return InstallFail(Reason::StoreCorrupt, "import " + d.logical_path + ": " + err);
                    if (k != key)
                        return InstallFail(Reason::StoreCorrupt, "import hash drift: " + d.logical_path);
                    found = true;
                    break;
                }
            }
            if (!found)
                return InstallFail(Reason::StoreCorrupt, "container lost object " + d.logical_path);
        }
    }

    // 3. Build candidate generation view (fully materialized before activation).
    const std::string game_id = v.manifest.game_id;
    std::vector<std::string> gens = Generations(game_id);
    int next = 1;
    if (!gens.empty()) {
        // gens are sorted newest-first as "gNNNN"
        next = std::atoi(gens.front().c_str() + 1) + 1;
    }
    char gen_buf[16];
    std::snprintf(gen_buf, sizeof(gen_buf), "g%04d", next);
    const std::string gen_name = gen_buf;
    const std::string gen_dir = JoinPath(GameDir(game_id), "generations/" + gen_name);

    for (const auto& d : v.manifest.content) {
        std::string bytes;
        std::string err;
        if (!store.FetchObject("sha256:" + d.sha256, &bytes))
            return InstallFail(Reason::StoreCorrupt, "store lost " + d.logical_path);
        const std::string dst = JoinPath(gen_dir, d.logical_path);
        if (!MakeDirs(dst.substr(0, dst.find_last_of('/'))))
            return InstallFail(Reason::InstallIncomplete, "mkdir for " + d.logical_path);
        if (!WriteFileBytes(dst, bytes))
            return InstallFail(Reason::InstallIncomplete, "materialize " + d.logical_path);
        // Verify the materialized view byte-for-byte before it can go active.
        std::string check;
        if (!ReadFileBytes(dst, &check) || dc::hash::Sha256::Hex(check) != d.sha256)
            return InstallFail(Reason::InstallIncomplete, "materialized view corrupt: " + d.logical_path);
    }

    // 4. Generation manifest (the declared form, with package_id) — written
    // BEFORE activation so an active generation is always fully resolvable.
    if (!WriteFileBytes(JoinPath(gen_dir, "manifest.json"), FullManifestJson(v.manifest)))
        return InstallFail(Reason::InstallIncomplete, "generation manifest write failed");

    // 5. Generation metadata (provenance for rollback, directive §12).
    const std::string identity = PackageIdentity(v.manifest);
    std::string meta = "{\"game_id\":\"" + game_id + "\",\"generation\":\"" + gen_name +
                       "\",\"package_id\":\"" + identity + "\",\"version\":\"" + v.manifest.version +
                       "\",\"content_objects\":" + std::to_string(v.manifest.content.size()) + "}";
    if (!AtomicWriteText(JoinPath(gen_dir, ".generation.json"), meta))
        return InstallFail(Reason::InstallIncomplete, "generation metadata write failed");

    // 6. Atomic activation — the only step that changes what "active" means.
    InstalledPackage pkg;
    pkg.game_id = game_id;
    pkg.version = v.manifest.version;
    pkg.package_id = identity;
    pkg.generation = gen_name;
    pkg.install_dir = gen_dir;
    auto ep = v.manifest.game.entrypoints.find(host_platform);
    if (ep == v.manifest.game.entrypoints.end())
        return InstallFail(Reason::PlatformUnsupported, "no entrypoint for " + host_platform);
    pkg.entrypoint = ep->second;
    pkg.required_profiles = v.manifest.game.required_profiles;
    pkg.optional_profiles = v.manifest.game.optional_profiles;
    pkg.save_schema = v.manifest.save.save_schema;
    pkg.suspend_ack_deadline_ms = v.manifest.lifecycle.suspend_ack_deadline_ms;
    pkg.quick_resume_tier = v.manifest.lifecycle.quick_resume_tier;
    pkg.honors_exit_request = v.manifest.lifecycle.honors_exit_request;
    pkg.flushes_saves = v.manifest.lifecycle.flushes_saves;

    std::string active = "{\"generation\":\"" + gen_name + "\",\"package_id\":\"" + identity +
                         "\",\"version\":\"" + pkg.version + "\"}";
    if (!AtomicWriteText(ActivePath(game_id), active))
        return InstallFail(Reason::InstallIncomplete, "activation pointer write failed");

    InstallResult res;
    res.installed = std::move(pkg);
    return res;
}

std::optional<InstalledPackage> Library::GetActive(const std::string& game_id) const {
    const std::string active_path = ActivePath(game_id);
    std::string bytes;
    if (!ReadFileBytes(active_path, &bytes)) return std::nullopt;
    std::string err;
    auto v = dc::json::Parse(bytes, err);
    if (!v || !v->is_object()) return std::nullopt;
    const auto* gen = v->find("generation");
    const auto* pid = v->find("package_id");
    const auto* ver = v->find("version");
    if (!gen || !gen->is_string()) return std::nullopt;

    // Load the generation's manifest for the resolved entrypoint/profiles.
    const std::string gen_dir = JoinPath(GameDir(game_id), "generations/" + gen->as_string());
    std::string manifest_bytes;
    if (!ReadFileBytes(JoinPath(gen_dir, "manifest.json"), &manifest_bytes))
        return std::nullopt;
    ParseResult p = ParseManifestJson(manifest_bytes);
    if (p.reason != Reason::Ok) return std::nullopt;

    InstalledPackage pkg;
    pkg.game_id = game_id;
    pkg.generation = gen->as_string();
    pkg.package_id = pid && pid->is_string() ? pid->as_string() : PackageIdentity(p.manifest);
    pkg.version = ver && ver->is_string() ? ver->as_string() : p.manifest.version;
    pkg.install_dir = gen_dir;
    pkg.entrypoint = p.manifest.game.entrypoints.count("windows-x64")
                         ? p.manifest.game.entrypoints.at("windows-x64")
                         : std::string();
    if (pkg.entrypoint.empty()) return std::nullopt;
    pkg.required_profiles = p.manifest.game.required_profiles;
    pkg.optional_profiles = p.manifest.game.optional_profiles;
    pkg.save_schema = p.manifest.save.save_schema;
    pkg.suspend_ack_deadline_ms = p.manifest.lifecycle.suspend_ack_deadline_ms;
    pkg.quick_resume_tier = p.manifest.lifecycle.quick_resume_tier;
    pkg.honors_exit_request = p.manifest.lifecycle.honors_exit_request;
    pkg.flushes_saves = p.manifest.lifecycle.flushes_saves;
    return pkg;
}

std::vector<std::string> Library::Generations(const std::string& game_id) const {
    std::vector<std::string> out;
    const std::string gens_dir = JoinPath(GameDir(game_id), "generations");
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(gens_dir, ec)) {
        if (e.is_directory()) out.push_back(e.path().filename().string());
    }
    std::sort(out.rbegin(), out.rend());  // newest first: g0002 before g0001
    return out;
}

bool Library::ActivateGeneration(const std::string& game_id, const std::string& generation,
                                 std::string* err) {
    // Only generations that fully passed install may be activated.
    const std::string gen_dir = JoinPath(GameDir(game_id), "generations/" + generation);
    std::string manifest_bytes;
    if (!ReadFileBytes(JoinPath(gen_dir, "manifest.json"), &manifest_bytes)) {
        if (err) *err = "generation has no manifest: " + generation;
        return false;
    }
    ParseResult p = ParseManifestJson(manifest_bytes);
    if (p.reason != Reason::Ok) {
        if (err) *err = "generation manifest invalid: " + p.detail;
        return false;
    }
    std::string active = "{\"generation\":\"" + generation + "\",\"package_id\":\"" +
                         PackageIdentity(p.manifest) + "\",\"version\":\"" + p.manifest.version + "\"}";
    return AtomicWriteText(ActivePath(game_id), active);
}

} // namespace dc::package
