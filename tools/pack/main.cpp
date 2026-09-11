// dc-pack — build a deterministic .11g package from a source directory
// (DK0-M3; ADR-0025, directive §7).
//
// Source layout:
//   <src>/manifests/game.json        dc.game/1 (repository schema)
//   <src>/manifests/lifecycle.json   dc.lifecycle/1 (repository schema)
//   <src>/manifests/save.json        dc.save/1 (save compatibility)
//   <src>/content/...                package content (logical_path = relpath)
//
// Output: deterministic .11g container. Identical inputs -> byte-identical
// package (verified by test_package_contract). No timestamps, no paths in
// canonical metadata. Optional --sign <private-blob-hex> embeds signature.json.
//
//   dc-pack <src-dir> <out.11g> [--sign <private-hex>]
#include "dc/hash.hpp"
#include "dc/json.hpp"
#include "dc/package.hpp"
#include "dc/package_container.hpp"
#include "dc/package_signing.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using namespace dc;
using namespace dc::package;

namespace {

std::string ReadText(const std::string& path, bool* ok) {
    std::string bytes;
    *ok = ReadFileBytes(path, &bytes);
    return bytes;
}

// Extract a dc.game/1 file into the GameManifest subset we carry.
bool LoadGameManifest(const std::string& bytes, GameManifest* g, std::string* err) {
    std::string e;
    auto v = json::Parse(bytes, e);
    if (!v || !v->is_object()) { *err = "game.json: " + e; return false; }
    const auto* schema = v->find("schema");
    if (!schema || !schema->is_string() || schema->as_string() != "dc.game/1") {
        *err = "game.json schema must be dc.game/1";
        return false;
    }
    const auto* id = v->find("id");
    if (!id || !id->is_string()) { *err = "game.json id missing"; return false; }
    g->game_id = id->as_string();
    const auto* ver = v->find("version");
    if (!ver || !ver->is_string()) { *err = "game.json version missing"; return false; }
    g->version = ver->as_string();
    if (const auto* n = v->find("name"); n && n->is_string()) g->name = n->as_string();
    if (const auto* p = v->find("publisher"); p && p->is_string()) g->publisher = p->as_string();
    if (const auto* eps = v->find("entrypoints"); eps && eps->is_object()) {
        for (const auto& [plat, path] : eps->as_object()) g->entrypoints[plat] = path.as_string();
    }
    if (const auto* rp = v->find("required_profiles"); rp && rp->is_array()) {
        for (const auto& p : rp->as_array()) g->required_profiles.push_back(p.as_string());
    }
    if (const auto* op = v->find("optional_profiles"); op && op->is_array()) {
        for (const auto& p : op->as_array()) g->optional_profiles.push_back(p.as_string());
    }
    if (const auto* c = v->find("controller_required"); c && c->is_bool())
        g->controller_required = c->as_bool();
    if (const auto* o = v->find("offline_launch"); o && o->is_bool())
        g->offline_launch = o->as_bool();
    return true;
}

bool LoadLifecycle(const std::string& bytes, LifecycleManifest* l, std::string* err) {
    std::string e;
    auto v = json::Parse(bytes, e);
    if (!v || !v->is_object()) { *err = "lifecycle.json: " + e; return false; }
    const auto* schema = v->find("schema");
    if (!schema || !schema->is_string() || schema->as_string() != "dc.lifecycle/1") {
        *err = "lifecycle.json schema must be dc.lifecycle/1";
        return false;
    }
    if (const auto* t = v->find("title_id"); t && t->is_string()) l->title_id = t->as_string();
    if (const auto* d = v->find("suspend_acknowledgement_deadline_ms"); d && d->is_number())
        l->suspend_ack_deadline_ms = static_cast<int>(d->as_number());
    if (const auto* q = v->find("quick_resume_tier"); q && q->is_string())
        l->quick_resume_tier = q->as_string();
    if (const auto* dch = v->find("display_change_handling"); dch && dch->is_string())
        l->display_change = dch->as_string();
    if (const auto* a = v->find("audio_route_change_handling"); a && a->is_string())
        l->audio_route_change = a->as_string();
    if (const auto* us = v->find("user_switching"); us && us->is_bool())
        l->user_switching = us->as_bool();
    if (const auto* eb = v->find("exit_behavior"); eb && eb->is_object()) {
        if (const auto* h = eb->find("honors_exit_request"); h && h->is_bool())
            l->honors_exit_request = h->as_bool();
        if (const auto* f = eb->find("flushes_saves"); f && f->is_bool())
            l->flushes_saves = f->as_bool();
        if (const auto* n = eb->find("no_orphan_children"); n && n->is_bool())
            l->no_orphan_children = n->as_bool();
    }
    return true;
}

bool LoadSave(const std::string& bytes, SaveManifest* s, std::string* err) {
    std::string e;
    auto v = json::Parse(bytes, e);
    if (!v || !v->is_object()) { *err = "save.json: " + e; return false; }
    const auto* schema = v->find("schema");
    if (!schema || !schema->is_string() || schema->as_string() != "dc.save/1") {
        *err = "save.json schema must be dc.save/1";
        return false;
    }
    if (const auto* ss = v->find("save_schema"); ss && ss->is_string()) s->save_schema = ss->as_string();
    if (const auto* m = v->find("migratable_from"); m && m->is_array()) {
        for (const auto& x : m->as_array()) s->migratable_from.push_back(x.as_string());
    }
    if (const auto* c = v->find("cloud_eligible"); c && c->is_bool()) s->cloud_eligible = c->as_bool();
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: dc-pack <src-dir> <out.11g> [--sign <private-blob-hex>]\n");
        return 2;
    }
    const std::string src = argv[1];
    const std::string out = argv[2];
    std::string sign_key;
    for (int i = 3; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--sign") sign_key = argv[i + 1];
    }

    // 1. Load manifests.
    PackageManifest m;
    bool ok = false;
    std::string err;
    if (!LoadGameManifest(ReadText(src + "/manifests/game.json", &ok), &m.game, &err) || !ok) {
        std::printf("dc-pack: %s\n", err.c_str());
        return 1;
    }
    if (!LoadLifecycle(ReadText(src + "/manifests/lifecycle.json", &ok), &m.lifecycle, &err) || !ok) {
        std::printf("dc-pack: %s\n", err.c_str());
        return 1;
    }
    if (!LoadSave(ReadText(src + "/manifests/save.json", &ok), &m.save, &err) || !ok) {
        std::printf("dc-pack: %s\n", err.c_str());
        return 1;
    }
    m.game_id = m.game.game_id;
    m.version = m.game.version;
    m.platforms = {"windows-x64"};
    m.runtime_abi = kRuntimeAbi;

    // 2. Enumerate content (deterministic order), hash it.
    const std::string content_root = src + "/content";
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(content_root, ec)) {
        if (e.is_regular_file(ec)) files.push_back(e.path());
    }
    if (ec) {
        std::printf("dc-pack: cannot enumerate %s\n", content_root.c_str());
        return 1;
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::printf("dc-pack: no content files under %s\n", content_root.c_str());
        return 1;
    }
    for (const auto& f : files) {
        std::string logical = fs::relative(f, content_root, ec).generic_string();
        std::string bytes;
        if (!ReadFileBytes(f.string(), &bytes)) {
            std::printf("dc-pack: cannot read %s\n", f.string().c_str());
            return 1;
        }
        ContentDescriptor d;
        d.logical_path = logical;
        d.sha256 = hash::Sha256::Hex(bytes);
        d.object_sha256 = d.sha256;
        d.size_bytes = bytes.size();
        m.content.push_back(std::move(d));
    }

    // 3. Identity + structural self-check before writing anything.
    m.package_id = PackageIdentity(m);
    std::string id_err;
    if (VerifyIdentity(m, &id_err) != Reason::Ok) {
        std::printf("dc-pack: identity self-check failed: %s\n", id_err.c_str());
        return 1;
    }

    // 4. Optional signing (over the canonical manifest, per ADR-0025).
    std::vector<ContainerMember> members;
    if (!sign_key.empty()) {
        SignResult s = SignManifest(m, sign_key);
        if (!s.ok) {
            std::printf("dc-pack: signing failed: %s\n", s.error.c_str());
            return 1;
        }
        members.push_back({kSignatureMember, SignatureJson(s.envelope)});
    }

    // 5. Build the container: manifest + objects.
    members.push_back({"manifest.json", FullManifestJson(m)});
    for (const auto& f : files) {
        std::string bytes;
        ReadFileBytes(f.string(), &bytes);
        members.push_back({"objects/" + hash::Sha256::Hex(bytes), bytes});
    }
    std::string container;
    if (!BuildContainer(members, &container)) {
        std::printf("dc-pack: container build failed\n");
        return 1;
    }

    // 6. Verify our own output (never emit an unverifiable package).
    ContainerVerifyResult v = VerifyPackage(container, "windows-x64", kRuntimeAbi);
    if (v.reason != Reason::Ok) {
        std::printf("dc-pack: output self-verification failed: %s (%s)\n",
                    v.reason_code.c_str(), v.detail.c_str());
        return 1;
    }
    if (!WriteFileBytes(out, container)) {
        std::printf("dc-pack: cannot write %s\n", out.c_str());
        return 1;
    }
    std::printf("dc-pack: %s\n  package_id %s\n  content objects %zu\n  signed %s\n",
                out.c_str(), v.identity.c_str(), m.content.size(),
                sign_key.empty() ? "no (unsigned developer package)" : "yes (ECDSA P-256)");
    return 0;
}
